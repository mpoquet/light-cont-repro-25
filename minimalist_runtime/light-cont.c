#define _GNU_SOURCE

#include <fcntl.h>
#include <sched.h>
#include <linux/sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <err.h>
#include <sys/types.h>
#include <dirent.h>
#include <getopt.h>


#define STACK_SIZE (1024 * 1024)
#define ROOTFS "./rootfs"
#define CGROUP_PATH "/sys/fs/cgroup/light-cont"
#define MAX_PID_LENGTH 20
#define DEFAULT_NAMESPACES_FLAGS (CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWIPC)

int opt_cgroupsv2 = 0;
int opt_in = 0;
int opt_out = 0;
int opt_nouserns = 0;
int path_specified = 0;
int host_uid;
unsigned long clone_flags = DEFAULT_NAMESPACES_FLAGS;
const char *cgroup_folder_path = CGROUP_PATH;

char image_loc[PATH_MAX];
char in_directory[PATH_MAX];
char out_directory[PATH_MAX];

char new_in[PATH_MAX];
char new_out[PATH_MAX];


int child(void *arg);

int create_cgroup_dir(const char *cgroup_folder_path);

int add_to_cgroup(const char *cgroup_folder_path, pid_t pid);

int opt_treatment(int argc, char *argv[]);

int cgroup_manager_child(void *arg);

int open_image_shell(const char *root_path);

int open_image_sh_here(void *arg);

void add_flag(unsigned long *flags, unsigned long flag_to_add);

void remove_flag(unsigned long *flags, unsigned long flag_to_remove);

int test_dir_access(const char *path);

void write_in_file(const char *path, const char *str);

void print_help();

pid_t clone3(struct clone_args *args) {
    pid_t pid = syscall(SYS_clone3, args, sizeof(struct clone_args));
    if (pid == -1) {
        err(EXIT_FAILURE,"clone3");
    }
    return pid;
}

static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

int main(int argc, char *argv[]) {

    pid_t child_pid;

    int cgroup_fd;

    opt_treatment(argc, argv);

    host_uid = geteuid();

    struct stat out_dir_stat;

    //Verifying superuser if cgroups or nouserns options selected.
    if (opt_cgroupsv2 || opt_nouserns) {
        if (host_uid != 0) {
            printf("Need to be superuser to launch with --cgroup or --nouserns option. Try with sudo.\n");
            exit(EXIT_FAILURE);
        }

    }

    //Default image location if none has been specified
    if (!*image_loc) {
        printf("No image location has been specified. Default: ./rootfs\n");
        snprintf(image_loc, sizeof(image_loc), "%s", ROOTFS);
    }

    //Testing access for specified in and out directories
    if (opt_in) {
        if (test_dir_access(in_directory) != 0) {
            err(EXIT_FAILURE, "Cannot access the specified entry directory");
        }
        
    }
    if (opt_out) {
        if (test_dir_access(out_directory) != 0) {
            err(EXIT_FAILURE, "Cannot access the specified output directory");
        }

        if (stat(out_directory, &out_dir_stat) != 0) { //getting stats about the directory to get the permissions
            err(EXIT_FAILURE, "stat out_dir");
        }
        if (host_uid == 0 && ((out_dir_stat.st_mode & 07777) != 0777)) { //Giving access to everybody for the time of execution if the perms aren't already 0777
            if (chmod(out_directory, 0777) != 0) {
                err(EXIT_FAILURE, "chmod out directory");
            }
        }
        
    }

    //Making paths to in and out dirs
    snprintf(new_in, sizeof(new_in), "%s%s", image_loc, "/in_dir");
    snprintf(new_out, sizeof(new_out), "%s%s", image_loc, "/out_dir");

    //Check if the specified directory actually exists
    int access_test = test_dir_access(image_loc);
    if (access_test == 1) {
        err(EXIT_FAILURE,"Path to the directory where the image fs is located does not exist");
    } else if (access_test == 2) {
        err(EXIT_FAILURE,"Cannot access the directory");
    }


    struct clone_args clone3_args = {
        .flags = clone_flags,
        .exit_signal = SIGCHLD,
        //.cgroup = cgroup_fd,
    };

    char *child_args[] = { image_loc, NULL };

    child_pid = clone3(&clone3_args);

    if (child_pid < 0) {
        perror("clone");
        return 1;
    } else if (child_pid == 0) {
        // Child
        return child(child_args);
    } else {
        // Parent
        
        waitpid(child_pid, NULL, 0);
    }

    if (opt_cgroupsv2) {
        close(cgroup_fd);
    }

    if (opt_out && host_uid == 0 && ((out_dir_stat.st_mode & 07777) != 0777)) { //Revocate access for everybody
        mode_t old_mode = out_dir_stat.st_mode & 07777; //getting only the permissions out of st_mode
        if (chmod(out_directory, old_mode) != 0) {
            err(EXIT_FAILURE, "chmod out directory");
        }
    }

    return 0;
    
}

int child(void *arg) 
{
    char        path[PATH_MAX];
    char        **args = arg;
    char        *new_root = args[0];
    const char  *put_old = "/oldrootfs";


    //Mapping UID/GID
    if (host_uid == 0) {
        write_in_file("/proc/self/setgroups", "deny");

        write_in_file("/proc/self/uid_map", "0 0 1");
        write_in_file("/proc/self/gid_map", "0 0 1");
    } else {
        char to_write[100];
        snprintf(to_write, sizeof(to_write), "0 %d 1", host_uid);
        write_in_file("/proc/self/uid_map", to_write);

        
    }

    int cgroup_fd;

    //Check if the user wish to include the container in a cgroup
    if (opt_cgroupsv2 == 1) {

        clone_flags = CLONE_INTO_CGROUP;

        //créer dossier cgroup
        cgroup_fd = create_cgroup_dir(cgroup_folder_path);
    }

    //Changer le hostname (UTS namespace)
    //Changer $PS1? -> env var qui contrôle le prompt string affiché - exple: user@hostname:~/Documents#
    sethostname("container", 10);
    
    //Based on the pivot_root example from man

    /* Ensure that 'new_root' and its parent mount don't have
        shared propagation (which would cause pivot_root() to
        return an error), and prevent propagation of mount
        events to the initial mount namespace. */

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
        err(EXIT_FAILURE, "mount-MS_PRIVATE");

    /* Ensure that 'new_root' is a mount point. */

    if (mount(new_root, new_root, NULL, MS_BIND, NULL) == -1)
        err(EXIT_FAILURE, "mount-MS_BIND");


    //Mounting the entry directory in read-only
    if (*in_directory) { //if in_directory not null


        if (mkdir(new_in, 0777) == -1 && errno != EEXIST) {
            err(EXIT_FAILURE, "mkdir new_in");
        }
        /*if (chmod(new_in, 0777) == -1) {
            err(EXIT_FAILURE, "chmod in_dir");
        }*/

        if (mount(in_directory, new_in, NULL, MS_BIND, NULL) == -1) {
            err(EXIT_FAILURE, "mount-MS_BIND - in_directory");
        }

        //RDONLY have to be executed on a remount, it doesn't apply on the first bind mount
        if (mount(NULL, new_in, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) == -1) {
            err(EXIT_FAILURE, "mount-REMOUNT-RDONLY - in_directory");
        }
    }

    if (*out_directory) { //if out_directory not null
        

        if (mkdir(new_out, 0777) == -1 && errno != EEXIST) {
            err(EXIT_FAILURE, "mkdir new_out");
        }
        if (chmod(new_out, 0777) == -1) {
            err(EXIT_FAILURE, "chmod out_dir");
        }

        //mounting in read-write, in order to let the container write output files
        if (mount(out_directory, new_out, NULL, MS_BIND, NULL) == -1) {
            err(EXIT_FAILURE, "mount-MS_BIND - out_directory");
        }

    }

    /* Create directory to which old root will be pivoted. */

    snprintf(path, sizeof(path), "%s/%s", new_root, put_old);
    if (mkdir(path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir");

    /* And pivot the root filesystem. */

    if (pivot_root(new_root, path) == -1)
        err(EXIT_FAILURE, "pivot_root");

    /* Switch the current working directory to "/". */

    if (chdir("/") == -1)
        err(EXIT_FAILURE, "chdir");

    /* Unmount old root and remove mount point. */

    if (umount2(put_old, MNT_DETACH) == -1)
        perror("umount2");
    if (rmdir(put_old) == -1)
        perror("rmdir");


    struct clone_args clone3_args = {
        .flags = clone_flags,
        .exit_signal = SIGCHLD,
        .cgroup = cgroup_fd,
    };

    pid_t child2_pid = clone3(&clone3_args);

    if (child2_pid < 0) {
        perror("clone");
        return 1;
    } else if (child2_pid == 0) {
        // Child
        return open_image_sh_here(NULL);
    } else {
        // Parent
        if (opt_cgroupsv2) {
            printf("The process executing the shell is in cgroup %s\n", cgroup_folder_path);
        }
        waitpid(child2_pid, NULL, 0);
    }


    if (opt_in) {
        if (umount2("./in_dir", MNT_DETACH) == -1) {
            err(EXIT_FAILURE, "unmount in_dir");
        }
        if (rmdir("./in_dir") == -1) {
            err(EXIT_FAILURE, "rmdir");
        }
    }
    if (opt_out) {
        if (umount2("./out_dir", MNT_DETACH) == -1) {
            err(EXIT_FAILURE, "unmount out_dir");
        }
        if (rmdir("./out_dir") == -1) {
            err(EXIT_FAILURE, "rmdir");
        }
    }

    return 0;
}

int create_cgroup_dir(const char *cgroup_folder_path) {

    //Creating cgroup directory, if it doesn't already exist
    if (mkdir(cgroup_folder_path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir");

    int cgroup_fd = open(cgroup_folder_path, O_DIRECTORY | O_RDONLY);
    if (cgroup_fd == -1) {
        perror("open cgroup dir");
        return 1;
    }

    return cgroup_fd;
}

int add_to_cgroup(const char *cgroup_folder_path, pid_t pid) {

    //Building path string to the cgroup.procs file
    char proclist_path[strlen(cgroup_folder_path) + strlen("/cgroup.procs ")]; 
    snprintf(proclist_path, sizeof(proclist_path), "%s%s", cgroup_folder_path, "/cgroup.procs");

    //Int to string conversion of the pid to write into the cgroup.procs file
    char pid_string[MAX_PID_LENGTH];
    snprintf(pid_string, sizeof(pid_string), "%d", pid);


    //Creating cgroup directory, if it doesn't already exist
    if (mkdir(cgroup_folder_path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir");

    //Opening cgroup.procs in write-only, append mode
    int procs_fd = open(proclist_path, O_WRONLY | O_APPEND | O_CREAT, 0777);

    if (procs_fd == -1) 
        err(EXIT_FAILURE, "cgroups - open");
    
    //Writing specified pid into cgroup.procs
    if (write(procs_fd, pid_string, strlen(pid_string)) == -1)
        err(EXIT_FAILURE, "write");

    close(procs_fd);
    return 0;
}

int opt_treatment(int argc, char *argv[]) {

    //Based on this example: https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

    int c;

  while (1)
    {
      static struct option long_options[] =
        {
          {"help",        no_argument,       0, 'h'},
          {"cgroup",      no_argument,       0, 'c'},
          {"network",     no_argument,       0, 'n'},
          {"nouserns",    no_argument,       0, 'u'},
          {"path",        required_argument, 0, 'p'},
          {"in",          required_argument, 0, 'i'},
          {"out",         required_argument, 0, 'o'},
        };
        
      int option_index = 0;

      c = getopt_long (argc, argv, "hcnup:i:o:",
                       long_options, &option_index);

      if (c == -1)
        break;

      switch (c)
        {

        case 'h':
            print_help();
            break;

        case 'c':
            printf("Cgroup option selected. The container will be placed in the cgroup located in /sys/fs/cgroup/light-cont\n");
            opt_cgroupsv2 = 1;
            break;

        case 'n':
            //désactiver isolation network
            printf("Network isolation disabled\n");
            remove_flag(&clone_flags, CLONE_NEWNET);
            break;

        case 'u':
            //désactiver user namespace
            printf("User namespace disabled\n");
            opt_nouserns =1;
            remove_flag(&clone_flags, CLONE_NEWUSER);
            break;

        case 'p':
            //changer variable root_path
            path_specified = 1;
            snprintf(image_loc, sizeof(image_loc), "%s", optarg);
            printf("Image directory location: %s\n", optarg);
            break;

        case 'i':
            //rep d'entrée à monter en rd-only
            snprintf(in_directory, sizeof(in_directory), "%s", optarg);
            opt_in = 1;
            printf("Entry directory location (mounted in /in_dir in the container, read-only): %s\n", optarg);
            break;

        case 'o':
            //rep de sortie à monter en rd-wr
            snprintf(out_directory, sizeof(out_directory), "%s", optarg);
            opt_out = 1;
            printf("Output directory location (mounted in /out_dir in the container, read-write): %s\n", optarg);
            break;

        case '?':
            printf("Option not recognized. Please use --help (-h) option to display help text.\n");
            exit(EXIT_FAILURE);
            break;

        default:
            printf("\n");
            //abort ();
        }
    }

  /* Print any remaining command line arguments (not options). */
  if (optind < argc)
    {
      printf ("non-option ARGV-elements: ");
      while (optind < argc)
        printf ("%s ", argv[optind++]);
      putchar ('\n');
    }

    return 0;
}


int cgroup_manager_child(void *arg) {
    char        path[PATH_MAX];
    char        **args = arg;
    char        *cgroup_folder_path = args[0];

    //Allocating space for a stack
    char *stack = malloc(STACK_SIZE); 
    if (stack == NULL) {
        perror("malloc");
        return 1;
    }

    //Adding self to a new cgroup
    add_to_cgroup(cgroup_folder_path, getpid());

    char *child_args[] = { image_loc, NULL };
    int child_pid = clone(child, stack + STACK_SIZE, clone_flags, child_args);

    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

    waitpid(child_pid, NULL, 0);

    return 0;
}

int open_image_shell(const char *root_path) {

    char sh_path[PATH_MAX];

    snprintf(sh_path, sizeof(sh_path), "%s/bin/sh", root_path);

    char *argshell[] = {sh_path, NULL};
    execvp(argshell[0], argshell);

    perror("execvp failed");
    return 1;    
}

int open_image_sh_here(void *arg) {
    return open_image_shell("");
}

void add_flag(unsigned long *flags, unsigned long flag_to_add) {
    *flags |= flag_to_add;
}

void remove_flag(unsigned long *flags, unsigned long flag_to_remove) {
    *flags &= ~flag_to_remove;
}

int test_dir_access(const char *path) {
    DIR *image_dir = opendir(path);
    if (image_dir) {
        closedir(image_dir);
        return 0;
    } else if (errno == ENOENT) {
        return 1;
    } else {
        return 2;
    }
}


void write_in_file(const char *path, const char *str) {
    int fd = open(path, O_WRONLY);
    if (fd == -1) { 
        perror("open"); exit(1); 
    }

    if (write(fd, str, strlen(str)) == -1) { 
        perror("write"); exit(1); 
    }

    close(fd);
}

void print_help() {
    printf(
        "\n\n"
        "======================================= LIGHT-CONT: HELP =======================================\n\n"
        "Light-cont is a lightweight container runtime intended to maximize reproducibility of experiments.\n"
        "Please note that this software is still under development.\n"
        "Not every planned fonctionalities are yet implemented, and some problems may occur during use.\n"
        "\nOptions:\n"
        "Display this help message:\t\t\t--help\t\t-h\n"
        "Specify image location (directory):\t\t--path\t\t-p\n"
        "Include the runtime in a cgroup (v2 only):\t--cgroup\t-c\tWARNING: Need to be superuser\n"
        "Disable Network isolation:\t\t\t--network\t-n\n"
        "Specify an entry directory (read-only):\t\t--in\t\t-i\n"
        "Specify an output directory (read-write):\t--out\t\t-o\n"
        "Disable the use of an user namespace:\t\t--nouserns\t-u\tWARNING: Need to be superuser\n"
    
    );
    exit(EXIT_SUCCESS);
}

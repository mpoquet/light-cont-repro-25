#define _GNU_SOURCE

#include <fcntl.h>
#include <sched.h>
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
#include <signal.h>

#define STACK_SIZE (1024 * 1024)
//"/tmp/light-cont/rootfs"
#define ROOTFS "./rootfs"
#define CGROUP_PATH "/sys/fs/cgroup/light-cont"
#define MAX_PID_LENGTH 20
#define DEFAULT_NAMESPACES_FLAGS (CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWIPC | SIGCHLD)

int option_cgroupsv2 = 0;
int opt_in = 0;
int opt_out = 0;
unsigned long clone_flags = DEFAULT_NAMESPACES_FLAGS;

char image_loc[PATH_MAX];
char in_directory[PATH_MAX];
char out_directory[PATH_MAX];

char new_in[PATH_MAX];
char new_out[PATH_MAX];


int child(void *arg);

int add_to_cgroup(const char *cgroup_folder_path, pid_t pid);

int traitement_opt(int argc, char *argv[]);

int cgroup_manager_child(void *arg);

int open_image_shell(const char *root_path);

void remove_flag(unsigned long *flags, unsigned long flag_to_remove);

void print_help();


static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

int main(int argc, char *argv[]) {

    int child_pid;

    traitement_opt(argc, argv);

    const char *cgroup_folder_path = CGROUP_PATH;

    printf("Starting...\n");

    //Default image location if none has been specified
    if (!*image_loc) {
        printf("No image location has been specified. Default: ./rootfs\n");
        snprintf(image_loc, sizeof(image_loc), "%s", ROOTFS);
    }

    //Making paths to in and out dirs
    snprintf(new_in, sizeof(new_in), "%s%s", image_loc, "/in_dir");
    snprintf(new_out, sizeof(new_out), "%s%s", image_loc, "/out_dir");

    //Check if the specified directory actually exists
    DIR *image_dir = opendir(image_loc);
    if (image_dir) {
        closedir(image_dir);
    } else if (errno == ENOENT) {
        perror("Path to the directory where the image fs is located does not exist.\n");
        exit(EXIT_FAILURE);
    } else {
        err(EXIT_FAILURE, "\n");
    }


    char *stack = malloc(STACK_SIZE);  //remplacement possible par un appel à mmap (voir example pivot_root)
    if (stack == NULL) {
        perror("malloc");
        return 1;
    }


    //Check if the user wish to include the container in a cgroup
    if (option_cgroupsv2 == 1) {
        if (geteuid() != 0) {
            printf("Need to be superuser to launch with --cgroups option. Try with sudo.\n");
            exit(EXIT_FAILURE);
        }

        //Création d'un fils qui se mettra lui même dans un cgroup, permettant à son fils d'y être inscrit d'office
        char *child_args[] = { (char *)cgroup_folder_path, NULL };
        child_pid = clone(cgroup_manager_child, stack + STACK_SIZE, SIGCHLD, child_args);



    } else {
        
        //note: essayer d'utiliser clone3() à la place de clone?
        char *child_args[] = { image_loc, NULL };
        child_pid = clone(child, stack + STACK_SIZE, clone_flags, child_args);
    }


    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

    waitpid(child_pid, NULL, 0);

    printf("Fin du process parent\n");



    //Unmounting and deleting in/out directories in the image
    if (opt_in) {
        if (umount2(new_in, MNT_DETACH) == -1) {
            err(EXIT_FAILURE, "unmount in_dir");
        }
        if (rmdir(new_in) == -1) {
            err(EXIT_FAILURE, "rmdir");
        }
    }
    if (opt_out) {
        if (umount2(new_out, MNT_DETACH) == -1) {
            err(EXIT_FAILURE, "unmount out_dir");
        }
        if (rmdir(new_out) == -1) {
            err(EXIT_FAILURE, "rmdir");
        }
    }

    free(stack);
    return 0;
    
}


int child(void *arg) 
{

    char        path[PATH_MAX];
    char        **args = arg;
    char        *new_root = args[0];
    const char  *put_old = "/oldrootfs";


    // Changer le hostname (UTS namespace)
    //affiche nobody à la place
    //Changer $PS1? -> env var qui contrôle le prompt string affiché - exple: user@hostname:~/Documents#
    sethostname("container", 10);


#ifdef DEBUG
    printf("[DEBUG][CHILD]dormance pdt 10s\n");
    sleep(10);
#endif
    
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

        if (mount(in_directory, new_in, NULL, MS_BIND, NULL) == -1) {
            err(EXIT_FAILURE, "mount-MS_BIND - in_directory");
        }

        //RDONLY have to be executed on a remount, it doesn't apply on the first bind mount
        if (mount(NULL, new_in, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) == -1) {
            err(EXIT_FAILURE, "mount-REMOUNT-RDONLY - in_directory");
        }
    }

    if (*out_directory) {
        

        if (mkdir(new_out, 0777) == -1 && errno != EEXIST) {
            err(EXIT_FAILURE, "mkdir new_out");
        }

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

    printf("pivot_root done.\n");

    /* Switch the current working directory to "/". */

    if (chdir("/") == -1)
        err(EXIT_FAILURE, "chdir");

    /* Unmount old root and remove mount point. */

    if (umount2(put_old, MNT_DETACH) == -1)
        perror("umount2");
    if (rmdir(put_old) == -1)
        perror("rmdir");



    open_image_shell("");
    return 1;
}


int add_to_cgroup(const char *cgroup_folder_path, pid_t pid) {

    char proclist_path[strlen(cgroup_folder_path) + strlen("/cgroup.procs ")]; //14 caractères en plus pour rajouter '/cgroup.procs'
    snprintf(proclist_path, sizeof(proclist_path), "%s%s", cgroup_folder_path, "/cgroup.procs");

    //conversion du pid en string pour pouvoir l'écrire dans le fichier cgroup.procs
    char pid_string[MAX_PID_LENGTH];
    snprintf(pid_string, sizeof(pid_string), "%d", pid);


    //création du dossier pour le cgroup, pas grave si il existe déjà
    if (mkdir(cgroup_folder_path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir");

    //on ouvre cgroup.procs en écriture, rajout à la fin du fichier seulement
    printf("%s\n", proclist_path);
    int procs_fd = open(proclist_path, O_WRONLY | O_APPEND | O_CREAT, 0777);

    if (procs_fd == -1) 
        err(EXIT_FAILURE, "cgroups - open");
    
    //on écrit le pid du child dans cgroup.procs pour le rajouter dans le cgroup
    if (write(procs_fd, pid_string, strlen(pid_string)) == -1)
        err(EXIT_FAILURE, "write");


    close(procs_fd);
    return 0;
}


int traitement_opt(int argc, char *argv[]) {

    //voir exemple https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

    int c;

  while (1)
    {
      static struct option long_options[] =
        {
          {"help",        no_argument,       0, 'h'},
          {"cgroup",      no_argument,       0, 'c'},
          {"network",     no_argument,       0, 'n'},
          {"path",        required_argument, 0, 'p'},
          {"in",          required_argument, 0, 'i'},
          {"out",         required_argument, 0, 'o'},
        };
        
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "hcnp:i:o:",
                       long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1)
        break;

      switch (c)
        {

        case 'h':
            print_help();
            break;

        case 'c':
            printf("Cgroup option selected. The container will be placed in the cgrouplocated in /sys/fs/cgroup/light-cont\n");
            option_cgroupsv2 = 1;
            break;

        case 'n':
            //désactiver isolation network
            printf("Network isolation disabled\n");
            remove_flag(&clone_flags, CLONE_NEWNET);
            break;

        case 'p':
            //changer variable root_path
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
            break;

        default:
            abort ();
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

    char *stack = malloc(STACK_SIZE);  //remplacement possible par un appel à mmap (voir example pivot_root)
    if (stack == NULL) {
        perror("malloc");
        return 1;
    }

    add_to_cgroup(cgroup_folder_path, getpid());
    printf("Ajouté au cgroup\n");


    char *child_args[] = { image_loc, NULL };
    int child_pid = clone(child, stack + STACK_SIZE, clone_flags, child_args);

    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

    printf("Container PID: %d", child_pid);

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

void remove_flag(unsigned long *flags, unsigned long flag_to_remove) {
    *flags &= ~flag_to_remove;
}


void print_help() {
    printf(
"======================================= LIGHT-CONT: HELP =======================================\n\n"
        "Light-cont is a lightweight container runtime intended to maximize reproducibility of experiments.\n"
        "Please note that this software is still under development.\n"
        "Not every planned fonctionalities are yet implemented, and some problems may occur during use.\n"
        "\nOptions:\n"
        "Display this help message:\t\t\t--help\t\t-h\n"
        "Specify image location (directory):\t\t--path\t\t-p\n"
        "Include the runtime in a cgroup (v2 only):\t--cgroup\t-c\n"
        "Disable Network isolation:\t\t\t--network\t-n\t(not yet implemented)\n"
        "Specify an entry directory (read-only):\t\t--in\t\t-i\n"
        "Specify an output directory (read-write):\t--out\t\t-o\n"
    
    );
    exit(EXIT_SUCCESS);
}

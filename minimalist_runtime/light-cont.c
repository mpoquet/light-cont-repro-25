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
#include <dirent.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>
#include <archive.h>
#include <archive_entry.h>


#include "include/jsmn.h"


#define STACK_SIZE (1024 * 1024)
#define MAX_PID_LENGTH 20
#define MAX_LAYERS 128
#define MAX_ARGS 20
#define MAX_MOUNT_DIRS 10
#define DEFAULT_ROOTFS "./rootfs"
#define ROOTFS "./rootfs"
#define CGROUP_PATH "/sys/fs/cgroup/light-cont"
#define DEFAULT_NAMESPACES_FLAGS (CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWTIME)
#define DEFAULT_SECONDCHILD_NSFLAGS (CLONE_NEWCGROUP | CLONE_NEWPID)

int opt_cgroupsv2 = 0;
int opt_network = 0;
int opt_mount_rdwr = 0;
int opt_mount_rdonly = 0;
int opt_no_user_ns = 0;
//int opt_no_time_ns = 0;
int opt_no_cgroup_ns = 0;
int opt_no_uts_ns = 0;
int opt_no_pid_ns = 0;
int opt_test = 0;
int opt_alr_extracted = 0;

int path_specified = 0;
int host_uid;
int host_gid;
char host_hostname[100];

char **envp = NULL; //No env variables by default.
int env_count;

unsigned long clone_flags = DEFAULT_NAMESPACES_FLAGS;
unsigned long child2_clone_flags = DEFAULT_SECONDCHILD_NSFLAGS;
const char *cgroup_folder_path = CGROUP_PATH;

char image_loc[PATH_MAX];
char extract_loc[PATH_MAX];
char in_directory[PATH_MAX];
char out_directory[PATH_MAX];
char command_to_run[PATH_MAX];

char new_in[PATH_MAX];
char new_out[PATH_MAX];

char to_mount_dirs[MAX_MOUNT_DIRS][PATH_MAX];
char to_mount_rdonly_dirs[MAX_MOUNT_DIRS][PATH_MAX];
char paths_to_mounted_rdwr_dirs[MAX_MOUNT_DIRS][PATH_MAX];
char paths_to_mounted_rdonly_dirs[MAX_MOUNT_DIRS][PATH_MAX];
int mt_dir_count = 0;
int mt_dir_rdonly_count = 0;

struct timens_offset {
    int clockid;
    long long offset;
};


int child(void *arg);

int create_cgroup_dir(const char *cgroup_folder_path);

int opt_treatment(int argc, char *argv[]);

char **copy_env();

int parse_command(const char *input, char *argv_out[MAX_ARGS + 1], char buf[MAX_ARGS][PATH_MAX]);

int run_command(char *const argv[]);

void add_flag(unsigned long *flags, unsigned long flag_to_add);

void remove_flag(unsigned long *flags, unsigned long flag_to_remove);

int test_dir_access(const char *path);

int test_file_access(const char *path);

void write_in_file(const char *path, const char *str);

void uid_gid_mapping(int host_uid, int host_gid);

const char *get_filename_from_path(const char *path);

//Not used
int reset_monotonic_and_boottime_clocks_to_zero();

char *read_file(const char *path);

int parse_manifest_oci(const char *json_str, char digests[][PATH_MAX], const char *path_to_image);

int parse_index(const char *json_str, char manifests[][PATH_MAX], const char *path_to_image);

int extract_tar(const char *layer_path, const char *outdir);

int extract_oci_image(const char *path_to_image, const char *path_to_extraction);

void print_help();

void test_child_pid_ns();

void test_child_mount_ns();

void test_child_uts_ns(const char *real_hostname);

void test_child_user_ns();

void test_child_net_ns();

void test_child_ipc_ns();

void test_child_time_ns();

void launch_all_tests();

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

    host_gid = getgid();

    struct stat out_dir_stat;
    struct stat rootfs_dir_stat;    

    //Verifying superuser if cgroups or nouserns options selected.
    if (opt_cgroupsv2 || opt_no_user_ns) {
        if (host_uid != 0) {
            printf("Need to be superuser to launch with --cgroup or --nouserns option. Try with sudo.\n");
            exit(EXIT_FAILURE);
        }

    }

    //User-error management about paths options.

    if (!opt_alr_extracted) { //if the user didn't use --extracted option
        if (!*image_loc) { // --path not specified (critical)
            printf("[ERROR] No image location has been specified. Please use --path (-p) option to specify the path to the oci image.\n");
            printf("[ERROR] Use --help to display usage and options.\n");
            exit(EXIT_FAILURE);
        } else {
            if (!*extract_loc) { // --extract not specified
                printf("[WARNING] No extract location has been specified. Default: ./rootfs\n");
                strncpy(extract_loc, DEFAULT_ROOTFS, PATH_MAX);

                if (extract_oci_image(image_loc, extract_loc) != 0) {
                    err(EXIT_FAILURE, "oci extraction");
                }
                printf("\n[SUCCESS] Image succesfully extracted to %s\n", extract_loc);

            } else {
                if (extract_oci_image(image_loc, extract_loc) != 0) {
                    err(EXIT_FAILURE, "oci extraction");
                }
                printf("\n[SUCCESS] Image succesfully extracted to %s\n", extract_loc);
            }
        }
    } else { // --extracted option
        if (!*image_loc) { // --path not specified
            if (!*extract_loc) {
                printf("[ERROR] No image location has been specified. \n");
                printf("[ERROR] You used the option --extracted (-E). Please specify the root directory of the image after the --path (-p) option.\n");
                exit(EXIT_FAILURE);
            } else {
                printf("[ERROR] You used the option --extracted (-E). Please specify the root directory of the image after the --path (-p) option.\n");
                exit(EXIT_FAILURE);
            }
        } else { // --path specified 
            if (!*extract_loc) {
                strncpy(extract_loc, image_loc, PATH_MAX);
            } else { // --path and --extractpath specified
                printf("[ERROR] You used the option --extracted (-E). Please specify the root directory of the image after the --path (-p) option.\n");
                printf("[ERROR] However, you have also specified another path after the --extractpath (-e) option." 
                                        "Please only specify the root directory path after the --path (-p) option.\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    if (!*command_to_run) {
        printf("[CONFIG] You did not specify a command to run (--run \"path/to/command arg1 arg2\"). Default: \"/bin/sh -i\".\n");
        strncpy(command_to_run, "/bin/sh -i", PATH_MAX);
    }

    //Extract_loc can not be null (anymore) at this point
    if (test_dir_access(extract_loc) != 0) {
        printf("[ERROR] Could not access to %s\n", extract_loc);
    }



    __mode_t old_modes[mt_dir_count];
    //Chmodding directories if launching in superuser
    if (host_uid == 0) {
        if (opt_mount_rdwr) {

            for (int i=0; i<mt_dir_count; i++) {
                if (stat(to_mount_dirs[i], &out_dir_stat) != 0) { //getting stats about the directory to get the permissions
                    err(EXIT_FAILURE, "stat out_dir");
                }
                old_modes[i] = out_dir_stat.st_mode;
                if (((out_dir_stat.st_mode & 07777) != 0777)) { //Giving access to everybody for the time of execution if the perms aren't already 0777
                    if (chmod(to_mount_dirs[i], 0777) != 0) {
                        err(EXIT_FAILURE, "chmod to-mount rd-wr directory");
                    }
                }
            }   
        }
        if (stat(extract_loc, &rootfs_dir_stat) != 0) { //getting stats about the directory to get the permissions
            err(EXIT_FAILURE, "stat rootfs_dir");
        }
        if (((rootfs_dir_stat.st_mode & 07777) != 0777)) { //Giving access to everybody for the time of execution if the perms aren't already 0777
            if (chmod(extract_loc, 0777) != 0) {
                err(EXIT_FAILURE, "chmod extract directory");
            }
        }


    }

    //Testing access for specified in and out directories
    if (opt_mount_rdonly) {
        for (int i=0; i<mt_dir_rdonly_count; i++) {
            if (test_dir_access(to_mount_rdonly_dirs[i]) != 0) {
                err(EXIT_FAILURE, "Cannot access the %s directory to mount in read-only", to_mount_rdonly_dirs[i]);
            }
        }
        
    }
    if (opt_mount_rdwr) {
        for (int i=0; i<mt_dir_count; i++) {
            if (test_dir_access(to_mount_dirs[i]) != 0) {
                err(EXIT_FAILURE, "Cannot access to %s directory to mount in read-write", to_mount_dirs[i]);
            }
        }
        
        
    }

    //Making paths to in and out dirs
    for (int i=0; i<mt_dir_count; i++) {
        snprintf(paths_to_mounted_rdwr_dirs[i], PATH_MAX, "%s/%s", extract_loc, get_filename_from_path(to_mount_dirs[i]));
        printf("[DEBUG] path_to_mount: %s\n", paths_to_mounted_rdwr_dirs[i]);
    }

    for (int i=0; i<mt_dir_rdonly_count; i++) {
        snprintf(paths_to_mounted_rdonly_dirs[i], PATH_MAX, "%s/%s", extract_loc, get_filename_from_path(to_mount_rdonly_dirs[i]));
        printf("[DEBUG] path_to_mount: %s\n", paths_to_mounted_rdonly_dirs[i]);
    }


    struct clone_args clone3_args = {
        .flags = clone_flags,
        .exit_signal = SIGCHLD,
    };

    char *child_args[] = { extract_loc, NULL };

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

    if (host_uid == 0) {
        if (opt_mount_rdwr) {
            for (int i=0; i<mt_dir_count; i++) {
                if ((old_modes[i] & 07777) != 0777) {
                    if (chmod(to_mount_dirs[i], old_modes[i]) != 0) {
                        err(EXIT_FAILURE, "chmod out directory: %s", to_mount_dirs[i]);
                    }
                }

                
            }
        }

        if (((rootfs_dir_stat.st_mode & 07777) != 0777)) { //Revocate access for everybody
            mode_t old_mode = rootfs_dir_stat.st_mode & 07777; //getting only the permissions out of st_mode
            if (chmod(extract_loc, old_mode) != 0) {
                err(EXIT_FAILURE, "chmod extract directory");
            }
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

    
    if (opt_no_cgroup_ns) {
        remove_flag(&child2_clone_flags, CLONE_NEWCGROUP);
    }
    int cgroup_fd;
    char *hostname = "container";

    struct clone_args clone3_args = {
        .exit_signal = SIGCHLD,
    };

    //Mapping UID/GID
    if (!opt_no_user_ns) {
        uid_gid_mapping(host_uid, host_gid);
    }

    /*
        Après beaucoup de sueurs, de sang et de larmes, je n'ai jamais réussi à faire fonctionner l'isolation du temps
        Le principe est le suivant: en étant le premier et le seul process dans le time namespace qui vient d'être créé, écrire dans /proc/self/timens_offset
        On peut ainsi écrire des offsets pour faire changer la perception du process et de ses fils sur la BOOTTIME_CLOCK et la MONOTONIC_CLOCK
        cf: https://www.man7.org/linux/man-pages/man7/time_namespaces.7.html

        Cependant, même en le faisant selon l'exemple du manuel (dans un shell, à la main), j'ai toujours eu l'accès refusé au fichier, même en étant su.
        Selon provient donc possiblement de ma machine. J'ai quand même laissé la fonction (pas propre du tout) qui est censé le faire automatiquement dans le code,
        voir reset_monotonic_and_boottime_clocks_to_zero().
        Si vous êtes un dev qui reprend mon code et que arrivez à le faire marcher, je pense qu'ici est le meilleur endroit pour le faire. Paix sur le monde.
    */

    
    //Check if the user wish to include the container in a cgroup
    if (opt_cgroupsv2 == 1) {

        add_flag(&child2_clone_flags, CLONE_INTO_CGROUP);

        //create cgroup dirrectory
        cgroup_fd = create_cgroup_dir(cgroup_folder_path);
        if (cgroup_fd == -1)
            err(EXIT_FAILURE, "open cgroup dir");
        clone3_args.cgroup = cgroup_fd;
    }

    clone3_args.flags = child2_clone_flags;

    sethostname(hostname, strlen(hostname));
    
    //Based on the pivot_root example from man pivot_root

    /* Ensure that 'new_root' and its parent mount don't have
        shared propagation (which would cause pivot_root() to
        return an error), and prevent propagation of mount
        events to the initial mount namespace. */

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
        err(EXIT_FAILURE, "mount-MS_PRIVATE");

    /* Ensure that 'new_root' is a mount point. */

    if (mount(new_root, new_root, NULL, MS_BIND, NULL) == -1)
        err(EXIT_FAILURE, "mount-MS_BIND");


    //Mounting the readonly dirs
    if (opt_mount_rdonly) {

        for (int i=0; i<mt_dir_rdonly_count; i++) {
            if (mkdir(paths_to_mounted_rdonly_dirs[i], 0777) == -1 && errno != EEXIST) {
                err(EXIT_FAILURE, "mkdir new_in");
            }

            if (mount(to_mount_rdonly_dirs[i], paths_to_mounted_rdonly_dirs[i], NULL, MS_BIND, NULL) == -1) {
                err(EXIT_FAILURE, "mount-MS_BIND - %s - %s", to_mount_rdonly_dirs[i], paths_to_mounted_rdonly_dirs[i]);
            }

            //RDONLY have to be executed on a remount, it doesn't apply on the first bind mount
            if (mount(NULL, paths_to_mounted_rdonly_dirs[i], NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) == -1) {
                err(EXIT_FAILURE, "mount-REMOUNT-RDONLY - %s", paths_to_mounted_rdonly_dirs[i]);
            }
        }
    }

    if (opt_mount_rdwr) {
        

        for (int i=0; i<mt_dir_count; i++) {
            if (mkdir(paths_to_mounted_rdwr_dirs[i], 0777) == -1 && errno != EEXIST) {
                err(EXIT_FAILURE, "mkdir %s", paths_to_mounted_rdwr_dirs[i]);
            }

            if (mount(to_mount_dirs[i], paths_to_mounted_rdwr_dirs[i], NULL, MS_BIND, NULL) == -1) {
                err(EXIT_FAILURE, "mount-MS_BIND - %s - %s", to_mount_dirs[i], paths_to_mounted_rdwr_dirs[i]);
            }
        }


    }

    /* Create directory to which old root will be pivoted. */

    snprintf(path, sizeof(path), "%s/%s", new_root, put_old);
    if (mkdir(path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir-oldroot");

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


    pid_t child2_pid = clone3(&clone3_args);

    if (child2_pid < 0) {
        perror("clone");
        return 1;
    } else if (child2_pid == 0) {
        // Child

        if (opt_test) {
            launch_all_tests();
            exit(EXIT_SUCCESS);
        }
        char buf[MAX_ARGS][PATH_MAX];
        char *parsed_args[MAX_ARGS + 1];
        parse_command(command_to_run, parsed_args, buf);

        return run_command(parsed_args);
    } else {
        // Parent
        if (opt_cgroupsv2) {
            printf("The process executing the shell is in cgroup %s\n", cgroup_folder_path);
        }
        waitpid(child2_pid, NULL, 0);
    }


    if (opt_mount_rdonly) {

        for (int i=0; i<mt_dir_rdonly_count; i++) {
            const char *dir_name = get_filename_from_path(paths_to_mounted_rdonly_dirs[i]);
            if (umount2(dir_name, MNT_DETACH) == -1) {
                err(EXIT_FAILURE, "unmount %s", paths_to_mounted_rdonly_dirs[i]);
            }
            if (rmdir(dir_name) == -1) {
                err(EXIT_FAILURE, "rmdir %s", paths_to_mounted_rdonly_dirs[i]);
            }
        }
    }
    if (opt_mount_rdwr) {
        for (int i=0; i<mt_dir_count; i++) {
            const char *dir_name = get_filename_from_path(paths_to_mounted_rdwr_dirs[i]);
            if (umount2(dir_name, MNT_DETACH) == -1) {
                err(EXIT_FAILURE, "unmount %s", paths_to_mounted_rdwr_dirs[i]);
            }
            if (rmdir(dir_name) == -1) {
                err(EXIT_FAILURE, "rmdir %s", paths_to_mounted_rdwr_dirs[i]);
            }
        }

    }

    if (opt_cgroupsv2) {
        close(cgroup_fd);
    }

    return 0;
}

int create_cgroup_dir(const char *cgroup_folder_path) {

    //Creating cgroup directory, if it doesn't already exist
    if (mkdir(cgroup_folder_path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir-cgroupfolderpath");

    int cgroup_fd = open(cgroup_folder_path, O_DIRECTORY | O_RDONLY);
    if (cgroup_fd == -1) {
        perror("open cgroup dir");
    }

    return cgroup_fd;
}

int opt_treatment(int argc, char *argv[]) {

    //Based on this example: https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

    int c;

    char *env_arg;

  while (1)
    {
      static struct option long_options[] =
        {
          {"help",        no_argument,       0, 'h'},
          {"test",        no_argument,       0, 'T'},
          {"cgroup",      no_argument,       0, 'C'},
          {"network",     no_argument,       0, 'n'},
          {"nouserns",    no_argument,       0, 'u'},
          {"nocgroupns",  no_argument,       0, 'c'},
          {"noutsns",     no_argument,       0, 'U'},
          {"nopidns",     no_argument,       0, 'P'},
          //{"notimens",    no_argument,       0, 't'},
          {"extracted",   no_argument,       0, 'E'},
          {"path",       required_argument, 0, 'p'},
          {"extractpath",required_argument, 0, 'e'},
          {"mount",      required_argument, 0, 'm'},
          {"mountrdonly",required_argument, 0, 'M'},
          {"run",        required_argument, 0, 'r'},
          {"env",        required_argument, 0, 'v'},
          {0,            0,                 0, 0} //sentinel
        };
        
      int option_index = 0;

      c = getopt_long (argc, argv, "hTCnucUPEp:e:m:M:r:v:",
                       long_options, &option_index);

      if (c == -1)
        break;

      switch (c)
        {

        case 'h':
            print_help();
            break;

        case 'T':
            printf("[CONFIG] Tests will be launched in the container after the end of the configuration\n");
            gethostname(host_hostname, 100);
            opt_test = 1;
            break;

        case 'C':
            printf("[CONFIG] Cgroup option selected. The container will be placed in the cgroup located in /sys/fs/cgroup/light-cont\n");
            opt_cgroupsv2 = 1;
            break;

        case 'n':
            //disable network isolation
            opt_network = 1;
            printf("[CONFIG] Network isolation disabled\n");
            remove_flag(&clone_flags, CLONE_NEWNET);
            break;

        case 'u':
            //disable user namespace
            printf("[CONFIG] User namespace disabled\n");
            opt_no_user_ns = 1;
            remove_flag(&clone_flags, CLONE_NEWUSER);
            break;

        case 'c':
            //disable cgroup namespace
            printf("[CONFIG] Cgroup namespace disbaled\n");
            opt_no_cgroup_ns = 1;
            remove_flag(&child2_clone_flags, CLONE_NEWCGROUP);
            break;

        case 'U':
            //disable UTS namespace
            printf("[CONFIG] UTS namespace disabled\n");
            opt_no_uts_ns = 1;
            remove_flag(&clone_flags, CLONE_NEWUTS);
            break;

        case 'P':
            //disable PID namespace
            printf("[CONFIG] PID namespace disabled\n");
            opt_no_pid_ns = 1;
            remove_flag(&child2_clone_flags, CLONE_NEWPID);
            break;
        
        /*
        case 't':
            //disable Time namespace
            printf("[CONFIG] Time namespace disabled\n");
            opt_no_time_ns = 1;
            remove_flag(&clone_flags, CLONE_NEWTIME);
            break;
        */

        case 'E':
            //The image is already extracted
            opt_alr_extracted = 1;
            printf("[CONFIG] You have specified that the image is already extracted.\n");
            break;

        case 'p':
            //changer variable image_loc
            path_specified = 1;
            snprintf(image_loc, sizeof(image_loc), "%s", optarg);
            printf("[CONFIG] OCI Image location: %s\n", optarg);
            break;

        case 'e':
            snprintf(extract_loc, sizeof(extract_loc), "%s", optarg);
            printf("[CONFIG] Extracted image location: %s\n", optarg);
            break;

        case 'm':
            //dirs à monter en rd-wr
            opt_mount_rdwr = 1; //TODO changer nom variable
            if (mt_dir_count >= MAX_MOUNT_DIRS - 1) {
                printf("[ERROR] Too much directories to mount in rd-wr." 
                              "Please do not mount more than %d directories in rd-wr or modify the MAX_MOUNT_DIRS constant in the source code.\n"
                            , MAX_MOUNT_DIRS);
                exit(EXIT_FAILURE);
            }
            strncpy(to_mount_dirs[mt_dir_count], optarg, PATH_MAX);
            printf("[DEBUG] Mounted dir n°%d (rd-wr): %s\n", mt_dir_count, to_mount_dirs[mt_dir_count]);


            mt_dir_count++;
            break;
        
        case 'M':
            //dirs à monter en rd-only
            opt_mount_rdonly = 1;
            if (mt_dir_rdonly_count >= MAX_MOUNT_DIRS - 1) {
                printf("[ERROR] Too much directories to mount in read-only." 
                              "Please do not mount more than %d directories in rd-wr or modify the MAX_MOUNT_DIRS constant in the source code.\n"
                            , MAX_MOUNT_DIRS);
                exit(EXIT_FAILURE);
            }
            strncpy(to_mount_rdonly_dirs[mt_dir_rdonly_count], optarg, PATH_MAX);
            printf("[DEBUG] Mounted dir n°%d (rdonly): %s\n", mt_dir_rdonly_count, to_mount_rdonly_dirs[mt_dir_rdonly_count]);

            mt_dir_rdonly_count++;
            break;

        case 'r':
            strncpy(command_to_run, optarg, PATH_MAX);
            printf("[CONFIG] Command to run: %s\n", command_to_run);
            break;

        case 'v':
            env_arg = strdup(optarg);

            if (strcmp(env_arg, "KEEPCURRENTENV") == 0) {
                //must be first --env call

                envp = copy_env();
                printf("[CONFIG] Current environment variables kept for the image.\n");

            } else {
                char **tmp = realloc(envp, (env_count + 2) * sizeof(char*));
                if (!tmp) {
                    perror("realloc");
                    //cleanup
                    for (int i = 0; i < env_count; ++i) free(envp[i]);
                    free(envp);

                    exit(1);
                }

                envp = tmp;
                envp[env_count++] = env_arg;
                envp[env_count] = NULL;
            }
            break;

        case '?':
            printf("Option not recognized. Please use --help (-h) option to display help text.\n");
            exit(EXIT_FAILURE);
            break;

        default:
            err(EXIT_FAILURE, "Unexpected error in option parsing.\n");
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

char **copy_env(void) {
    int count = 0;
    while (environ[count]) count++;

    char **env_copy = malloc((count + 1) * sizeof(char *));
    if (!env_copy) return NULL;

    for (int i = 0; i < count; ++i) {
        env_copy[i] = strdup(environ[i]);
    }
    env_copy[count] = NULL;

    env_count = count;

    return env_copy;
}

int parse_command(const char *input, char *argv_out[MAX_ARGS + 1], char buf[MAX_ARGS][PATH_MAX]) {
    int argc = 0;
    char buffer[PATH_MAX * (MAX_ARGS + 1)];

    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *token = strtok(buffer, " ");
    while (token != NULL && argc < MAX_ARGS) {
        strncpy(buf[argc], token, PATH_MAX - 1);
        buf[argc][PATH_MAX - 1] = '\0';
        argv_out[argc] = buf[argc];
        argc++;
        token = strtok(NULL, " ");
    }

    argv_out[argc] = NULL; //execvp needs a NULL-terminated array

    return argc;
}

int run_command(char *const argv[]) {

    execvpe(argv[0], argv, envp);
    perror("execvp failed");

    for (int i = 0; i < env_count; ++i) free(envp[i]);
    free(envp);
    return 1;
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

int test_file_access(const char *path) {
    struct stat sb;
    return (stat(path, &sb) == 0 && S_ISREG(sb.st_mode));
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

void uid_gid_mapping(int host_uid, int host_gid) {
    if (host_uid == 0) {
        write_in_file("/proc/self/setgroups", "deny");
        write_in_file("/proc/self/uid_map", "0 0 1");
        write_in_file("/proc/self/gid_map", "0 0 1");
    } else {
        char uid_map[100];
        snprintf(uid_map, sizeof(uid_map), "0 %d 1", host_uid);
        write_in_file("/proc/self/uid_map", uid_map);

        char gid_map[100];
        snprintf(gid_map, sizeof(gid_map), "0 %d 1", host_gid);
        write_in_file("/proc/self/setgroups", "deny");
        write_in_file("/proc/self/gid_map", gid_map);        
    }
}

const char* get_filename_from_path(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        return last_slash + 1;
    } else {
        return path;
    }
}

//doesn't work, maybe only on my system - i can't even write in timens_offsets manually, even as superuser
int reset_monotonic_and_boottime_clocks_to_zero() {
    int fd = open("/proc/self/timens_offsets", O_WRONLY);
    if (fd == -1) {
        perror("open /proc/self/timens_offsets");
        return -1;
    }

    struct timespec now;
    //struct timens_offset offsets[2];
    struct timens_offset offset;

    // CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime(CLOCK_MONOTONIC)");
        close(fd);
        return -1;
    }
    offset.clockid = CLOCK_MONOTONIC;
    offset.offset = -((long long)now.tv_sec * 1e9 + now.tv_nsec);

    printf("Now: %ld\n", now.tv_sec);

    /*
    // CLOCK_BOOTTIME
    if (clock_gettime(CLOCK_BOOTTIME, &now) != 0) {
        perror("clock_gettime(CLOCK_BOOTTIME)");
        close(fd);
        return -1;
    }
    offsets[1].clockid = CLOCK_BOOTTIME;
    offsets[1].offset = -((long long)now.tv_sec * 1e9 + now.tv_nsec);
    */

    offset.offset = -5000000000LL;  // -5s
    //offsets[1].offset = -5000000000LL;

    printf("offset[0] clockid = %d, offset = %lld\n", offset.clockid, offset.offset);
    //printf("offset[1] clockid = %d, offset = %lld\n", offsets[1].clockid, offsets[1].offset);

    char *to_write = "monotonic -200 0\nboottime -200 0";

    if (write(fd, to_write, strlen(to_write))) {
        perror("write timens_offsets");
        err(EXIT_FAILURE, "write timens offsets 1");
        close(fd);
        return -1;
    }
    


    close(fd);
    return 0;
}

/**
 * Read entire file into memory.
 */
 char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    //printf("[DEBUG] fopen: %s\n", path);
    if (!f) {
        perror("fopen");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (!buf) {
        perror("malloc");
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, size, f) != (size_t)size) {
        perror("fread");
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

int parse_manifest_oci(const char *json_str, char digests[][PATH_MAX], const char *path_to_image) {
    jsmn_parser parser;
    jsmntok_t tokens[1024];
    jsmn_init(&parser);

    int ret = jsmn_parse(&parser, json_str, strlen(json_str),
                         tokens, sizeof(tokens)/sizeof(tokens[0]));
    if (ret < 0) {
        fprintf(stderr, "Failed to parse manifest OCI JSON: %d\n", ret);
        return -1;
    }

    int tokcount = ret;

    for (int i = 1; i < tokcount; i++) {
        if (tokens[i].type == JSMN_STRING &&
            tokens[i+1].type == JSMN_ARRAY &&
            (int)(tokens[i].end - tokens[i].start) == 6 &&
            strncmp(json_str + tokens[i].start, "layers", 6) == 0) {

            int arr_size = tokens[i+1].size;
            int idx = i + 2;
            int count = 0;

            for (int j = 0; j < arr_size && count < MAX_LAYERS; j++) {
                int obj_end = tokens[idx].end;

                for (int k = 0; idx + k < tokcount && tokens[idx + k].start < obj_end; k++) {
                    if (tokens[idx + k].type == JSMN_STRING &&
                        (int)(tokens[idx + k].end - tokens[idx + k].start) == 6 &&
                        strncmp(json_str + tokens[idx + k].start, "digest", 6) == 0) {

                        jsmntok_t *val = &tokens[idx + k + 1];
                        int len = val->end - val->start;

                        if (len < 7 || strncmp(json_str + val->start, "sha256:", 7) != 0) {
                            fprintf(stderr, "Unexpected digest format\n");
                            return -1;
                        }

                        // Format final : <path_to_image>/blobs/sha256/<digest>
                        snprintf(digests[count], PATH_MAX, "/blobs/sha256/%.*s",
                                len - 7, json_str + val->start + 7);
                        digests[count][PATH_MAX - 1] = '\0';

                        //printf("[DEBUG]: layer: %s\n", digests[count]);

                        count++;
                        break;
                    }
                }

                idx++;
            }

            return count;
        }
    }

    fprintf(stderr, "No layers array found in manifest OCI\n");
    return -1;
}


int parse_index(const char *json_str, char manifests[][PATH_MAX], const char *path_to_image) {
    jsmn_parser parser;
    jsmntok_t tokens[1024];
    jsmn_init(&parser);
    int ret = jsmn_parse(&parser, json_str, strlen(json_str), tokens, sizeof(tokens)/sizeof(tokens[0]));
    if (ret < 0) {
        fprintf(stderr, "Failed to parse index.json: %d\n", ret);
        return -1;
    }

    int tokcount = ret;

    for (int i = 1; i < tokcount; i++) {
        if (tokens[i].type == JSMN_STRING &&
            tokens[i+1].type == JSMN_ARRAY &&
            (int)tokens[i].end - tokens[i].start == 9 &&
            strncmp(json_str + tokens[i].start, "manifests", 9) == 0) {

            int arr_size = tokens[i+1].size;
            int idx = i + 2;
            int count = 0;

            for (int j = 0; j < arr_size && count < MAX_LAYERS; j++) {
                int obj_end = tokens[idx].end;
                for (int k = 0; idx + k < tokcount && tokens[idx + k].start < obj_end; k++) {
                    if (tokens[idx + k].type == JSMN_STRING &&
                        (int)tokens[idx + k].end - tokens[idx + k].start == 6 &&
                        strncmp(json_str + tokens[idx + k].start, "digest", 6) == 0) {

                        jsmntok_t *val = &tokens[idx + k + 1];
                        int len = val->end - val->start;
                        if (len >= PATH_MAX - 18) len = PATH_MAX - 19;

                        // Format : blobs/sha256/<digest>.json
                        snprintf(manifests[count], PATH_MAX, "%s/blobs/sha256/%.*s", path_to_image, len - 7, json_str + val->start + 7);
                        //printf("[DEBUG]: man : %s\n", manifests[count]);
                        manifests[count][PATH_MAX - 1] = '\0';
                        count++;
                        break;
                    }
                }
                idx++;
            }

            return count;
        }
    }

    fprintf(stderr, "No manifests array found in index.json\n");
    return -1;
}

/**
 * Extract tar file using libarchive
 */
int extract_tar(const char *layer_path, const char *outdir) {
    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int r;

    a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_gzip(a);

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, layer_path, 10240))) {
        fprintf(stderr, "Could not open archive %s\n", layer_path);
        return -1;
    }

    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char *current_file = archive_entry_pathname(entry);
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", outdir, current_file);
        archive_entry_set_pathname(entry, full_path);



        r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK) {
            const char *link_target = archive_entry_hardlink(entry);
            if (link_target) {
                // Ignoring hard link errors
                fprintf(stderr, "[OCI EXTRACTION] Warning: skipping unresolved hard link to %s\n", link_target);

                continue;
            } else {
                fprintf(stderr, "%s\n", archive_error_string(ext));
            }
        } else {
            const void *buff;
            size_t size;
            la_int64_t offset;

            while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK) {
                r = archive_write_data_block(ext, buff, size, offset);
                if (r != ARCHIVE_OK) {
                    fprintf(stderr, "%s\n", archive_error_string(ext));
                    break;
                }
            }
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
    return 0;
}

int remove_dir_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *entry;
    char fullpath[PATH_MAX];

    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        struct stat st;
        if (lstat(fullpath, &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            remove_dir_recursive(fullpath);
        } else {
            unlink(fullpath);
        }
    }
    closedir(d);
    return rmdir(path);
}

int extract_oci_image(const char *path_to_image, const char *path_to_extraction) {
    char real_image_path[PATH_MAX];
    int using_temp_dir = 0;

    //test si path_to_image est bien un dossier décompressé (bundle oci)
    if (test_dir_access(path_to_image) != 0) {
        
        //test si c'est une archive tar
        if (test_file_access(path_to_image)) {
            // Try to decompress the OCI tar archive into a temp directory
            char tmpdir[] = "/tmp/oci_unpack_XXXXXX";
            if (!mkdtemp(tmpdir)) {
                perror("mkdtemp");
                return -1;
            }
            if (extract_tar(path_to_image, tmpdir) != 0) {
                fprintf(stderr, "Failed to extract OCI archive\n");
                return -1;
            }
            strncpy(real_image_path, tmpdir, PATH_MAX);
            using_temp_dir = 1;
        } else {
            fprintf(stderr, "Invalid path: %s\n", path_to_image);
            return -1;
        }

    } else {
        strncpy(real_image_path, path_to_image, PATH_MAX);
    }


    //construire path vers index.json
    //read_file et gestion d'erreur 
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index.json", real_image_path);
    char *json = read_file(index_path);
    if (!json) {
        if (using_temp_dir) remove_dir_recursive(real_image_path);
        return 1;
    }



    char manifests[8][PATH_MAX];
    int mcount = parse_index(json, manifests, real_image_path);
    free(json);



    for (int i=0; i<mcount; i++) {
        printf("[OCI EXTRACTION] [DEBUG] manifest path: %s\n", manifests[i]);
        char layers[MAX_LAYERS][PATH_MAX];
        char *json = read_file(manifests[i]);
        int n = parse_manifest_oci(json, layers, real_image_path);
        free(json);
        if (n < 0) {
            if (using_temp_dir) remove_dir_recursive(real_image_path);
            return 1;
        }

        //pour chaque layers
        //construire chemin vers layer (path_to_image + layertab[i])
        //extraire la layer vers path_to_extraction
        for (int i = 0; i < n; i++) {
            char layer_file[PATH_MAX];
            snprintf(layer_file, sizeof(layer_file), "%s/%s", real_image_path, layers[i]);
            //printf("[DEBUG] layer: %s\n", layer_file);
            if (extract_tar(layer_file, path_to_extraction) != 0) {
                if (using_temp_dir) remove_dir_recursive(real_image_path);
                return 1;
            }
            printf("[OCI EXTRACTION] Extracted layer: %s\n", layers[i]);
        }

    }


    if (using_temp_dir) {
        remove_dir_recursive(real_image_path);
    }
    return 0;
}

void print_help() {
    printf(
        "\n\n"
        "======================================= LIGHT-CONT: HELP =======================================\n\n"
        "Light-cont is a lightweight container runtime intended to maximize reproducibility of experiments.\n"
        "Please note that this software is still under development.\n"
        "Not every planned fonctionalities are yet implemented, and some problems may occur during use.\n"
        "\tNote: time namespace is set by default but time isolation is not complete\n"
        "\n"
        "Options:\n"
        "\n"
        "--help\t\t\t-h\tDisplay this help message\n"
        "--path /path\t\t-p \tSpecify OCI image location\n"
        "--extractpath /path\t-e \tSpecify extract location for the image\n"
        "--extracted\t\t-E\tSpecify that the image has already been extracted\n"
        "--run \"/path/cmd args\"\t-r\tRun command path.\n"
        "--cgroup\t\t-C\tInclude the runtime in a cgroup (v2 only)\tWARNING: Need to be superuser\n"
        "--network\t\t-n\tDisable Network isolation\n"
        "--mountrdonly\t\t\t-M\tSpecify an entry directory (read-only)\n"
        "--mount\t\t\t-m\tSpecify an output directory (read-write)\n"
        "--nouserns\t\t-u\tDisable the use of an user namespace\t\tWARNING: Need to be superuser\n"
        "--nopidns\t\t-P\tDisable the use of a PID namespace\n"
        "--nocgroupns\t\t-c\tDisable the use of a cgroup namespace\n"
        "--noutsns\t\t-U\tDisable the use of a UTS namespace\n"
        //"--notimens\t\t-t\tDisable the use of a time namespace\n"
        "--test\t\t\t-T\tLaunch embedded isolation tests\n"
        "--env VAR=val\t\t-v\tAdd environment variable\n"
        "\n"
        "By default no environment variables are defined. You can call --env VAR=value as many time as you wish.\n"
        "If you wish to keep your environment setup, please call --env KEEPCURRENTENV.\n"
        "\tNote: this must be on the first occurence of --env callings\n"
    
    );
    exit(EXIT_SUCCESS);
}


void test_child_pid_ns() {
    printf("\n==========[TEST: PID isolation]==========\n\n");
    if (opt_no_pid_ns) {
        printf("[TEST] [CHILD]: My PID should not be 1\n");
    } else {
        printf("[TEST] [CHILD]: My PID should be 1\n");
    }

    printf("[TEST] [CHILD]: My actual PID: %d\n\n", getpid());
}

void test_child_mount_ns() {
    printf("\n==========[TEST: Mount system isolation]==========\n\n");
    printf("[TEST] [CHILD]: Executing \"mount\" command, it should print an error.\n");
    
    if (system("mount") != 0) {
        printf("[TEST] [CHILD] Something went wrong. Testing with busybox.\n\n");
        system("/bin/busybox mount");
    }
    printf("\n");
}

void test_child_uts_ns(const char *real_hostname) {
    printf("\n==========[TEST: Hostname and Domainname isolation]==========\n\n");
    if (opt_no_uts_ns) {
        printf("[TEST] [CHILD]: My hostname should be the same as my father's\n");
    } else {
        printf("[TEST] [CHILD]: My hostname should not be the same as my father's\n");
    }

    char my_hostname[100];
    if (gethostname(my_hostname, 100) != 0) {
        err(EXIT_FAILURE, "gethostname");
    }
    printf("[TEST] [CHILD]: My hostname: %s\tMy father's hostname: %s\n\n", my_hostname, real_hostname);
}

void test_child_user_ns() {
    printf("\n==========[TEST: User isolation]==========\n\n");
    if (host_uid == 0) {
        printf("[TEST] [CHILD]: Both my UID and GID should be 0, because the runtime was launched by a privileged user\n");
    } else {
        if (opt_no_user_ns) {
            printf("[TEST] [CHILD]: Both my UID and GID should not be 0\n");
        } else {
            printf("[TEST] [CHILD]: Both my UID and GID should be 0\n");
        }
    }

    printf("[TEST] [CHILD]: My actual UID: %d\tMy actual GID: %d\n\n", geteuid(), getgid());
}

void test_child_net_ns() {
    printf("\n==========[TEST: Network isolation]==========\n\n");

    if (opt_network) {
        printf("[TEST] [CHILD] Network isolation disabled. Executing \"ping 1.1.1.1\"." 
            "It should work if the image has ping installed and if the host is connected\n\n");
    } else {
        printf("[TEST] [CHILD] Network isolation disabled. Executing \"ping 1.1.1.1\"." 
            "It should print an error.\n\n");
    }

    if (system("ping -c 1 1.1.1.1") != 0) {
        printf("[TEST] [CHILD] Something went wrong. Testing with busybox.\n\n");
        system("/bin/busybox ping -c 1 1.1.1.1");
    }
    printf("\n");
}

void test_child_ipc_ns() {

}


void launch_all_tests() {
    test_child_pid_ns();

    test_child_user_ns();

    test_child_uts_ns(host_hostname);

    test_child_mount_ns();

    //not implemented yet
    test_child_ipc_ns();

    test_child_net_ns();

    printf("\n==========[ALL TESTS HAVE TERMINATED]==========\n");
}
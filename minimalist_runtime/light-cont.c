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

#include <archive.h>
#include <archive_entry.h>

#include "include/jsmn.h"


#define STACK_SIZE (1024 * 1024)
#define MAX_PID_LENGTH 20
#define MAX_LAYERS 128
#define DEFAULT_ROOTFS "./rootfs"
#define ROOTFS "./rootfs"
#define CGROUP_PATH "/sys/fs/cgroup/light-cont"
#define DEFAULT_NAMESPACES_FLAGS (CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWTIME)

int opt_cgroupsv2 = 0;
int opt_in = 0;
int opt_out = 0;
int opt_no_user_ns = 0;
int opt_no_time_ns = 0;
int opt_no_cgroup_ns = 0;
int opt_no_uts_ns = 0;
int opt_no_pid_ns = 0;

int path_specified = 0;
int host_uid;
int host_gid;
unsigned long clone_flags = DEFAULT_NAMESPACES_FLAGS;
const char *cgroup_folder_path = CGROUP_PATH;

char image_loc[PATH_MAX];
char extract_loc[PATH_MAX];
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

int test_file_access(const char *path);

void write_in_file(const char *path, const char *str);

void uid_gid_mapping(int host_uid, int host_gid);

char *read_file(const char *path);

int parse_manifest(const char *json_str, char layers[][PATH_MAX]);

int extract_tar(const char *layer_path, const char *outdir);

int extract_oci_image(const char *path_to_image, const char *path_to_extraction);

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

int test_extract_oci() {

    /*if (mkdir("./temp-rootfs", 0777) != 0 && errno != EEXIST) {
        err(EXIT_FAILURE, "mkdir temp-rootfs");
    }

    if (mkdir("./temp-rootfs/rootfs", 0777) != 0 && errno != EEXIST) {
        err(EXIT_FAILURE, "mkdir temp-rootfs/rootfs");
    }*/

    if (extract_oci_image("./temp-rootfs/ubuntu.tar", "./temp-rootfs/rootfs") != 0) {
        err(EXIT_FAILURE, "oci extraction");
    }

    return 0;
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

    

    if (!*extract_loc) {
        printf("No extract location has been specified. Default: ./rootfs\n");
        strncpy(extract_loc, DEFAULT_ROOTFS, PATH_MAX);
    }

    //Chmodding directories if launching in superuser
    if (host_uid == 0) {
        if (opt_out) {
            if (stat(out_directory, &out_dir_stat) != 0) { //getting stats about the directory to get the permissions
                err(EXIT_FAILURE, "stat out_dir");
            }
            if (((out_dir_stat.st_mode & 07777) != 0777)) { //Giving access to everybody for the time of execution if the perms aren't already 0777
                if (chmod(out_directory, 0777) != 0) {
                    err(EXIT_FAILURE, "chmod out directory");
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

    //Extracting OCI Image
    if (!*image_loc) {
        printf("No image location has been specified. please use -p option to specify the path to the oci image\n");
        exit(EXIT_FAILURE);
    } else {
        if (extract_oci_image(image_loc, extract_loc) != 0) {
            err(EXIT_FAILURE, "oci extraction");
        }
        printf("Image succesfully extracted to %s\n", extract_loc);
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
        
    }

    //Making paths to in and out dirs
    snprintf(new_in, sizeof(new_in), "%s%s", extract_loc, "/in_dir");
    snprintf(new_out, sizeof(new_out), "%s%s", extract_loc, "/out_dir");


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
        if (opt_out && ((out_dir_stat.st_mode & 07777) != 0777)) { //Revocate access for everybody
            mode_t old_mode = out_dir_stat.st_mode & 07777; //getting only the permissions out of st_mode
            if (chmod(out_directory, old_mode) != 0) {
                err(EXIT_FAILURE, "chmod out directory");
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

    unsigned long child2_clone_flags = CLONE_NEWCGROUP;
    int cgroup_fd;
    char *hostname = "container";

    //Mapping UID/GID
    uid_gid_mapping(host_uid, host_gid);
    
    struct clone_args clone3_args = {
        .exit_signal = SIGCHLD,
    };

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

    //Set fancy shell prompt
    char prompt[256];
    snprintf(prompt, sizeof(prompt), "[%s@%s]>> ", getenv("USER"), hostname);
    setenv("PS1", prompt, 1);

    sethostname(hostname, strlen(hostname));
    
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

int add_to_cgroup(const char *cgroup_folder_path, pid_t pid) {

    //Building path string to the cgroup.procs file
    char proclist_path[strlen(cgroup_folder_path) + strlen("/cgroup.procs ")]; 
    snprintf(proclist_path, sizeof(proclist_path), "%s%s", cgroup_folder_path, "/cgroup.procs");

    //Int to string conversion of the pid to write into the cgroup.procs file
    char pid_string[MAX_PID_LENGTH];
    snprintf(pid_string, sizeof(pid_string), "%d", pid);


    //Creating cgroup directory, if it doesn't already exist
    if (mkdir(cgroup_folder_path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir-cgroupfolder");

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
            opt_no_user_ns =1;
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

    char *argshell[] = {sh_path, "-i", NULL};
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

/**
 * Read entire file into memory.
 */
 char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
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

/**
 * Parse manifest.json, extract layer paths into layers[]. Returns count or -1.
 */
int parse_manifest(const char *json_str, char layers[][PATH_MAX]) {
    jsmn_parser parser;
    jsmntok_t tokens[1024];
    jsmn_init(&parser);
    int ret = jsmn_parse(&parser, json_str, strlen(json_str), tokens, sizeof(tokens)/sizeof(tokens[0]));
    if (ret < 0) {
        fprintf(stderr, "Failed to parse JSON: %d\n", ret);
        return -1;
    }
    int tokcount = ret;

    // Find "Layers" key
    for (int i = 1; i < tokcount; i++) {
        if (tokens[i].type == JSMN_STRING &&
            (int)tokens[i].end - tokens[i].start == 6 &&
            strncmp(json_str + tokens[i].start, "Layers", 6) == 0) {
            // Next token is array
            if (tokens[i+1].type != JSMN_ARRAY) continue;
            int arr_size = tokens[i+1].size;
            int idx = i+2;
            if (arr_size > MAX_LAYERS) arr_size = MAX_LAYERS;
            for (int j = 0; j < arr_size; j++) {
                jsmntok_t *t = &tokens[idx];
                int len = t->end - t->start;
                if (len >= PATH_MAX) len = PATH_MAX-1;
                memcpy(layers[j], json_str + t->start, len);
                layers[j][len] = '\0';
                idx++;
            }
            return arr_size;
        }
    }
    fprintf(stderr, "No Layers array found in manifest.json\n");
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
                // C'est un hard link, on ignore l'erreur pour éviter les warnings non bloquants
                fprintf(stderr, "Warning: skipping unresolved hard link to %s\n", link_target);

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


    //construire path vers manifest.json
    //read_file et gestion d'erreur (retour de -1)
    char manifest_path[PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", real_image_path);
    char *json = read_file(manifest_path);
    if (!json) {
        if (using_temp_dir) remove_dir_recursive(real_image_path);
        return 1;
    }

    //créer tab layers contenant path vers les layers
    //parse_manifest et gestion d'erreur
    //free le pointeur vers le buffer du manifest
    char layers[MAX_LAYERS][PATH_MAX];
    int n = parse_manifest(json, layers);
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
        if (extract_tar(layer_file, path_to_extraction) != 0) {
            if (using_temp_dir) remove_dir_recursive(real_image_path);
            return 1;
        }
        printf("Extracted layer: %s\n", layers[i]);
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

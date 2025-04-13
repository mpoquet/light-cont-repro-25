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
#define ROOTFS "/tmp/light-cont/rootfs"
#define CGROUP_PATH "/sys/fs/cgroup/light-cont"
#define MAX_PID_LENGTH 20
#define NAMESPACES_FLAGS (CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD)

int option_cgroupsv2 = 0;


int child(void *arg);

int add_to_cgroup(const char *cgroup_folder_path, pid_t pid);

int traitement_opt(int argc, char *argv[]);

void treat_sig_donothing();

int cgroup_manager_child(void *arg);


static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

int main(int argc, char *argv[]) {

    int child_pid;

    traitement_opt(argc, argv);

    const char *cgroup_folder_path = CGROUP_PATH;

    printf("Starting...\n");


    char *stack = malloc(STACK_SIZE);  //remplacement possible par un appel à mmap (voir example pivot_root)
    if (stack == NULL) {
        perror("malloc");
        return 1;
    }


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
        char *child_args[] = { ROOTFS, NULL };
        child_pid = clone(child, stack + STACK_SIZE, NAMESPACES_FLAGS, child_args);
    }


    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

    waitpid(child_pid, NULL, 0);

    printf("Fin du process parent\n");

    free(stack);
    return 0;
    
}


int child(void *arg) 
{

    char        path[PATH_MAX];
    char        **args = arg;
    char        *new_root = args[0];
    const char  *put_old = "/oldrootfs";


    printf("Dans le nv namespace isolé\n");
    printf("[CHILD] PID: %d\n", getpid());

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



    char *argshell[] = {"/bin/bash", NULL};
    execvp(argshell[0], argshell);

    perror("execvp failed");
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
          
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"cgroups",     no_argument,       0, 'a'},
        };
        
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "a",
                       long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1)
        break;

      switch (c)
        {

        case 'a':
          option_cgroupsv2 = 1;
          break;

        case 'b':
        printf("cgroups v1 non supportés pour l'instant.\n");
        break;


        case '?':
          /* getopt_long already printed an error message. */
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


    char *child_args[] = { ROOTFS, NULL };
    int child_pid = clone(child, stack + STACK_SIZE, NAMESPACES_FLAGS, child_args);

    printf("PID du processus exécutant l'image: %d", child_pid);

    waitpid(child_pid, NULL, 0);

    return 0;

}

void treat_sig_donothing() {}


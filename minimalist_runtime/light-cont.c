//#include <fcntl.h>
#define _GNU_SOURCE
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

#define STACK_SIZE (1024 * 1024)
#define ROOTFS "./rootfs"
#define MAX_PID_LENGTH 20



int child(void *args);

int add_child_to_cgroup(const char *cgroup_folder_path, pid_t child_pid);

static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

int main(int argc, char *argv[]) {

    int cgroupv2_option = 0;

    const char *cgroup_folder_path = "/sys/fs/cgroup/my_cgroup";

    printf("Starting...\n");

    char *stack = malloc(STACK_SIZE);  //remplacement possible par un appel à mmap (voir example pivot_root)
    if (stack == NULL) {
        perror("malloc");
        return 1;
    }

    //on crée d'abord un child dans nv namespace 
    //il va lui même créer un child après avoir sethostname afin que le hostname soit à jour dès le lancement du process qu'on veut exécuter
    // -> j'ai abandonné l'idée pour l'instant, le hostname est quand même "nobody"

    //note: essayer d'utiliser clone3() à la place de clone?
    char *child_args[] = { "./rootfs", NULL };
    int child_pid = clone(child, stack + STACK_SIZE, CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD, child_args);

    if (child_pid == -1) {
        perror("clone");
        return 1;
    }
    
    
    printf("child lancé avec PID %d\n", child_pid);

    if (cgroupv2_option == 1) {
        add_child_to_cgroup(cgroup_folder_path, child_pid);
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

    printf("[DEBUG][CHILD]dormance pdt 10s\n");
    sleep(10);

    
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


int add_child_to_cgroup(const char *cgroup_folder_path, pid_t child_pid) {

    char proclist_path[strlen(cgroup_folder_path) + 14]; //14 caractères en plus pour rajouter '/cgroup.procs'
    snprintf(proclist_path, sizeof(proclist_path), "%s%s", cgroup_folder_path, "/cgroup.procs");

    //conversion du pid en string pour pouvoir l'écrire dans le fichier cgroup.procs
    char child_pid_string[MAX_PID_LENGTH];
    snprintf(child_pid_string, sizeof(child_pid_string), "%d", child_pid);


    //création du dossier pour le cgroup, pas grave si il existe déjà
    if (mkdir(cgroup_folder_path, 0777) == -1 && errno != EEXIST)
        err(EXIT_FAILURE, "mkdir");

    //on ouvre cgroup.procs en écriture, rajout à la fin du fichier seulement
    int procs_fd = open(proclist_path, O_WRONLY | O_APPEND | O_CREAT, 0777);

    if (procs_fd == -1) 
        err(EXIT_FAILURE, "open");
    
    //on écrit le pid du child dans cgroup.procs pour le rajouter dans le cgroup
    if (write(procs_fd, child_pid_string, strlen(child_pid_string)) == -1)
        err(EXIT_FAILURE, "write");


    close(procs_fd);
    return 0;
}
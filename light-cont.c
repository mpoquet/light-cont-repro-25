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



static int
pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}



int args_treatments(char *argv[]) { //traitement de argv, retourne un int pouvant ensuite être utilisé dans un switch par exemple
    if (argv[1] != NULL && (strcmp(argv[1], "run")==0)) {
        printf("run\n");
        return 1;
    }
    
    return 0;
}


int cp(const char *src, const char *dest) { //src doit être un fichier, dest un dossier
    char newFilePath[PATH_MAX]; 
    int srcFileFd, newFileFd;
    ssize_t bytes_read, bytes_written;
    char buf[4096];


    srcFileFd = open(src, O_RDONLY); //on ouvre le fichier source en rd only
    if (srcFileFd == -1) { 
        perror("open");
        return -1;
    }

    
    snprintf(newFilePath, sizeof(newFilePath), "%s", dest); //chemin de destination (sans le fichier copié)
    const char *fileName = strrchr(src, '/'); 
    fileName = (fileName == NULL) ? src : fileName + 1; //pointe sur le char après le dernier slash, i.e. sur le nom du fichier à copier (si dans répertoire courant on prend juste le nom)

    strcat(newFilePath, fileName);
    printf("copy path: %s\n", newFilePath);

    //on crée le nv fichier
    newFileFd = open(newFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0755); //permissions rwxr-xr-x
    if (newFileFd == -1) {
        close(srcFileFd);
        perror("open");
        return -1;
    }

    //copie du contenu
    while ((bytes_read = read(srcFileFd, buf, sizeof(buf))) > 0) {
        bytes_written = write(newFileFd, buf, bytes_read);
        if (bytes_written != bytes_read) {
            close(srcFileFd);
            close(newFileFd);
            return -1;
        }
    }
    


    close(srcFileFd);
    close(newFileFd);

    return (bytes_read == -1) ? -1 : bytes_read;
}

int child(void *arg) {

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


    /*
    struct dirent *lecture;
    DIR *rep;
    rep = opendir("..");
    while ((lecture = readdir(rep)))
    {
        printf("FICHIER: %s\n", lecture->d_name);
    }
    closedir(rep);
    */

    //on est bien dans le bon fichier, sachant que opendir ouvre le répertoire parent (..), 
    //et qu'on se retrouve bien dans . (/tmp/rootfs), et pas dans /tmp


    //on exécute un shell - busybox finalement
    char *argshell[] = {"/bin/busybox", "sh", NULL};
    execvp(argshell[0], argshell);
    //marche pas!!!!!
    //->manque les dépendances
    //essayer avec busybox, car pas besoin de dépendance
    

    //TODO - remplacer par exécution de l'image



    perror("execvp failed");
    return 1;
}


int main(int argc, char *argv[]) {

    printf("Starting...\n");

    char *stack = malloc(STACK_SIZE);  //remplacement possible par un appel à mmap (voir example pivot_root)
    if (!stack) {
        perror("malloc");
        return 1;
    }


    //exple pivot_root le fait pas, on est censé le faire manuellement dans un shell avnt de le lancer
    if (mkdir("/tmp/rootfs", 0777) == -1 && errno != EEXIST) { //pas besoin de CAP_SYS_ADMIN car on est dans /tmp/
        err(EXIT_FAILURE, "mkdir");
    }
    printf("/tmp/rootfs directory succesfully created.\n");

    if (mkdir("/tmp/rootfs/bin", 0777) == -1 && errno != EEXIST) { //pas besoin de CAP_SYS_ADMIN car on est dans /tmp/
        err(EXIT_FAILURE, "mkdir");
    }
    printf("/tmp/rootfs/bin directory succesfully created.\n");

    //copier sh dans ce dossier - exple pivot_root le fait pas, on le fait manuellement avant aussi
    //Finalement on fait avec busybox, car pas besoin de dépendances
    if (cp("/usr/bin/busybox", "/tmp/rootfs/bin/") == -1){
        err(EXIT_FAILURE, "cp");
    }
    printf("busybox succesfully copied.\n");

    //JUSQUE ICI, PAS BESOIN DE CAP_SYS_ADMIN DONC PAS BESOIN DE SUDO

    
    

    //on crée d'abord un child dans nv namespace 
    //il va lui même créer un child après avoir sethostname afin que le hostname soit à jour dès le lancement du process qu'on veut exécuter
    // -> j'ai abandonné l'idée pour l'instant, le hostname est quand même "nobody"



    //note: essayer d'utiliser clone3() à la place de clone?
    char *child_args[] = { "/tmp/rootfs", NULL };
    int child_pid = clone(child, stack + STACK_SIZE, CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD, child_args);

    if (child_pid == -1) {
        perror("clone");
        return 1;
    }
    
    
    printf("child lancé avec PID %d\n", child_pid);
    waitpid(child_pid, NULL, 0);

    printf("Fin du process parent\n");

    free(stack);
    return 0;
    
}



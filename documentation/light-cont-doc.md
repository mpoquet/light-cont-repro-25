# Introduction

Light-cont is a lightweight, minimalist container runtime that aims to isolate scientific experiments from side effects with the help of contenairization.
It was developped for a university project, at the University of Toulouse, by three undergraduate students.
It is programmed in C, and use 2 dependencies: libarchive, and jsmn, a lightweight json parser that consists of a single header file.
It only runs OCI images, but does not respect the OCI runtime specifications.
Please note that this project is only runnable on linux, as it relies on linux features to function.

# Features

## OCI image decompression

Light-cont can only accept OCI container images (tar archives, containing a index.json file and compressed layers, or blobs, conforming the OCI image specs).
The image is extracted in a temporary dir in /tmp/, then into the specified extract location (if none, ./rootfs by default).
It is also possible to pass in entry a OCI bundle: opened tarball, i.e. a directory containing the content of the tar archive.
The extracted image is not destroyed after execution, the user have to do it manually. This is for reusability reasons, as it is possible to run a container from an already extracted image (see options).

## Isolation

Namespaces are the roots of container isolation. Light-cont isolates the container in the usual namespaces used by most container runtimes.
- User namespace: the container thinks it has root access and all capabilities on the system, but cannot affect anything outside of himself.
- Mount namespace: the container does not have access to any mount points.
- PID namespace: the container sees itself at the n°1 process, but it's not the case on the host.
- UTS namespace: different hostname, and changes to the NIS domain name will not be repercuted on the host.
- IPC namespace: isolation from IPC resources, such as System V IPC objects and POSIX message queues.
- Cgroup namespace: changes the control groups root directory for the process.
- Network namespace: isolate the network. Please note that no bridge is set up, the container will be totally isolated from the network.
- Time namespace: used to lie to the container about the real values of the boottime clock and the monotonic clock. Please note that it is not the case here.

User, time, cgroup, uts, pid and network namespaces are all deactivable (see options).

- Environment: by default, environment variables are not kept in the execution of the container. It is possible to keep the current environment and to add environment variables manually (see options).

## Control groups

The container can be placed in a cgroup (see options). No limitations are applied, and the user must be superuser to do so.
Only cgroups version 2 are supported.

## Mounting outside directories

Light-cont can natively accept up to 20 directories (10 in read-only and 10 in read-write) to mount inside the container.
The `MAX_MOUNT_DIRS` constant inside the c source file can be modified if you need more than that.

# Installation and build

[Github repository link](https://github.com/mpoquet/light-cont-repro-25)

As the project consists of only one file and has only two dependencies, the build command is really simple: \
with GCC: `gcc light-cont.c -o light-cont -larchive`

The syntax might be slightly different with other compilers.

You can also use make to compile the project, as it comes with a makefile.
Please note that you need libarchive installed on your system, and light-cont needs `./include/jsmn.h` to function.

## Dependencies

Light-cont relies on two dependencies:

- libarchive is used to manipulate tar and gzip archive. [Link to the github repo](https://github.com/libarchive/libarchive)
- jsmn is a lightweight json parser, used to parse index and manifest in the OCI image. [Link to the github repo](https://github.com/zserge/jsmn)


# Options ands arguments

- `--help`: Display a help message.
- `--path loc`: Specify OCI image location, or the root directory of the extracted image if `--extracted` is used.
- `--extractpath loc`: Specify extract location for the image.
- `--extracted`:Specify that the image has already been extracted.
- `--run "/path/to/cmd arg1"`: Path (absolute from the container root directory) to the commande to run. By default, /bin/sh -i is executed.
- `--cgroup`: Include the runtime in a cgroup (v2 only). Need to be launched as superuser.
- `--network`: Disable Network isolation.
- `--mountrdonly /path/to/dir`: Specify a directory to mount inside the container, in read-only mode. 10 max, can be modified in the source code by modifying MAX_MOUNT_DIRS constant.
- `--mount /path/to/dir`: Specify a directory to mount inside the container, in read-write mode. 10 max, can be modified in the source code by modifying MAX_MOUNT_DIRS constant.
- `--nouserns`: Disable the use of an user namespace. Need to be launched as superuser.
- `--nopidns`: Disable the use of a PID namespace.
- `--nocgroupns`: Disable the use of a cgroup namespace.
- `--noutsns`: Disable the use of a UTS namespace.
- `--notimens`: Disable the use of a time namespace.
- `--test`: Launch embedded isolation tests.
- `--env VAR=val`: Add environment variable. If you call `--env KEEPCURRENTENV` in the first occurence of `--env` in the options, your current environment will be kept. You can add others environment variables by subsequent `--env` calls.



## Use examples

- Extract an OCI Alpine linux image located in `./images` and launch a shell in it: \
`./light-cont --path ./images/alpine.tar` \
Note: `--extractpath` and `--run` are not specified, therefore the image will be extracted in `./rootfs` and `./bin/sh -i` will be executed within the container. 
    
- Extract an OCI Alpine linux image located in `./images` into `./alpine` and execute `/bin/busybox echo Hello! `: \
`./light-cont --path ./images/alpine.tar --extractpath ./alpine --run "/bin/busybox echo Hello! "` \
Note: for some reason (linked to the shell presumably), running `"/bin/busybox echo Hello!"` without the space before the doublequote will leave a blank prompt in the shell and not launch the program
    
- Run `sysbench cpu run` in an already extracted sysbench image located in `./sysbench-rootfs`: \
`./light-cont --extracted --path ./sysbench-rootfs --run "/bin/sysbench cpu run"` 

- Run a shell in an already extracted image in `./rootfs`, mounting two dirs in read-only, two dirs in read-write and keeping the current environment: \
`./light-cont --extracted --path rootfs --mountrdonly ./in1 --mountrdonly ./in2 --mount out1 --mount ./out2 --env KEEPCURRENTENV` 

- Run `/bin/printenv` in an already extracted image in `./rootfs`, adding two environment variable from a blank environment: \
`./light-cont --extracted --path rootfs --env VAR1=42 --env MESSAGE=hello --run "/bin/printenv"` 

- Run a shell in an already extracted image in `./rootfs`, placing the container in a cgroup without using a user namespace nor a pid namespace: \
`sudo ./light-cont --extracted --path rootfs --cgroup --nouserns --nopidns` \
Note: both `--cgroup` and `--nouserns` options need to be launched by a superuser 

- Run the embedded minimal test suite in an already extracted image located in `./rootfs`: \
`./light-cont --extracted --path ./rootfs --test` \
Note: you MUST specify an image, even if you only run tests. 

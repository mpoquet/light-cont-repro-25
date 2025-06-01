from subprocess import *
import os
import argparse

N = 2

FILE_OUT: str = "./data.csv"
IMAGE: str = "./docker-image-sysbench.tar.gz"
IMAGE_OCI: str = "./sysbench.oci.tar"
IMAGE_OCI_PATH_FROM_MR: str = "../overhead_benchmark/sysbench.oci.tar"
IMAGE_NAME = "sysbench:tp"
ENGINES: list[str] = ["native", "docker", "runc", "podman", "crun", "youki", "light-cont"] 
TIME = "--time=20"

SYSBENCH_ARGS: dict[str, list[str]] = {
    "cpu": [
        "cpu",
        "--cpu-max-prime=2000",  # More demanding workload
        "--threads=4",             # Multi-threaded
        TIME                # Fixed duration
    ],

    "fileio": [
        "fileio",
        "--file-test-mode=seqwr",
        "--file-num=1",
        TIME
    ],

    "memory": [
        "memory",
        "--memory-block-size=4K",   # Common page size
        "--memory-total-size=20G",  # Large enough to exceed cache
        "--memory-oper=write",      # Or mixed with --memory-access-mode=rnd
        "--memory-scope=global",    # Test full system memory bandwidth
        "--threads=4",              # Multi-threaded
        TIME
    ]
    
}

'''
#args pour tester avec ramfs
"fileio": [
        "fileio",
        "--file-test-mode=rndrw",
        "--file-total-size=2G",
        "--file-num=4",
        "--file-extra-flags=direct", # Bypass cache
        "--file-fsync-freq=100",
        "--file-rw-ratio=4",
        "--file-path=/mnt/ramfs",  # Pointing to RAMFS
        TIME
    ]
'''


def setup_ramfs():
    ramfs_path = "/mnt/ramfs"
    if not os.path.exists(ramfs_path):
        os.makedirs(ramfs_path, exist_ok=True)
        run(["mount", "-t", "ramfs", "ramfs", ramfs_path])
        # Ajuster les permissions si nécessaire
        run(["chmod", "777", ramfs_path])

def print_running_cmd(cmd: list[str]):
    cmd_joined:str = " ".join(cmd)
    formatted_str: str = "Running: {}".format(cmd_joined)
    print(formatted_str)


def generate_load_cmd(engine: str) -> list[str]:
    if engine in ["docker", "podman"]:
        return [engine, "load", "-i", IMAGE]
    if engine == "crictl":
        return [engine, "load", IMAGE]
    if engine == "light-cont":
        return ["../minimalist_runtime/light-cont", "--path", "./sysbench.oci.tar", "--extractpath", "./sysbench-rootfs", "--run", "\"nothing\""]
        #la commande nothing ne va pas être trouvé dans le fs donc light-cont va extraire l'image dans sysbench-rootfs et se terminer en renvoyant une erreur
        #pas encore implémenté le fait de pouvoir extract une image sans rien lancer dessus
    return None

def generate_run_cmd(engine: str, sysbench_args: list[str], mode: str, args) -> list[str]:
    if engine in ["docker", "podman"]:
        if args.fileio_ramfs == True:
            return [engine, "run", "--rm", "--network=none", "--volume", "/tmp/benchfileio:/benchfileio", "--workdir", "/benchfileio", IMAGE_NAME, "sysbench"] + sysbench_args + ["run"]
        return [engine, "run", "--rm", "--network=none", IMAGE_NAME, "sysbench"] + sysbench_args + ["run"] 
    if engine == "native":
        if args.fileio_ramfs == True:
            os.chdir("/tmp/benchfileio")
        return ["sysbench"] + sysbench_args + ["run"] 
    if engine == "light-cont":
        full_cmd = ["/bin/sysbench"] + sysbench_args + ["run"]
        cmd_str = " ".join(full_cmd) 
        return [
            "../minimalist_runtime/light-cont",
            "--extracted",
            "--path", "./sysbench-rootfs",
            "--run", cmd_str
        ]
    if engine in ["runc", "crun", "youki"]:
        dir_name_build = ["./bench-"] + [mode]
        bench_dir_name = "".join(dir_name_build)
        return [engine, "run", "--bundle", bench_dir_name, "sysbench"]
    if engine == "styrolite":
        #todo
        return None
    return None

def cleanup():
    run(["umount", "/mnt/ramfs"], check=False)
    os.rmdir("/mnt/ramfs")

def main():
    '''
    # Configurer RAMFS pour les tests fileio
    if any("fileioramfs" in args for args in SYSBENCH_ARGS.values()):
        print("Setting up RAMFS for fileio tests...")
        setup_ramfs()
    '''

    parser = argparse.ArgumentParser()
    parser.add_argument('--only', type=str)
    parser.add_argument('--test-n', type=int)
    parser.add_argument('--test-type', choices=SYSBENCH_ARGS.keys(), type=str)
    parser.add_argument('--fileio-ramfs', action="store_true")
    parser.add_argument('--cpu-mitigate-noise', action="store_true")

    args = parser.parse_args()

    if args.only is not None and args.only in ENGINES:
        engines = [args.only]
    else:
        engines = ENGINES

    if args.test_n is not None and args.test_n > 0:
        N = args.test_n

    if args.test_type:
        bench_args = {args.test_type: SYSBENCH_ARGS[args.test_type]}
    else: 
        bench_args = SYSBENCH_ARGS
    
    print(bench_args)

    for engine in engines:
        if engine not in ["native", "crun", "youki", "runc"]:
            print("Loading image for {}...".format(engine))
            load_cmd = generate_load_cmd(engine)
            print_running_cmd(load_cmd)
            run(load_cmd, stdout = DEVNULL)
        
    print("Begin tests")

    #option permettant de mitiger le bruit liée à la possible (sur)chauffe préalable du cpu en lançant les technos à tour de rôle. 
    # Il faut également lancer avec l'option --test-type cpu
    if args.cpu_mitigate_noise:
        i = 0
        while (i < N):
            
            for engine in engines:
                sysbench_args: list[str] = bench_args["cpu"]
                run_cmd = generate_run_cmd(engine, sysbench_args, "cpu", args)
                print("[engine={}, test={}, num={}/{}]".format(engine, "cpu", i + 1, N))
                #print_running_cmd(run_cmd)
                p1 = Popen(run_cmd, stdout=PIPE, stderr=PIPE) #stdout=PIPE
                parse_cmd = ["python", "./sysbench_script.py", "-c", engine, "-b", "cpu", "-o", FILE_OUT]
                p2 = Popen(parse_cmd, stdin=p1.stdout, stdout=PIPE)
                p1.stdout.close()   
                p2.communicate()
                p1.wait()
            i = i + 1


    else:
        for engine in engines:
            for mode in bench_args:
                #pour crun et youki:
                #changer config.json pour avoir le fichier config avec la bonne commande à lancer
                #option --config <fichier>
                sysbench_args: list[str] = bench_args[mode]
                run_cmd = generate_run_cmd(engine, sysbench_args, mode, args)
                #print("DEBUG: run_cmd = ", run_cmd)
                for n in range(1, N + 1):
                    if mode == "lauchtime":
                        #COMPLETER ICI
                    
                    else:
                    print("[engine={}, test={}, num={}/{}]".format(engine, mode, n, N))
                    #print_running_cmd(run_cmd)
                    p1 = Popen(run_cmd, stdout=PIPE, stderr=PIPE) #stdout=PIPE
                    parse_cmd = ["python", "./sysbench_script.py", "-c", engine, "-b", mode, "-o", FILE_OUT]
                    p2 = Popen(parse_cmd, stdin=p1.stdout, stdout=PIPE)
                    p1.stdout.close()   
                    p2.communicate()
                    p1.wait()

if __name__ == "__main__":
    main()
    #cleanup()

from subprocess import *
import os

N = 30

FILE_OUT: str = "./data.csv"
IMAGE: str = "./docker-image-sysbench.tar.gz"
IMAGE_OCI: str = "./sysbench.oci.tar"
IMAGE_OCI_PATH_FROM_MR: str = "../overhead_benchmark/sysbench.oci.tar"
IMAGE_NAME = "sysbench:tp"
ENGINES: list[str] = ["light-cont", "native", "ctr", "podman", "docker"] #, "crictl"]
# TODO ajouter CRI-O

# ChatGPT
SYSBENCH_ARGS: dict[str, list[str]] = {
    "fileio": [
        "fileio",
        "--file-test-mode=rndrw",  # Mixed random read/write
        "--file-total-size=2G",    # Explicit size
        "--file-num=4",            # Multiple files
        "--file-extra-flags=direct",  # Bypass cache
        "--file-fsync-freq=100",   # Realistic fsync frequency
        "--file-rw-ratio=4"       # 4:1 read:write ratio
    ],
    "cpu": [
        "cpu",
        "--cpu-max-prime=100000",  # More demanding workload
        "--threads=4",             # Multi-threaded
        "--time=30"                # Fixed duration
    ],
    "memory": [
        "memory",
        "--memory-block-size=4K",   # Common page size
        "--memory-total-size=20G",  # Large enough to exceed cache
        "--memory-oper=write",      # Or mixed with --memory-access-mode=rnd
        "--memory-scope=global",    # Test full system memory bandwidth
        "--threads=4"              # Multi-threaded
    ]
}

def print_running_cmd(cmd: list[str]):
    cmd_joined:str = " ".join(cmd)
    formatted_str: str = "Running: {}".format(cmd_joined)
    print(formatted_str)


def generate_load_cmd(engine: str) -> list[str]:
    if engine in ["docker", "podman", "ctr"]:
        return [engine, "load", "-i", IMAGE]
    if engine == "crictl":
        return [engine, "load", IMAGE]
    if engine == "light-cont":
        return ["../minimalist_runtime/light-cont", "--path", "../overhead_benchmark/sysbench.oci.tar", "--extractpath ./sysbench-rootfs", "--run \"nothing\""]
        #ne fonctionne pas pour l'ins
        #la commande nothing ne va pas être trouvé dans le fs donc light-cont va extraire l'image dans sysbench-rootfs et se terminer en renvoyant une erreur
        #pas encore implémenté le fait de pouvoir extract une image sans rien lancer dessus
    return None

def generate_run_cmd(engine: str, sysbench_args: list[str]) -> list[str]:
    if engine in ["docker", "podman"]:
        return [engine, "run", "--rm", "--network", "host", IMAGE_NAME, "sysbench"] + sysbench_args + ["run"]
    if engine == "ctr":
        return [engine, "run", "--rm", "docker.io/library/"+IMAGE_NAME, "sysbench", "sysbench"] + sysbench_args + ["run"]
    if engine == "crictl":
        return [engine, "exec", IMAGE_NAME, "sysbench"] + sysbench_args + ["run"]
    if engine == "native":
        return ["sysbench"] + sysbench_args + ["run"]
    if engine == "light-cont":
        sysbench_cmd = ["\"/bin/sysbench"] + sysbench_args + ["\"" ]
        print("sysbench_cmd = {}".format(sysbench_cmd))
        return ["../minimalist_runtime/light-cont", "--extracted", "--path ./sysbench-rootfs", "--run "] + sysbench_cmd + ["run"]
    return None


def main():


    for engine in ENGINES:
        if engine != "native" and engine != "light-cont":
            print("Loading image for {}...".format(engine))
            load_cmd = generate_load_cmd(engine)
            print_running_cmd(load_cmd)
            run(load_cmd, stdout = DEVNULL)
        
    print("Begin tests")
    for engine in ENGINES:
        for mode in SYSBENCH_ARGS:
            sysbench_args: list[str] = SYSBENCH_ARGS[mode]
            run_cmd = generate_run_cmd(engine, sysbench_args)
            #print("DEBUG: run_cmd = ", run_cmd)
            for n in range(1, N + 1):
                print("[engine={}, test={}, num={}/{}]".format(engine, mode, n, N))
                #print_running_cmd(run_cmd)
                p1 = Popen(run_cmd, stdout=PIPE, stderr=PIPE)
                parse_cmd = ["python", "./sysbench_script.py", "-c", engine, "-b", mode, "-o", FILE_OUT]
                p2 = Popen(parse_cmd, stdin=p1.stdout, stdout=PIPE)
                p1.stdout.close()   # Très important
                p2.communicate()
                p1.wait()


if __name__ == "__main__":
    main()

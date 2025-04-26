from subprocess import *
import os

N = 30

FILE_OUT: str = "./data.csv"
IMAGE: str = "./docker-image-sysbench.tar.gz"
IMAGE_NAME = "sysbench:tp"
ENGINES: list[str] = ["native", "ctr", "podman", "docker"] #, "crictl"]
# TODO ajouter CRI-O

SYSBENCH_ARGS: dict[str, list[str]] = {
        "fileio": ["fileio", "--file-test-mode=seqwr", "--file-num=1"],
        "cpu": ["cpu", "--cpu-max-prime=2000"],
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
    return None


def main():


    for engine in ENGINES:
        if engine != "native":
            print("Loading image for {}...".format(engine))
            load_cmd = generate_load_cmd(engine)
            print_running_cmd(load_cmd)
            run(load_cmd, stdout = DEVNULL)
        
    print("Begin tests")
    for engine in ENGINES:
        for mode in SYSBENCH_ARGS:
            sysbench_args: list[str] = SYSBENCH_ARGS[mode]
            run_cmd = generate_run_cmd(engine, sysbench_args)
            for n in range(1, N + 1):
                print("[engine={}, test={}, num={}/{}]".format(engine, mode, n, N))
                #print_running_cmd(run_cmd)
                p1 = Popen(run_cmd, stdout=PIPE, stderr=DEVNULL)
                parse_cmd = ["python", "./sysbench_script.py", "-c", engine, "-b", mode, "-o", FILE_OUT]
                p2 = Popen(parse_cmd, stdin=p1.stdout, stdout=PIPE)
                p1.stdout.close()   # Très important
                p2.communicate()
                p1.wait()


if __name__ == "__main__":
    main()
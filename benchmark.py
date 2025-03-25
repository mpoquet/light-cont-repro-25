from subprocess import *
import os

N = 4

FILE_OUT: str = "./file_out.txt"
IMAGE: str = "./docker-image-sysbench.tar.gz"
IMAGE_NAME = "sysbench:tp"
ENGINES: list[str] = ["podman", "docker"]
# TODO ajouter CRI-O


LOAD_ARGS: list[str] = ["load", "-i"]
RUN_ARGS: list[str] = ["run", "--rm", "--network", "host"]

SYSBENCH_ARGS: dict[str, list[str]] = {
        "fileio": ["--test=fileio", "--file-test-mode=seqwr", "--file-num=1"]
        }

for engine in ENGINES:
    load_cmd = [engine] + LOAD_ARGS + [IMAGE]
    run(load_cmd)
    for mode in SYSBENCH_ARGS:
        for _ in range(N):
            sysbench_args: list[str] = SYSBENCH_ARGS[mode]
            run_cmd = [engine] + RUN_ARGS + [IMAGE_NAME] + ["sysbench"] + sysbench_args + ["run"]
            p1 = Popen(run_cmd, stdout = PIPE)
            parse_cmd = ["python", "./sysbench_script.py", "-c", engine, "-b", mode, "-o", FILE_OUT]
            p2 = Popen(parse_cmd, stdin = p1.stdout, stdout = PIPE)
            p1.commumicate(p2)











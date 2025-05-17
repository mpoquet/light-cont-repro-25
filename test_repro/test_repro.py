import os
import time
import subprocess
import socket

HOST_TEST:str = "example.com"
ENGINES: list[str] = ["light-cont", "native", "ctr", "podman", "docker"]
IMAGE_NAME =
# Tester l'accès au système de fichiers
def test_filesystem_access():
    try:
        with open("./test.txt", "x") as f:
            print("Accès au fichier réussi.")
    except PermissionError:
        print("Accès au fichier refusé.")

# Tester l'accès réseau
def test_network_access():
    print("Testing network access...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except socket.error as err:
        print("Socket creation failed {}".format(err))
        return False
    print("Socket succesfully created")
    port = 80
    try:
        host_ip = socket.gethostbyname(HOST_TEST)
    except socket.gaierror:
        print("Can't resolve host {}".format(HOST_TEST))
        return False
    try:
        s.connect((host_ip, port))
    except socket.error as err:
        print("Can't connect to {}".format(HOST_TEST))
        return False
    return True

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


# Tester le temps courant
def test_current_time():
    print(f"Temps courant : {time.ctime()}")

# Exécuter les tests
if __name__ == "__main__":

    #test_filesystem_access()
    test_network_access()
    #test_current_time()

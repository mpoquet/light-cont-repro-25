import os
import time
import subprocess
import socket

HOST_TEST:str = "example.com"
RUNTIMES: list[str] = ["light-cont", "native", "runc", "crun", "youki"]
TEST_IMAGE = 'alpine test'
FS_TEST_FILE = '/tmp/container_test_file'
FS_TEST_CONTENT =
NETWORK_TEST_URL = 'http://example.com'
STYROLITE_CHEMIN = '~/styrolite'


def setup():
    """Ensure test image is available"""
    print("Pulling test image...")
    subprocess.run(["docker", "pull", TEST_IMAGE], check=True)
    subprocess.run(["podman", "pull", TEST_IMAGE], check=True)
    subprocess.run(["sh","../minimalist_runtime/pull_and_save_oci.sh", IMAGE_NAME, "."], check=True)
    #Les engines tel que docker, podman ont pour rôle de pull les images

# Tester l'accès au système de fichiers
def test_filesystem_access(runtime: str, fresult):
    message_succes = runtime + " Accès au fichier réussi."
    message_defaut = runtime + " Accès au fichier refusé."
    if runtime == "native":
        try:
            with open(FS_TEST_FILE, "x") as f:
                f.write(FS_TEST_CONTENT)
            with open(FS_TEST_FILE )
                content = f.read()
            f.write(message_succes)
        except PermissionError:
            f.write(message_defaut)
    else:
        cmd = []
        if runtime == "light-cont":
            cmd = [
            "../minimalist_runtime/light-cont",
            "--path", TEST_IMAGE,
            "--extractpath", "./sysbench-rootfs",  # Split into separate argument
            "--run",
            "sh -c 'echo $(id -u) > /test.txt && echo \"light-cont: Accès réussi\" || echo \"light-cont: Accès refusé\"'"
            ]
        elif runtime == "runc":
            cmd = [
                "docker",
                "run",
                "--rm",
                "-v", "/tmp:/tmp",
                "alpine",
                "sh", "-c",
                "echo $(id -u) > /test.txt && echo \"runc: Accès réussi\" || echo \"runc: Accès refusé\""
                ]
        elif runtime == "crun":
            cmd = [
                "podman",  # Now using Podman → crun
                "run",
                "--rm",
                "-v", "/tmp:/tmp",
                "alpine",
                "sh", "-c",
                "echo $(id -u) > /test.txt && echo 'crun: Success' || echo 'crun: Failed'"
                ]
        elif runtime == "youki":
            cmd = [
                "youki",
                "run",
                "--bundle", "mycontainer",  # Path to OCI bundle
                "utopia39",           # Unique container ID
                "sh", "-c",
                 "echo $(id -u) > /test.txt && echo 'youki: Success' || echo 'crun: Failed'"
            ]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True)
        except subprocess.CalledProcessError as e:
            print(f"Command failed with exit code {e.returncode}")
    return None

# Tester l'accès réseau
def test_network_access(runtime: str, result_file: TextIO):
    test_host = "example.com"
    test_port = 80
    timeout_sec = 5
    success_msg = f"{runtime}: Socket creation and connection successful"
    fail_msg = f"{runtime}: Socket creation/connection failed"

    if runtime == "native":
        try:
            # Create socket and attempt connection
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(timeout_sec)
                s.connect((test_host, test_port))
                result_file.write(success_msg + f" (Connected to {test_host}:{test_port})\n")
        except Exception as e:
            result_file.write(f"{fail_msg}: {str(e)}\n")
    else:
        # Socket test command (uses /dev/tcp for bash-based testing)
        socket_test_cmd = (
            f"timeout {timeout_sec} bash -c '"
            f'if echo -n "" > /dev/tcp/{test_host}/{test_port}; then '
            f'echo "{success_msg}"; '
            f'else echo "{fail_msg}"; '
            f"fi' 2>/dev/null"
        )

        cmd = []
        if runtime == "light-cont":
            cmd = [
                "../minimalist_runtime/light-cont",
                "--path", TEST_IMAGE,
                "--extractpath", "./sysbench-rootfs",
                "--run", socket_test_cmd
            ]
        elif runtime == "runc":
            cmd = [
                "docker", "run", "--rm", "--network", "host",
                "alpine",
                "sh", "-c", socket_test_cmd
            ]
        elif runtime == "crun":
            cmd = [
                "podman", "run", "--rm", "--network", "host",
                "alpine",
                "sh", "-c", socket_test_cmd
            ]
        elif runtime == "youki":
            cmd = [
                "youki", "run", "--bundle", "mycontainer",
                "network_test",
                "sh", "-c", socket_test_cmd
            ]

        try:
            result = subprocess.run(
                cmd,
                stdout=result_file,
                stderr=subprocess.PIPE,
                text=True
            )
            if result.returncode != 0:
                result_file.write(f"{runtime}: Process failed (exit code {result.returncode})\n")
        except Exception as e:
            result_file.write(f"{runtime}: Execution error: {str(e)}\n")


def test_current_time(runtime: str, tresult):
    message_success = f"{runtime}: Time fetched successfully"
    message_failure = f"{runtime}: Failed to get time"

    if runtime == "native":
        from datetime import datetime
        tresult.write(f"{runtime}: {datetime.now().isoformat()}")
    else:
        cmd = []
        if runtime == "light-cont":
            cmd = [
                "../minimalist_runtime/light-cont",
                "--path", TEST_IMAGE,
                "--extractpath", "./sysbench-rootfs",
                "--run",
                f"sh -c 'date -u +\"%Y-%m-%dT%H:%M:%SZ\" && echo \"{message_success}\" || echo \"{message_failure}\"'"
            ]
        elif runtime in ["runc", "crun"]:
            runtime_cmd = "docker" if runtime == "runc" else "podman"
            cmd = [
                runtime_cmd, "run", "--rm",
                "alpine",
                "sh", "-c",
                f"date -u +'%Y-%m-%dT%H:%M:%SZ' && echo '{message_success}' || echo '{message_failure}'"
            ]
        elif runtime == "youki":
            cmd = [
                "youki", "run", "--bundle", "mycontainer",
                "time_test",
                "sh", "-c",
                f"date -u +'%Y-%m-%dT%H:%M:%SZ' && echo '{message_success}' || echo '{message_failure}'"
            ]

        try:
            result = subprocess.run(cmd, capture_output=True, text=True)
            tresult.write(result.stdout)
        except subprocess.CalledProcessError as e:
            tresult.write(f"Command failed: {e.stderr}")

# Exécuter les tests
if __name__ == "__main__":
     try:
        with open("./fresult", "x") as fresult
    except PermissionError:
            f.write(message_defaut)
    for runtime in RUNTIMES:
        test_filesystem_access(runtime, fresult )

    #test_network_access()
    #test_current_time()

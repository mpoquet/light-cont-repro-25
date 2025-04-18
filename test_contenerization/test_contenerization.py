import os
import time
import subprocess
import socket

HOST_TEST:str = "example.com"

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

# Tester le temps courant
def test_current_time():
    print(f"Temps courant : {time.ctime()}")

# Exécuter les tests
if __name__ == "__main__":
    #test_filesystem_access()
    test_network_access()
    #test_current_time()

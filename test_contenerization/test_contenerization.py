import os
import time
import subprocess

# Tester l'accès au système de fichiers
def test_filesystem_access():
    try:
        with open("./test.txt", "r") as f:
            print("Accès au fichier réussi.")
    except PermissionError:
        print("Accès au fichier refusé.")

# Tester l'accès réseau
def test_network_access():
    try:
        subprocess.run(["ping", "-c", "1", "google.com"], check=True)
        print("Accès réseau réussi.")
    except subprocess.CalledProcessError:
        print("Accès réseau échoué.")

# Tester le temps courant
def test_current_time():
    print(f"Temps courant : {time.ctime()}")

# Exécuter les tests
if __name__ == "__main__":
    test_filesystem_access()
    test_network_access()
    test_current_time()

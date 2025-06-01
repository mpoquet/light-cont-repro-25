#!/usr/bin/env python3
import os
import subprocess
import unittest
import shutil
from pathlib import Path

class ContainerRuntimeTest(unittest.TestCase):
    # Configurations
    TEST_IMAGE = "python:3.9-alpine"
    TEMP_DIR = Path("/dev/shm/container_test")
    RUNTIMES = ["docker", "podman"]

    @classmethod
    def setUpClass(cls):
        """Initialisation des tests"""
        cls.TEMP_DIR.mkdir(exist_ok=True, mode=0o777)
        cls.build_test_image()

    @classmethod
    def build_test_image(cls):
        """Construit l'image de test pour tous les runtimes"""
        dockerfile = cls.TEMP_DIR / "Dockerfile"
        dockerfile.write_text(f"""
        FROM {cls.TEST_IMAGE}
        RUN mkdir -p /test_workspace && chmod 777 /test_workspace
        VOLUME /test_workspace
        WORKDIR /test_workspace
        """)

        for runtime in cls.RUNTIMES:
            try:
                subprocess.run(
                    [runtime, "build", "-t", "fs-test", "-f", str(dockerfile), str(cls.TEMP_DIR)],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE
                )
            except (subprocess.CalledProcessError, FileNotFoundError) as e:
                print(f"⏩ Runtime {runtime} non disponible : {e.stderr.decode().strip() or 'Non installé'}")
                cls.RUNTIMES.remove(runtime)

        if not cls.RUNTIMES:
            raise unittest.SkipTest("Aucun runtime container disponible (Docker/Podman)")

    def test_temporary_files(self):
        """Vérifie l'isolation des fichiers temporaires avec tmpfs"""
        for runtime in self.RUNTIMES:
            with self.subTest(runtime=runtime):
                # Solution 1: Double backslash pour l'échappement
                cmd = [
                    runtime, "run", "--rm", "--tmpfs", "/tmp:rw,noexec,nosuid,size=1m",
                    "fs-test", "sh", "-c", "touch /tmp/testfile && ls /tmp/testfile"
                ]

                # Solution alternative: Utilisation de raw string
                # cmd = [
                #    runtime, "run", "--rm", "--tmpfs", "/tmp:rw,noexec,nosuid",
                #    "fs-test", "sh", "-c", r"touch /tmp/testfile && ls /tmp/testfile"
                # ]

                result = subprocess.run(cmd, capture_output=True, text=True)
                self.assertIn("/tmp/testfile", result.stdout)
                self.assertEqual(result.returncode, 0,
                               f"{runtime} n'a pas correctement isolé /tmp")

    def test_host_volume_isolation(self):
        """Teste l'isolation des volumes montés"""
        test_file = self.TEMP_DIR / "host_data.txt"
        test_file.write_text("contenu_original")

        for runtime in self.RUNTIMES:
            with self.subTest(runtime=runtime):
                # Solution recommandée: raw string pour les commandes shell
                cmd = [
                    runtime, "run", "--rm",
                    "-v", f"{self.TEMP_DIR}:/test_workspace:ro",
                    "fs-test", "sh", "-c",
                    r"echo 'modification' > /test_workspace/host_data.txt 2>/dev/null || true"
                ]

                subprocess.run(cmd, check=False)
                self.assertEqual(test_file.read_text(), "contenu_original",
                                f"{runtime} a permis une modification non autorisée")

    def test_absolute_paths(self):
        """Vérifie la gestion des chemins via variables d'environnement"""
        for runtime in self.RUNTIMES:
            with self.subTest(runtime=runtime):
                # Correction du problème d'échappement:
                # Solution 1: Double backslash
                cmd = [
                    runtime, "run", "--rm",
                    "-e", "DATA_PATH=/test_workspace/data",
                    "fs-test", "sh", "-c",
                    "mkdir -p \\$DATA_PATH && touch \\$DATA_PATH/file"
                ]

                # Solution alternative (préférée): raw string
                # cmd = [
                #    runtime, "run", "--rm",
                #    "-e", "DATA_PATH=/test_workspace/data",
                #    "fs-test", "sh", "-c",
                #    r"mkdir -p \$DATA_PATH && touch \$DATA_PATH/file"
                # ]

                result = subprocess.run(cmd, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0,
                               f"{runtime} n'a pas géré correctement les variables de chemin")

    @classmethod
    def tearDownClass(cls):
        """Nettoyage des ressources"""
        for runtime in cls.RUNTIMES:
            subprocess.run(
                [runtime, "rmi", "-f", "fs-test"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
        shutil.rmtree(cls.TEMP_DIR, ignore_errors=True)

if __name__ == "__main__":
    unittest.main(
        verbosity=2,
        argv=[__file__] + (["-v"] if os.getenv("VERBOSE") else [])
    )

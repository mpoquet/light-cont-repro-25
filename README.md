# Project TODO
TODO

## Rendu n°1
- Début de listage des privilèges et contraintes: pdf dans docs
- Début de code pour évaluer l'overhead de chaque technologie: becnhmark.py dans overhead_benchmark
- Début de code de test de sources de non-reproductibilités: test_contenerization
- Début de code du runtime minimal: minimalist runtime

Technologies retenues pour la veille: Docker, udocker, podman, containerd, apptainer (anciennement singularity)

Avancement du runtime minimal:
    - Isole le processus dans un user namespace
    - Isole également les pid et le hostname
    - Effectue un pivot_root afin d'isoler du file system de l'hôte
    - Lance un bash depuis le fs ubuntu copié dans le nouveau rootfs
    - Pas de cgroups
    - Crée la nouvelle racine dans le répertoire courant (ici minimalist_runtime)


## License
- Code: Apache-2.0
- Everything else, in particular documentation and measurements: CC-BY-SA-4.0

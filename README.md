# Project TODO
TODO

## BRANCHE UNSTABLE
cgroups v2 supportés, pas d'options au démarrage, il faut changer l'option cgroupv2_option dans le code directement.
Inclus le child dans un nouveau cgroups (v2) 
-> Problème: besoin de lancer avec sudo pour modifier /sys/fs/cgroup
    -> en tant que su, on ne peut plus créer de répertoire dans le dossier utilisateur (pourquoi?)
        -> donc erreur en créant le dossier oldroot lors du pivot_root

Pas de comptabilité des ressources utilisées pour l'instant

TODO:
    - traitement des options pour pouvoir lancer le programme avec --cgroupv2
    - régler le problème du super utilisateur qui ne peut pas créer de dossier dans un répertoire utilisateur
    - comptabilité ressources utilisées
    - support cgroup v1
    - suite développement


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

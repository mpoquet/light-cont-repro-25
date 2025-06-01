# Script de benchmark des différents runtimes

## Installation, compilation et dépendances

Nécessite Python 3. Pour fonctionner, le programme a besoin de sysbench_script.py, déjà présent dans le même dossier.
Le programme lance trois types de benchmarks sysbench: CPU, memory et file i/o, sur les différents runtimes de conteneur cités plus bas, mais également en natif (en dehors d'un conteneur) et avec light-cont.

Pour fonctionner correctement, le programme a besoin que les runtimes suivants soient installés sur la machine: docker, podman, runc, crun et youki. Light-cont doit également être compilé dans son dossier minimalist_runtime. 
Egalement: 
- [sysbench](https://github.com/akopytov/sysbench) doit être installé sur la machine.
- l'archive gzip `docker-image-sysbench.tar.gz` (fourni par M. Poquet) [(Lien de téléchargement)](https://www.swisstransfer.com/d/ba7e72c8-3cbe-4602-a664-5805bb932a01) **doit être présent dans le répertoire overhead_benchmark**
- la tarball `sysbench.oci.tar` [(Lien de téléchargement)](https://www.swisstransfer.com/d/1ea0199a-cf8e-4172-b8f3-5b099f0853d6) **doit être présent dans le répertoire overhead_benchmark** Comment l'obtenir si le lien a expiré:
    - lancer `./convert_sysbenchtar_to_oci.sh`
    - `docker-image-sysbench.tar.gz` doit déjà être présent dans le répertoire
- Avant de lancer le programme, lancer `install_rootfs_in_bench_reps.sh`, script qui va utiliser light-cont pour extraire `sysbench.oci.tar` vers les trois répertoires bench-\[mode], nécessaire pour que runc, crun et youki fonctionne correctement.
- Note: les deux images sysbench étant trop lourdes, nous avons préféré ne pas les inclure dans le repo github, voici pourquoi ces étapes de setup sont nécessaires.

## Lancement

Lancer avec `python benchmark.py`. Il est nécessaire d'avoir lancé au préalable le daemon de docker (`systemctl start docker`, nécessite l'accès root).

### Options principales

Les options sont combinables.

- `--only engine`: Lance les benchmarks seulement sur l'engine (technologie de runtime de conteneur) spécifié. Le runtime doit être dans la liste des runtimes supportés (docker, podman, runc, crun, youki, native, light-cont).

- `--test-type benchmark`: Lance seulement le benchmark du type spécifié (cpu, fileio, memory).

- `--test-n nb`: Lance nb fois les tests.

- `--cpu-mitigate-noise`: Lance seulement les tests cpu, en alternant les technos pour mitiger le bruit liée à la chauffe du cpu


## Contraintes

Nécessite sysbench, docker, podman, runc, crun et youki installées sur la machine pour fonctionner correctement. Si un runtime n'est pas installé, le programme se lancera mais les tests ne s'exécuteront évidemment pas pour ce programme. \
Nécessite également l'image `sysbench:tp` (provenant de l'archive [`docker-image-sysbench.tar.gz`](https://www.swisstransfer.com/d/ba7e72c8-3cbe-4602-a664-5805bb932a01)), et la tarball `sysbench.oci.tar` présente dans le même répertoire. 
[Lien vers un transfert de la tarball sysbench.oci.tar](https://www.swisstransfer.com/d/1ea0199a-cf8e-4172-b8f3-5b099f0853d6)
Si le lien a expiré, vous pouvez lancer `./convert_sysbenchtar_to_oci.sh`, un script simple qui utilise podman pour convertir `docker-image-sysbench.tar.gz` en `sysbench-oci.oci.tar`. Ce script a été fait à la hâte est n'est pas modulable. Vous pouvez cependant le modifier au besoin.
**Voir installation pour la premier lancement du programme, plusieurs étapes de setup est nécessaire**
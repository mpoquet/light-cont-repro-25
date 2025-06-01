# Projet Bureau d'études: Runtime légers de conteneurs pour exécutions reproductible

# Liste des programmes et livrables

# Light-cont : runtime minimaliste

Pour plus d'information, voir la documentation complète (en anglais): [/documentation/light-cont-doc.md](./documentation/light-cont-doc.md)

## Installation, compilation et dépendances

Light-cont est seulement compatible avec les systèmes d'exploitation Linux, car il repose sur des fonctionnalités du noyau linux pour fonctionner. \
Light-cont possède 2 dépendances, libarchive et jsmn 

- Libarchive est une librairie plutôt populaire généralement présente dans les bases de données des gestionnaires de paquets. Également disponible sur leur [repo Github](https://github.com/libarchive/libarchive). La version utilisée pendant le développement est la version `3.7.9-2`, cependant cela devrait fonctionner avec des versions ultérieures.

- Jsmn est un parser de fichier json minimaliste sous licence MIT, consistant seulement un fichier d'en-tête, jsmn.h, déjà présent dans ce projet à /minimalist_runtime/include/jsmn.h . [Lien vers leur repo Github](https://github.com/zserge/jsmn)

La compilation de light-cont peut se faire facilement à l'aide de la commande make dans le dossier /minimalist_runtime, car un makefile est présent. Il est également possible de taper la commande soi-même (avec gcc): `gcc light-cont.c -o $(PROG) -larchive` \
D'autres compilateurs requièrent peut être une syntaxe légèrement différente.

## Lancement

Light-cont traite seulement des images de conteneurs au format OCI. Il également possible d'exécuter des bundle déjà décompressés, c'est-à-dire un répertoire contenant le file system de l'image. Voici les principales options pour un démarrage rapide:

### Options principales

- `--help`: Affiche un message d'aide.
- `--path loc`: Spécifier la localisation `loc` de l'image OCI. Si `--extracted` est utilisé, alors spécifier plutôt la localisation du répertoire racine du file system de l'image décompressée.
- `--extractpath loc`: Spécifier la localisation `loc` où extraire l'image. Si non spécifié, par défaut la localisation sera `./rootfs`.
- `--extracted`: Spécifier que l'image a déjà été extraite. Alors, l'argument `--path` doit indiquer la localisation du répertoire racine du file system de l'image décompressée.
- `--run "/path/to/cmd arg1"`: Chemin absolu depuis le répertoire racine de l'image vers la commande à exécuter, et les arguments à passer. Si non spécifié, par défaut la commande exécutée sera `/bin/sh -i`.

Toutes les options n'ont pas été listées. Pour plus d'informations, consulter la documentation complète se trouvant à [/documentation/light-cont-doc.md](./documentation/light-cont-doc.md). 

### Exemples d'utilisations

- Extraire une image OCI d'Alpine Linux se trouvant dans `./images` et lancer un shell à l'intérieur: \
`./light-cont --path ./images/alpine.tar` \
Note: `--extractpath` et `--run` ne sont pas présentes, donc l'image sera extraite vers `./rootfs` et `./bin/sh -i` sera exécuté à l'intérieur du conteneur.

- Lancer `sysbench cpu run` dans une image de sysbench déjà extraite à la localisation `./sysbench-rootfs`: \
`./light-cont --extracted --path ./sysbench-rootfs --run "/bin/sysbench cpu run"` \

Pour plus d'exemples d'utilisations, consulter la documentation complète se trouvant à [/documentation/light-cont-doc.md](./documentation/light-cont-doc.md).

## Contraintes

Light-cont repose entièrement sur des fonctionnalités apportées par le kernel Linux, ainsi il est impossiblde de lancer le programme sur un autre système d'exploitation.
La version minimale du kernel pour utiliser le programme est la version `5.7`. 
Le noyau doit également autoriser les unprivileged user namespace. 
Seulement les cgroups version 2 sont supportés. Pour lancer le programme avec les options `--cgroup` ou `--nouserns`, l'utilisitateur doit être root.

### Troubleshooting

Pour extraire une image, light-cont extrait temporairement son contenu dans `/tmp/...`. Si votre répertoire `/tmp/` est monté en tant que tmpfs ou ramfs et que votre mémoire vive est saturée, il est possible que l'image ne puisse pas être extraite. Vous verrez alors une succession de `Write failed` dans votre terminal. Il faut alors libérer la mémoire avant de relancer le programme.

# Script de benchmark des différents runtimes

## Installation, compilation et dépendances

Nécessite Python 3. Pour fonctionner, le programme a besoin de sysbench_script.py, déjà présent dans le même dossier.
Pour fonctionner correctement, le programme a besoin que les runtimes suivants soient installés sur la machine: docker, podman, runc, crun et youki.
Il y a également besoin de [sysbench](https://github.com/akopytov/sysbench) installé sur la machine, et de la tarball `sysbench.oci.tar` présente dans le même répertoire.
Le programme lance trois types de benchmarks sysbench: CPU, memory et file i/o, sur les différents runtimes de conteneur cités plus haut, mais également en natif (en dehors d'un conteneur) et avec light-cont.

## Lancement

Lancer avec `python benchmark.py`.

### Options principales

Les options sont combinables.

- `--only engine`: Lance les benchmarks seulement sur l'engine (technologie de runtime de conteneur) spécifié. Le runtime doit être dans la liste des runtimes supportés (docker, podman, runc, crun, youki, native, light-cont).

- `--test-type benchmark`: Lance seulement le benchmark du type spécifié (cpu, fileio, memory).

- `--test-n nb`: Lance nb fois les tests.

- `--fileio-ramfs`: Lance les tests file i/o sur un ramfs (en réalité un tmpfs, dans /tmp). Voir la section suivante.

### Test de file i/o sur un tmpfs (ramfs)

TODO: je vais probablement retirer l'option `--fileio-ramfs` et demander à l'utilisateur d'exécuter le script dans /tmp (en vérifiant qu'il est bien monté en tant que tmpfs), et faire un script pour copier les fichiers nécessaires là-bas. J'expliquerai ici comment procéder.

## Contraintes

Nécessite sysbench, docker, podman, runc, crun et youki installées sur la machine pour fonctionner correctement. Si un runtime n'est pas installé, le programme se lancera mais les tests ne s'exécuteront évidemment pas pour ce programme.
Nécessite également l'image `sysbench:tp`, et la tarball `sysbench.oci.tar` présente dans le même répertoire. 
[Lien vers un transfert de la tarball sysbench.oci.at](https://www.swisstransfer.com/d/1ea0199a-cf8e-4172-b8f3-5b099f0853d6)
Si le lien a expiré, vous pouvez lancer `./convert_sysbenchtar_to_oci.sh`, un script simple qui utilise podman pour convertir `docker-image-sysbench.tar.gz` en `sysbench-oci.oci.tar`. Ce script a été fait à la hâte est n'est absolument pas modulable. Vous pouvez cependant le modifier.
TODO: Expliquer comment obtenir la tarball sysbench oci (trouver version )

# Script de traitement des données


# Script de test des sources de reproductibilités


## License
- Code: Apache-2.0
- Everything else, in particular documentation and measurements: CC-BY-SA-4.0

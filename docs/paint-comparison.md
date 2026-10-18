# Paint C/LVGL et Suchu/Raylib — expérience KyIlOS

> État de référence **avant** optimisation RLSW. Les gains mesurés ensuite sont
> documentés dans [raylib-optimization.md](raylib-optimization.md). La référence
> brute a été conservée dans `build/raylib-optimization/before-raw/` et
> `before-results.json` ; `build/paint-comparison/results.json` contient désormais
> les nouveaux runs. Ces chiffres initiaux ne décrivent plus le renderer actuel.
> L'optimisation applicative suivante est décrite dans
> [paint-retained-canvas.md](paint-retained-canvas.md), avec sa propre référence.



Les deux applications restent indépendantes dans la même ISO :



```text

run /vex_paint

run /usr/bin/kyil-paint

```



Paint C/LVGL reste l'application du menu desktop. Aucune migration du desktop

ni suppression de LVGL n'a été engagée.



## Contrat reproduit



- Client initial 1200×720, document logique 640×360, fond `#d9d9d9`.

- Pinceau rouge, noir ou `#3820ff`, rayons document 5/11/18.

- Gomme qui dépose la couleur du fond, effacement du document par appui long

  de 400 ms sur le bouton `+`, sélection de couleur/taille qui désactive la gomme.

- Interpolation DDA avec troncature vers zéro, centres bornés au document,

  limite originale de 4096 stamps ; saturation silencieuse conservée.

- Toolbar verticale, puis horizontale lorsque largeur <520 ou hauteur <330.

  Le document survit au redimensionnement ; la mise à l'échelle et le rayon

  suivent les mêmes règles que C (rayon basé sur la largeur).

- Aucun bucket fill, undo, import/export, sauvegarde ou format fichier dans

  l'original. Le document est une liste de stamps, pas un fichier image.

  Les surfaces finales utilisent le contrat natif XRGB8888.



Le document, l'interpolation, les outils, le hit-test, les deadlines d'appui

long et le layout Suchu sont dans `paint_model.suchu` et `paint.suchu`.

Il n'existe pas de moteur Paint C/OCaml appelé par cette version. Le batch

`draw.circles` est une primitive graphique générique déjà existante. Son entrée

et son cache de géométrie sont construits en Suchu et invalidés sur clear/resize.

Les rasterizers restent ceux des frameworks : ils ne produisent pas forcément

les mêmes pixels sur les contours antialiasés ou avec leurs polices différentes.



## Corrections et instrumentation



La lecture initiale a révélé une clock CPU native non supportée et l'absence de

clock monotone branchée à LVGL. Les clocks CPU 2/3 utilisent maintenant les

compteurs réels FlowSched, avec actualisation du CPU courant. LVGL est branché

à la clock monotone native.



L'appui long stationnaire n'avait pas de source de réveil. Paint C dispose d'un

timer d'effacement et d'une attente native bornée tant que ce timer est actif.

Suchu utilise une deadline de réveil à travers son bridge générique. Hors de ce

travail, les deux applications restent sur leur attente normale d'événements.

Les anciens flags d'attente native et la taille de la requête restent compatibles.

Les bits supérieurs ajoutent un délai relatif en ticks scheduler.



Deux primitives génériques de dessin manquantes, rectangle arrondi et texte

tourné, ainsi que les propriétés de taille minimale explicite ont été ajoutées

à Suchu. Elles contiennent seulement de la traduction Raylib, aucune politique

de document ou d'outil Paint.



Le module commun `vex_bench.c` enregistre en mémoire les clocks monotone/CPU,

les pages PMM et les durées de présentation. Il ne dessine rien. Les hooks sont

inactifs hors du mode benchmark. Les lignes FRAME ne sont exportées qu'après

la charge. Le parent échantillonne le PMM toutes les 20 ms et bufferise ses

résultats ; aucun log RAM périodique n'est émis pendant la charge.



## Protocole



VirtualBox, 4096 Mio RAM, VMSVGA, VRAM 128 Mio, accélération 3D désactivée,

desktop 1920×1080. Même ISO finale pour 1 et 4 vCPU. Trois répétitions par pile

et configuration, ordre alterné A/B puis B/A puis A/B. Aucun WebKit ouvert.



Le parent natif fait un fork/exec réel. Le démarrage est mesuré avant le fork

jusqu'à la fin de la première présentation framebuffer. Les applications sont

ensuite focalisées, un clic PS/2 réel est observé dans une capture, puis une

barrière d'input et trois secondes sans input précèdent le replay.



Le replay contient 64 frames : 16 tracés, 16 remplissages par tracés denses,

16 gommes sur les tracés bleus existants, 16 mouvements de coordonnées rapides.

Il appelle les méthodes du document et le vrai rendu des applications, pas un

renderer de substitution. Le résultat final est vérifié : 3584 stamps,

checksum logique 543696415. Le dispatch physique est validé séparément par

les clics et drags réels ; le replay ne mesure pas le débit de la queue PS/2.



Une expérience indépendante ouvre deux vrais processus et deux fenêtres,

avec trois secondes entre créations, puis les ferme indépendamment.



Définitions des mesures :



- RAM = incrément de pages physiques **global PMM** par rapport au parent

  juste avant fork/exec. Cela inclut processus, runtime, surfaces et coûts kernel.

  Ce n'est pas un RSS privé/proportionnel ; les coûts partagés sont comptés une

  seule fois dans le système. Le pic est un **pic observé**, résolution 20 ms,

  complété par les snapshots en fin de frame, pas un high-water allocator exact.

- CPU = temps réellement chargé au process_group, kernel exécuté pour lui

  inclus. Sous charge, les compteurs absolus incluent aussi les intervalles

  entre frames et les éventuels busy-waits de pacing. Les pourcentages sont

  exprimés par rapport à un cœur ; à 4 vCPU, diviser par quatre pour la capacité VM.

- Repos = fenêtre calme d'environ 3 s. La mesure inclut encore le coût de

  l'éveil de la barrière de mesure ; c'est une petite borne supérieure,

  pas une preuve que le renderer travaille périodiquement au repos.

- Rendu = préparation applicative/framework jusqu'à la fin de présentation.

  Présentation = appel natif incluant copie, composition et copie framebuffer.

  Cadence = intervalle entre présentations complètes. Les p50/p95/p99 portent

  sur les 192 frames, et 189 intervalles, des trois répétitions.

- Visibilité = borne haute host entre injection du clic et premier pixel rouge

  effectivement capturé. Un lecteur Python persistant est prêt avant le clic.

  Cette borne inclut VirtualBox, compression PNG et observation ; elle ne permet

  pas de départager des écarts de quelques millisecondes et n'est pas une mesure

  de scanout physique sur un moniteur.



Raylib conserve son pacing normal à 60 FPS, LVGL son traitement événementiel.

Le plafond Raylib ne suffit pas à expliquer des frames lourdes >16,7 ms.

L'échauffement comprend l'initialisation des ressources et un vrai clic avant

la trace. Les premières allocations froides restent visibles dans les logs.



## Résultats

| Mesure | 1 CPU C/LVGL | 1 CPU Suchu/Raylib | 4 CPU C/LVGL | 4 CPU Suchu/Raylib |
|---|---:|---:|---:|---:|
| RAM repos, Mio | 13.89 | 37.94 | 13.89 | 37.94 |
| RAM pendant dessin, Mio | 13.89 | 41.38 | 13.89 | 41.38 |
| Pic observé, Mio | 13.89 | 41.45 | 13.89 | 41.45 |
| Seconde instance, incrément Mio | 10.82 | 31.46 | 10.83 | 31.46 |
| Deux instances, total Mio | 24.72 | 69.40 | 24.72 | 69.40 |
| CPU repos, borne haute % cœur | 0.21 | 0.46 | 0.20 | 0.47 |
| CPU charge, % cœur | 98.8 | 99.5 | 100.0 | 100.0 |
| CPU actif cumulé des 64 renders, ms | 1240.8 | 9012.2 | 1272.4 | 8933.9 |
| Démarrage première présentation, ms | 88.1 | 426.4 | 98.8 | 617.4 |
| Rendu p50 / p95 / p99, ms | 13.55 / 40.48 / 42.95 | 87.32 / 295.05 / 306.73 | 13.88 / 39.11 / 43.87 | 94.41 / 283.32 / 298.13 |
| Présentation p50 / p95 / p99, ms | 5.51 / 7.42 / 7.95 | 5.58 / 7.07 / 7.80 | 5.90 / 7.11 / 7.89 | 6.32 / 8.53 / 9.78 |
| Intervalle frame p50 / p95 / p99, ms | 13.78 / 40.53 / 43.02 | 88.55 / 295.33 / 306.95 | 14.04 / 39.30 / 43.89 | 95.59 / 283.96 / 298.19 |
| Débit réel du replay, FPS | 51.06 | 7.07 | 50.28 | 7.16 |
| Clic visible, borne haute host ms | 197.5 | 203.4 | 194.5 | 200.9 |

À 1 vCPU : Suchu utilise **2.73×** la RAM au repos, **2.98×** au pic, **4.84×** le temps de démarrage, **6.44×** le rendu médian et **7.26×** le CPU actif de rendu. Le débit est inférieur de **86.2%**.

À 4 vCPU : Suchu utilise **2.73×** la RAM au repos, **2.98×** au pic, **6.25×** le temps de démarrage, **6.80×** le rendu médian et **7.02×** le CPU actif de rendu. Le débit est inférieur de **85.8%**.

La présentation reste proche : la majeure partie de l’écart est située avant cet appel. Les 4 vCPU n’accélèrent pas substantiellement ces renderers essentiellement monothread. La borne de clic visible (~200 ms) est dominée par la méthode de capture ; elle ne démontre pas un avantage de réactivité entre les piles.

## Reproduction et données



```powershell

.\scripts\build-paint-comparison.ps1

# Dans WSL, après compilation des bibliothèques Suchu/Raylib :

# bash scripts/build-suchu-paint.sh

.\scripts\repack-iso.ps1

.\scripts\run-virtualbox.ps1 -Headless -CpuCount 1

.\scripts\run-paint-comparison.ps1 -CpuCount 1 -Repetitions 3

# Arrêter la VM puis refaire le même protocole à -CpuCount 4.

python scripts/analyse-paint-comparison.py

```



Le harness suppose un desktop neuf ; il ouvre le terminal lui-même. Le test

fonctionnel prend un ID initial explicite pour les fenêtres déjà créées.

Les sources originales Suchu du Desktop n'ont pas été modifiées.



Mesures brutes finales : `build/paint-comparison/raw/cpu1/` et `cpu4/`, logs

complets, captures et `visible.json`. Extraction : `results.json`, `frames.csv`.

Les pilotes avec erreurs et variantes initiales sont archivés séparément ; ils

ne sont pas incorporés au tableau final. Le cache/batch a aussi été utilisé en

dehors du benchmark dans le chemin de production.



## Code, binaires et limites

| Exécutable | Taille fichier | Octets PT_LOAD en fichier | Dépendances dynamiques |
|---|---:|---:|---|
| lvgl | 559,984 | 499,195 | aucune |
| lvgl_profile | 565,600 | 503,947 | aucune |
| suchu | 10,761,120 | 6,671,680 | aucune |

Code applicatif, hors blancs/commentaires : **383 lignes C**, **137 lignes Suchu** (interface 110, modèle 27). Ces comptes incluent le replay optionnel dans les deux sources. Réduction applicative : 64.2%. Bridge de logique Paint : **0 ligne C/OCaml** dans la version Suchu.

Instrumentation commune séparée : **83 lignes C** de compteurs/export et **38 lignes C** de lancement/observation. Les wrappers de mesure ajoutent 6 lignes CAML et 12 lignes OCaml, hors headers et points d’appel. Ils ne contiennent aucune logique de dessin.

Infrastructure graphique générale : 16 lignes OCaml pour les deux nouvelles primitives de dessin ; les adaptateurs existants (GUI, Raylib, LVGL, ABI) sont réutilisés. Les modifications de clock et attente sont localisées dans les fonctions listées ci-dessous. Le dépôt contient des changements antérieurs non liés : un diff global Git ne serait pas un compte honnête des lignes de cette expérience.

Les tailles PT_LOAD excluent notamment les symboles/debug non chargés. La taille de fichier ne doit donc pas être interprétée comme le RSS. Le binaire Suchu est environ 19,2× plus gros sur disque ; les archives spécifiques sont intégrées statiquement, pas nécessaires comme fichiers séparés au lancement.

Les deux exécutables sont statiques. Les archives compiler/Raylib/LVGL ne sont

pas chargées comme dépendances dynamiques de l'application. Suchu embarque son

runtime, ses bindings, le renderer et les polices. Les fichiers sources Suchu

sur l'ISO sont éditables mais ne sont pas nécessaires à l'exécutable compilé.



Ce test compare les **piles complètes C/LVGL et Suchu/Raylib**, pas les frameworks

isolés à langage, modèle de données et rasterizer identiques. On ne peut pas

attribuer tout l'écart à Raylib seul. Le temps avant présentation regroupe Suchu,

runtime, gestion de géométrie et rasterization software. Pas de profilage général

ni de troisième contrôle C/Raylib pour les départager dans cette expérience.



Pas de GPU matériel, shader programmable ou bénéfice multithread démontré.

Les appels 2D utilisés sont réellement rendus. La présence d'autres APIs dans

Raylib ne prouve pas leur support actuel sur KyIlOS. Le host n'est pas une

machine de mesure temps réel isolée ; répétitions, limites de capture, caches

et variation de l'hyperviseur interdisent des conclusions à la microseconde.

Le coût de la seconde instance est mesuré au repos. Les snapshots RAM après

fermeture incluent le parent observateur encore vivant et le cleanup différé.



## Validation et conclusion

Comptage des blocs d’infrastructure touchés, hors blancs/commentaires (code à jour, pas diff Git historique) :

| Adaptation | C | OCaml |
|---|---:|---:|
| Clock CPU et sélection clocks 2/3 | 9 | 0 |
| Callback monotone LVGL et branchement | 6 | 0 |
| Deadline kernel, flag ABI, helper native wait | 17 | 0 |
| Deadline Raylib et bridge wake | 10 | 2 |
| Primitives graphiques et minimum explicite | 0 | 20 |
| Total infrastructure | 42 | 22 |

Le collecteur, le parent et les wrappers de mesure sont comptés séparément dans la section Code. Les bibliothèques vendoriées ne sont pas du nouveau code Paint.

Tests réellement exécutés : trois répétitions par pile à 1 et 4 vCPU, deux instances indépendantes, checksums communs, clics PS/2 et détection de pixels visibles. Les tests de production vérifient les trois couleurs, les tailles petite/grande, la sélection de la taille moyenne, gomme, clear stationnaire avant relâchement, drag et resize compact avec conservation du document. Neuf assertions Suchu vérifient DDA positive/négative, bornes, cap 4096, clear, gomme et la trace commune.

Comparaison des documents réellement capturés, toolbar exclue : **99.345%** de pixels identiques et **97.60%** d’IoU des zones noires, reproduits dans les six paires de captures. Les écarts de contour ne sont pas masqués ; LVGL et RLSW ont des règles de couverture/antialiasing différentes.

**Conclusion : pour ce Paint et ces backends software, la pile Suchu/Raylib actuelle n’est pas un meilleur remplacement général de C/LVGL en mémoire ou performances.** La réduction du code applicatif est réelle, et l’écriture de dessin 2D en Suchu reste intéressante, mais elle s’accompagne ici de beaucoup plus de RAM, de CPU par travail identique et de frames lourdes moins régulières. Le batch/cache ne change pas cette conclusion. Une migration globale motivée par un gain de vitesse n’est pas étayée. Pour des applications scriptables, Suchu peut être choisi pour ses qualités de langage ; le prix runtime/rendu doit rester explicite. Ce résultat ne classe pas Raylib isolé contre LVGL isolé et ne prédit pas les performances d’un futur backend GPU.

Fichiers applicatifs : `userspace/apps/vex_paint.c`, `userspace/suchu/examples/paint.suchu`, `paint_model.suchu`, `paint_test.suchu`.

Adaptation/infrastructure : `kernel/src/scheduler.c` (clock CPU réelle), `kernel/src/vex_posix_compat.c` (clocks 2/3), `kernel/src/vex_syscall.c` (attente deadline), `kernel/include/unos/vex_abi.h` (flag timeout), `userspace/libvex/include/vex_app.h`, `userspace/libvex/src/vex_lvgl.c`, `userspace/suchu/src/gui_backend.ml`, `platform_backend.ml`, `platform_stubs.c`, `userspace/raylib/rcore_vexos.c`, `vex_input_observer.h`.

Mesure/build : `userspace/libvex/src/vex_bench.c`, `include/vex_bench.h`, `userspace/apps/paint_bench_runner.c`, `scripts/build-suchu.sh`, `build-suchu-paint.sh`, `build-paint-comparison.ps1`, `run-paint-comparison.ps1`, `test-paint-functions.ps1`, `paint-pixels.py`, `analyse-paint-comparison.py`, `paint-comparison-metadata.py`, `report-paint-comparison.py`.

# Paint Suchu : canvas conservé et rendu incrémental

## Changement applicatif

L'ancien Paint Suchu conservait la géométrie transformée, mais effaçait le
canvas et rasterisait **tous** les stamps à chaque frame. Le coût croissait
avec l'historique, même pour un seul nouveau point ou un changement d'outil.

`paint.suchu::draw_document` conserve maintenant les pixels du document,
dessine seulement le suffixe de nouveaux stamps, et met à jour le cache lorsque
le document change. La toolbar est dessinée après la sauvegarde du document :
elle ne s'accumule pas dans le cache et peut changer indépendamment.

La liste de stamps reste la source de vérité Suchu. Un resize, clear, cache
absent ou sauvegarde échouée force une reconstruction complète. Le rayon, les
coordonnées, les outils, DDA, palette, cap 4096 et document logique 640×360 restent
identiques. Aucun stamp n'est supprimé pour accélérer le test. Le modèle et la
logique Paint ne sont pas déportés en C/OCaml.

## Support générique

`draw.cache_save(key)`, `cache_restore(key)`, `cache_drop(key)` sont des opérations
génériques de conservation des pixels d'un Canvas, indépendantes de Paint.
Les clés sont locales au nœud Canvas de la fenêtre. Le backend logiciel Raylib
copie ses pixels internes directement, sans texture/depth buffer superflu,
conversion de couleur ni lecture du framebuffer du desktop.

Le cache représente une image compositée complète, pas une couche transparente
indépendante du backdrop. L'application doit invalider/reconstruire si ce qui
est capturé change. Les rectangles doivent être intégraux et entièrement
visibles en 2D. Un cache absent, dimensions différentes, clipping partiel ou
depth test actif retourne false ; l'application redessine normalement. Les
allocations de remplacement sont transactionnelles : OOM conserve l'ancien
objet. Capture réussie seulement après copie ; libération au drop/fermeture,
avant disparition du contexte Raylib. Interface privée userspace, aucun nouvel
objet/capability kernel et aucun protocole de transport des frames ajouté.

`Window max-fps` expose le réglage Raylib existant, défaut 60. Paint demande 0
(aucun plafond) pour traiter les événements dès qu'ils arrivent. Le mode
d'attente d'événements reste intact : cela ne force pas un redraw permanent.
Les autres applications conservent leur plafond par défaut. La validation du
réglage précède la création de la fenêtre native.

## Mesures réelles

VirtualBox, même kernel/ISO/environnement, **1 vCPU**, client 1200×720.
Trois répétitions alternées par pile, trace commune 64 frames, 3584 stamps et
checksum 543696415 ; deux instances pour la RAM. Compteurs guest monotone/CPU,
pas de profiler ni de logs série par frame dans la zone chronométrée.

| Mesure | Suchu avant cette passe | Suchu optimisé | C/LVGL actuel |
|---|---:|---:|---:|
| Débit du replay, rendus/s | 26,30 | **107,74** | 58,65 |
| CPU actif / 64 frames, ms | 2281,85 | **585,46** | 1080,16 |
| Rendu p50, ms | 30,01 | **9,33** | 12,02 |
| Rendu p95, ms | 71,47 | **10,92** | 33,74 |
| Rendu p99, ms | 74,38 | **11,82** | 35,70 |
| Présentation p50, ms | 4,98 | 4,68 | 4,78 |
| RAM repos incrémentale, Mio | 37,94 | **41,26** | 13,89 |
| Pic PMM incrémental, Mio | 41,45 | **44,19** | 13,89 |
| Coût seconde instance, Mio | 31,46 | 34,78 | 10,82 |
| Démarrage première présentation, ms | 333,85 | 323,27 | 75,40 |

Gain vs la version précédente : **4,10×** le débit et **74,3 %** de CPU de
rendu en moins. Vs le C/LVGL inchangé du même run : **1,84×** le débit et
**45,8 %** de CPU de rendu en moins. Un cache 1200×720×4 occupe 3,30 Mio de
pixels ; l'incrément mesuré au repos est 3,32 Mio. Le CPU « repos » mesuré
par les marqueurs, incluant un réveil, est 0,027 % d'un cœur pour Suchu.

Un pilote conservant le plafond 60 rendus/s mesurait déjà 620,35 ms de CPU
(au lieu de 2281,85), mais seulement 50,26 rendus/s : `WaitTime/nanosleep`
arrondi aux ticks limite alors le débit. Le plafond a été retiré pour
l'application entière, pas seulement pour le replay. C/LVGL n'a pas ce plafond.

Les rendus/s mesurent les frames calculées et présentées par l'application,
**pas la fréquence physique de l'écran**, ni la preuve que le compositor
montre chaque frame intermédiaire. La borne de clic visible par PNG host
(157 ms Suchu, 166 ms C) est trop grossière pour classer la latence en ms.
Le host n'est pas isolé ; ce sont des médianes de trois runs, pas une
certification du langage ou une comparaison des frameworks isolés.

Suchu/Raylib est ici plus efficace grâce à une **stratégie applicative
différente**. On pourrait aussi appliquer une stratégie incrémentale au Paint C.
Le résultat ne démontre pas que Suchu exécute toute logique plus vite que C.
La RAM reste environ 3× supérieure, le démarrage plus lent ; aucun gain GPU ou
scaling SMP revendiqué. Cette passe finale n'a été chronométrée qu'en 1 vCPU.

## Validation et traçabilité

- Trois captures finales **100 % identiques** à la version précédente, sur
  tout le client 1200×720, toolbar comprise ; checksum identique à C.
- Production en VM : rouge/noir/bleu, tailles, gomme, clear maintenu avant
  relâchement, tracés et resize compact avec document conservé ; neuf assertions
  du modèle Suchu passent. Créations/fermetures répétées, deux instances.
- Test sur le véritable RLSW avec ASan/UBSan : rendu incrémental identique au
  rendu complet, transparence, overlay exclu du cache, clipping refusé sans
  écriture, resize, OOM conservant l'ancien objet, bounds, depth refusé et
  100 cycles de création/destruction sans allocation restante.

Reproduction du test host : `bash scripts/test-raylib-canvas-cache.sh`.
Benchmark VM : `scripts/run-paint-comparison.ps1 -CpuCount 1 -Repetitions 3`,
puis `analyse-paint-comparison.py --cpu-count 1`. Le filtre évite de mélanger
les anciens runs 4 vCPU avec cette nouvelle application.

Référence : `build/paint-retained/before-results.json`, `before-raw/`.
Pilote plafonné : `pilot-capped-results.json`, `pilot-capped-raw/`.
Mesures : `final-results.json`, `final-metadata.json`, `final-pixels.json`,
`functions-final.log`, et `build/paint-comparison/raw/cpu1/`.
ELF des trois runs : `measured-paint`, SHA256
`4ab753f97ac7bf39d40e9328aeff0b1327608053c5a8a4488672ce6c3ce1079b`.
Après ces runs, la propriété max-fps a été ajoutée à la liste reconnue et sa
validation déplacée avant init (avertissement trompeur supprimé). Le chemin
de rendu ne change pas ; un replay de contrôle valide le binaire final de l'ISO.
Ce replay final contient 64 frames, 583,87 ms de CPU, 107,43 rendus/s et
une capture encore 100 % identique après normalisation de la position de fenêtre.
Sortie du processus : status 0. ELF final extrait de l'ISO identique à celui
exécuté : SHA256 `0c9309b46a7e03b7b8a3eab55f5a9fc9eb4b6691d03874be8b6477b7089c19d9`.
VM de test arrêtée, configuration rétablie à 4 vCPU.

Fichiers applicatifs : `userspace/suchu/examples/paint.suchu` (145 lignes
applicatives avec le modèle, hors blancs/commentaires ; modèle inchangé).
Support : `userspace/suchu/src/gui_backend.ml`, `platform_stubs.c`,
`userspace/raylib/vex_canvas_cache.h`, `rlsw_canvas_cache.inc`, `rcore_vexos.c`.
Tests/outils : `tests/rlsw_canvas_cache_test.c`,
`scripts/test-raylib-canvas-cache.sh`, filtres des deux analyseurs Paint.
Le C/LVGL original, le kernel, WebKit et le Suchu original du Desktop restent
inchangés dans cette passe. ISO reconstruite avec les deux Paint indépendants.

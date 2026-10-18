# Performances interactives de Vex-Browser — enquête et corrections

Symptômes (VM, 4 CPU) : navigation 30–65 s, souris qui lague pendant le chargement, animations
à ~0,4–3 FPS, bureau peu réactif pendant le travail de WebKit, alors que le CPU global restait
à 50–70 % d'inactivité.

## Outils (ajoutés)

* **`prof <secondes>`** (shell Vex) : profileur statistique du noyau. Échantillonne chaque CPU à
  1 kHz (idle / user / kernel / attente sur un verrou noyau, thread, RIP), mesure le temps de
  tenue de chaque verrou noyau par appelant, attribue ce que fait le CPU qui *tient* un verrou
  disputé, chronomètre les appels système (Linux et natifs) et des tronçons nommés, et enregistre
  des latences (tick GUI, file souris, intervalle/coût de présentation WebKit). Rapport « PROF-… »
  sur le port série à la fin de la fenêtre. Code : `kernel/src/profiler.c`.
* **`scripts/profile-report.py <log>`** : agrège le rapport (symbolise le noyau et les
  bibliothèques utilisateur, par module/fonction/ligne avec `--lines`).
* **`scripts/bench-gl.sh`** / **`bench-gl-profile.sh`** : banc du rendu logiciel GLES
  (`userspace/linux_tests/gl_render_bench.c`) ; `bench-gl.sh 4 256 equivalence` vérifie que
  les chemins natifs rendent *exactement* les mêmes pixels que l'interpréteur.
* Diagnostic optionnel côté WebKit : `VEX_GL_STATS=1` (sources GLSL des programmes et
  statistiques de dessin par programme).

## Où partait le temps (mesures avant correction)

Chargement DuckDuckGo, 4 CPU, 100 s de profil :

| Constat | Mesure |
|---|---|
| CPU en attente active sur `memory_gate` | **29 % du temps CPU total** (92 s sur 4×100 s) |
| `memory_gate` tenu | 80 s sur 100 s (424 000 prises, tenue max 118 ms) |
| Latence file souris | moyenne 2,5 ms, **max 86 ms** ; tick GUI max 86 ms |
| Animation de test (rAF + survol) | **0,4 FPS** |

Causes, dans l'ordre d'impact :

1. **Chaque défaut de page copiait tout l'inventaire mémoire** (`copy_active_root` → `kmalloc`
   d'un bloc ≥ 4 Ko = chunk mappé sur tous les CPU, zéro-rempli, puis démappé avec shootdown TLB)
   et parcourait toutes les régions (une recherche par hachage chacune), le tout sous le verrou
   mémoire global : ~480 µs par défaut, 100 000 défauts. Idem pour la validation des copies
   utilisateur (`clock_gettime` : 21 µs), `mprotect`, `munmap`, enregistrement de région.
2. **`mmap` anonyme ≤ 1 Mio eager** : `kmalloc` + `memset` + copie ORA + matérialisation page par
   page, ~30 ms chacun, sous le verrou.
3. **Parcours complet de l'arbre de pages** pour `mprotect`/`munmap` d'une plage, et **un
   shootdown TLB inter-CPU (IPI + attente, interruptions coupées) par page** modifiée.
4. **Rendu GLES logiciel** : un interpréteur SPIR-V qui reconstruit à *chaque pixel* les
   constantes du module (~60 % du temps), un seul thread, aucune spécialisation des programmes
   Skia triviaux ; plus un pointeur local sorti de portée (voir « Bug corrigé »).
5. WebKit croyait n'avoir qu'**un seul CPU** (`sched_getaffinity` renvoyait toujours CPU 0) :
   aucun pool de threads dimensionné sur les cœurs.

## Corrections

| Domaine | Changement |
|---|---|
| Noyau, mémoire | Index trié par adresse des régions (`process_memory.c`) : recherches, validation de plage, tests de chevauchement en O(log n) ; lecture de l'inventaire sans copie dans le chemin de défaut de page ; liste d'adresses avec capacité (plus de copie à chaque enregistrement) ; `mprotect`/`munmap` ne préparent que les régions touchées. |
| Noyau, mmap | `mmap` anonyme privé toujours paresseux (zéro-remplissage au défaut). |
| Noyau, VMM | Parcours des tables restreint à la plage ; plage > 16 pages traitée en une passe ; **un shootdown par table de pages** (au lieu d'un par page) ; un seul pour `vmm_set_page_flags_range`. |
| Noyau, compat | `sched_getaffinity` annonce tous les CPU en ligne. |
| Rendu | Instantané du prologue du module par dessin ; démarrage direct à la fonction d'entrée ; état d'invocation minimal ; chemins natifs pour les 3 programmes Skia dominants (couleur par sommet, texture, couverture d'atlas) reconnus par leur source GLSL, avec pixels identiques à l'interpréteur (test) ; rasterisation par bandes de lignes sur plusieurs threads (`VEX_GL_THREADS`). |

### Bug corrigé au passage

`vk_rasterize_clip_triangle` donnait au pipeline des pointeurs vers des tableaux déclarés dans
un bloc déjà fermé (`quad_inputs`/`quad_coords`) : comportement indéfini qui faisait échouer
des dessins texturés de façon aléatoire. Les tableaux vivent maintenant dans la portée du dessin.

## Résultats (VM 4 CPU)

| Mesure | Avant | Après |
|---|---:|---:|
| Chargement DDG « vexos operating system » | 62–68 s | **8,4–11,5 s** |
| Attente active sur verrou noyau (part du CPU) | 29 % | 1–3 % |
| Latence file souris pendant le chargement (moy. / max) | 2,5 / 86 ms | **0,37 / 11 ms** |
| Tick GUI pendant le chargement (max) | 86 ms | 25 ms |
| `mprotect` (moyenne) | 4,7 ms | 0,45 ms |
| `munmap` (moyenne) | 3,0–7,2 ms | 0,34 ms |
| `mmap` anonyme (moyenne) | 29 ms | ~0,8 ms |
| `clock_gettime` (moyenne) | 21 µs | < 1 µs |
| Animation de test (FPS affiché par la page) | 0,4 | **~32** (≈4 pendant un survol soutenu) |
| Bande passante du rendu (banc, quad plein écran texturé) | 1,07 Mpx/s | ≈ 4 Mpx/s générique, ≈ 25 Mpx/s Skia natif sur 4 threads |

## Limites connues

* Le rendu reste logiciel et générique pour les programmes non reconnus ; le survol de pages
  riches en transitions reste lent (≈4 FPS sur la page de test).
* Un `munmap` énorme peut encore tenir le verrou mémoire quelques ms.
* Les chiffres viennent d'une VM VirtualBox et d'un serveur de test local (`10.0.2.2`) ; le
  réseau réel fait varier le temps de chargement.

## Validation

* `scripts/run-smoke-tests.ps1` (4 CPU) : tous les marqueurs noyau passent, dont le nouveau
  `smoke: region index matches model ok` (test différentiel aléatoire de l'inventaire mémoire,
  `process_memory_index_test`), JIT W^X, JSC Baseline/DFG/FTL, fork/propriété mémoire. Le test
  Vulkan « vk_test: ok » n'est jamais émis par la source (`vk_test.c` se termine par
  « programmable renderer and WSI still incomplete »), indépendamment de ces changements.
* `scripts/bench-gl.sh 4 256 equivalence` : 12 cas (3 programmes Skia × filtre × mélange)
  identiques entre chemin natif et interpréteur, et identiques entre rendu série et parallèle.
* Les trois créations de threads utilisateur du noyau (`process_clone_*`) préremplissent
  maintenant la page de pile qu'elles écrivent (les mappings anonymes sont à la demande).
* Démarrage du navigateur à 4 CPU : environ 1 lancement sur 5 dépasse 120 s sans que le marqueur
  « entering main loop » apparaisse (observé aussi avant ces changements) ; non résolu ici.

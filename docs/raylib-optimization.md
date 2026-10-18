# Raylib/RLSW : optimisation du chemin de rendu KyIlOS

> Étape précédente, isolant le renderer. Paint a ensuite reçu son propre canvas
> conservé : les nouveaux résultats sont dans [paint-retained-canvas.md](paint-retained-canvas.md).

## Diagnostic

Le backend utilisé par les applications Suchu est Raylib 6.0 avec RLSW 1.5,
un rasterizer CPU userspace. Ce chemin ne passe pas par le renderer Vulkan.
Le profil VM de la trace Paint, symbolisé contre le binaire exact, contient
834 échantillons userspace du PID Paint :

| Fonction | Part |
|---|---:|
| sw_raster_triangle_span_TEX_BLEND | 43,05 % |
| sw_raster_quad_TEX_BLEND | 15,47 % |
| sw_blend_SRC_ALPHA_ONE_MINUS_SRC_ALPHA | 14,99 % |
| sw_pixel_read_color_GRAYALPHA | 6,83 % |
| sw_triangle_clip_and_project | 4,56 % |
| sw_raster_triangle_TEX_BLEND | 4,44 % |
| memcpy | 4,20 % |

Le coût dominant de cette charge est donc la rasterization, pas l'exécution
Suchu/OCaml. Cela ne démontre pas une équivalence générale Suchu/C.

`rcore.c::InitWindow` donne aux formes la texture de police via
`SetShapesTexture`, avec une petite région blanche opaque. `rshapes.c`
tesselle les cercles en quads/triangles. RLSW conserve le chemin texturé et
le blending parce que la texture entière contient de l'alpha, même lorsque
le footprint de la primitive est entièrement blanc opaque.

## Adaptation

`scripts/patch-raylib-software.py` applique une adaptation contrôlée au header
vendorié pendant `build-raylib.sh`. Aucun changement dans le Suchu original
du Desktop, aucune logique Paint ajoutée dans le renderer.

- `sw_primitive_samples_white` vérifie tous les texels du footprint nearest/
  linear de la primitive. Un footprint blanc opaque est une texture identité :
  la primitive peut utiliser le chemin non texturé avec les mêmes couleurs.
  W variable, UV non finis, footprint hors texture ou trop coûteux : fallback
  général. Pas de verdict mis en cache après mutation.
- Pour les spans/rows plats sans texture, depth test ou blending, conversion
  couleur une fois et remplissage des mêmes pixels. Géométrie, clipping,
  coverage et nombre de segments des cercles inchangés.
- L'élision du blending opaque est limitée au couple SRC_ALPHA /
  ONE_MINUS_SRC_ALPHA. L'ancien test « dépend de l'alpha » était incorrect
  pour le blending additif.
- Une mise à jour partielle de texture invalide la classification opaque :
  elle ne permet pas d'inférer l'alpha de toute l'image depuis le sous-rectangle.
- Sampling clamp : borne nearest à width/height exclusifs, floor et borne
  inférieure pour les voisins linear. Les accès au bord restent dans la texture.

Les primitives normales texturées/translucides restent rasterisées ; aucun
rendu de référence n'est remplacé par une image synthétique. Kernel, compositor,
politique de redraw et logique Paint inchangés.

## Mesures

Même replay de 64 frames, canvas client 1200×720, 3584 stamps finaux,
checksum 543696415. Trois répétitions alternées de chaque pile, plus deux
instances. Mesures CPU/monotone dans le guest, export après la charge ; aucune
trace de profiler pendant les runs chronométrés. Version C/LVGL non modifiée.

| 1 vCPU | Suchu avant | Suchu après | C/LVGL après |
|---|---:|---:|---:|
| FPS trace | 7,07 | 26,30 | 57,33 |
| Rendu p50, ms | 87,32 | 30,01 | 12,11 |
| Rendu p95, ms | 295,05 | 71,47 | 35,06 |
| Rendu p99, ms | 306,73 | 74,38 | 36,73 |
| CPU actif / 64 frames, ms | 9012,17 | 2281,85 | 1101,42 |
| Présentation p50, ms | 5,58 | 4,98 | 4,99 |
| RAM repos, Mio | 37,94 | 37,94 | 13,89 |
| Pic PMM incrémental, Mio | 41,45 | 41,45 | 13,89 |

Gain brut 3,72× en FPS et baisse de 74,7 % du CPU de rendu. Le contrôle
C/LVGL passe aussi de 51,06 à 57,33 FPS : host/hyperviseur non isolé,
variation d'environ 12 %. Gain relatif normalisé au contrôle : environ 3,31×.
Ce n'est pas une mesure à la microseconde ni un test Raylib contre LVGL à
langage identique. Suchu/Raylib reste ici environ 2,1× plus coûteux en CPU de
rendu ; la RAM/runtime, les font atlases et le démarrage n'ont pas été optimisés.

| 4 vCPU | Suchu avant | Suchu après | C/LVGL après |
|---|---:|---:|---:|
| FPS trace | 7,16 | 25,95 | 57,01 |
| Rendu p50, ms | 94,41 | 31,44 | 12,13 |
| Rendu p95, ms | 283,32 | 69,60 | 36,02 |
| Rendu p99, ms | 298,13 | 73,35 | 38,63 |
| CPU actif / 64 frames, ms | 8933,94 | 2348,64 | 1122,26 |
| Présentation p50, ms | 6,32 | 5,15 | 5,05 |

Les deux renderers restent essentiellement monothread : aucun scaling 1→4
revendiqué. RAM Suchu identique en 4 vCPU : 37,94 Mio repos, 41,45 Mio pic,
31,46 Mio pour la seconde instance. Démarrages observés Suchu : 333,85 ms
(1 CPU), 528,89 ms (4 CPU) ; le contrôle C varie aussi, ce n'est pas une
optimisation démontrée de l'initialisation.

## Validation et données

`scripts/test-raylib-software.sh` compile le véritable header adapté avec et
sans les fast paths. 136 scènes pixel-identiques : nearest/linear, wrap/clamp,
mutation, alpha/additif, clipping/scissor, gradients, quads, perspective.
Oracles indépendants pour blending additif, bord clamp et mutation transparente.
ASan/UBSan passent. Les samplers des tests sont explicitement configurés.

Dans les VM 1 et 4 vCPU, les six captures finales Suchu après optimisation sont
**100 % identiques** aux captures Suchu avant optimisation sur le client complet
1200×720, toolbar comprise. Le gain ne vient pas de contours simplifiés.
Les captures et pourcentages sont dans `before-after-pixels.json`.

Tests fonctionnels de production exécutés en 1 vCPU : rouge/noir/bleu,
tailles petite/grande, gomme, maintien stationnaire pour clear, drag,
resize compact et document conservé. Neuf assertions du modèle Suchu passent.
En 4 vCPU : trois replays par pile et création/fermeture de deux instances,
captures visuelles identiques et smoke du chrome Editor/Browser. Ce dernier
vérifie démarrage/rendu/fermeture de la fenêtre ; ce n'est pas une campagne
de navigation Web ni de fonctionnalités complètes de l'éditeur.

Référence conservée : `build/raylib-optimization/before-results.json`,
`before-raw/`, `before-paint`, `before-profile.log`, `before-hotspots.txt`.
Nouvelles mesures : `build/paint-comparison/raw/`, `results.json`, `frames.csv`.
Le script `profile-raylib.py` filtre le PID kyil-paint et symbolise les RIP avec
les symboles du bon ELF statique, malgré l'absence de path dans ces samples.

Les bibliothèques Suchu et les applications Paint, Editor et Browser sont
reliées à nouveau et intégrées à l'ISO. Le contenu WebKit utilise son autre
chemin EGL/GLES ; aucun gain de navigation Web n'est revendiqué ici.
Le Paint extrait de `build/vex-os.iso` est identique au binaire mesuré :
SHA256 `ce7064b084b77f439694c01afcff160a1dbe2f0fc291a6c01306654de6b83643`.
La VM de test est arrêtée ; sa configuration reste en 4 vCPU.

## Profil après adaptation

Un second run instrumenté hors benchmark utilise le même replay. 254 samples
userspace Paint, contre 834 auparavant ; ces profils incluent le démarrage et
ont une incertitude d'échantillonnage. Ne pas comparer leurs pourcentages comme
des temps absolus exacts.

| Fonction | Samples | Part |
|---|---:|---:|
| sw_raster_triangle_span_BASE.isra.0 | 77 | 30,31 % |
| sw_raster_triangle_BASE | 37 | 14,57 % |
| sw_triangle_clip_and_project | 37 | 14,57 % |
| memcpy | 35 | 13,78 % |
| sw_immediate_push_vertex | 14 | 5,51 % |
| swReadPixels | 9 | 3,54 % |

Plus aucun sample dans TEX_BLEND ou le blender alpha ; deux seulement dans
la lecture GRAYALPHA, qui reste nécessaire pour du vrai contenu texturé.
La suite éventuelle serait une optimisation générale du rasterizer/traitement
géométrique et l'étude des copies de présentation, pas un transfert de la
logique Paint en C. Aucun de ces nouveaux chantiers n'a été engagé ici.

Fichiers concernés : `userspace/raylib/rlsw_solid_white.inc`,
`scripts/patch-raylib-software.py`, `scripts/build-raylib.sh`,
`tests/rlsw_optimization_test.c`, `scripts/test-raylib-software.sh`,
`scripts/profile-raylib.py`. Les sources Paint et le Suchu original ne changent
pas dans cette passe. Les autres modifications du dépôt sont antérieures.

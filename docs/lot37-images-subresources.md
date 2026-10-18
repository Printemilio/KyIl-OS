# Lot 37 — images et sous-ressources, état du 30 septembre 2026

Le modèle mono-mip/mono-layer est remplacé. La queue reste asynchrone : aucune
exécution n'a été ramenée dans `vkQueueSubmit`. Ce document décrit cette passe,
pas une conformité Vulkan complète ni la clôture du Lot 37.

## Modèle effectif

`vk_image_impl_t` possède une table immuable de géométrie des sous-ressources,
indexée par `(arrayLayer, mipLevel)`. Chaque entrée contient extent, offset,
row pitch, taille, layout exécuté et layout planifié à la fin des submissions
enfilées. Le backing et son ownership restent ceux de l'image et de sa mémoire
Vex refcountée ; aucune ownership physique n'est créée par mip ou view.

Le backend actuel expose des images 2D, profondeur 1, jusqu'à 4096 × 4096,
256 layers et 13 mips au maximum. Le nombre de mips accepté dépend aussi des
dimensions réelles. Les additions, tailles et strides sont vérifiés avant accès.
Les mip extents diminuent jusqu'à 1, y compris pour les dimensions non puissances
de deux. Le stockage software est un ensemble de chaînes de mips par layer.

Formats réels : BGRA8/RGBA8 UNORM, D32 float et S8 uint. Les couleurs supportent
tiling linear/optimal ; D32 et S8 sont optimal uniquement. S8 utilise un octet
par texel, D32 et les couleurs quatre. Les trois aspects sont donc représentés
pour les formats qui les possèdent. Aucun format depth+stencil combiné n'est
encore supporté : il n'y a pas de prétention à un tracking indépendant de deux
aspects d'un tel format.

## Opérations mises en œuvre

- `vkGetImageSubresourceLayout` décrit les vrais offsets et pitches des images
  linear. `offset` est relatif à l'image, indépendamment de l'offset de binding.
- Les ranges normalisent `VK_REMAINING_MIP_LEVELS` et `VK_REMAINING_ARRAY_LAYERS`.
  Aspects, bornes, usages, formats, device et layouts sont validés.
- Views 2D et 2D-array portent de vrais ranges. Le framebuffer encore limité à
  une layer et le sampling 2D utilisent le mip/layer de base de la view.
- Barriers et preflight suivent chaque sous-ressource, avec une table de travail
  extensible. Une erreur de preflight ne publie aucun layout planifié ni ne
  modifie le contenu.
- Clears color/D32/S8 parcourent les ranges sélectionnés. Le clear D32 n'a plus
  de chemin mono-mip/mono-layer distinct.
- Copies buffer ↔ image et image ↔ image respectent mip, layers, offsets, extent,
  padding de ligne et de layer. Les transferts stencil ont des texels d'un octet.
- Copies et blits entre régions disjointes de la même image sont permis. Les
  chevauchements sont refusés même si deux handles mémoire importent séparément
  le même backing : l'identité comparée est celle de l'objet Vex.
- Blits color nearest/linear ciblent les mips/layers sélectionnés.
- SPIR-V `OpImageSampleExplicitLod` sélectionne des mips relatifs à la view,
  applique les clamps du sampler et effectue une sélection nearest ou une
  interpolation trilinéaire réelle.

Les références enfilées conservent images, métadonnées, mémoire, views, samplers
et descriptors jusqu'à la fin de leur utilisation. Fermer un handle local ne
rend pas invalide une référence déjà retenue par un travail en vol.

## Progression descriptors, SPIR-V et stencil

Les shaders peuvent indexer des tableaux fixes de UBO descriptors et de combined
image samplers, avec des indices calculés à l'exécution. Le cumul reste borné à
16 descriptors par stage. Les capabilities SPIR-V 28/29 et les features Vulkan
`shaderUniformBufferArrayDynamicIndexing` / `shaderSampledImageArrayDynamicIndexing`
sont supportées et doivent être activées lors de la création du device pour les
modules qui les déclarent. Elles ont été ajoutées après implémentation du chemin
de lecture réel, sans augmenter la version Vulkan annoncée.

`OpAccessChain` distingue sélection d'un descriptor et adressage d'octets dans un
UBO. Il traverse des membres, des tableaux `ArrayStride`, des composantes de
vecteurs et des colonnes de matrices, et peut chaîner les pointeurs résultants.
La réflexion std140 calcule récursivement alignement et taille. Les loads UBO
float/int32 et leurs vecteurs sont réels. Les arrays et les offsets n'utilisent
pas le nombre de FD/handle comme identité de ressource.

Le même stade de fragments du renderer implémente S8 : comparaison avec masque,
reference, writeMask, faces avant/arrière et les huit opérations stencil. Les
clears d'attachment respectent la view. Le chemin depthFail est représenté, mais
aucun format combiné depth+stencil n'existe pour valider son interaction avec un
test de profondeur. Les tests D32 séparés restent valides.

## Validation

Compilation native et ISO, puis boot VirtualBox avec `auto_vk_test`. Sources des
nouveaux tests : `vk_subresources_test.h`, `vk_descriptor_arrays_test.h` et
`vk_stencil_test.h`, appelés par le vrai `vk_test`.

Les attentes d'offset/pixels sont calculées indépendamment du runtime. Cas validés :

- chaînes 4×4 → 2×2 → 1×1 sur trois layers et chaîne NPOT 7×5 ; guards de binding ;
- clears isolés, layout par mip/layer, copies avec padding, readback et blits ;
- views partielles servant réellement de texture et render target ;
- D32 et S8 : clear d'un range, transferts, voisins inchangés ;
- mauvais aspect, bornes, padding débordant, extrémités signées extrêmes de blit
  et mauvais layout rejetés ;
- overlap refusé entre imports distincts, copie disjointe sur ce même backing ;
- indices de descriptors issus de push constants, copie d'un élément, descriptor
  absent rejeté avant mutation ;
- LOD 0/1/2, fractionnaire, négatif et supérieur au range, nearest/trilinéaire ;
- tableau std140 avec index chargé depuis un membre uint et AccessChain chaîné ;
- stencil KEEP/ZERO/REPLACE/INVERT, incrément/décrément saturants et wrap,
  masques, comparaison, front/back, clear d'attachment et enum invalide ;
- image/mémoire/pool fermés avec travail encore bloqué sur un semaphore ;
- tableaux de descriptors et leurs ressources fermés avec un draw encore en vol ;
- régressions du renderer, des queues, timelines/fences, capabilities Surface,
  WSI multi-image et recréation de swapchain.

Les logs de référence sont dans `build/lot37-final-serial.log` et
`build/lot37-final-build.log`. Le dernier marqueur de `vk_test` reste volontairement
`programmable renderer and WSI still incomplete` et le test conserve son critère
final rouge. Les assertions ciblées passant ne justifient ni de remplacer ce
critère ni d'annoncer une conformité officielle. Aucun CTS officiel n'a été lancé.
Le harness global `auto_smoke`, qui lance WebKit, n'a pas été exécuté dans cette
passe hors scope WebKit.

Le premier essai du nouveau test de descriptors en vol échouait parce qu'il
recevait le swapchain principal avant sa création. Les checkpoints ont localisé
le refus dans `vkAcquireNextImageKHR`, après un enfilement réussi. Le test crée
désormais son propre swapchain pour produire son signal externe ; aucune
sémantique de queue n'a été modifiée pour faire passer ce cas.

Fichiers source modifiés : `userspace/libvex/src/vulkan.c`,
`userspace/apps/vk_test.c` et les trois nouveaux headers de tests ci-dessus.
Le kernel et les ABI natives sont inchangés dans cette passe.

## Limites et dépendances restantes

1. MSAA : nombre de samples dans le stockage, couverture/interpolation,
   profondeur/stencil par sample, resolve différé et attachments de resolve.
   Les images/pipelines MSAA restent refusés ; sampleCounts reste 1.
2. Formats depth+stencil combinés, inventaire/layout par aspect et interactions
   depthFail réellement testables.
3. Images arrayed dans les shaders, 3D/cube, formats plus larges, blits inversés,
   swizzles et généralisation des render passes/framebuffers.
4. Dérivées/LOD implicite général. Le profil implicit-LOD accepte seulement le
   sampler clampé à LOD zéro ; un sampler mipmapped est explicitement refusé sur
   ce chemin. Les filtres min/mag différents, anisotropie, comparaison et bias
   non nul restent refusés.
5. SPIR-V général : variables locales/fonctions, aggregates complets,
   storage-buffer/image shaders, matrices/arrays plus générales et davantage de
   built-ins. Les arrays de matrices UBO sont refusés. Les limites actuelles
   restent 512 IDs, 64 blocs, 4 membres par structure et budget d'exécution 4096 ;
   relever ces limites exige aussi de sortir les grandes tables fixes de la stack.

Renderer et shaders restent userspace. Aucun changement d'ABI native, du modèle
kernel/userspace, des capabilities ou du driver GPU n'a été introduit ici. Aucun
code GL/EGL/WebKit ni driver matériel n'a été ajouté.

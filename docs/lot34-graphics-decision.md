# Lot 34 — audit de la stack graphique et décisions préalables

État au 27 septembre 2026. Cette note est un constat du code, pas une spécification
adoptée. Le Lot 34 demande explicitement de présenter les alternatives avant de
figer l'ABI kernel/userspace, le modèle de driver matériel, l'IR shader natif,
l'ownership global des ressources GPU ou leur sécurité. Ces décisions sont
atteintes avant qu'un véritable Vex Surface partageable et un renderer général
puissent être implémentés.

## Chaîne réellement présente

| Couche | Ce qui fonctionne | Limite démontrée |
| --- | --- | --- |
| GUI/compositor | `kernel/src/app_surface.c` crée et présente des `compositor_surface`; `userspace/libvex/src/vex_lvgl.c` utilise la GUI native. | Le mapping de surface (`kernel/include/unos/compositor_surface.h`) est un pointeur **kernel**, sans mapping utilisateur de la même surface. |
| Surface | `kernel/src/compositor_surface.c` valide les dimensions et formats, utilise des handles avec génération, compte références/maps/opérations, accumule des dégâts et compose des pixels. | 64 slots fixes ; autorisation par PID propriétaire ; `compositor_surface_reap_pid()` détruit les surfaces à la sortie du propriétaire ; `present()` marque immédiatement le serial terminé. Pas de capacité transférable ni de fence asynchrone. |
| GPU natif | `kernel/src/gpu.c` et `kernel/src/gpu_driver_software.c` allouent des buffers CPU, réalisent clear/fill/blit/composite/present. `userspace/libvex/src/vex_vk.c` traduit ces opérations. | Une seule queue ; `VEX_GPU_CMD_CLEAR/FILL_RECT/BLIT` uniquement ; `gpu_buffer_create()` n'accepte que XRGB8888. Aucun shader, pipeline GPU ou submission générale. |
| Vulkan | Quelques allocations/copies/clears CPU et un rasterizer triangle fixe dans `userspace/libvex/src/vulkan.c`. | `vkCreateShaderModule()` ignore `pCode`; les graphics pipelines ignorent les stages ; descriptors non appliqués ; draw utilise une structure de vertex fixe ; sync/barriers partiels. `vk_test` demande même un pipeline avec zéro stage et le déclare réussi. |
| EGL/GLES | Bibliothèques chargées par WPE et trace d'appels possible. | `egl_stub.c` rend des sentinelles et `eglSwapBuffers()` ne présente rien ; `gles_stub.c` annonce ES 3.0 alors que compilation/link, textures, FBO, draws et readback sont fictifs ou nuls. |
| WPE/WebKit | Le vrai WebKit démarre, obtient son endpoint et demande des snapshots. | `vex_wpe_backend.c` alloue un buffer `calloc` mais EGL ne dessine pas dedans ; `frame_rendered` accuse réception sans garantie de frame. `webkit_linux_main.c` copie les octets de `WebKitImage` dans un autre SHM. Les images noires ne prouvent aucun rendu WebKit. |

Le driver sélectionné dans `gpu_init()` est actuellement le driver logiciel.
Les identifiants PCI détectés ne correspondent pas à un driver de commande GPU.
`gpu_driver_ops_t` ne définit que les opérations de buffer et de composition
ci-dessus. LVGL doit rester un client GUI ; il ne fournit pas de GLES.

L'en-tête Vulkan natif `userspace/libvex/include/vulkan/vulkan.h` n'est pas
compatible avec les valeurs officielles de `VkStructureType` : par exemple
`MEMORY_ALLOCATE_INFO=45` au lieu de 5, `COMMAND_POOL_CREATE_INFO=38` au lieu de
39 et `RENDER_PASS_BEGIN_INFO=100` au lieu de 43. Le code accepte ces valeurs
parce qu'il ne valide presque jamais `sType`. Un programme Vulkan standard ne
peut donc pas être considéré compatible avec cette ABI. Référence :
[Khronos Vulkan-Headers, `vulkan_core.h`](https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/main/include/vulkan/vulkan_core.h).

Les documents antérieurs `docs/GPU_SURFACE_API.md` et
`docs/VULKAN_GPU_ARCHITECTURE.md` décrivent une intention utile : surface comme
frontière du compositor, backend opaque, capacités annoncées seulement si
opérationnelles. La promesse de conserver l'ABI userspace inchangée ne couvre
toutefois pas un objet surface directement partageable et mappable entre
processus : les syscalls actuels importent un buffer du même PID et exposent
un handle local. `message_ipc` possède des attachments, mais leur release
est codée pour `ladybird_fd_object_t`; ce n'est pas encore un transfert générique
de surface.

## Invariants communs aux options

1. Surface = description validée (dimensions, format, stride, usage, alpha) +
   référence vers un backing. Le format et le stride doivent être explicites ;
   aucun client ne peut supposer 4 octets/pixel ou une mémoire linéaire GPU.
2. Le backing survit à la mort du producteur tant qu'un client autorisé
   (compositor, snapshot, autre processus) le retient. Les droits de lecture,
   écriture, présentation et transfert sont séparés et jamais élargis lors
   d'un transfert.
3. Un CPU mapping est une opération autorisée sur un backing compatible ;
   un backing protégé ou device-local n'expose pas arbitrairement une adresse
   userspace. Un snapshot demande explicitement une vue CPU ou une copie
   asynchrone si nécessaire.
4. Un serial de présentation signifie une version du contenu ; sa complétion
   signifie que les écritures requises sont visibles, pas simplement que
   `present()` a été appelé. Damage et remplacement de surface ne changent
   pas l'ownership des octets déjà empruntés par un consommateur.
5. Le renderer valide tailles, offsets, handles, droits et commandes avant
   accès à la mémoire. Aucun shader non validé ne s'exécute en mode kernel.
6. Une API Vulkan/GLES annonce uniquement les versions, extensions, formats et
   fonctionnalités dont les contrats sont effectivement tenus. Erreurs et
   diagnostics reflètent les échecs réels.

## Décision A — emplacement des ressources et ABI Surface

**A1 : objet de backing/surface kernel à capacités locales (recommandé à
étudier en premier).** Le kernel gère identité, droits, références, mapping
CPU autorisé, transfert via attachment typé, import compositor, fences et
teardown. Le renderer logiciel et les API Vulkan/GLES restent en userspace.
Un futur driver GPU produit un backing opaque importable selon le même contrat.
Cela étend l'ABI Vex actuelle (versionnement requis), tout en réutilisant
`compositor_surface`, `process_memory`/SHM et `message_ipc`. Le principal coût
est de définir précisément droits, cohérence CPU/GPU et attach/detach.

**A2 : étendre `gpu_buffer`/`compositor_surface` PID-owned et faire exécuter
le renderer par `gpu_driver_ops_t` dans le kernel.** Cela réutilise le trajet
actuel et minimise le nombre de primitives userspace à court terme. En
revanche chaque nouveau shader, descriptor et commande graphique accroît la
surface d'attaque et la complexité du kernel ; la sortie du PID propriétaire
et le nombre de slots doivent de toute façon être corrigés. Un moteur shader
kernel devrait prouver validation et isolation bien plus strictes.

**A3 : backing principalement userspace, SHM natif + compositor important une
capacité de mémoire.** Favorise un renderer logiciel indépendant du kernel.
Il faut toutefois spécifier la cohérence et la sécurité de l'import, le
snapshot d'un backing GPU futur et les fences interprocessus. Une surface
réduite à un simple nom SHM ne permet pas d'exprimer ces besoins.

## Décision B — moteur programmable et représentation shader

**B1 : intégrer/adapter un renderer général existant en userspace** (par
exemple une pile Mesa llvmpipe/lavapipe, ou SwiftShader pour Vulkan avec
une couche GLES appropriée). Meilleure perspective de conformité et moins
de nouvelles règles de langage shader à inventer ; portage libc, threads,
JIT, build et winsys/Vex Surface lourds. Le JIT éventuel doit respecter W^X
sans exception. Mesa décrit llvmpipe/lavapipe dans sa
[documentation source](https://docs.mesa3d.org/sourcetree.html) ;
[SwiftShader](https://github.com/google/swiftshader) documente son Vulkan
logiciel et son architecture.

**B2 : construire un renderer logiciel Vex général en userspace**, avec
frontends SPIR-V validé et GLSL ES réellement compilé vers une IR interne
définie. Maîtrise maximale et intégration native, mais coût très élevé :
validateur, typage et contrôle de flux shader, textures/descriptors,
rasterisation, depth/stencil, blending, synchronisation et tests de
conformité. Le rasterizer triangle fixe actuel n'est pas une base suffisante
pour annoncer Vulkan ou GLES.

**B3 : SPIR-V comme IR de renderer commune**, avec GLSL ES compilé vers
SPIR-V. Réutilise directement l'entrée Vulkan ; nécessite malgré tout un
compilateur GLSL ES et un moteur capable d'exécuter le sous-ensemble annoncé.
Si cette IR devient une **ABI Vex** plutôt qu'un détail privé du runtime,
sa version, ses capacités, sa validation et ses règles de sécurité doivent
être décidées explicitement. Garder l'IR privée évite de figer trop tôt
une ABI shader native.

Ces choix ne sont pas totalement exclusifs : A1 + B1 peut établir le contrat
de surfaces et réutiliser un moteur mature ; A1 + B2 privilégie un renderer
Vex original. Aucun de ces choix n'implique que LVGL exécute des shaders.

## Décision C — premier backend matériel et frontière de confiance

**C1 : moteur graphique en userspace, kernel réduit aux ressources, mappings,
validation de commandes/accès matériels et scheduling.** Les drivers doivent
explicitement exposer import/export des backings, soumission, fences et
perte/reset du device. Un driver logiciel peut servir de référence avant
un transport matériel. Ce modèle élargit `gpu_driver_ops_t` et l'ABI Vex,
mais limite le parser shader hors kernel.

**C2 : driver de rendu kernel unique étendant `gpu_driver_ops_t`**, proche
des documents GPU historiques. Cela peut simplifier certains chemins de
présentation, mais concentre compilation/validation, mémoire et scheduling
GPU dans le kernel. Le modèle actuel de `CLEAR/FILL_RECT/BLIT` ne démontre
pas qu'il puisse servir de contrat Vulkan général sans refonte substantielle.

Il faut choisir aussi le premier transport matériel concret (virtio-gpu,
VMSVGA/SVGA3D ou matériel natif) **après** avoir défini le contrat commun ;
la présence d'un device PCI n'est pas une preuve de support Vulkan.

## Dépendances de mise en œuvre et validation après décision

1. Versionner l'ABI Surface et corriger l'inventaire de capabilities/rights ;
   séparer identité de surface, backing et handle de processus. Étendre les
   attachments IPC en objets typés avec rollback et destruction exacts.
2. Tester transfert producteur→compositor/snapshot, mort du producteur,
   destruction dans tous les ordres, protections de mapping, stride/format,
   damage, serials et attente sans polling. Vérifier que deux clients voient
   les mêmes pixels et qu'aucun PID ne peut deviner un handle tiers.
3. Porter ou construire le moteur choisi ; connecter d'abord une Surface
   CPU partagée, puis Vulkan et GLES/EGL à ce moteur. Corriger l'en-tête
   Vulkan selon Khronos et retirer les faux succès. Les objets/states sont
   testés séparément des opérations de rendu.
4. Utiliser des tests de pixels déterministes : shader de couleur variant
   selon les coordonnées, texture filtrée, deux triangles qui se recouvrent
   avec blending/depth, stencil, FBO→texture→compositor et lecture snapshot.
   Un pipeline avec zéro stage ou un shader invalide doit échouer ; une
   variation du shader doit modifier les pixels. Exécuter ensuite les suites
   de conformité adaptées à la version **réellement annoncée**.
5. Brancher WPE sur la même surface, retirer l'ack de frame non corrélé
   au rendu, puis vérifier plusieurs frames WebKit avec hash, nonblack,
   diversité et capture des octets. W^X/JIT demeure inchangé.

## État de validation de cette note

Audit statique du code et des scripts seulement. Aucun code exécutable n'a
été modifié, aucune build ou VM n'a été lancée dans cette étape. La tentative
de vérifier les outils graphiques WSL a été refusée par l'environnement
(`Wsl/Service/CreateInstance/E_ACCESSDENIED`) ; cela ne tranche aucune des
décisions ci-dessus.

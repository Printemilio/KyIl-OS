# Lot 37 — couverture fonctionnelle de la passe large

État de développement du 30 septembre 2026. **Lot 37 reste ouvert.** Les définitions publiques sont celles de Khronos ; le runtime expérimental annonce une version d'API 1.0 mais **n'implémente pas encore le contrat complet Vulkan 1.0**, ni toutes ses limites minimales. Aucun résultat de conformité Khronos n'est revendiqué.

**Pause décidée par l'utilisateur :** cet audit reste la référence de couverture
Vulkan. La passe suivante extrait le cœur shader/raster pour le frontend
EGL/GLES puis reprend WebKit ; elle ne clôture pas Lot 37.

Le runtime est `userspace/libvex/src/vulkan.c` avec les helpers privés compute/memory/texel. Les tests sont `vk_test.c`, `vk_subresources_test.h`, `vk_msaa_test.h`, `vk_api_contract_test.h`, `vk_coverage_test.h`, `vk_compute_test.h`, `vk_compute_graphics_test.h` et `vk_texel_test.h`. La présence d'un symbole, un retour de création réussi et un pixel non nul ne sont pas des preuves interchangeables.

## Statuts

- **IMPLEMENTED** : contrat décrit réellement exécuté ; la colonne tests précise la preuve.

- **PARTIAL** : sous-ensemble réel avec limitations identifiées.

- **REJECTED** : tentative explicitement refusée, feature false ou erreur de recording/création.

- **MISSING** : pas de mécanisme/API.

- **INCORRECT** : divergence connue, à corriger ; un test qui la révèle demeure rouge.

Une famille peut être PARTIAL avec plusieurs contrats IMPLEMENTED et d'autres REJECTED/MISSING. Les validations défensives du runtime ne remplacent pas une validation exhaustive du SPIR-V ou les layers Vulkan.

## Matrice des familles Vulkan 1.0

| Famille | État | Contrats réels et preuve | Limites / absence / erreur connue |
|---|---|---|---|
| Instance / physical device / device | PARTIAL | Headers Khronos, fonctions de lookup séparées global/instance/device, device features validées, un physical device software, négatifs lookup/sparse dans `vk_api_contract_smoke` ; capacité zéro : count=0, sortie inchangée et INCOMPLETE testé | Pas de layers ni callbacks d'allocation personnalisés. Profil 1.0 incomplet. |
| Memory | PARTIAL | Backing Vex, identité indépendante des handles, import/export local sécurisé, binding/refcount, map/unmap et ranges host cohérents, flush/invalidate réels par fence CPU cohérente ; tests mapping/alias/lifetime ; heap égal au PMM réel et accounting 384 événements sans croissance | Un type HOST_VISIBLE+HOST_COHERENT ; pas de mémoire GPU/lazy allocation. `vkGetDeviceMemoryCommitment` absent. Extents MRAC recyclés exact-size ; fragmentation entre tailles différentes encore possible. |
| Buffers | PARTIAL | Mémoire native partagée ; usages/bounds/alignements ; copy/update/fill différés ; SSBO std430/runtimes arrays read/write et atomiques compute vérifiés. Destruction publique in-flight testée. | Draw indirect graphics absent. Range SSBO limitée à 64 KiB, inférieure au minimum Vulkan 1.0. |
| Images | PARTIAL | 2D, mips/layers/pitches et layouts indépendants ; MSAA ; storage RGBA8 compute réel, partial views et 64 pixels vérifiés. | Pas 1D/3D/cube/compressed/sparse ; color BGRA8/RGBA8 UNORM, D32/S8/D32_S8 ; autres storage formats et image atomics absents. Backing natif maximal 64 MiB. |
| Image views | PARTIAL | 2D/array, ranges réels, sampling/render target/storage image sur partial views ; références retenues in-flight. | Pas cube/3D/réinterprétations générales. Storage views un mip ; framebuffer une layer. |
| Buffer views | PARTIAL | R32_UINT/SINT/SFLOAT, offsets/ranges et VK_WHOLE_SIZE validés ; vues refcountées ; texel fetch/write et copies de descriptors. VM : vk_texel_smoke passe, voir rapport compute. | Autres formats, image atomics et indexation dynamique des arrays texel explicitement refusés. |
| Samplers | PARTIAL | nearest/linear, LOD explicite, mip nearest/trilinéaire, clamp et sampling array ; tests pixels exacts / LOD | Pas d'anisotropie, comparaison profondeur, texture multisample ou dérivées/implicit LOD général. |
| Shader modules | PARTIAL | Copie immutable du code fourni ; validation/reflection à la création de pipeline, specialization sur copie privée ; modules retenus après destruction publique | Création du module seule ne signifie pas shader supporté. Seulement SPIR-V 1.0 et subset détaillé plus bas. |
| Descriptors | PARTIAL | UBO/SSBO, sampler/image/combined/storage RGBA8, texel views ; arrays fixes et indexation dynamique UBO/SSBO/sampled/storage images ; pending sets et ressources retenus. | Dynamic buffer offsets absents. 16 ressources réfléchies/stage, 8 sets. Graphics storage writes/atomics non annoncés et rejetés. |
| Pipeline layouts | PARTIAL | Sets typés, ranges de push constants stage/offset/size validés ; 128 octets par stage ; padding exclu du masque de données utilisées | Pas de pipeline-layout compatibility générale démontrée sur tous les rebindings. Push/UBO matrices physiques encore limités à mat4 column-major stride 16. |
| Pipelines graphics / compute | PARTIAL | Graphics vertex/fragment et compute réels ; Dispatch/DispatchIndirect, IDs locaux/globaux, memory/barriers/atomics. Tests GLSL et aller-retour compute→graphics→compute avec 64 pixels exacts. | Pas geometry/tessellation/derivation générale ; une attachment/viewport/binding vertex. Compute borné, voir rapport dédié. |
| Pipeline cache | PARTIAL | Cache réel code+reflection, append atomique, sérialisation avec header Khronos + format privé, import validé, merge ; multi-create et draw après destruction des caches testés | Cache privé borné à 16 MiB ; ce n'est pas un cache de code machine/JIT. Pas de promesse de compatibilité inter-version de son payload privé. |
| Command pools / buffers | PARTIAL | Primary/secondary hors render pass ; ExecuteCommands dans moteur commun ; génération immutable, rerecord invalide le primary ; reset pending rejeté ; pool destruction in-flight testée. | Secondary graphics/inheritance render pass absent ; pas de nesting. Primary direct uniquement au submit. |
| Render passes | PARTIAL | Une subpass graphics, color/depth/stencil/resolve indexés ; load/clear/store et layouts ; dépendances EXTERNAL→0 et 0→EXTERNAL conservatrices réelles ; clear attachments différé | `vkCmdNextSubpass` absent. Pas d'input attachments/MRT/multisubpasses/self-dependencies. Resolve attachment loadOp autre que DONT_CARE explicitement refusé. |
| Framebuffers | PARTIAL | Vues/extent/formats/sample count réels ; lifetime retenu ; pixels de clear partiel et voisins vérifiés ; granularity 1×1 | Une layer, une color + une depth/stencil + une resolve maximum. Pas de framebuffer sans attachment. |
| Synchronization | PARTIAL | Binary semaphores sur event counters, kernel timelines/fences, completion vraie, waits scheduler ; mapped coherent visibility ; barriers globales conservatrices et layouts par sous-ressource | Une queue. Pas de modèle multi-queue/ownership transfer GPU. Barriers in-render-pass et self dependencies refusées. Destruction/idle peut attendre des dépendances non satisfaites par l'application. |
| Events | IMPLEMENTED pour le backend CPU exposé | Host set/reset/status, deferred set/reset/wait ; inscription→check→sleep et signal atomiques ; waiter retenu, pas de polling temporisé ; submit non bloquant testé | Stages exécutés conservativement à la frontière de commande sur la seule queue CPU. Pas de stage overlap GPU annoncé. |
| Queries | PARTIAL | Occlusion précise par sample réellement passé, reset/begin/end, host 32/64-bit/availability/WAIT/PARTIAL, copy différée ; voisins et discard zéro vérifiés | Timestamps / pipeline statistics explicitement refusés, timestampValidBits=0. Validation des transitions de query encore à l'exécution, pas de transaction de préflight complète pour une séquence invalide. |
| Transfer operations | PARTIAL | Copies buffer/image et aspects depth/stencil séparés, ranges/padding, alias overlap ; update/fill capturés différés ; tests mip/layer/voisins | Uniques formats supportés, images 2D ; pas de compressed/3D. Un format supporté pour un rôle ne l'est pas nécessairement pour tous. |
| Clears / resolves / blits | PARTIAL | Ranges mip/layer, clears color/D32/S8/combined conservant autre aspect ; resolves MSAA explicites et render-pass, blit nearest/linear ; oracles samples/quantification | Pas de depth/stencil resolves, formats integer/sRGB/HDR, 3D ou tout cas de scaling complexe. MSAA sampled interdit. |
| Dynamic state | PARTIAL | Viewport/scissor, blend constants, stencil front/back masks/ref, lineWidth=1, depthBias constant/slope(clamp=0) ; record immuable et pixels testés | `vkCmdSetDepthBounds` absent et feature false ; wideLines false. Slope depth bias implémenté mais sans oracle de gradient dédié dans cette passe. |
| Rasterization | PARTIAL | Clipping homogène, culling, top-left, interpolation perspective corrigée, profondeur, subpixel grid 8 bits et edge produits entiers ; RGB asymétrique et unequal-W indépendants | Lines encore DDA, strictLines=false : pas de conformité générale de la rasterisation des lignes. Points taille 1 ; pas de flat/noperspective/centroid/point-size shader. Couverture grande taille testée séparément. |
| Multisampling | PARTIAL | Storage 1/2/4/8, positions standard, masks, couverture/depth/stencil/blend par sample, resolve color ; 2/4/8 comparés à oracles indépendants ; FragCoord évalué au centre du pixel, neuf oracles x/y/depth/1-w et resolve | Sample shading / alphaToOne / sample IO false ou rejetés ; alphaToCoverage déterministe par alpha quantifié. Pas de claim de conformité de tout le shader environment multisample. |
| Depth / stencil | PARTIAL | D32 / S8 / D32_S8, front/back, huit opérations/masks/compare, depthFail, bias ; transferts/clears des aspects indépendants conservant voisins | Layout depth+stencil couplé conformément au profil sans separateDepthStencilLayouts ; pas D16/D24, depth bounds/clamp. |
| Blending | PARTIAL | Opérations/facteurs supportés, write masks, dynamic constants ; 16 logic ops avec oracle indépendant, blend ignoré lorsque logicOp actif | UNORM8 seulement, une attachment ; independentBlend / dualSrc false. Pas de blend HDR/sRGB. |
| Vertex input | PARTIAL | float32 scalar/vec2/vec3/vec4, UNORM8/SNORM8, locations multiples ; index uint16/uint32 ; instance rate, firstInstance/InstanceIndex et stride zéro | Un binding, 8 attributes ; pas de integer IO complet, 16/64-bit vertex formats, multi-binding. Le test stride zéro a été corrigé pour ne plus confondre son mode avec le booléen resolve. |
| Shader environment / SPIR-V | PARTIAL | Float/int32/vectors/matrices/composites/functions/AccessChain, GLSL450 Logical, std140/std430, Workgroup rendezvous, atomiques CPU32 seq_cst, storage images/texel buffer subset. | SPIR-V1.0 borné ; derivatives, IO général, physical matrix layouts et nombreuses instructions/capabilities absents. Aucune conformité globale. |
| Queues | PARTIAL | Une queue GRAPHICS+COMPUTE+TRANSFER avec worker Vex réel ; enqueue non bloquant ; chaîne submits/events/semaphores/timelines/fences et lifetime retenu. | Pas multi-queue/priorités générales ; exécution CPU sérielle par queue, workgroups sériels et lanes via continuation. |
| WSI Vex expérimental (hors core 1.0) | PARTIAL | Surface partagée, swapchain 2–4 images, acquire/render/present/release async, compositor sur backing réel, fences ; retirement/recréation trois images testés | BGRA8 / FIFO / extent fixe ; pas de plateforme stabilisée/numéro Khronos attribué. Échec du run 7 expliqué par la fuite de références des capabilities mappées (correction et nouvelle validation décrites ci-dessous). |

## SPIR-V : contrats et limites

- Scalars : bool/int32 signed+unsigned/float32 ; vectors 2–4 ; opérations arithmétiques, conversions, comparisons, logique/bitwise et subset GLSL450 validés. Pas int64/float64/float16.

- Matrices logiques : 2–4 lignes/colonnes, MatrixTimesVector, VectorTimesMatrix, MatrixTimesMatrix, MatrixTimesScalar, Transpose, OuterProduct ; test rectangulaire 2×3 / 3×2 indépendant. Les matrices physiques de blocks restent mat4 column-major stride16.

- Structures/arrays logiques imbriquées : représentation compacte distincte du std140/std430 ; CompositeConstruct/Extract/Insert, CopyObject, VectorExtract/InsertDynamic ; Function locals avec initialized/written tracking. Fonctions typées, ReturnValue, pointeurs locaux propagés ; recursion/type mismatch rejetés.

- UBO : structures/arrays fixes std140, integer loads et chained AccessChain, contrôle bounds de descriptor. Push : offsets std430 jusqu'à 128, arrays/structures et masque excluant padding.

- Specialization : SpecId bool/int32/float32 et composites ; copie privée, defaults préservés. **OpSpecConstantOp refusé.**

- IO : float locations, VertexIndex, InstanceIndex ; **FrontFacing bool testé en VM dans les deux orientations de pipeline, avec pixels MSAA et resolve**. **FragCoord vec4 float32 réel** : x/y au centre, depth interpolée et 1/w, neuf cas triangles 2/4/8 samples avec oracles indépendants. Pas FragDepth, flat/no-perspective, shader IO arrays généraux, gl_PerVertex blocks complets. Line/point FragCoord implémenté mais sans oracle dédié ; la rasterization line demeure partielle.

- Texture : descriptors fixes/arrays, dimension 2D/array, explicit Lod et mip linear ; storage RGBA8 imageLoad/imageStore en compute ; pas de derivatives/ImageFetch/texture gather/image atomics.

- Bornes privées : 2048 IDs, 256 blocks, 16 members/parameters, profondeur agrégat 8, agrégat logique 16 words, call chain 32, instruction budget min(16×wordCount,65536). Ce sont des barrières de ressources explicites ; elles demeurent plus restrictives qu'un environnement Vulkan général.

- Limite encore à auditer : passage de pointeurs de variables globales Input/Output/descriptor comme paramètres de helper ; les paramètres Function-local sont couverts, pas tous les autres storage classes. Ne pas en déduire une implémentation complète d'AccessChain/functions.

## Corrections démontrées durant cette passe

1. **Libération native de heap à taille zéro** : `vk_heap_free` passait une requête size=0, rejetée par `process_memory_remove_region`. Cela conservait les régions/pages de tous les objets Vulkan ; une longue série de tests atteignait l'échec d'allocation du root MRAC. Le runtime conserve désormais la taille exacte page-rounded dans un header privé de 16 octets. Le run 2 échoue ; les runs 3–7 franchissent le point précédent. Le contrôle PMM est désormais réel après séparation native/legacy ; trois défauts mémoire supplémentaires ont été corrigés (voir contrat syscall et validation).

2. **Barycentriques cycliquement associées aux mauvais vertices** : e1/e2/e0 sont les arêtes opposées aux vertices 0/1/2 ; l'ancien e0/e1/e2 passait les tests trop symétriques mais faussait les couleurs. Oracle RGB asymétrique, unequal clip-W et samples/resolve vérifient désormais les bons poids.

3. **Coverage grand viewport** : produits des arêtes évalués sur la grille subpixel 8 bits avec int64 avant conversion, pour conserver signes/égalité à zéro sans cancellation float. Oracle dédié deux triangles, 4096 pixels, quatre samples, couverture exacte sans doubles ni trous.

4. **Push constants** : contrôle public limité à 128 octets par stage au lieu de la taille cumulée des deux banques privées ; masque des mots nécessaires hors padding ; high offset112 / spec defaults réellement testés.

5. **Clear stencil avec depth inutilisé** : validation selon aspects demandés ; un champ depth invalide n'invalide plus un clear stencil-only. Vérification que depth et couche voisine restent intacts.

6. **Stride zéro / instance fetch** : division par stride protégée, offset d'attribut ne doit pas être artificiellement limité par stride, firstInstance et InstanceIndex réels ; test event-gated et closure du backing producteur.

7. **Ancien vex_vk** : utilisateurs audités (sources + scripts), six références mortes de build retirées, façade `vex_vk.c/.h` supprimée. Instance/device sont désormais directement dans le runtime Vulkan ; aucun deuxième moteur créé.

8. **Capabilities Surface retenues puis mappées** : `vk_memory_create_vex_backing` crée un handle, `vk_memory_attach_surface` retient une référence puis mappe, et le créateur rend sa référence. `compositor_surface_release` refusait cependant toute release si la capability était mappée, même avec `local_refs=2`. Le retour ignoré conservait une référence par allocation ; l'épuisement des 256 slots se produisait soit à la recréation WSI (run 7), soit sur le dernier cas MSAA (runs 10–12, ligne 112, premier backing color du mode 31). La correction conserve le refus de fermer la **dernière** référence mappée, mais autorise la release d'une référence additionnelle. Pas de quota augmenté ni de changement public de modèle. Le test répète 300 créations/retain/map/releases, vérifie BUSY sur last-close avant unmap, maintien du contenu, puis invalidité du handle après la dernière release.

## Validation et traçabilité

Build complet kernel/apps/tests Linux/PE/ISO par `scripts/build.ps1 -SkipWebKit`.

Le main Linux utilisant le SHM natif a également été réellement recompilé sous

WSL ; objdump confirme les IDs tagués 161/163/162 et le hash correspond au staging.

Aucun lancement WebKit, GL/EGL ou driver matériel. VM VirtualBox `Vex-OS`, boot

ciblé `auto_vk_test`, compositor réel. Tests de style conformance avec oracles

indépendants : **pas de CTS officiel ni de certification Vulkan**.

Les logs historiques `lot37-wide*-serial.log` restent conservés. Leurs échecs

heap/capabilities et collision ABI ne sont pas effacés ; ils précèdent cette passe.

Les nouvelles preuves sont :

- `build/lot37-abi1-serial.log` : séparation des deux IDs 1 correcte, PMM enfin lu,

  mais +9 pages après 128 événements.

- `build/lot37-abi2-diagnostic-serial.log` : +256 versions privées d'inventaire par

  série ; +8/+10/+8 pages PMM. `store_root_copy` retenait tous ses anciens roots.

- `build/lot37-abi3-root-serial.log` : root constant à une version mais +3 pages

  par série : métadonnées des chunks/pages libérés jamais recyclées.

- `build/lot37-abi4-serial.log` : 69 cas graphiques passent, root stable,

  delta PMM total 1 page après 384 événements (seuil inchangé 8).

- `build/lot37-abi5-regression-fixture-serial.log` : arrêt sur le fixture static

  attendu à tort avec exit=0 ; son contrat historique est exit=7 (PIE=9).

  L'oracle a été corrigé, pas le programme ni l'OS.

- `build/lot37-abi6-VA-diagnostic-serial.log` : les 17 régressions syscall passent,

  puis MRAC refuse 100 KiB avec 892 042 pages libres : fenêtre VA kernel épuisée.

  Les extents libérés sont maintenant réutilisés à taille identique.

- `build/lot37-abi7-final-serial.log` : full build7, 17 régressions passent ;

  800 MiB cumulés avec 4 MiB vivants ; 69 cas graphiques, 300 cycles capabilities,

  16384 samples exacts, queues async et swapchain 2 puis 3 images passent.

  Heap Vulkan égal au PMM ; root constant à une version et **delta PMM zéro**

  après 3×128 événements. Aucun panic/fault kernel observé. Les fautes ring3

  volontaires, dont le processus non géré attendu avec exit=139, sont testées.

- `build/lot37-abi7-repeat-serial.log` : second boot de la même ISO, mêmes

  résultats et delta PMM zéro. VM arrêtée après validation.

Le détail du wire, des fonctions modifiées, des trois causes mémoire distinctes

et des tests se trouve dans [lot37-syscall-domains.md](lot37-syscall-domains.md).

Les corrections ne relèvent aucun quota/seuil et n'enlèvent pas les tests rouges.

La ligne série historique «128 event allocation/free cycles» donne le delta

total des trois epochs de 128, comme l'attestent les trois relevés précédents.

SHA256 identiques binaires/staging de l'ISO testée :

- `vk_test` : `B8AF04A8FD2E17D4CCA1CA43395D580B049E877A5DCF98B00200A5F47053CED4`.

- `build/vex-os` : `6A7BC5EC2D10F9F2E05CA0F22B7B6E0AD2E2169F67BB0B6F16A956D22C89813B`.

- `webkit_linux_main` : `7054A530557645B482FE44FA048E419C99C8AE145C4225C1C12AA135A373BB2E`.

Les smokes réexécutés couvrent capabilities/backings/import/export, queues async

et WSI/compositor, mips/layers/layouts, descriptors/lifetime, shaders/vertex fetch,

D32/S8, sampling, queries/events. Le harness global n'est pas exécuté : son boot

`auto_smoke` lance `/webkit` même dans cette passe graphique hors WebKit. Une suite

finie de 17 fixtures spawn/wait est exécutée avant Vulkan (sécurité ring3/RSP,

événements/faute, static/PIE, TLS/FPU, futex, ownership, mmap fichier, appels non

supportés, pipe/exec, SEQPACKET/transfert FD, signaux/faute fatale, W^X et glibc).

Cela ne constitue pas la réexécution exhaustive des Lots 1–36.

Le marqueur final `programmable renderer and WSI still incomplete` et le retour

1 restent **intentionnels**, y compris lorsque les contrats testés passent.

Après les deux runs, le seul marqueur boot `auto_vk_test` est retiré du staging

et de limine.conf puis l'ISO est repackée pour boot normal, sans changer les

binaires ci-dessus. La suite reste reproductible avec le marqueur et un build.

## Inventaire des symboles et absences

126 symboles présents sur 137 entrées core 1.0 inventoriées, contre 97 avant cette passe. **Ce compteur ne mesure pas la conformité.** Les 11 absences restantes sont :

`vkGetDeviceMemoryCommitment`, `vkCmdSetDepthBounds`, `vkCmdDrawIndirect`, `vkCmdDrawIndexedIndirect`, `vkCmdNextSubpass`.

Des APIs présentes refusent explicitement : sparse binding, timestamp, pipeline statistics ; leurs features/properties doivent être lues conjointement. LogicOp, occlusionQueryPrecise et les deux features de descriptor-array dynamic indexing supportées sont true ; les autres features demeurent false.

## Décision native appliquée

Option 2 décidée par l'utilisateur : domaine natif tagué dans RAX, avant tout

décodage d'arguments ; une seule entrée CPU et mêmes validations userspace.

BULK reste 1, legacy WRITE reste 1. Les wrappers/binaires natifs sont migrés,

l'adaptateur legacy 4..196 est figé et les autres personnalités restent cohérentes.

Voir [le contrat et les régressions](lot37-syscall-domains.md).

Trois défauts mémoire mesurables ont été corrigés sans changer le modèle MRAC :

versions propriétaires des roots privés non libérées ; descripteurs morts non

recyclés ; extents kernel VA libérés jamais réutilisés. Les caches exact-size

restent bornés et leur fragmentation entre tailles est une limite ouverte.

La reprise Vulkan ajoute FragCoord réel (x,y,z,1/w), validation du type/stage,

et neuf oracles triangles MSAA 2/4/8 ; l'UUID du payload privé de pipeline cache

change pour refuser les anciennes reflections. L'énumération de physical device

à capacité zéro renvoie correctement INCOMPLETE. Aucun support fictif ajouté.

## Prochaines briques nécessaires

1. Généraliser l'environnement shader : limites SSA/pointeurs, layouts matrices physiques, IO/built-ins/interpolation/derivatives, SSBO range au minimum Vulkan, plus formats texel/storage et image atomics.

2. Secondary graphics avec inheritance, multisubpasses/input attachments/MRT/layered framebuffer ; indirect draws graphics et query transition preflight.

3. Formats Vulkan obligatoires, dimensions images/samplers/image operations, précision et rasterization line ; audit complet limites minimales.

4. Validation indépendante plus large et sélection CTS/deqp adaptée au runner Vex ; revoir le marqueur d'incomplétude uniquement après preuve du contrat entier.

GL/EGL/WebKit, drivers matériels et JIT JSC restent hors scope.

## Passe compute / secondary / texel du 30 septembre

Voir [contrat, tests indépendants, validation et limites](lot37-compute.md).

Les 29 familles restent PARTIAL/MISSING selon leurs contrats complets. Cette passe

ne remplace aucune preuve antérieure par un simple symbole ou succès de création.

Vulkan reste expérimental/incomplet 1.0.


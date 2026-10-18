# Lot 37 — Compute, mémoire shader et secondary command buffers

Passe du 30 septembre 2026. **Lot 37 ouvert ; API 1.0 expérimentale et incomplète.**
Les shaders et le renderer restent en userspace. Aucune modification de l'ABI
native, des capabilities ou de la frontière kernel/userspace n'est nécessaire
pour cette passe. GL/EGL/WebKit et les drivers matériels ne sont pas exécutés.

## Exécution et mémoire

`vkCreateComputePipelines` construit un vrai pipeline GLCompute à partir du module
spécialisé, de sa reflection et du layout. `vkCmdDispatch` et
`vkCmdDispatchIndirect` capturent pipeline, sets, push constants et buffer indirect
dans le modèle différé commun. Le worker asynchrone existant exécute ces records ;
les fences et semaphores conservent leur completion réelle. Les dimensions
indirectes sont lues pendant l'exécution, après les commandes/barrières antérieures.

Une workgroup dispose de mémoire partagée et de continuations pour ses lanes.
Chaque lane conserve valeurs, PC, prédécesseur, fonction et call stack lorsqu'elle
atteint `OpControlBarrier`. L'exécution reprend seulement après rendezvous de
toutes les lanes au même site dynamique. Une divergence irrécupérable est une
erreur DEVICE_LOST ; aucune barrière n'est remplacée par un no-op ou un timeout.
Les groupes sont sériels dans ce backend CPU, sans empêcher une autre implémentation
future de paralléliser le moteur userspace.

Le profil mémoire supporté est **Logical / GLSL450**, pas VulkanMemoryModelKHR.
Device/Workgroup/Invocation et les ordres GLSL450 reconnus reçoivent une sémantique
CPU plus forte, seq_cst. Les atomiques 32 bits utilisent les primitives atomiques
CPU, avec contrôle d'alignement, type, portée, permissions et descriptor range.
Les portées ou bits de sémantique inconnus sont refusés à la création de pipeline.
Il ne s'agit pas d'émulation par load/store ordinaire.

SSBO BufferBlock std430, arrays/runtimes arrays, `AccessChain` et `OpArrayLength`
utilisent offsets/strides et bounds réels. La taille physique d'une Workgroup n'est
pas limitée aux 16 words de la représentation SSA logique : une array shared de
16 uint est utilisée par le test. Storage images : RGBA8 UNORM, 2D/array, view
d'un mip, GENERAL, read/write et conversion UNORM réels. Les voisins de mip/layer
ne sont pas touchés. La validation des descriptors et des layouts est commune
aux étapes compute et graphics ; les writes/atomics graphics ne sont pas annoncés.

Les quatre features core d'indexation dynamique UBO/sampled image/SSBO/storage
image sont réellement prises en compte dans le device. La reflection détecte
aussi les index non constants lorsque glslang n'émet pas de capability spécifique.
Un device créé sans ces features refuse le pipeline concerné.

## SPIR-V et commandes

Ajouts : GLCompute/LocalSize, six built-ins de workgroup/invocation, runtime array,
Workgroup/Private, Switch, InBoundsAccessChain, ArrayLength, CopyMemory simple,
imageLoad/imageStore et les barrières/atomiques décrites ci-dessus. Les fonctions,
loops, conversions, composites et opérations déjà supportés sont utilisés par
les shaders compute sans créer un interpréteur distinct.

Une divergence démontrée par glslang a été corrigée : l'ancien moteur refusait
`OpIAdd` entre uint et une constante int de même largeur. SPIR-V permet cette
différence de signedness pour ces instructions ; l'opcode détermine l'interprétation.
Le type de résultat et le nombre de composantes restent contrôlés. Cette correction
est exercée par une boucle uint, un Switch et une fonction GLSL.

Secondary command buffers : compute/transfers **hors render pass**. Un primary
retient le secondary et sa génération. Rerecord/reset invalide les captures
antérieures ; reset pending est refusé. Destruction du pool public ne libère pas
les records encore retenus. Le preflight et la retenue des descriptors utilisent
le même parcours des records ; l'exécution utilise le même moteur de queue.
Direct-submit secondary et nesting sont refusés. L'inheritance graphics reste absent.

Texel buffers : `vkCreateBufferView`/`vkDestroyBufferView`, descriptors uniform et
storage texel réels, offsets/ranges (dont WHOLE_SIZE), R32_UINT/R32_SINT/R32_SFLOAT,
65536 texels maximum et alignement 4 octets. `OpImage` + `OpImageFetch` accèdent aux
vues uniform ; `OpImageRead`/`OpImageWrite` aux vues storage. Les composantes
absentes sont réellement complétées par 0/0/1 selon le type. Descriptors et copies
de descriptors retiennent la vue, puis le buffer et le backing. Formats inconnus,
indexation dynamique des arrays texel et atomiques image restent refusés.

Le préflight graphics ne contrôlait auparavant que UBO/combined samplers. Il
vérifie désormais aussi les storage images et leurs layouts par sous-ressource ;
le contrôle des ressources et des formats texel est partagé avec compute. Un
fragment shader GLSL readonly image2D est réellement exécuté. Le négatif effectue
une transition incompatible puis demande un draw : submit échoue avant de clear
la cible, qui garde ses 64 sentinelles. Le second submit valide lit les 64 pixels
rouges, sans modifier l'image source.

## Tests indépendants et preuves VM

Huit sources GLSL dans `userspace/apps/shaders/compute_*.comp` et
`fragment_readimage.frag`, compilées par
Khronos glslang 15.1 et validées par SPIRV-Tools 2025.1 :
`glslangValidator -V --target-env vulkan1.0` et
`spirv-val --target-env vulkan1.0`. Les fixtures C sont générées, pas fabriquées
pour la grammaire du runtime. Reproduction : `scripts/build-vulkan-shaders.sh`
puis `scripts/build-vulkan-shaders.ps1` ; le switch `-Compile` combine les deux
avec WSL. Les binaires générés restent disponibles sans installer ces outils
dans la VM. Les assertions utilisent des oracles CPU distincts de l'interpréteur.

| Test | Oracle et cas négatifs |
|---|---|
| `vk_compute_smoke` | 6 workgroups × 16 lanes, échange shared + deux rendezvous, 192 words exacts, guards ; submit retourne derrière un event non signalé ; closure pipeline/module/pool/backing avant completion. |
| `vk_compute_image_smoke` | 64 pixels sur une view mip1/layers1–2, image read/write, barrier image→buffer, voisins intacts ; layout incorrect refusé avant mutation. |
| `vk_compute_ops_smoke` | 8 familles d'opcodes atomiques, 9 appels dont CAS succès/échec ; 192 words, Switch/loop/function, ArrayLength. Secondary dispatchIndirect lit les counts produits par UpdateBuffer ; stale generation/direct-submit/reset pending négatifs ; destruction pool et backing en vol. |
| `vk_compute_arrays_smoke` | Deux SSBO + deux storage images dynamiquement indexés ; deux dispatches et barrière compute→compute, compteurs 8, pixels distincts, guards conservés. |
| `vk_compute_negative_smoke` | Device isolé : local size 129, portée CrossDevice, feature disabled, dispatch sans pipeline et count 65536 refusés ; vrai accès au-delà du descriptor range termine la queue en DEVICE_LOST, pas en faux succès ; guards conservés. |
| `vk_compute_graphics_smoke` | Compute écrit les vertices ; barrière → draw réel 8×8 → barrière → compute imageLoad. 64 pixels rouges exacts, readback SSBO protégé, 64 samples occlusion ; second draw readonly storage-image et mauvais layout refusé sans clear. |
| `vk_texel_smoke` | 8 texels × 3 formats uint/sint/float ; fetch/write, composantes par défaut, offset16/range32, WHOLE_SIZE, invalid alignment/overflow/format ; descriptors copiés, propriétaires fermés derrière event non signalé ; outputs et guards exacts. |

`build/lot37-compute-full1-serial.log` : build complet kernel/apps/ISO, 17 fixtures
syscall/spawn/glibc/security/memory/IPC, 800 MiB heap churn avec 4 MiB vivants,
compute et tous les anciens cas graphiques, queues asynchrones, WSI 2 puis 3
images, 300 cycles capabilities et 384 événements passent. Le premier test
compute→graphics ne relisait pas encore le framebuffer depuis compute.

`build/lot37-compute-full2-serial.log` ajoute la lecture compute du framebuffer,
avec les mêmes résultats et **delta PMM zéro**, root owner une version. L'unique
diagnostic worker error / SPIR-V Store failure est le négatif bounds intentionnel
sur device isolé, suivi de son assertion DEVICE_LOST réussie. Les logs diagnostiques
intermédiaires restent conservés ; ils ne remplacent pas les runs complets.

`build/lot37-texel-pass-serial.log` : premières preuves texel, puis tous les
autres tests / WSI / PMM. Les trois logs `lot37-texel-diagnostic*-serial.log`
gardent l'échec de harness initial : le helper de recording reset un command
buffer, mais le nouveau pool n'avait pas RESET_COMMAND_BUFFER_BIT. Le refus du
runtime était correct ; le test crée désormais le pool avec le droit nécessaire.

`build/lot37-compute-final-serial.log` : reconstruction complète depuis les sources
finales, huit shaders validés Khronos, 17 fixtures syscall et churn 800 MiB,
tous les compute/texel/graphics (dont le fragment readonly storage-image), les
cas graphiques historiques, WSI async et capabilities passent. Delta PMM zéro
après 384 événements ; root owner une version ; aucun panic kernel. L'échec
DEVICE_LOST du test bounds reste visible et attendu. Le marqueur final incomplet
reste volontairement rouge.

SHA256 `vk_test` identique au staging testé :
`C4AA455D992EF8F45E1189F9C59CD0F85202A27934A2BD4B455FB46DA0528156`.
Kernel/staging identiques et inchangés dans cette passe :
`6A7BC5EC2D10F9F2E05CA0F22B7B6E0AD2E2169F67BB0B6F16A956D22C89813B`.

Après validation, `auto_vk_test` est retiré et l'ISO repackée sans changer ces
binaires. Le boot normal a été revérifié : bureau/compositor et prompt terminal
visibles dans `build/lot37-compute-normal-ui.png`. Cela ne constitue pas un test
interactif exhaustif de toutes les applications.

Une confusion de harness a aussi été corrigée dans les nouveaux tests : le macro
historique `vkQueueSubmit` de `vk_test.c` attendait queue idle pour les anciens
pixel-tests. Utiliser ce wrapper derrière un event signalé par l'appelant créait
une attente du test sur lui-même. Les tests async appellent désormais la fonction
raw, sans modifier queue/scheduler pour ce défaut de harness.

Pas de runner CTS/dEQP installé ou porté : ces tests indépendants ne constituent
**ni le CTS officiel ni une certification de conformité**. Le marqueur final
`programmable renderer and WSI still incomplete` / exit=1 est conservé.

## Limites restantes

- Une seule queue, renderer CPU sériel ; pas de overlap GPU/stages indépendant.
- Invocations/group 128, axes 128/128/64, counts 65535, shared 16 KiB ; limits
  contrôlées et annoncées telles quelles. Range SSBO 64 KiB **inférieure au minimum
  Vulkan 1.0** ; backing natif 64 MiB. Aucun claim de profil 1.0 complet.
- 2048 IDs, 256 blocks, 16 members/params, SSA/local aggregate 16 words,
  profondeur aggregate8/calls32, budget min(16×words,65536). Encore trop restrictif
  pour un environnement SPIR-V général ; pas de simple relèvement arbitraire.
- Atomiques 32 bit load/store/exchange/CAS/inc/dec/add/sub/min/max/and/or/xor
  implémentées ; pas toutes les variantes individuellement testées. Pas int64,
  atomiques image, Vulkan memory model extensions ou sous-groupes.
- Matrix layouts physiques limités, IO/built-ins/interpolation/derivatives
  incomplets ; pas geometry/tessellation ou shader storage writes graphics.
- Secondary graphics, multisubpasses/input attachments/MRT, draw indirect
  graphics, formats obligatoires généraux et queries timestamp/statistics absents.
- Le runtime annonce API 1.0 mais reste incomplet ; 132/137 symboles core
  inventoriés sont présents, sans valeur de certification. L'ancienne façade
  `vex_vk` est supprimée ; EGL/GLES stubs historiques restent séparés et hors scope.
- Les handles Vulkan publics restent des objets userspace suivant valid usage
  Vulkan. Cela ne les transforme pas en capabilities kernel et ne promet pas
  une sandbox exhaustive pour shaders hostiles ; les accès mémoire supportés
  sont explicitement bornés, les features inconnues refusées.

Sources de contrat : [SPIR-V Khronos](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html),
[shader environment Vulkan](https://docs.vulkan.org/spec/latest/chapters/shaders.html),
[memory model](https://docs.vulkan.org/spec/latest/appendices/memorymodel.html).

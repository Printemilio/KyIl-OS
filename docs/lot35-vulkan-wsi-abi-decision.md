# Lot 35 — contrat graphique retenu et état d'implémentation

Décisions du 28 septembre 2026 : `VK_VEX_surface` sera une extension WSI de
plateforme connectée à une Vex Surface native partageable. Le renderer,
Vulkan/OpenGL et les shaders restent en userspace. Le kernel fournit les
ressources typées, les capabilities et les primitives de synchronisation.
SPIR-V reste un détail privé du runtime. WebKit est hors scope de ce lot.

## Migration d'en-têtes

`userspace/libvex/include/vulkan/vulkan.h` redirige désormais vers les
en-têtes Khronos officiels Vulkan-Headers v1.4.350, figés sous
`vendor/vulkan-headers/` avec leur licence. Les structures Vulkan publiques
ont donc les layouts Khronos. Le build pointe vers `vendor/vulkan-headers/include`
pour les dépendances `vk_video`.

L'ancien `vkCreateVexSurfaceKHR` et son `VkStructureType` privé `1000002000`
sont retirés. Ce numéro appartient à `VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR`.
Le nom expérimental est `vkCreateVexSurfaceVEX`, défini dans
`userspace/libvex/include/vulkan/vulkan_vex.h`. Son `sType` provisoire
`0x6f000001` n'est **pas** un identifiant d'extension Khronos attribué. Il
doit être remplacé avant la stabilisation publique, avec rupture explicite
de l'ABI expérimentale. L'extension n'est ni annoncée ni activable pour le
moment : ses points d'entrée ne sont pas exposés par `vkGet*ProcAddr`, et
créer ou interroger une surface ou un swapchain retourne une erreur, car
aucune capability Vex Surface native n'est encore exposée à ce runtime.
Les énumérations Vulkan d'extensions/layers d'instance et de device renvoient
actuellement zéro élément, sans annoncer un WSI encore absent.

## Sous-ensemble Vulkan vérifié

La création d'instance, device, buffer, mémoire et image utilise les types
Khronos. Les commandes de copie buffer, clear couleur hors écran, transition
de layout d'image, copie buffer→image et copie image→image sont maintenant enregistrées puis
rejouées en ordre lors de `vkQueueSubmit`. Les arguments de régions sont
copiés à l'enregistrement. Le test `vk_test` vérifie que les octets restent
inchangés avant la soumission puis changent après. Les records sont libérés
avec le command buffer/pool ou à la réinitialisation.

Les opérations sans exécution correcte (`vkCmdDraw*`, render pass,
blit image et autres commandes encore non implémentées) rendent la
fin du command buffer explicitement en erreur au lieu de produire un faux
rendu. Les pools de queries et descriptors, qui auparavant déclaraient un
succès sans fournir leur sémantique, sont refusés. Les fences des
soumissions CPU synchrones sont vérifiées, y compris `VK_TIMEOUT` pour une
attente immédiate non satisfaite. `vkCreateGraphicsPipelines` refuse les
pipelines tant qu'il n'y a
pas de véritable exécuteur shader. Le parseur de module SPIR-V ne valide
que son enveloppe et les limites des instructions. Le smoke Vulkan global
reste volontairement rouge ; il ne doit pas être déclaré réussi avant un
renderer programmable, la synchronisation et le WSI réels.

## Contrat restant à construire

`compositor_surface` possède encore des slots globaux dont les handles et
le teardown dépendent du PID créateur. Il faut séparer l'identité de backing
de chaque handle local, ajouter des rights/refcounts/transferts interprocess,
un mapping contrôlé et une fence de présentation. Le backend logiciel doit
utiliser ce même contrat. Le runtime Vulkan doit ensuite fournir ses objets,
descriptors, pipelines et l'exécution shader réelle, puis WSI et
OpenGL/GLES/EGL. Aucune de ces capacités n'est revendiquée aujourd'hui.

Le dernier boot VM a confirmé le smoke de transferts et les principaux
smokes GPU/isolation. Le harness global échoue intentionnellement sur
`Vulkan smoke test did not pass`. Les ajouts d'énumération d'extensions/layers
faits ensuite ont été recompilés et liés, mais pas encore réexécutés en VM.

# Lot 37 — dispatch natif et legacy séparés

## Contrat wire et audit des entrées

Une seule entrée x86-64 `SYSCALL` reste installée dans LSTAR. Le passage
per-CPU/GS vers la pile kernel, l'enregistrement des registres, la préemption,
FS_BASE et le retour d'événements restent communs et inchangés. La sélection
se fait dans `syscall_dispatch_raw_inner`, avant toute lecture d'argument.

| RAX à l'entrée | Domaine | Décodage |
|---|---|---|
| `0x5645584e00000000 \| id32` | Natif explicite | id logique sur 32 bits ; RDI/RSI/RDX restent les arguments natifs |
| Non tagué, personnalité Vex native | Legacy | WRITE=1, EXIT=2, READ=3 ; adaptateur figé pour les anciens IDs natifs 4..196 et les handlers hashés enregistrés |
| Non tagué, personnalité Linux | Linux | Numéros et registres Linux existants, dont WRITE=1 ; anciens bridges SHM 0x4001..0x4003 conservés |
| Non tagué, personnalité PE | PE | Enveloppe `VEX_PE_SYSCALL_CALL` existante |

Les notes ELF VEXOS et la détection PE/ELF par contenu continuent à déterminer
la personnalité. Aucune seconde entrée CPU, nouvelle heuristique ELF ou
inférence à partir d'un pointeur n'est ajoutée. Le sélecteur wire distingue
les appels au sein d'une même personnalité : les appels natifs explicites
sont également disponibles aux processus Linux/PE avec les mêmes validations
userspace, ownership et capabilities ; une personnalité n'est pas un privilège.

Le nombre logique **BULK reste 1**. Legacy WRITE reste 1. Le premier est
tagué, le second ne l'est pas. Les numéros natifs futurs n'étendront pas
l'adaptateur legacy. La réservation du domaine est aussi vérifiée lors de
l'enregistrement d'un hash legacy. Un numéro inconnu échoue ; un résultat
natif zéro ne déclenche jamais un deuxième dispatcher.

## Migration

`vex_syscall0..3` émettent le sélecteur ; `vex_legacy_syscall0..3` permettent
d'exprimer intentionnellement l'ancien wire. Les appels kernel de confiance
`vex_syscall_dispatch` restent internes : ils ne passent pas par la sélection
wire ni les wrappers userspace. Les appels explicites venant de ring3 passent
tous dans `vex_syscall_dispatch_user`.

Les wrappers partagés migrent les applications natives et le runtime Vulkan.
Le header SHM hébergé utilise maintenant les vrais IDs natifs 161..163 ; le
launcher Linux est réellement recompilé et intégré au staging ISO. Ses anciens
binaires peuvent toujours utiliser les trois bridges Linux historiques. Les
autres binaires Linux et PE gardent leur convention existante.

## Défauts mémoire devenus mesurables

La collision empêchait BULK de lire le PMM et annonçait un heap Vulkan de taille
zéro. Après correction, le premier contrôle de 128 événements révèle 9 pages
de croissance. Trois séries diagnostiques prouvent +256 versions propriétaires
de la racine d'inventaire par série, avec +8/+10/+8 pages.

`store_root_copy` publiait chaque nouvel inventaire sans libérer la version
précédente. Les clés privées de racine détiennent maintenant leur inventaire
courant : publication avant libération de l'ancien propriétaire, OOM préservant
l'ancienne racine. Les snapshots gardent leurs clés de namespace distinctes ;
les borrows MRAC conservent leur référence ; les historiques des clés MRAC
applicatives ne changent pas.

Après cette correction, trois séries gardent exactement une version mais
croissent encore de 3 pages chacune. `release_chunk_backing` libérait les pages
physiques sans recycler leurs descripteurs/chunk dans l'arène de métadonnées
permanente. Deux freelists typées réutilisent maintenant ces descripteurs après
retrait de toutes les références hash et libération du backing, y compris le
rollback de construction. Les pools de métadonnées gardent leur high-water
mark ; ce test ne prouve pas une restitution au PMM de chaque octet de cache.

La validation après ces deux corrections donne une version de racine constante
et seulement **1 page de delta total après 384 cycles**. Le seuil de 8 pages
n'a pas été relevé. Le heap Vulkan doit être exactement `total_pages * 4096`.

L'exécution de 17 régressions avant Vulkan révèle ensuite un troisième défaut :
un backing de 100 KiB échoue malgré 892 042 pages PMM libres. La fenêtre VA
kernel des chunks MRAC (768 MiB) avançait à chaque création sans réutiliser
les extents libérés. Les descripteurs recyclés conservent maintenant leur
extent virtuel non mappé ; une allocation de même taille le réutilise avec
de nouvelles pages physiques, remappées et remises à zéro. Les références
hash/borrows doivent être retirées avant restitution de l'extent. Le rollback
partiel libère uniquement les pages effectivement matérialisées.

Ce cache d'extents est exact-size : il ne split/coalesce pas des tailles
différentes. La fragmentation de cette fenêtre bornée demeure une limite ;
aucune promesse d'allocation illimitée n'est faite.

## Tests

- `vk_syscall_domain_test.h` : instructions raw indépendantes des nouveaux
  wrappers ; coexistence des deux IDs 1, même pointeur passé dans les deux
  domaines sans deviner son type, READ capacity=1, EXIT dans un enfant,
  ancien GETPID, appel inconnu, domaine invalide, pointeurs invalides/kernel
  et sortie BULK kernel interdite.
- `linux_native_domain_smoke.S` : ELF sans note native, vrais appels natifs
  BULK/compteur avec deux arguments, et Linux WRITE=1 dans le même processus ;
  ancien bridge SHM reconnu et handle invalide refusé.
- `security_boundary_test` : SYSCALL avec RSP invalide dans les deux domaines.
- `vk_allocation_accounting_smoke` : 3 × 128 événements, PMM réel,
  nombre de versions d'inventaire stable et heap Vulkan égal au PMM.
- `vk_native_heap_extent_smoke` : 200 allocations/libérations de 4 MiB,
  soit 800 MiB cumulés avec seulement 4 MiB vivants ; contrôle du zeroing.
- 17 fixtures séquentielles : frontières user/kernel, faults/events,
  TLS/FPU, futex, ownership, file mmap, syscalls refusés, pipes/exec,
  SEQPACKET/attachments, W^X, ELF/TLS dynamique et véritable glibc.
  Les fixtures historiques static/static-PIE retournent volontairement 7/9 ;
  la faute non gérée doit retourner 139. Le harness vérifie ces contrats.

## Fichiers concernés

- `kernel/include/unos/syscall_wire.h`, `kernel/src/syscall.c`,
  `kernel/src/vex_syscall.c` : sélection explicite et refus des inconnus.
- `userspace/libvex/include/vex_syscall.h`, `vex_shm_abi.h`,
  `scripts/build_and_stage_webkit.sh` : migration des wrappers et du main Linux.
- `kernel/src/process_memory.c`, `kernel/src/mrac.c` : publication/libération
  des inventaires privés et recyclage des métadonnées/extents libérés.
- `userspace/apps/vk_syscall_domain_test.h`, `vk_test.c`,
  `vk_api_contract_test.h`, `security_boundary_test.c`,
  `userspace/linux_tests/linux_native_domain_smoke.S`,
  `scripts/build-linux-tests.ps1` : tests wire, sécurité et accounting.
- `userspace/libvex/src/vulkan.c`, `userspace/apps/vk_msaa_test.h` : reprise
  Vulkan (`FragCoord`, énumération avec capacité zéro, oracles de pixels).

Les résultats VM définitifs et la matrice Vulkan actualisée sont consignés dans
`lot37-wide-coverage.md`. Le marqueur final Vulkan incomplet reste volontairement
en échec : cette migration ne clôture pas Lot 37.

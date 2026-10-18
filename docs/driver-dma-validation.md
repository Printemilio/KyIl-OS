# Préparation DMA — résultats du 10 octobre 2026

Périmètre validé : stockage et médiation DMA du moteur, pas un pilote réseau DMA
activé. Aucun TX/RX E1000 via archive ni benchmark de débit revendiqué.

| Validation | Résultat | Preuve |
|---|---|---|
| Parser/VM 1.0, RTC BCD/binary/12h/24h, mutations/troncatures, OOM, budgets | PASS | `build/driver-tests/dma-host.log` |
| Broker, exclusivité et bornes, 8 threads / 40000 tentatives | PASS | même log |
| ORA + moteur DMA de production sous ASan/UBSan, 8 contextes × 100 cycles | PASS | même log |
| Descripteurs typés, masques MMIO, références stale/étrangères, longueurs/overflow, OOM/rollback | PASS | `tests/driver_dma_test.c`, même log |
| Manifeste/grants hôte : taille réduite, grant non demandé, MMIO read-only, publication et champs protégés | PASS | même source, même log |
| Référence ORA retenue après fermeture du token ; destruction contexte refusée jusqu'au dernier release | PASS | même source, même log |
| Publication partiellement échouée, quiescence refusée, longueur RX corrompue | PASS, périphérique **simulé de test** | même source, même log |
| Opérations DMA refusées en entrée IRQ | PASS | `archive_test`, même log |
| Régressions ORA storage/versioned/named : collisions, mutable independence, lifetime, OOM, concurrency | PASS | `scripts/test-ora-storage.ps1` exécuté |
| Build kernel complet puis recompilation/link des unités modifiées finales | PASS | `build/driver-dma-build-final.log`, kernel final ci-dessous |
| VirtualBox PIIX3 sans IOMMU, 1 vCPU, PMM/VMM/ORA + archive externe 1.1 | PASS | `build/driver-tests/dma-cpu1/serial.log`, `screen.png` |
| Même kernel, 4 vCPU | PASS | `build/driver-tests/dma-cpu4/serial.log`, `screen.png` |
| Chargement de l'archive mémoire comme périphérique | **DENIED attendu**, 1/4 vCPU | logs série, avant dmatest |
| RTC réel + UART externe TX/RX, IRQ, wait timeout, conflit, stale ID, unload/reload, kill waiter, variante baudrate | PASS, 1/4 vCPU | mêmes logs série |

Extraits finaux :

```
driver-result: denied id=0
driver-dma-test: PASS real PMM/VMM/ORA storage + external bytecode; no hardware DMA claimed
driver-test: PASS clock/UART/IRQ/timeout/conflict/stale/unload/reload
```

Le test mémoire kernel vérifie un buffer 4096 octets et un ring 32 octets
(8192 octets physiques engagés), la protection des champs adresse, la rétention
du buffer après fermeture du token créateur, puis le retour du compteur propre
au contexte à zéro après destruction de la dernière référence. Il ne confond
pas ce compteur avec le PMM global, que d'autres tâches peuvent modifier.

L'archive externe `dma-memory-test.driver` fait ALLOC/WRITE/READ/RELEASE dans
le vrai interpréteur avec le vrai fournisseur PMM/VMM/ORA. Elle n'est pas
autoloadée ; `driver dmatest` constitue le banc mémoire administratif. Les
callbacks matériels du test host sont explicitement des fixtures ; ceux du
fournisseur mémoire kernel ne prétendent fournir aucun périphérique.

Configuration finale restaurée : VM éteinte, PIIX3, 4 vCPU, `iommu=none`,
carte NAT 82540EM, UART2 désactivée. Aucun `KERNEL PANIC` dans les deux logs
finaux. Le boot conserve le backend réseau natif e1000 ; DNS/HTTPS et les
applications graphiques n'ont pas fait l'objet d'une nouvelle campagne ici.

SHA-256 des artefacts effectivement testés :

- Kernel et copie stagée `build/iso_root/boot/vex-os` :
  `8a6f60540a584266f837cd526ea8531a033c9d139461ff5f8207ba1dca0e49a2`.
- ISO `build/vex-os.iso` :
  `782a2bb94fe3a573eafcbc6ffd256e0482adde5070e34edbb58e4f470c35a43d`.
- Archive `drivers/dma-memory-test.driver` :
  `897476e7880ed4f5b4b83f70b35515b461413acee7ee629d5f49b1a16735ffb8`.

Fichiers ajoutés : `driver_dma.h`, `driver_dma.c`, `driver_dma_memory.c`,
`tests/driver_dma_test.c`, `drivers/dma-memory-test.driver.json`, documentation.
Étendus : `driver_vm.h/.c`, `driver_manager.c`, packer, suites host/VM et
documentation du format. Archives/trust catalogue générés et ISO reconstruite.
`ora.c`, PMM/VMM et le pilote natif E1000 n'ont pas été modifiés dans cette passe.

Limites et prochain travail : voir [le contrat DMA](driver-dma.md), notamment
la gestion des slots d'un ring actif, les grants BAR/PCI, la preuve matérielle
de completion/stop, IRQ/worker réseau et le handoff exclusif du fallback.

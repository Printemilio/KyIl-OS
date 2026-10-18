# `.driver` : audit et intégration KyIlOS

Audit initial : `disk.c` (AHCI/NVMe/ATAPI), `netdev.c` (e1000/virtio), `gpu.c`
dupliquent CF8/CFC et leurs fenêtres MMIO. DMA utilise PMM/HHDM et des buffers
propres à chaque pilote ; aucune infrastructure IOMMU trouvée. Aucun host USB
UHCI/EHCI/xHCI ou arbre de périphériques USB dans le code actuel. `irq.c` avait
un handler C par vecteur, sans réservation dynamique. `rtc.c` utilise déjà
une transaction CMOS SMP ; `sched_clock.c` lit CMOS seulement à la calibration
du boot. ELF/PE sont des loaders d'applications, pas un chargeur de modules
kernel à recycler en pilote.

Réutilisé : PMM/VMM/ORA et leases `fs_read_view`, SHA-256 Mbed TLS, gates
par sous-système, compteur d'événements et waiters/lifetime scheduler, IRQ/PIC,
interfaces publiques RTC et découverte PCI. L'interpréteur n'appelle aucun
pilote natif spécifique. Le renderer graphique reste en userspace.

Ordre d'intégration :

1. Format archive compressé/versionné, parser strict, VM registres/scratch,
   budget instructions et deadline. Tests host sur le vrai code.
2. Leases matérielles exclusives, CF8/CFC partagé ; pilotes existants gardent
   leurs BDF/MMIO. IRQ dynamique exclusivement attribuée, masked puis armed.
3. Manager local, pin digest séparé de l'intégrité ; aucune découverte de fichier
   ne confère d'autorité. Installation/activation administrative distinctes.
4. Handoff du CMOS depuis le fallback early-boot, sous son gate existant.
   Lire/conversion/conditions dans l'archive, pas un delegate vers `rtc.c`.
5. UART émulée COM2 séparée de COM1 de debug ; vrais registres, boucle locale
   matérielle, IRQ, RX/wait scheduler, hot unload/reload et variante de baudrate.
6. Boot VM, réseau/desktop/applications et adversaires ; ISO finale/documentation.

Limites assumées de la première plateforme : deux contrats de classe CLOCK et
CHARACTER. Autorités PIO/indexed-PIO approuvées pour CMOS/UART2. PCI est découvert
et les propriétaires natifs sont suivis, mais aucun BAR n'est approuvé pour
une archive. MMIO, DMA et USB sont explicitement refusés dans v1, pas simulés.
La première analyse envisageait un domaine IOMMU pour DMA. La décision suivante
retient une médiation logicielle sans IOMMU obligatoire : ORA_STORAGE mutables,
références locales, schémas de descripteurs et registres contrôlés par le moteur,
quiescence prouvée par l'adaptateur. Aucun accès à une adresse PMM arbitraire.
Cette protection vise les archives, pas du matériel hostile. Voir
[la préparation DMA et ses limites](driver-dma.md). Aucune feature matérielle
non réalisée n'est annoncée pour faire accepter une archive.

Le catalogue bootstrap est un module du medium de boot de confiance. Les pins
installés sont dans un fichier root 0600, modifiable uniquement par l'administration
du système de fichiers ; FS_ADMIN/root appartient donc au périmètre de confiance,
comme pour les autres fichiers de boot. Ce modèle n'est pas une signature de
fournisseur ni Secure Boot. Un dépôt distant devra fournir une origine/pin ou
signature approuvé explicitement, avant installation. Aucun accès réseau au boot
ou téléchargement automatique ajouté.

# Dell Latitude D630 : portage matériel en cours

Le portage complet n'est **pas validé**. Les preuves VirtualBox ci-dessous ne
constituent pas une exécution sur le Dell. Aucun disque physique, notamment
ArchVista, n'a été ouvert ni modifié par les outils de cette passe.

## Dépendances et architecture

BIOS → Limine x86-64 → PMM/VMM/ORA → scheduler/per-CPU → entropie → stockage/FS
→ import de l'archive → Driver Manager → réseau → compositor → applications.
Le noyau, ORA et les services existants restent uniques ; aucun kernel D630
parallèle. CPU, mémoire et transports sont des fournisseurs du même système.

1. Bootstrap sûr : instructions CPU conditionnelles et RNG réellement seedé.
2. Stockage sûr : reconnaître le matériel sans formater un volume inconnu.
3. Disponibilité du système après le passage de relais BIOS : archive autonome.
4. UHCI/EHCI + mass-storage : transports pour les autres supports physiques.
5. BCM5755M : autorité matérielle, DMA médié, protocole externe, IRQ, réseau.
6. Validation sur le Dell : entrées, affichage, horloges, SMP, stockage et Ethernet.
7. Extensions séparées : Wi-Fi BCM4311, audio complet, énergie, accélération GMA.

## Implémenté

### RNG commun et générations de CPU

`random_provider.c` cache CPUID par CPU, au BSP et sur chaque AP. RDSEED est
préféré, puis RDRAND ; les instructions ne sont exécutées que si ce CPU les
annonce. Les échecs matériels sont bornés. Un CPU sans ces instructions utilise
**jitterentropy upstream**, avec collecte de variations de temps d'exécution,
conditionnement SHA3, tests de démarrage et tests continus RCT/APT. Ce n'est ni
une valeur RDTSC utilisée comme seed, ni une garantie physique déjà établie pour
le T7250 : la santé de ce fournisseur doit réussir sur chaque environnement.

Les sources alimentent le CTR-DRBG AES-256 Mbed TLS existant. L'état est protégé
par une transaction SMP ; le DRBG reseed selon sa politique existante. Les
capabilities, AT_RANDOM, `/dev/urandom`, getrandom et le callback TLS utilisent
ce même service. Une erreur de source est propagée et aucune sortie prévisible
n'est substituée. Les générations de capabilities font leur rollback avant
publication. Le boot s'arrête explicitement si aucun fournisseur sain n'existe.

Source épinglée : `smuellerDD/jitterentropy-library`, commit
`2aa10e2423fc7d34d67ef666d8eb186d236272d8` ; licence upstream conservée.
Le code de collecte est compilé **-O0 -fwrapv**. Le port privé fournit allocation
kernel résidente, effacement sécurisé, CPUID, RDTSC/PAUSE et constantes errno.
Le patch de header exclut les fichiers POSIX inutiles dans le build bare-metal.
Pas de bypass des tests de santé, pas de timer interne simulé.

L'étude `.driver` conclut que le RNG bootstrap doit être disponible **avant** le
FS/Driver Manager. Son code de confiance reste donc natif ; aucune archive ne
reçoit un droit CPU/MSR arbitraire. Ajouter une politique de sélection externe
ultérieure ne doit pas créer une dépendance circulaire pour générer les handles
qui servent à charger cette politique.

Audit instructions : le kernel est compilé `-march=x86-64` ; XSAVE/AVX sont
conditionnels et FXSAVE reste utilisé sans eux ; x2APIC est optionnel et le
chemin xAPIC MMIO existe. Le test AVX SMP vérifie OSXSAVE/XCR0. AES-NI Mbed TLS
est choisi via détection de capacités. La vérification des binaires utilisateurs
et des horloges sur le T7250 physique reste nécessaire.

### ATA/IDE

`ata_pio.c` est un protocole ATA général avec callbacks de transport. Le provider
PCI de `disk.c` reconnaît la classe IDE, canaux legacy ou BAR I/O natifs,
réserve les ressources et active l'I/O sans activer le bus mastering pour PIO.
IDENTIFY, LBA28/LBA48, secteur logique 512 octets, lectures/écritures multi-secteurs,
flush, erreurs/absence et timeout borné sont réels. Géométrie non prise en charge
refusée. Pas de DMA PIO déguisé, ni de protocole E1000 réutilisé pour ATA.
Le provider PIO désactive le bus mastering ; une sonde sans disque rend le lease
PCI et restaure le registre command original. ATA et ATAPI sont sérialisés pour éviter les sélections concurrentes d'un même
canal ; les chemins AHCI/NVMe restent disponibles.

VexFS ne formate **plus** un disque inconnu, illisible ou invalide au démarrage.
Après un mount échoué, les entrées RAM partielles sont écartées. Un disque GPT,
MBR, NTFS ou Linux ne devient pas automatiquement un volume VexFS. Il n'y a pas
encore de lecteur de ces FS/partitions : détecter un disque ATA ne signifie pas
pouvoir ouvrir les fichiers ArchVista.

`diskselftest` est un outil explicite pour un **disque jetable tagué**, réservé
à SYSTEM_ADMIN. Il refuse les autres supports, sauvegarde/restaure les secteurs
testés et vérifie les bornes. Ne pas l'utiliser sur un support de travail.

### Archive de boot ORA

Le premier essai à 959 modules échouait dans **Limine** : `Memory map exhausted`.
La solution est un module `system.boot`, pas une augmentation arbitraire d'une
limite du bootloader. Format v1 : header 64 octets `KYILBOOT`, version, nombre,
taille totale, SHA256 de table+payload, champ réservé nul ; table de records
272 octets (chemin absolu NUL-terminé 256, offset/longueur u64 LE). Les données
commencent sur des pages 4 KiB ; les fins de fichier sont complétées de zéro.
Chemins stricts triés/uniques, traversées interdites, tailles/offsets bornés,
SHA256 vérifié **avant** publication. Les aliases immutables peuvent référencer
exactement les mêmes octets.

Une seule ingestion ORA_CONTENT/UNIQUE, puis références/vues explicites par
fichier. `fs_read_view` expose la longueur et l'offset du fichier, jamais ceux de
l'archive entière. Les overlays VexFS continuent à prendre priorité ; le rebind
d'un fichier indépendant ne modifie pas le contenu immutable du boot. Les pages
Limine de la source sont rendues au PMM après ingestion réussie. Le rollback OOM
avant publication libère les métadonnées et laisse la source valide.

L'image de cette passe réserve environ **603,39 MiB** de payload ; l'ingestion
copiée produit temporairement une deuxième copie. C'est compatible avec le boot
VM 2 Gio testé, mais la marge exacte 1,9 Gio et une grosse navigation WebKit
doivent être mesurées sur le Dell. Ce profil garde tous les fichiers/applications.
Il n'implémente **pas** USB : le BIOS charge le module, ensuite les fichiers sont
résidents. Les supports USB ajoutés après boot restent non pris en charge.

## Ce qui reste à implémenter / valider

| Composant | État après cette passe |
|---|---|
| BIOS / Core 2 / SMP | Boot VirtualBox, pas encore Dell physique |
| ATA IDE | Protocole et tests réels sur PIIX émulé ; ICH8M 2828 physique à tester |
| Applications / ressources | Archive autonome et vues ORA, sans backend CD-ROM |
| GMA X3100 | Framebuffer BIOS 32 bits logiciel ; pas de driver accéléré GMA |
| Clavier/touchpad | PS/2 existant, souris/clavier VM ; touchpad Dell à tester |
| USB UHCI/EHCI/mass-storage | **Absent**, archive boot ne remplace pas ce chantier |
| BCM5755M Ethernet | **Absent**, aucun `bcm5755.driver` annoncé |
| BCM4311 Wi-Fi | **Absent**, firmware/PHY/802.11 distincts d'Ethernet |
| Intel HDA | Chemin natif de registres existant, pas une preuve de lecture audio |
| Énergie / horloges | RTC/PIT/LAPIC existants ; TSC, C-states/SpeedStep/suspend Dell à vérifier |

BCM5755M n'est pas un E1000 avec un ID différent : RX producer/return rings,
host coalescing/status DMA, mailboxes, MDIO/PHY, NVRAM et reset sont différents.
La politique `.driver` actuelle n'accorde que le contrat E1000 8086:100e ; le
bus DMA médié existant doit être étendu aux objets/champs/completions réellement
demandés par Tigon3 avant un paquet autonome. Ressources/IRQ propres au Dell,
quiescence avant destruction, pas d'adresse bus fournie par l'archive. Références
techniques : [tg3.c](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/tg3.c),
[tg3.h](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/tg3.h).
Ne pas copier ce pilote Linux en faisant croire à une description interprétée.

UHCI/EHCI nécessitent aussi leurs propres queues/descripteurs DMA médiés,
handoff BIOS, reset/ports, enumeration/control transfers, companion routing,
bulk-only/SCSI, erreurs et quiescence. Pas de MMIO/DMA générique non borné accordé
à un fichier. Le BCM4311 relève séparément de radio/firmware/802.11 ; source
[b43](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/broadcom/b43).

## Reconstruction et essai physique prudent

1. Compiler : `scripts/build.ps1 -NoIso` (ou build complet avec `-PortableBoot`).
2. Avec un staging courant : `scripts/make-portable-iso.ps1` génère
   `build/kyilos-legacy.iso`, séparément de `build/vex-os.iso`.
3. Vérifier SHA256 dans `build/portable-iso-provenance.json`.
4. Écrire **manuellement** cette image sur une clé identifiée et sacrifiable,
   en mode image disque. Cette opération détruit la clé ; aucun script de cette
   passe ne choisit ni écrit un périphérique physique.
5. Choisir la clé dans le menu F12 BIOS du Dell, garder SATA en IDE pour ce test.
   Si disponible, capturer COM1 115200 ; sinon photographier la dernière étape.
6. Ne pas lancer d'installation ni préparer/formater le disque ArchVista.
   Aucun formatage automatique VexFS. Le disque peut être déconnecté pour isoler
   complètement le premier test. Retirer la clé restaure le chemin de boot habituel.
7. Collecter le résultat CPU/RNG/SMP/framebuffer, clavier/touchpad et `diskinfo`.
   L'absence de réseau Broadcom à cette étape est **attendue**, pas un succès.

Les preuves et résultats précis sont dans `docs/d630-validation.md` et les logs
`build/d630-tests/`. Le portage reste ouvert jusqu'aux critères matériels complets.

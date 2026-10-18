# KyIlOS `.driver` v1 — format, autorité et VM

Ce format transporte des données et du bytecode de protocole, **jamais du code
CPU natif**, un ELF renommé ou un identifiant d'appel vers un pilote C. Il est
interprété par `driver_vm.c`. Toutes les valeurs wire sont little endian.
La description ci-dessous couvre la version 1.0 historique, préservée.
La version 1.1 ajoute les références DMA et le médiateur de registres, sans
accorder de BAR/PCI aux archives par simple déclaration : voir
[le contrat DMA](driver-dma.md). Versions/codec/features inconnus refusés.

## Archive

Header fixe de 80 octets (`drv_archive_header_t`) :

| Offset | Taille | Champ |
|---:|---:|---|
| 0 | 8 | ASCII `KYDRIVER` |
| 8 | 2 | major = 1 |
| 10 | 2 | minor = 0 |
| 12 | 4 | header_size = 80 |
| 16 | 4 | codec = 1 (RLE littéraux/runs bornés) |
| 20 | 4 | compressed_size, exactement le reste du fichier |
| 24 | 4 | payload_size, 8..65536 |
| 28 | 4 | flags = 0 |
| 32 | 32 | SHA-256 du payload décompressé |
| 64 | 16 | réservés, zéro |

RLE : un token octet décrit `(token & 127)+1` octets. Bit 7 = 0 : littéral
suivant de cette longueur ; bit 7 = 1 : un octet suivant répété. Entrée/sortie
doivent se terminer exactement aux longueurs déclarées ; aucun dépassement,
trailing data ou allocation non bornée. Fichier entier limité à 65536 octets.

Payload : `u32 member_count` (1..8), `u32 reserved=0`, puis member_count
records de 32 octets : nom ASCII NUL-padded 16, offset u32, size u32, type u32,
flags u32=0. Les membres sont contigus, sans chevauchement/trou/trailing bytes.
Noms uniques, pas de séparateurs de chemin, extraction vers le FS inexistante.
Membres requis `manifest` type 1 et `program` type 2 ; type 3 metadata opaque.
Les outils produisent un membre `info` JSON pour documentation/origine.

Le SHA-256 interne détecte la corruption : **il ne confère pas la confiance**.
Un pin SHA-256 du fichier ENTIER provenant du catalogue/admin est vérifié avant
le parser. Les deux digests couvrent des objets différents.

## Manifest et compatibilité

272 octets, layout exact déclaré par `drv_manifest_t` et static assertions :

| Champ | Représentation |
|---|---|
| name, platform | chacun 32 octets ASCII NUL-padded |
| bus, device_class | u32 : platform=1/PCI=2 ; CLOCK=1/CHARACTER=2 |
| vendor, device, pci_class | u32 ; zéro pour platform |
| features, engine_min, priority | u32 ; engine_min=1 |
| entries | 8 u32 PC absolus ; absent = 0xffffffff |
| code_count, resource_count, scratch_size, irq | u32 |
| resources | 4 records de 32 octets ; non utilisés intégralement zéro |

Entrées : probe, init, stop, read, write, irq, control, diagnostic. Probe/init/
stop obligatoires, read pour CLOCK, irq si route IRQ requise. Code maximum
2048 instructions, scratch maximum 4096 octets, jusqu'à 4 ressources.

Resource record : kind u32, selector u32, offset u64, length u32, rights u32,
widths u32, reserved u32=0. Rights READ=1/WRITE=2 ; widths mask 1/2/4/8.
Kinds PIO=1, MMIO=2, indexed-PIO=3, DMA=4. **Les nombres sont des demandes** :
le manager les intersecte avec la politique et les leases du périphérique.
Pas d'adresse physique ou de port absolu contrôlé par la description.

Features v1 : BASE=1, PIO=2, INDEXED=8, BUFFER=16, IRQ=32. MMIO=4/DMA=64
réservés et refusés actuellement. La plateforme v1 accorde seulement PIO
8-bit et indexed-PIO read-only. PCI est découvert mais sans BAR accordé ;
aucune prétention de support USB/DMA/MSI ni de GPU matériels.

## Instructions

Record 16 octets : `op:u8 dst:u8 a:u8 b:u8 aux:u32 immediate:u64`.
32 registres u64, zéro à chaque entrée. 0..7 inputs, 8..15 outputs, 16..31
temporaires. Scratch privé de l'instance, persistant, sérialisé par son gate.

| IDs | Sémantique |
|---|---|
| 0 RET | status i32 dans immediate ; copie des outputs |
| 1 CONST, 2 MOV | dst=imm ; dst=reg[a] |
| 3..12 | ADD/SUB/MUL/DIV/MOD/AND/OR/XOR/SHL/SHR, dst=op(reg[a],reg[b]) |
| 13..15 | JMP/JZ/JNZ vers PC immediate (condition reg[a]) |
| 16 EQ, 17 LT | comparaison u64 vers dst, 0/1 |
| 18 IO_READ | dst <- resource aux, byte width b, offset immediate |
| 19 IO_WRITE | resource aux <- reg[a], width b, offset immediate |
| 20 LOAD | dst <- scratch à adresse reg[a], width b |
| 21 STORE | scratch à adresse reg[a] <- reg[dst], width b |
| 22 EMIT | byte reg[a] vers RX CHARACTER/event counter |

Arithmétique entière unsigned ; overflow modulo 2^64 défini. Division zéro,
shift >=64, registres/PC/ressources hors borne sont des erreurs. Accès scratch
par copie, sans pointeurs kernel fournis au programme. Capacités/types/rights
vérifiés au load et à l'opération ; pas de syscall, heap allocation ou native
function call dans l'ISA. Aucune relocation/chargement ELF.

Exécution synchrone par instance : 20000 instructions/1 ms, IRQ 256/200 µs,
première limite atteinte = timeout. Pas de pause/sleep dans les transactions.
Les clients attendent les données via le compteur natif/scheduler hors pin/gate.
Protection contre IRQ storm : 4096 IRQ ou 20 ms de travail par fenêtre 100 ms,
puis mask/quarantaine. Ces limites sont politique host, pas décidées par l'archive.

## Autorité actuelle

- `rtc.cmos` : banque indexée 0x70/0x71, sélection NMI identique au fallback,
  lectures calendrier/status 0,2,4,7,8,9,10,11,13 uniquement ; aucune écriture
  date/CMOS ni modification NMI demandable. Handoff sous le gate RTC existant.
- `uart.com2` : ports 0x2f8..0x2ff, IRQ3 exclusive ; LSR/MSR read-only, masques
  de contrôle limités, mode DMA UART interdit. COM1 de debug reste réservé.
- Les contrôleurs système/PIO et BDF/MMIO natifs sont déjà réservés. Une archive
  ne remplace pas un pilote actif en changeant un vendor ID ou un nom.
- Stop/fault conserve l'autorité jusqu'au quiesce ; pas de retour au fallback
  RTC pendant qu'une instance est loading/stopping/faulted. Erreur de stop garde
  une instance faulted réservée. Un unload réussi masque/détache IRQ, attend
  les pins bornés, ferme le counter (wake des waiters), libère leases/payload.
- IDs d'instance kernel monotones non réutilisés. Ils ne sont pas des capabilities
  userspace ; aucune ABI raw-hardware utilisateur ajoutée. Les commandes de
  mutation exigent SYSTEM_ADMIN, install exige aussi FS_ADMIN.

## Trust/install/manager

Catalogue `KYTRUST1` : 16 octets (magic 8, count u32, reserved u32), jusqu'à 24
records 256 octets : path NUL-padded 192, archive SHA-256 32, flags u32
(autoload=1), reserved 28 zéro. Publication en mémoire transactionnelle.

Bootstrap `/drivers/boot.trust` doit être un module du medium de boot. Catalogue
installé `/home/root/packages/driver-trust` : propriétaire root, sans écriture
group/world ; install crée 0600. Root/FS_ADMIN et le medium de boot appartiennent
à la base de confiance, comme pour les autres fichiers exécutables/boot. Ce n'est
ni Secure Boot ni une signature fournisseur ; aucune fausse authenticité annoncée.

Installation : SHA fourni explicitement, validation intégrale, politique host,
archive copiée sous name+préfixe SHA (collision départagée par SHA complet),
pin persisté via les primitives VexFS existantes. Pas d'exécution implicite.
Sans disque VexFS attaché, le backend RAM est naturellement volatile. Les pins
installés ont autoload=0 ; chargement à chaud explicite. Auto boot sélectionne
les descriptions compatibles autorisées par priorité. Découverte locale montre
aussi les fichiers non autorisés ; extension `.driver` ne vaut pas permission.

Commandes : `driver list`, `devices`, `time`, `install PATH SHA256`, `load PATH`,
`unload ID`, `send ID TEXT`, `read ID`, `selftest`. Fichier chargé deep-copié et
immutable : modifier sa source après load ne modifie pas l'instance active.

Sources indépendantes dans `drivers/*.driver.json` ; compiler générique
`python scripts/pack-driver.py SOURCE OUTPUT`, ou `build-driver-packages.py`.
Modifier une description puis repacker l'ISO ne compile ni kernel ni engine.
Un dépôt distant pourra fournir archives/pins/signatures aux mêmes points
de validation. Aucun client de dépôt, réseau au boot ou téléchargement silencieux.

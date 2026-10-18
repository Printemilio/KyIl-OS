# Amorçage USB-HDD BIOS et Rufus

## Cause de la détection UEFI seulement

L'ISO contient El Torito BIOS + UEFI, un MBR actif et les deux stages Limine BIOS.
`bios-install` installe le chemin disque en plus du chemin CD-ROM. Toutefois,
Rufus en **mode ISO/extraction** reconnaît certaines familles de loaders BIOS
(GRUB, Syslinux, Windows, etc.), pas Limine. Sa macro `IS_BIOS_BOOTABLE` ne teste
pas simplement l'existence d'une entrée El Torito ou d'un MBR exécutable :
[source officielle Rufus](https://github.com/pbatard/rufus/blob/master/src/rufus.h).
L'extraction/reformatage UEFI détruirait le stage MBR Limine. Ce n'est donc pas
une méthode valide pour préparer cette clé Legacy.

Le générateur conserve l'ISO hybride et produit aussi **un vrai disque FAT32**,
`build/kyilos-legacy-usb.img`. Ce n'est pas une ISO simplement renommée.

## Image disque

- Taille 738 197 504 octets (704 MiB).
- MBR exécutable Limine, signature 55AA ; stage 2 embarqué dans le gap après MBR.
- Une partition active, type EF (ESP), au LBA 2048.
- FAT32, hidden sectors 2048 et taille BPB égale à la taille de partition.
- `limine-bios.sys`, config, kernel, `system.boot` et `EFI/BOOT/BOOTX64.EFI` /
  `BOOTIA32.EFI`. Le payload applicatif est déjà dans l'archive ; inutile de le
  dupliquer sur le volume FAT32.
- Même kernel et même contenu ORA que l'ISO. Aucun changement kernel pour Rufus.

## Préparer la clé

Dans Rufus, sélectionner **`kyilos-legacy-usb.img`**, image disque, et laisser
Rufus l'écrire en brut/DD. Il n'a pas à installer ou reconstruire Limine.
Ne pas utiliser l'extraction ISO UEFI pour le Dell. Une autre écriture
octet-pour-octet de l'image est également valide. L'écriture efface la clé
choisie ; les scripts de ce dépôt ne sélectionnent ni n'écrivent aucun disque
physique et n'accèdent pas à ArchVista.

Le mode BIOS du Dell utilisera le MBR ; un firmware UEFI pourra découvrir les
loaders de la partition FAT32 ESP. Les fichiers et le chemin UEFI de l'ISO
restent présents. Voir `build/portable-iso-provenance.json` pour les hashes.

## Validation réelle de l'amorçage

`scripts/test-usb-hdd-boot.py` utilise SeaBIOS et un contrôleur **USB EHCI** avec
un vrai périphérique QEMU USB mass-storage, blocksize 512, bootindex=1. Aucun
média CD/IDE/SATA de démarrage n'est fourni. `query-block` et `info usb` sont
enregistrés pour prouver le bus ; les captures montrent le bureau après boot.

- Image FAT32/MBR via USB EHCI/SeaBIOS : **PASS**, bureau, 68 s dans ce run.
- ISO hybride lue comme disque USB brut, USB EHCI/SeaBIOS : **PASS**, bureau,
  69,9 s. Donc sa structure de boot disque fonctionnait déjà ; Rufus extraction
  était le mauvais chemin pour conserver ce loader.
- Image disque VirtualBox BIOS, CD retiré : **PASS**, bureau. C'est une preuve
  disque complémentaire ; elle ne remplace pas le test USB réel émulé ci-dessus.
- Image disque VirtualBox UEFI, CD retiré : **PASS**, bureau. Captures et traces
  `build/usb-hdd-vbox-bios.png/.log` et `build/usb-hdd-vbox-uefi.png/.log`.

Hashes de cette reconstruction :

- USB `.img` : `105B74DA9C67417586A5AE808AE01B7B9AACB6F56424C53959AEE8223325D5E0`.
- ISO : `F54E7E5F0747576458EDA35DA51F524CBFA47D3D5F9F2E93804B38F76186D0ED`.
- Kernel inchangé : `19A500841EA9A219200BF216B186D017B25AB7704E414524AA18A9EB91AEB897`.

Traces/captures : `build/usb-boot-tests/*/result.json`, `block.json`, `usb.txt`,
`serial.log`, `desktop.ppm` / `desktop.png`. Layout vérifié dans
`build/usb-image-layout.json`.

Le modèle QEMU utilisé est `pc-i440fx-2.0` (Legacy), 2 Gio, 2 CPU, physical bits
36, LA57 désactivé. Le modèle PC récent réserve une fenêtre à 1 Tio : le VMM
actuel mappe jusqu'au plus haut PFN réservé et se bloque alors dans son init.
Cette limitation distincte du kernel est documentée, pas corrigée ici.

OVMF USB : le firmware trouve et lance BOOTX64.EFI, mais le run n'a pas atteint
le kernel avant la deadline. Ne pas annoncer une validation complète de cette
combinaison OVMF/EHCI. Le test UEFI disque VirtualBox est relevé séparément.

Ces essais ne constituent pas un test sur le D630 physique. Ils ne fournissent
pas de driver USB au kernel : le firmware charge le système résident en RAM.

## Reproduction

`scripts/make-portable-iso.ps1` génère ISO + image USB par défaut ; `-IsoOnly`
conserve le comportement ISO seul. Le disque est construit avec mtools sous WSL
via `scripts/make-usb-disk-image.sh`, puis le même Limine épinglé installe le MBR.
Les dépendances sont `mtools`, Python et QEMU/SeaBIOS pour le test ; OVMF sert
uniquement au test UEFI. Aucune signature de bootloader factice n'est ajoutée
pour tromper Rufus.

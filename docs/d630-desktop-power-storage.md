# Dell D630 : alimentation, desktop et persistance

## Modifications

- `power.c` exécute shutdown/reboot dans un service noyau dédié. Le demandeur doit posséder SYSTEM_ADMIN. Les boutons du menu natif et les commandes du shell utilisent ce même service.
- L'AML ACPI est interprété par uACPI v6.1.1, commit `2cb5e71ef58393839e1988ef9a8f1d8ba2845a8d`. Le backend fournit mappings superviseur/NX, accès IO/PCI bornés, mutex/events natifs, SCI PIC et une file de travail différée bornée. Aucun port d'extinction privé VirtualBox/QEMU n'est utilisé.
- Séquence : notification de fermeture des fenêtres, grâce de 3 s, flush des objets fichiers ouverts, terminaison des contextes utilisateurs restants, attente bornée de leur arrêt, vérification des erreurs de dernière fermeture, flush du volume, préparation AML S5, arrêt des drivers et du réseau sous transaction, désactivation du bus mastering PCI, rendez-vous d'arrêt sûr des autres CPU, WBINVD, S5. Une erreur de flush ou de préparation annule l'alimentation au lieu d'annoncer un succès. Un échec après quiescence peut laisser des périphériques arrêtés ; aucun redémarrage automatique des applications tuées n'est annoncé.
- Reboot : reset FADT, puis fallback matériel 8042 si le reset ne se produit pas. Les attentes sont mesurées avec l'horloge calibrée.
- Le mode BIOS ACPI hérité est conservé pendant le desktop ; l'entrée dans le mode ACPI se fait pour S5. Les routes SCI au-delà du PIC (GSI >= 16) ne sont pas implémentées et sont refusées explicitement. Les boutons physiques ACPI, suspend/resume et la gestion thermique OS ne sont pas ajoutés dans cette passe.

## Régression identifiée pendant l'intégration

Dans la VM de test, l'E1000 du slot PCI 3 est routée vers IRQ 9 (PIRQC). Le pilote natif réservait IRQ 11. L'installation SCI a démasqué IRQ 9 : une interruption E1000 restait alors assertée sans lecture de son ICR, entraînant une tempête PIC qui empêchait le BSP de retourner aux tâches READY.

Preuves : PIC1 ISR/IRR = `02/02`, PIRQC -> IRQ9, E1000 ICR=`8004`, une interruption levée et aucune abaissée ; RIP BSP dans les callbacks IO du handler ACPI. Le scheduler n'était pas la cause.

Le pilote natif lit maintenant Interrupt Line/Pin PCI. Le dispatch PIC appelle les sources réseau et SCI présentes sur la même ligne avant un EOI unique. Les sources noyau de niveau peuvent partager une réservation IRQ ; les claims `.driver` restent exclusifs, tout comme PCI/MMIO/DMA. Le retrait d'une source conserve la route des autres et les bits ELCR appropriés. Test de broker concurrent : partage noyau, refus des claims exclusifs conflictuels, retrait dans les deux ordres.

Les clics du menu contextuel sont également traités avant les surfaces sous-jacentes : sans cette correction, un bouton Shutdown placé au-dessus du navigateur envoyait le clic à Suchu.

## WebKit et identité visible

Le volume portable fournit les exécutables et bibliothèques depuis `system.boot`, mais les fonctions de listing ne rendaient pas les enfants de cette archive observables. Les parents implicites, notamment `/home/root`, étaient donc invisibles aux opérations directory/stat quand aucun backend CD n'existait. `fs_list` et `fs_list_directory` incluent désormais les modules d'archive, avec priorité aux entrées modifiables. La détection POSIX de répertoire utilise un contexte local, supprimant son état global de scan concurrent.

Validation : le vrai Suchu Browser prépare ses répertoires puis WebKit atteint settings/context/web view/main loop, en BIOS Core2 avec `disk: no cdrom backend`. Le test `runtimetest` vérifie mkdir/stat sur les répertoires réels. Ce résultat ne prétend pas valider une navigation Internet sur la BCM5755M du Dell, dont le support reste un chantier distinct. Les caches et données WebKit actuellement sous `/tmp` restent volatils.

Les chaînes visibles système, prompt, uname, wordmark et messages ont été renommées KyIlOS ; les identifiants internes VEX, notes ELF, API et format VexFS restent compatibles.

## GUI : changements et mesures

Le memcpy noyau utilisait des copies octet par octet. Le nouveau chemin utilise REP MOVSQ puis la queue exacte REP MOVSB, sans SSE/AVX/ERMS requis. Le papier peint conservait une division 64 bits par pixel lors de chaque dommage : son image de fond est désormais mise en cache par résolution, puis copiée uniquement sur les régions invalidées. Les icônes et overlays restent composés normalement. À 1024x768, ce cache a un coût borné de 3 Mio ; l'OOM conserve le rendu d'origine.

`guibench` compare les deux chemins sur le véritable framebuffer et son pitch, avec huit copies de 3 Mio et contrôle des octets :

| Mesure VM Core2, 2 vCPU | Ancien scalaire | REP | Rapport |
|---|---:|---:|---:|
| Copie RAM, 8 répétitions | 6,352 ms | 1,270 ms | 5,00x |
| Scanout, 8 répétitions | 5,586 ms | 0,686 ms | 8,14x |

Une session `guiprof` de 15 s avec dix déplacements réels de fenêtre fournit : 307 ticks GUI, moyenne 0,175 ms, maximum 2,446 ms ; 285 observations de queue souris, moyenne 0,105 ms, maximum 0,429 ms ; 25 présentations, moyenne 72,4 us ; 21 restaurations de fond, moyenne 38,7 us. Les échantillons sont majoritairement idle (99,8 % sur les deux CPU). Ce protocole léger n'est pas un benchmark de charge maximale ni une mesure du GMA X3100 physique. Aucun polling permanent ni redraw complet à chaque input n'a été ajouté.

## Partition dédiée : aucune préparation automatique

Le choix utilisateur est une partition interne VexFS, préparée explicitement. Le kernel sait accéder au disque IDE/SATA ; aucun driver de stockage USB après Limine n'a été ajouté.

Le formatage doit porter uniquement sur une partition primaire MBR préalablement créée et marquée type `0xDA`, avec une géométrie sans chevauchement. KyIlOS ne crée ni ne redimensionne cette partition et ne touche pas NTFS/FAT/GPT. La préparation avec un partitionneur externe exige sauvegarde et identification explicite du disque. GPT et partitions logiques ne sont pas pris en charge ici.

Dans KyIlOS, l'administrateur peut ensuite demander `vexfsformat <1..4> CONFIRM`. Cette commande refuse une partition d'un autre type, une géométrie invalide, un volume déjà formaté VexFS ou le volume monté. Les tables sont écrites avant le header final, puis le cache disque est flushé ; le montage se fait au prochain boot. Les fichiers RAM de la session précédente ne sont pas automatiquement migrés.

Le montage détecte une seule partition VexFS valide. Plusieurs candidates sont refusées comme ambiguës. Les anciens disques bruts VexFS restent reconnus en l'absence de partitionnement. Toutes les IO FS sont bornées et traduites dans le volume choisi. Un montage invalide ne déclenche aucun formatage.

`persisttest` écrit et synchronise un motif de 65536 octets ; après reboot, `persistcheck` le relit sans le réécrire et vérifie chaque octet. `sync` flush les fichiers ouverts et le backend disque. Les commandes refusent de présenter un volume RAM comme persistant.

La passe ajoute les flush ATA LBA28/LBA48, AHCI et NVMe ; seul IDE est validé en VM ici. Le FS existant n'est pas transformé en journal transactionnel : la récupération après coupure brutale de courant n'est pas garantie par ces tests.

## Preuves et limites

Fichiers principaux : `power.c`, `acpi_host.c`, `boot_requests.c`, `main.c`, `smp.c`, `scheduler.c` pour l'alimentation ; `gui.c`, `gui_overlays.inc`, `vex_lvgl_ui.h`, `libc.c`, `memory_copy.h` pour GUI/copies ; `fs.c`, `storage_volume.c`, `disk.c`, `ata_pio.c`, `vex_posix_compat.c` pour stockage et répertoires ; `irq.c`, `netdev.c`, `hardware_resources.c` pour la route partagée ; `shell.c`, `shell_commands.c`, `framebuffer.c`, `inst.c`, `spotlight.c`, `vex_syscall.c`, `compat_pe.c`, `boot/limine.conf` pour commandes/identité ; `scripts/build.ps1`, `scripts/test-usb-hdd-boot.py` et les tests hôte associés pour build/validation. Les APIs internes et headers correspondants sont ajoutés/étendus ; aucune migration générale Suchu/ORA/FS n'est engagée.

- Build kernel complet : `build/d630-final-build.log`, puis recompilation ciblée GUI et relink pour la priorité du menu.
- Tests hôte PASS : memory_copy (32x32 alignements, longueurs 0..511, bornes), storage_volume (legacy, partition dédiée, foreign/overlap/bounds), ATA (flush et erreurs/timeouts), broker matériel (8 threads/40000 tentatives), calibrations RTC/TSC et PIT/LAPIC antérieures.
- VM BIOS Core2, 2 Gio : desktop et clavier, drag, répertoires WebKit, vrai navigateur, DHCP ACK, 64 Kio persistants après reboot firmware et S5 à 2 vCPU.
- VM BIOS Core2, 4 vCPU : desktop, readback 64 Kio, Suchu/WebKit jusqu'à main loop, arrêt S5 après fermeture du navigateur.
- VM BIOS Core2, 1 vCPU : readback 64 Kio, Suchu/WebKit jusqu'à main loop et bouton Shutdown réellement exécuté au-dessus du navigateur, après correction de la priorité du menu. Log : `build/d630-1cpu-browser-s5-verified.log`.
- Logs : `build/d630-persistence-write.log`, `build/d630-reboot-readback-shutdown.log`, `build/d630-4cpu-shutdown-verified.log`, `build/d630-gui-profile-summary.txt` ; captures `build/d630-power-menu.png`, `build/d630-4cpu-browser.png`. Les statistiques de latence et les spans sont tirés du log brut ; les noms de fonctions du profil ne sont pas retenus car l'ELF exact de ce run n'a pas été conservé avant les relinks ultérieurs.
- Image livrable : `build/kyilos-d630-desktop-usb.img` ; ISO : `build/kyilos-d630-desktop.iso`. Le SHA-256 et celui du kernel intégré se trouvent dans `build/portable-iso-provenance.json`.
- SHA-256 USB final : `3AF678B963D82C8083B0D535211FED5025B54E718660969912C1D913851CE936`. Kernel intégré : `EA2EB8BC5811731C70BA6CFB8374641F4293046FD0EB339C0E5B3CD6672DDB9B`.
- Boot USB-HDD BIOS final PASS, QEMU/KVM, 1 vCPU, 2 Gio, sans disque IDE ni CD : 75,7 s ; preuve indépendante dans `build/usb-boot-tests/kyilos-d630-desktop-usb-bios-modern-map/result.json` et `block.json`.
- Le loader UEFI existant est conservé byte-for-byte. Le test OVMF USB n'a pas atteint l'entrée kernel en 350 s : 476791808 octets lus, 14562 lectures, aucune erreur IO remontée. Il reste donc non validé, et ne doit pas être présenté comme PASS. Le Dell utilise le chemin BIOS vérifié ; ce timeout UEFI est distingué des résultats desktop et S5.
- Un contrôle UEFI optique distinct a également expiré avant le premier log kernel (170 s). Il ne constitue aucune preuve de boot USB et confirme seulement que la validation UEFI n'est pas acquise dans cet environnement. Aucun panic kernel n'est enregistré ; la frontière reste firmware/loader avant l'entrée diagnostiquée. Les captures et statistiques sont dans `build/usb-boot-tests/kyilos-d630-desktop-uefi-modern-map-optical-control/`.
- À valider physiquement : découverte/interprétation des tables Dell, S5 et reset effectifs, arrêt des deux cœurs, persistance sur la partition interne explicitement préparée, latence GUI et rendu du framebuffer BIOS du GMA X3100. Aucun disque physique de l'hôte ni du Dell n'a été formaté pendant ce travail.

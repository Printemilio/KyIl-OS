# Dell D630 : calibration PIT2 / LAPIC

## Observation physique et portée du diagnostic

Le Dell atteint la calibration TSC/RTC puis s'arrête dans
`lapic_calibrate_timer`, avant RNG/stockage/archive/desktop. L'ancien code
réduisait à un même échec un timeout PIT et un résultat LAPIC nul. Cette seule
ligne ne permet pas de déterminer lequel s'est produit sur le Dell.

La référence est **PIT canal 2**, mode 0, OUT lu au port 0x61 bit 5.
Elle ne dépend pas d'une IRQ : IF est masqué et IRQ0 reste masquée au PIC pour
éviter de compter simultanément PIT0 et LAPIC. LVT timer masquée n'arrête pas
le décompte LAPIC. LINT0 ExtINT/PIC concerne les interruptions périphériques
ultérieures, pas cette mesure. TSC/RTC fournit uniquement le watchdog ; le
nombre LAPIC par milliseconde est calculé à partir de 1193182 Hz et de la
différence entre les comptes PIT effectivement lus aux bornes de la mesure.
Le compte de rechargement 11932 vise une fenêtre d'environ 10 ms.

## Défauts identifiés et corrections

- L'ancien read/modify/write du port 0x61 recopiait les bits de statut 7:4.
  ICH8 impose zéro pour ces bits lors des écritures. Les écritures conservent
  maintenant uniquement les contrôles 3:2 et programment gate/speaker 1:0.
- L'armement était supposé terminé après les écritures. `timer_calibration_measure`
  lit maintenant le statut 8254 (commande 0xe8, compteur 2 seulement), attend
  NULL COUNT=0, vérifie le mode/binaire, le compte chargé latched, OUT=0 et
  GATE=0 avant de démarrer la référence. Aucun délai arbitraire ne remplace
  l'acquittement matériel.
- La mesure partait systématiquement de UINT32_MAX. Elle utilise maintenant
  le Current Count réellement lu après armement (lecture qui ordonne aussi
  les écritures MMIO xAPIC), avec contrôle de DIV/LVT. Elle refuse un LAPIC
  non chargé, arrêté, expiré ou une fréquence calculée nulle/invalide.
- Les watchdogs bornent l'armement et la mesure. Échec ou succès : le LAPIC de
  mesure est arrêté et seuls les bits de contrôle sauvegardés du port 0x61
  sont restaurés. PIT2 reste réservé à cette calibration de boot ; l'ancienne
  programmation de son éventuel générateur sonore n'est pas restaurée.
- Le premier test instrumenté a montré, sous QEMU et VirtualBox, que le PIT
  pouvait déjà décrémenter avec GATE=0. Le calcul repose donc sur les deux
  comptes réellement latched, pas sur l'hypothèse compte initial=reload.
  Le dépassement du zéro terminal est inclus par soustraction 16 bits.
  Une durée dépassant un tour complet du PIT est refusée (watchdog TSC), afin
  de ne pas confondre plusieurs wraps et publier une mauvaise fréquence.
- La calibration est sérialisée entre CPU, chacun mesurant son propre LAPIC.
  Un AP renvoie maintenant son échec au BSP au lieu de se figer dans le job
  et de laisser `smp_boot_call` attendre indéfiniment.

Ces défauts sont démontrés dans le code/protocole. **Aucune mesure brute de
l'ancien échec physique n'est disponible : attribuer définitivement le
blocage Dell à un seul d'entre eux serait prématuré.** La nouvelle image
permet de les distinguer sans port série et sans supprimer SMP.

## Diagnostics visibles

Avant tout arrêt : CPU, IA32_APIC_BASE, SVR, port 0x61 avant/armé/final,
statut PIT armé/final, compte PIT initial/final, DIV/LVT, Current Count LAPIC
initial/final, durée TSC et compte LAPIC/ms. Les lignes sont aussi sérialisées.
Le calcul ne contient aucun log ; ils sont émis seulement après la mesure.

Les motifs distinguent : programmation PIT refusée, NULL COUNT bloqué,
compte/OUT d'armement invalide, gate incorrecte, programmation LAPIC refusée,
LAPIC jamais chargé, PIT inchangé au watchdog, OUT jamais terminal, LAPIC
immobile/expiré et fréquence invalide. Une référence invalide reste fatale :
aucune fréquence de secours et aucune désactivation de SMP.

Si la calibration réussit mais le boot s'arrête plus tard, photographier le
nouveau dernier jalon ; l'absence d'IRQ PIT ne doit pas être accusée pour une
mesure PIT2 réussie.

## Spécifications utilisées

- [Intel ICH8, document 313056-004](https://www.intel.com.tr/content/dam/www/public/us/en/documents/datasheets/io-controller-hub-8-datasheet.pdf),
  §5.7, §9.3.1–9.3.2 et §9.7.1 (NMI_SC, port 0x61).
- [Intel SDM volume 3A](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf),
  timer LAPIC, divide configuration, initial/current count et masque LVT.

## Validation

`tests/timer_calibration_test.c` exécute le même protocole via des callbacks
de matériel modélisé : chargements PIT/LAPIC retardés, niveau OUT hérité,
interdiction des écritures de statut ICH8, décompte VM avec gate basse,
dépassement terminal, rate mesuré, treize chemins
d'échec et restauration des contrôles/arrêt du LAPIC. Ce sont des tests de
protocole déterministes, pas une simulation présentée comme un essai Dell.

### Résultats de la version finale

- Compilation kernel complète puis recompilation/relink des deux unités
  corrigées : OK. Test hôte du protocole : PASS, treize erreurs détectées.
- QEMU/KVM, BIOS, **USB EHCI mass-storage comme seul disque de boot**,
  2 Gio/2 CPU, carte mémoire PC moderne (grand trou réservé), x2APIC :
  desktop atteint, timers CPU0=62491 et CPU1=62498 counts/ms, tous deux OK.
  `build/usb-boot-tests/kyilos-d630-timer-usb-bios-modern-map/result.json`
  consigne la commande et le SHA effectivement testé. Les 69,96 s de boot
  comprennent le chargement firmware de l'archive de 603 Mio : ce n'est pas
  le temps de calibration.
- VirtualBox BIOS, profil Intel Core2 T7600, 2 Gio/2 CPU, image disque sans
  CD-ROM, xAPIC : desktop atteint, CPU0=63669 et CPU1=63594 counts/ms.
  RNG jitter/SHA3 fonctionne ensuite. IRQ souris/clavier confirmées, lancement
  du terminal puis `kmaptest` : PASS (six threads, deux CR3, nouveau root,
  arbres kernel partagés, NX/USER, unmap, 32 cycles de destruction).
  Logs : `build/d630-timer-core2-final.log` ; captures
  `build/d630-timer-core2-desktop.png` et `build/d630-timer-core2-kmaptest.png`.
  Ce profil CPU ne reproduit pas le chipset ICH8 physique.
- Les échecs de la première instrumentation, où le compte chargé était
  encore exigé égal au reload, sont conservés dans
  `build/d630-timer-initial-{qemu,core2}-failure.log`. Ils ont conduit au calcul
  par bornes PIT réellement mesurées ; ils ne sont pas présentés comme PASS.

### Image pour le prochain essai physique

`build/kyilos-d630-timer-usb.img`, 738197504 octets (704 Mio), vrai disque
MBR/FAT32 avec stages Limine BIOS et chargeur UEFI conservé.

SHA-256 :
`618E453E02171E1C2A609D45C9EC4695DF67723DB9140037F51F15D513EDB753`

Le kernel intégré correspond exactement à `build/vex-os` :
`B4B43B1F3BFAE53CFC74CB6FAE45FD61070F10B7BD3C6976153D29E5ACF5B7CB`.
ISO sœur : `build/kyilos-d630-timer.iso`.

**Écrire l'IMG en mode disque brut/DD. Le retest sur le Dell reste à faire.**
En cas d'arrêt, une photo du bloc `PIT/LAPIC` complet et du dernier `[FAIL]`
permettra de conclure sur le matériel réel. L'image précédente
`kyilos-d630-trace-usb.img` est conservée pour comparaison.

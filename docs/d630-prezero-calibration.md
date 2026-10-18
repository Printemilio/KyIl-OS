# D630 : calibration LAPIC par fenêtre PIT avant zéro

## Ce que prouve le troisième essai physique

Le TSC RTC est maintenant calibré à `0x76E05FD0` = 1994416080 Hz.
`0x1306F00` = 19951360 cycles correspondent à **10003,61 µs**.
Le précédent écran imprimait les durées en hexadécimal : `0x2713` vaut
**10003**, et non 2713. Il n'y a donc pas de preuve d'une mesure de 2,713 ms.

LAPIC `0xFFFFFFEC → 0xFFFE18A3` : différence 124745 ticks. Sur environ 10 ms,
cela représente un compteur divisé d'environ 12,47 MHz ; aucune de ces
mesures ne justifie de déclarer le LAPIC défectueux.

La transcription PIT est arithmétiquement contradictoire :

- `0x2E9B → 0x0FF4` donne `0x1EA7` (7847), pas `0x2EA7` ;
- `0x2E9B → 0xFFF4` donnerait bien `0x2EA7` (11943).

Il manque les photographies originales dans la demande actuelle ; on ne
choisit donc pas arbitrairement entre ces lectures. Avec les secondes
valeurs, la formule donnerait **12463 counts/ms**, pas zéro.

## Défauts du chemin utilisé

`timer_calibration_measure()` calculait le delta modulo 16 du PIT après OUT
terminal puis le rejetait lorsqu'il était inférieur au compte de départ.
`TIMER_CAL_RATE` recouvrait ce rejet de référence, le résultat arrondi nul
et un résultat hors largeur. `count_per_ms=0` pouvait donc signifier
**calcul non effectué / refusé**, pas fréquence physique nulle.

Surtout, le bloc `done` relisait les compteurs en cas d'erreur et remplaçait
`pit_final` / `apic_final`. Le delta affiché était recalculé à partir de ces
nouveaux champs. Il ne permettait plus de reconstituer les opérandes exacts
ayant provoqué le rejet. Le TSC arrêté sur OUT, le PIT et le LAPIC pouvaient
alors décrire des instants différents.

Ce bug de diagnostic est confirmé dans le code. La raison unique du rejet
physique antérieur n'est pas récupérable à partir de valeurs remplacées et
d'une transcription contradictoire. La correction ci-dessous traite les
défauts de mesure et rend le prochain rejet, s'il existe, non ambigu.

## Nouvelle méthode effective

La référence reste **PIT2 à 1193182 Hz**. Pas de fréquence CPU/bus codée,
pas de désactivation SMP, pas d'acceptation forcée d'une référence invalide.

1. Programmer PIT2 mode 0 binaire (`0xB0`), reload **23864** : deux fenêtres
   nominales de 11932 ticks, soit environ 20 ms de headroom total.
2. Vérifier mode, NULL COUNT, count chargé et gate. Armer le LAPIC one-shot
   masqué, divider `0x3` (/16), initial count `UINT32_MAX`, avec readback.
3. Démarrer gate puis lire un snapshot initial. Commande **`0xC8`** : latch
   COUNT + STATUS du seul canal 2 ; lire statut, low, high, en drainant tout.
4. Encadrer ce latch par deux lectures LAPIC et deux timestamps TSC. Garder
   les bornes brutes et utiliser leur midpoint aux deux extrémités.
5. Échantillonner jusqu'à **au moins 11932 ticks réellement décomptés**, avec
   compteur positif, OUT bas, mode inchangé et compte monotone. Le delta est
   une soustraction positive, **pas un delta modulo après zéro**.
6. Calculer en 64 bits :
   `round(LAPIC_delta * 1193182 / (PIT_delta * 1000))`.
   Conserver numérateur, dénominateur et quotient même en cas de rejet.
7. Vérifier séparément le passage OUT haut (port 0x61 bit 5 **et** statut
   PIT bit 7) dans le watchdog d'une seconde. Les octets lus après terminal
   ne participent jamais au calcul de fréquence.
8. Publier le count/ms uniquement si toute la séquence réussit. Sinon
   arrêter le timer de mesure, restaurer les contrôles du port 0x61 et
   échouer explicitement. Aucun cleanup ne remplace les snapshots.

Les deux horloges périphériques sont toujours mesurées ensemble ; le TSC
calibré fournit les timestamps/watchdogs et le contrôle des fenêtres longues.
Les samples avant/après et la vérification OUT sont explicitement distincts.

## Configuration / concurrence

Le caller `lapic_calibrate_timer()` garde IF=0 et le verrou de calibration
PIT commun. Il ne fait aucun yield ni appel scheduler pendant la mesure.
Les AP reçoivent un job bootstrap sur leur CPU : pas de migration au milieu
du latch, et chaque CPU mesure son propre LAPIC. PIT0/IRQ0 reste masqué,
sans dépendance IRQ pour cette calibration. LINT0/PIC n'est pas modifié.
xAPIC MMIO et x2APIC MSR utilisent les mêmes callbacks de mesure.
RNG, ORA, VMM, scheduler, drivers et interfaces publiques restent inchangés.

## Diagnostics utiles au Dell

Les registres/cycles gardent le préfixe **0x**. Durée µs, Hz, ticks, fréquence
candidate/publiée et itérations sont explicitement en **décimal**.
L'écran montre reload/target/loaded, mode/readback, raw low/high/status/port
de START et END, bornes LAPIC/TSC et valeurs choisies, delta exact, operands
du calcul, quotient, indicateur formula-evaluated, code et raison du rejet.
La ligne SEPARATE terminal indique les valeurs après OUT et dit explicitement
qu'elles ne sont **pas des opérandes**. Les champs sont figés avant cleanup.

## Spécifications

[Intel ICH8 313056-004](https://www.intel.co.jp/content/dam/www/public/us/en/documents/datasheets/io-controller-hub-8-datasheet.pdf),
§5.7.2.3 et §9.3.1 : count/statut latched simultanément, statut lu en premier
puis les deux octets du count. §9.7.1 : port 0x61 bit 5 reflète OUT, bits
7:4 écrits zéro. Le protocole ne suppose pas une lecture atomique de deux
octets non latched.

## Tests ciblés

Le modèle de test est indépendant des VM et ne prouve pas le Dell :

- Snapshots groupés et latches entièrement consommés ; midpoint et delta
  exacts, référence avant zéro, mode et OUT vérifiés.
- Valeur terminal volontairement `0x0FF4`, différente de END : aucune
  substitution dans le calcul ; conservation des operands lors du rejet
  d'une incohérence OUT port/statut.
- GATE ignorée par émulateur, chargements retardés, PIT/APIC arrêtés, OUT
  prématuré/absent/incohérent, compte PIT croissant, TSC/LAPIC non monotones,
  fenêtre trop longue, fréquence nulle et restauration des contrôles.
- Tests séparés du calcul : transcriptions Dell, rounding, référence nulle,
  `UINT32_MAX` produits/diviseurs, résultat hors largeur, arrondi zéro.
- Régression du faux watchdog RTC 87180 Hz ; tests du lecteur RTC stable
  précédent conservés et exécutés.

### Résultats de validation

- Deux unités kernel modifiées recompilées/reliées : OK. Tests de protocole,
  calculs et régression RTC : PASS. Pas de rebuild WebKit/userspace inutile.
- VirtualBox BIOS, profil Core2 T7600, 2 Gio/2 CPU, xAPIC : les deux timers
  passent, desktop et terminal affichés, IRQ souris/clavier confirmées,
  `kmaptest` PASS (six threads, deux CR3, kernel tree partagé, NX/USER,
  unmap, 32 cycles de destruction). Fréquences mesurées : 62490 / 61906
  counts/ms. Fenêtres TSC : 10051 / 7766 µs ; clocks périphériques et TSC
  virtualisés peuvent progresser différemment, ce n'est pas une mesure Dell.
  La référence de calcul est toujours le delta PIT réel. VM dédiée arrêtée.
- QEMU/KVM BIOS, **vrai boot USB EHCI mass-storage**, 2 Gio/2 CPU,
  profil CPU host moderne 36-bit/4-level, x2APIC : desktop PASS. Fenêtres
  10021 / 10058 µs ; PIT 11958 / 12001 ticks ; LAPIC 626362 / 628651 ticks ;
  fréquence calculée/publiée 62499 / 62503 counts/ms. Snapshots pré-zéro OUT
  bas, vérification terminal OUT haut sur les deux CPU. Boot total 71,74 s
  incluant le chargement firmware du gros module, pas la calibration seule.
- Preuves : `build/d630-window-core2.log`,
  `build/d630-window-core2-kmap.png`,
  `build/usb-boot-tests/kyilos-d630-window-usb-bios-modern-map/serial.log`
  et `result.json` (commande et SHA testé).

### Image pour le prochain essai physique

**`build/kyilos-d630-window-usb.img`**, 704 Mio, vrai disque MBR/FAT32,
Limine BIOS et fichiers UEFI conservés. Écrire l'IMG en mode brut/DD.

SHA-256 :
`569E263290CB390381FAB1D7C53AAA69ADAEBE13128AAB14E455EF91673DA1F6`

Kernel intégré et `build/vex-os` :
`B2B780B261F66033E3ABCF9E470F580D055888D158BFC2CDBADE43646D1C4B29`.
ISO sœur : `build/kyilos-d630-window.iso`. Anciennes images conservées.

**Le test physique de cette version reste nécessaire.** En cas de rejet,
photographier le bloc entier à partir de CPU/TSC Hz jusqu'au dernier FAIL :
START/END count + bytes/status/port, brackets et valeurs retenues LAPIC/TSC,
durée en décimal, ticks, numérateur/dénominateur, candidate/published,
formula-evaluated, error-code/reason et SEPARATE terminal. Les données de
mesure et de vérification terminal ne doivent pas être mélangées.

Le succès attendu sur Dell est un count/ms mesuré cohérent (les anciennes
mesures suggèrent environ 12500, **ce chiffre n'est jamais utilisé comme
valeur de secours**), puis timers AP, RNG et poursuite du boot. Si un nouveau
blocage apparaît après la calibration, relever ce nouveau dernier jalon.

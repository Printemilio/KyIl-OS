# D630 : faux timeout PIT causé par la calibration TSC/RTC

## Mesures physiques du deuxième essai

Les dernières photos corrigées donnent :

| Mesure | Valeur |
| --- | --- |
| TSC annoncé Hz | `0x1548C` = 87180 |
| TSC elapsed | `0x185F6` = 99830 cycles |
| PIT chargé | `0x2E9C` = 11932 |
| PIT initial / final | `0x2E9A` / `0x2E5F`, différence 59 |
| Statut PIT armé / final | `0x30` / `0x30` |
| LAPIC initial / final | `0xFFFFFFED` / `0xFFFFFD8F`, différence 606 |

59 ticks PIT à 1193182 Hz correspondent à **49,448 µs**, soit seulement
0,49 % de la fenêtre prévue de 10 ms. 99830 cycles dans cet intervalle
donnent un ordre de grandeur TSC de **2,019 GHz**, cohérent avec ce CPU.
Ce n'est pas une nouvelle calibration utilisée par le kernel : ces courtes
mesures ne sont pas exactement simultanées et ne servent qu'au diagnostic.

## Première cause

`sched_clock_frequency()` est en hertz, stocké/retourné en `uint64_t`, affiché
en 64 bits. Aucun facteur MHz/kHz ou cast 16 bits ne tronque la valeur.
Sur cette génération sans ratio architectural CPUID.15 utilisable,
`sched_clock_calibrate()` passait par la RTC.

L'ancien chemin lisait seulement les secondes CMOS et acceptait deux
changements successifs comme une seconde écoulée. Il ne contrôlait ni UIP,
ni la stabilité des lectures, ni le format/range, ni les transitions +1.
Il pouvait donc mesurer quelques microsecondes pendant une mise à jour RTC
et les publier comme cycles/seconde. L'ICH8 déconnecte les octets calendaires
pendant la mise à jour ; ces lectures ne sont pas une référence valide.

Le watchdog était correctement exprimé en cycles :
`cycles_now - cycles_start >= sched_clock_frequency()`.
Avec la fausse fréquence 87180, sa prétendue seconde expirait après environ
43,6 µs sur un TSC 2 GHz. `PIT_OUT_TIMEOUT` était la conséquence, pas la
preuve d'un PIT arrêté. Le détail des valeurs RTC transitoires du Dell
n'avait pas été capturé ; le défaut du lecteur est reproduit dans les tests.

## Correction et invariants

`rtc_clock_measure()` vérifie UIP avant/après, deux secondes identiques,
format BCD/binaire stable, range 0–59 et SET non actif. Il attend trois
transitions consécutives +1 modulo 60, donc **deux intervalles complets**.
Chaque front réel est encadré entre les observations ancienne/nouvelle.
Les bornes de fréquence déduites des deux secondes doivent se recouper.
La fréquence choisie est au milieu de leur intersection : aucune fréquence
CPU attendue n'est codée et aucun seuil GHz arbitraire ne remplace la mesure.
Une interruption de séquence ou un intervalle incohérent réarme l'acquisition,
dans le budget de polling borné existant ; une acquisition invalide ou un
TSC non monotone reste fatale. Le chemin CPUID.15 reste inchangé.

Le watchdog LAPIC reste **une seconde mesurée**. La référence LAPIC reste
PIT2 : mêmes protections, IRQ non requise, calibration propre à chaque CPU,
aucune désactivation de SMP. Les registres PIT/OUT ne sont pas réinterprétés
pour annoncer un succès lorsque OUT reste bas.

## Instrumentation

- RTC : fronts en secondes, deltas TSC des deux secondes, bornes Hz, nombre
  de polls et de rejets UIP/torn/invalid/skipped/inconsistent.
- PIT/LAPIC : TSC start/end bruts, elapsed cycles, watchdog cycles, elapsed µs
  selon la fréquence validée, delta PIT modulo 16, itérations d'armement
  PIT/LAPIC et de boucle OUT.
- Raison exacte de sortie : bit OUT devenu haut, watchdog d'armement,
  watchdog OUT avec bit 5 toujours bas, ou rejet de contrôle.

Le timestamp de sortie est figé **avant** les lectures de diagnostic et
les logs ; ceux-ci ne gonflent plus la durée annoncée du watchdog.

## Contrat ICH8 vérifié

[Intel ICH8 313056-004](https://www.intel.co.jp/content/dam/www/public/us/en/documents/datasheets/io-controller-hub-8-datasheet.pdf),
§5.7/§9.7.1 : mode 0, OUT reste bas avant zéro puis haut jusqu'à la
reprogrammation ; port 0x61 bit 5 reflète OUT. Read-back status bit 7 expose
le même OUT. `0x30` = low/high, mode 0 binaire, NULL COUNT=0, **OUT=0**.
§5.12.2/§9.6.2.1 : contrôles UIP et disponibilité des octets calendaires.

## Tests

- `rtc_clock_calibration_test` : l'ancien lecteur reproduit une fréquence
  de quelques dizaines/centaines de milliers de cycles en pleine UIP ; le
  nouveau retrouve le TSC modélisé 2 GHz. Tests BCD/binaire, rollover,
  données incohérentes, SET/frozen, valeurs invalides, saut de secondes,
  changement de cadence et TSC non monotone : PASS.
- `timer_calibration_test` : avec des périphériques fonctionnels et la
  fausse valeur Hz du Dell, le watchdog OUT échoue prématurément ; la
  fréquence correcte du modèle permet OUT puis la vraie mesure. Les treize
  autres erreurs et les timestamps/raisons/itérations passent également.

Les fixtures sont des tests de protocole indépendants de la VM et ne sont
pas présentées comme des mesures sur le Dell. Le retest physique de la
nouvelle image reste nécessaire.

### Validation VM et image finale

- Quatre unités kernel affectées compilées/reliées avec les flags freestanding
  habituels : OK ; aucun rebuild WebKit/userspace inutile.
- QEMU/KVM BIOS, vrai USB EHCI mass-storage sans lecteur CD, 2 Gio/2 CPU,
  x2APIC : PASS jusqu'au desktop. TSC mesuré 3874942382 Hz, sorties OUT haut
  sur les deux CPU ; boucle BSP 10023 µs, AP 10043 µs ; LAPIC respectivement
  62498 et 62517 counts/ms. Boot 70,8 s incluant le chargement firmware de
  l'archive de 603 Mio, pas 70,8 s de calibration.
- VirtualBox BIOS, profil Core2 T7600, 2 Gio/2 CPU, xAPIC : PASS jusqu'au
  desktop. Deux secondes RTC complètes cohérentes, 37 observations UIP
  rejetées, fréquence TSC 3692001099 Hz (fréquence réellement mesurée dans
  la VM, pas celle du CPU Dell). Les deux timers réussissent. Le compte PIT
  peut déjà diminuer avec GATE=0 dans cet émulateur : les fenêtres effectives
  sont ici 9447/7921 µs ; le calcul utilise leurs deltas PIT réels.
  IRQ souris/clavier confirmées et `kmaptest` PASS : six threads, deux CR3,
  kernel tree partagé, NX/USER, unmap, 32 destructions. VM dédiée arrêtée.
- Preuves : `build/d630-rtc-core2.log`,
  `build/d630-rtc-core2-kmap.png`,
  `build/usb-boot-tests/kyilos-d630-rtc-usb-bios-modern-map/result.json`
  et son `serial.log`.

Nouvelle image USB-HDD : **`build/kyilos-d630-rtc-usb.img`**, 704 Mio.
MBR/FAT32, stages Limine BIOS et chargeur UEFI conservés ; écrire en brut/DD.

SHA-256 :
`58B31A8EBADAFBA965A6923A25183FBEF92BA5081EA2A266CB6A0CD55E831D3B`

Kernel intégré et `build/vex-os` identiques :
`43039731A274299861D50C0E1E1A50323A15051B5A5A079CE304AB7718986872`.
ISO sœur : `build/kyilos-d630-rtc.iso`.

Les deux images Dell précédentes sont conservées. Le boot physique avec cette
nouvelle correction n'est pas encore validé ; en cas d'arrêt, relever les
intervalles/bornes RTC, TSC Hz, TSC start/end/elapsed/watchdog, itérations,
raison `exit`, registres PIT/LAPIC et dernier `[FAIL]`.

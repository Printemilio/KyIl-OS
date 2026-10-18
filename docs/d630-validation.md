# Preuves du portage D630 — 10 octobre 2026

**État : fondations Legacy implémentées ; portage matériel complet non terminé.**
Pas de test sur le Dell physique. Aucun accès à son disque ArchVista. Les tests
VirtualBox utilisent une VM séparée `KyIlOS-Legacy-Test`, PIIX3, BIOS, 2048 MiB,
IDE/PIIX émulé, VGA/VBE logiciel, PS/2. Le profil Core 2 est **T7600**, pas T7250 :
il vérifie une personnalité CPUID ancienne, pas toutes les propriétés physiques
du T7250 (notamment le comportement réel des instructions, horloges et C-states).

## Image finale

- ISO : `build/kyilos-legacy.iso` (1 277 962 240 octets).
- SHA256 ISO : `262A8A29DEFD1BC3B3D9A100BC573372393F8C29CBDA257E744B44CCC3EDED85`.
- SHA256 kernel : `19A500841EA9A219200BF216B186D017B25AB7704E414524AA18A9EB91AEB897`.
- Archive : 632 705 024 octets, 603,39 MiB, 982 chemins, un seul module Limine.
- Staging/artefacts normaux `build/vex-os.iso` non remplacés par cette image.
- Provenance exacte : `build/portable-iso-provenance.json`, fichiers JSON des runs
  dans `build/d630-tests/`. Les traces de runs antérieurs sont conservées avec
  un suffixe temporel ; les hashes doivent toujours être comparés.

## Résultats effectivement obtenus

| Test | Résultat / limites |
|---|---|
| Compilation kernel freestanding | Réussie par `build.ps1 -NoIso`, puis relink du correctif de log FS |
| ATA host | PASS : IDENTIFY, LBA28/LBA48, buffers non alignés, read/write/flush, capacité invalide, bornes, erreurs, timeout |
| Archive host | PASS : aliases, SHA256, chemins, offsets/taille, doublons, toutes les troncatures de la fixture |
| RNG négatif host | PASS : compteur à delta constant rejeté avec erreur 10, aucune allocation retenue |
| RNG OOM host | PASS : allocation refusée, erreur 12, aucune allocation retenue |
| Moteur .driver host | PASS : descriptions CMOS, malformations, budgets, deadlines, OOM, droits |
| Broker matériel host | PASS : exclusivité/ranges, 8 threads, 40 000 tentatives |
| DMA ORA host | PASS : descripteurs/MMIO typés, références stale/foreign, ownership/quiescence, IO partiel, longueur RX corrompue, rollback/OOM, 8 contextes ×100 cycles |
| UDP/DHCP host | PASS : pseudoheader/checksum, machine d'état DHCP réelle, deadlines et absence de faux fallback IP |
| BIOS Core 2, 1 vCPU | Bureau, RNG jitter/SHA3 + CTR-DRBG, ATA, PS/2, commande de test ATA PASS |
| BIOS Core 2, 2 vCPU | `smp: detected=2 online=2`, deux timers PIT/LAPIC, RNG jitter, bureau |
| BIOS CPU moderne, 2 vCPU | `rng: ready provider=RDSEED + CTR-DRBG`, deux CPU et ATA PASS |
| Applications | KyIl Editor Suchu chargé depuis l'archive ; fenêtre et contenu rendus, clavier/souris exercés |
| JSC/JIT Core 2 | Les quatre modes du smoke existant passent : Baseline, DFG, FTL, variante de policy ; checksum -1250079968, code RX exécuté, W^X inchangé |
| Migration | Run Core 2 : gui-loop exécuté sur CPU0 et CPU1, 113 migrations observées (mesure diagnostique, pas benchmark) |
| Support boot absent | `disk: no cdrom backend` / `iso9660: no CD-ROM driver ready` et applications néanmoins accessibles |
| Disque étranger | Volume non-VexFS refusé sans formatage ; test explicite sur disque jetable tagué, données restaurées |

Le run JSC final a mesuré 850/164/88/95 ms dans les quatre modes ; ce n'est
**pas** un benchmark T7250 ni une mesure de navigation. Une nouvelle trace brute
rattachée à l'image finale est conservée dans `core2-cpu2.log`.

Le test ATA VM est réellement exécuté contre le périphérique IDE émulé, pas un
callback simulé : écriture/relecture de quatre secteurs, flush, restauration et
refus hors bornes. Export intégral RAW avant/après :

`43F93917765D74ED7D53CFB52F93B0FD71774D8BCAB18B8B0FB1605E1503BA43`

pour `ata-foreign.raw`, `ata-after.raw` et `ata-final.raw`. Le disque est
strictement jetable et identifiable par `KYIL-ATA-TEST-1`, pas un disque utilisateur.

## Régressions / limites observées

- Première approche à 959 modules : panic **Limine** `Memory map exhausted`,
  avant le kernel. Remplacée par l'archive unique, pas masquée.
- Aucun panic kernel/`#UD` observé dans les runs réussis.
- Le bruit série SMP peut entrelacer les lignes ; ne pas confondre une ligne
  tronquée avec une erreur de source d'entropie.
- Le total PMM correspond au domaine de PFN suivi et contient des trous/régions
  réservées ; il ne faut pas déduire 4 Gio de RAM réelle du dénominateur des logs.
  La VM a bien 2048 MiB. La marge exacte du Dell ~1,9 Gio reste à mesurer.
- Le backend réseau VM observé est le **E1000 natif** (archive refusée par son
  contrat matériel dans ce BIOS). DHCP/HTTPS n'ont pas été validés dans ces runs.
  Le fallback natif contient encore une réservation d'IRQ11 fixe ; une adaptation
  générale des routes PCI/PIC/IOAPIC est à examiner pour les autres machines.
- Aucun test d'USB réel, de BCM5755M, de BCM4311, d'audio PCM ou de suspend.
- Pas de campagne exhaustive SMP/FS/graphics : tests ciblés et smokes existants.
- Le remplacement d'un disque brut en présence de fichiers ouverts, le hotplug
  ATA, les secteurs logiques 4 KiB, le DMA ATA et les partitions/FS ArchVista ne
  sont pas annoncés.

## Prochaines dépendances

1. Essai physique de l'image et collecte BIOS/CPU/IRQ/BAR/PS2/VBE : confirmer que
   les fondations passent réellement sur le D630, sans installation sur le disque.
2. Transport UHCI/EHCI/USB mass-storage sécurisé et intégration stockage.
3. Contrat DMA/IRQ générique nécessaire à Tigon3 puis `bcm5755.driver` véritable,
   avec protocole, PHY, completions et quiescence — aucun paquet fictif ajouté.
4. Réseau sur le Dell : DHCP → DNS → TCP → TLS → navigateur HTTPS.
5. Extensions Wi-Fi/audio/énergie/GMA séparées, avec niveaux de preuve propres.

La procédure physique et le plan détaillé sont dans `docs/d630-port.md`.

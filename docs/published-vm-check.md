# Contrôle de publication — v0.29.2

Date : 19 octobre 2026. Image provenant de `Vex-OS - Last - Copie - Copie`,
sans reconstruction ni modification des octets de l’ISO.

## Vérification exécutée

- ISO : 1 396 013 056 octets.
- SHA-256 ISO : `467E1201309FDBBD4FFED7A1395FC771E526A6CBF28031C758C1C46FDCB80BD8`.
- Le noyau extrait de l’ISO correspond exactement à `build/vex-os` de la copie :
  `D6A798179EE0B79D1DBE6C2A6D86BF712E2311D63EF361901421918CA5FE2890`.
- VM distincte, console visible, 4 vCPU, 5120 Mio, CPU hôte, UEFI/ICH9,
  VMSVGA 128 Mio, framebuffer 1920×1080, lecteur AHCI, E1000/NAT.
- Démarrage jusqu’au bureau ; Terminal Suchu ouvert par menu avec souris réelle
  via l’interface VirtualBox ; commande `top` saisie par scancodes.
- `top` confirme 4 CPU : échantillon de 1000 ms, busy 0,4 %, idle 99,5 %.
  Il s’agit d’une observation de démarrage, pas d’un benchmark.
- Le journal confirme le pilote E1000 `.driver` et le DMA ORA.
- `shutdown` saisi dans le Terminal : fermeture des applications, retrait DMA,
  synchronisation et arrêt des CPU ; état VirtualBox final `poweroff`.

La capture du Terminal dans `docs/images/desktop.png` provient de ce contrôle.
La VM personnelle existante n’a pas été utilisée ni modifiée.

## Portée

Un seul démarrage de publication en 4 vCPU. Pas de nouveau runner complet 1/4 CPU,
pas de test de navigation Internet ni de Chrome GUI pour cette mise en ligne.
Les résultats historiques référencés ailleurs gardent leur date et leur périmètre.
Aucun disque de données n’est publié : la session de ce kit est volatile.

Les sources essentielles sont inventoriées dans `source-manifest.json`.
Le script de développement `run-virtualbox.ps1` est adapté uniquement pour nommer
l’ISO publiée `kyilos.iso`, permettre sa sélection explicite et utiliser un BOM
UTF-8 lisible sous Windows PowerShell 5.1. Le wrapper `start-vm.ps1` et le downloader
vérifient les checksums. Aucun code système de la copie n’est modifié pour publier.

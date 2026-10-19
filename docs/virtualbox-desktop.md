# Profil de bureau VirtualBox — 18 octobre 2026

Le lancement local normal est :

```powershell
.\scripts\run-virtualbox.ps1
```

La VM conserve son nom historique `Vex-OS` pour les scripts existants, mais son
profil est désormais **CPU hôte, 4 vCPU, 5 Gio (5120 MiB), UEFI, ICH9,
AHCI/SATA, VMSVGA et 128 MiB de VRAM**. Le framebuffer réel est 1920×1080×32.
Elle utilise `build/vex-os.iso`, image de bureau remise à jour dans cette passe.
Le profil Dell/BIOS/Core 2 reste séparé dans `run-legacy-port-test.ps1`.

Le script fournit `-MemoryMB`, `-CpuCount`, `-IsoName`, `-VmName`, `-Headless`,
`-ConfigureOnly`, `-SerialLogPath` et `-Plan` (description sans modification).
Il ne compile rien, ne formate aucun disque, préserve les disques existants et
refuse un arrêt forcé ou l'abandon automatique d'un état sauvegardé. Les anciens
journaux sont archivés dans `build/virtualbox-logs/`.

Le lanceur accepte aussi l'état VirtualBox `aborted` : la session est déjà
interrompue et peut être relancée à froid, sans arrêt forcé ni suppression de
sauvegarde. Une VM `running` ou `paused` produit un message explicite et un
retour normal, sans reconfiguration. L'état `saved` reste protégé.
Le script est enregistré en UTF-8 avec BOM et utilise les API Path disponibles
sous Windows PowerShell 5.1, afin d'éviter les accents mal décodés et les
surcharges .NET réservées à PowerShell 7. Relancement depuis `aborted`, retour
sur VM active et `-Plan` ont été vérifiés sous Windows PowerShell 5.1.
L'utilisateur a ensuite expliqué l'état `aborted` : il avait tué la VM dans le
Gestionnaire des tâches, car sa fenêtre n'était pas visible. Ce n'était pas une
preuve de crash du noyau.

### Fenêtre hors écran corrigée

La position Qt mémorisée était `1920,23,1920,1009,max`. Les écrans connectés
étaient à X=0, X=-1920 et X=-818 : aucun écran à droite de X=1920. La console
GUI existait donc hors écran. La VM a été arrêtée par `shutdown` dans KyIlOS,
avec sync, retrait du DMA et ACPI S5 confirmés, puis laissée éteinte.

Le lanceur recalcule maintenant ses positions normale et mise à l'échelle
dans la zone de travail de l'écran principal (`160,66,1600,900` sur ce poste).
La vue hôte s'adapte à cette fenêtre ; le framebuffer invité reste 1920×1080.
Le frontend normal est démarré explicitement avec `WindowStyle Normal` et son
état de démarrage est contrôlé. Sur une VM active, l'API officielle
`CanShowConsoleWindow/ShowConsoleWindow` demande l'affichage de la console.
Le mode `-Headless` reste réservé aux lancements explicitement demandés.

Le gestionnaire VirtualBox a été ouvert en mode normal. L'utilisateur a
confirmé qu'il le voit. La VM est restée arrêtée pour lui laisser le contrôle
du prochain démarrage. Aucune extinction forcée dans cette correction.

## Ce qui est réellement moderne, et les limites

- Le CPU invité n'est plus un profil Core 2 : ses capacités sont celles exposées
  par VirtualBox avec le profil `host`, détectées par KyIlOS.
- Virtualisation matérielle et nested paging demandés. Si Windows impose le
  backend Hyper-V/NEM à VirtualBox, le lanceur ne modifie pas la configuration
  de l'hôte. Les enlightenments *invités* Hyper-V/KVM ne sont pas implémentés
  par KyIlOS ; le provider invité reste donc `none`.
- E1000/NAT est conservé car son chemin matériel est réellement pris en charge.
  Sur le boot moderne testé, **`e1000.driver` est actif**, avec DMA ORA ; aucun
  pilote VirtIO ou autre interface fictive n'est annoncé.
- VMSVGA/GOP fournit le framebuffer. Le rendu KyIlOS reste logiciel : ni cette
  option ni les 128 MiB de VRAM ne constituent une accélération 3D matérielle.
- 1920×1080 est le mode de boot vérifié. Pas de Guest Additions ni de changement
  automatique de résolution invité au redimensionnement de la fenêtre hôte.
  Le vieux `setvideomodehint` post-boot, sans driver invité, a été supprimé.
- Sans `-DataDisk` ou disque de données déjà attaché, les fichiers de session
  restent en RAM. Aucun disque de persistance n'est créé automatiquement.

## Panne UEFI corrigée, sans retour au BIOS

Le premier boot UEFI reproduit une panne réelle, visible dans
`build/modern-vm-first.png` :

```text
HHDM RAM base=0x0000000000000000 size=0x0000000000001000
[FAIL] VMM: invalid RAM extent or page-table allocation failed
```

`vmm_get_page_flags()` assimilait un numéro de frame physique nul à une entrée
vide. Or une PTE présente vers la frame zéro est valide ; la carte mémoire UEFI
peut justement commencer par une telle page appartenant au bootloader. Le
mapping réussissait, puis sa vérification était refusée à tort.

`kernel/src/vmm.c` distingue désormais une **PTE entièrement vide** d'une PTE
vers la frame zéro dans `vmm_get_page_flags`, `vmm_get_physical_any` et
`vmm_set_page_flags`. Les permissions et le contrôle de la moitié noyau restent
appliqués. Rien n'est exclu de la carte mémoire pour masquer l'erreur.

Le test `kmaptest` comprend maintenant un alias superviseur readonly/NX vers la
frame zéro, sans lire ni écrire ses octets : permissions, traduction avec offset,
PROT_NONE/restauration, refus USER et unmap. Ses tests existants de six threads,
deux CR3, publication, racine tardive et 32 destructions restent présents.

## Validations réellement effectuées

| Cas | Résultat |
|---|---|
| UEFI / ICH9 / CPU hôte / 4 vCPU / 5120 MiB | bureau atteint, quatre CPU en ligne |
| Framebuffer et capture | 1920×1080 réels |
| `kmaptest`, incluant frame zéro | PASS en UEFI 4 vCPU |
| Terminal Suchu | ouverture et saisie, commandes `kmaptest` et `top` |
| Paint Suchu | ouverture, vrai tracé, déplacement de fenêtre, fermeture |
| Arrêt natif du test moderne | ACPI terminé ; DMA du driver retiré |
| Même noyau, BIOS / PIIX3 / Core 2 T7600 / 1 vCPU / 2048 MiB | bureau et `kmaptest` PASS, arrêt natif |
| Lanceur par défaut après promotion de l'ISO | bureau atteint en UEFI 4 vCPU / 5 Gio |

Captures : `build/modern-vm-terminal.png`, `build/modern-vm-paint-drag.png`,
`build/modern-vm-final-desktop.png`. Journaux :
`build/modern-vm-test-serial.log`, `build/modern-vm-bios-regression.log`,
`build/vex-serial.log`. Configuration constatée :
`build/modern-vm-final-info.txt`. Synthèse : `build/modern-vm-validation.json`.

Le boot initial arrêté avant heap/FS a été éteint une seule fois avec
l'autorisation explicite de l'utilisateur. Les deux tests fonctionnels ont
ensuite été arrêtés par la commande native. La VM temporaire a été désenregistrée
sans suppression de ses fichiers ; la VM normale reste ouverte sur le bureau.

Ce sont des validations locales ciblées : pas de nouveau test physique Dell,
de benchmark de performances, de campagne Windows complète ou de validation
Chrome graphique. Le kit GitHub déjà publié n'a pas été republié dans cette
passe. L'ancienne ISO locale est conservée dans `build/virtualbox-logs/`.

ISO de bureau, 2 115 528 704 octets :
`021944E146A9616937ECBD8EF19C032470C630C81FEE7A80F5E11D109308105E`.
Noyau : `205FAFE2817A6FD6B799C735AF08A68255A183E3C3EAF76D7B7C681511B3C069`.

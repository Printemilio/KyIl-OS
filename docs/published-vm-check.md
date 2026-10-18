## Vérification du kit VM publié

Avant publication, l’archive a été relue (CRC ZIP), extraite par
`download-vm.ps1 -ArchivePath ...`, puis l’ISO extraite a été vérifiée avec le
SHA-256 du checkpoint. Le nouveau `start-vm.ps1` a créé une VM VirtualBox neuve,
BIOS/PIIX3, 1 vCPU, 4 Gio RAM, sans disque de données.

Résultats : bureau atteint, DHCP ACK réel, Terminal Suchu ouvert, commande `top`
affichée, puis arrêt natif `shutdown` jusqu’à extinction de la VM. La capture
`images/desktop.png` provient de ce run, sans retouche. Cette vérification du kit
n’est pas présentée comme une nouvelle campagne WebKit, Windows ou matérielle.

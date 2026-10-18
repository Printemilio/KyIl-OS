# Intel 82540EM par archive `.driver`

Le backend réseau normal privilégie maintenant `drivers/e1000.driver` avant
l'initialisation du fallback C. Le navigateur Suchu/WPE/WebKit et les sockets
existants utilisent la même API Ethernet/IPv4/TCP/UDP ; pas de réseau privé.
Voir le rapport de validation pour distinguer les mesures VM des tests host.

## Protocole et moteur

`drivers/e1000.driver.json` contient le véritable programme : reset, polling
du bit de reset, EEPROM/MAC, multicast table, contrôle du lien, configuration
RX/TX, réception/émission, acquittement ICR, diagnostics et arrêt. L'archive
compressée/versionnée est produite par le packer commun. Aucun appel vers
`e1000_init/send/recv/read` du pilote natif n'existe dans ce programme ou dans
le moteur. Changer ce programme ne demande pas de recompilation du kernel.

Le C ajouté est un HAL et une autorité :

- `hardware_resources.c` : leases BDF/BAR/IRQ exclusifs, configuration PCI
  sérialisée, sizing BAR avec decoding/BME désactivés et registres restaurés.
- `driver_pci.c` : mapping MMIO kernel-only RW/NX/PCD/PWT, traduction PCI et
  fournisseur DMA, activation bus mastering après reset prouvé, fermeture après
  reset/quiet conditions/BME off et shootdown VMM.
- `driver_pci_policy.c` : **données de sécurité** pour 8086:100e, classe 020000,
  BAR 128 KiB, champs des descripteurs, registres/masques/conditions de reset.
  Aucune séquence d'initialisation ou de traitement de paquet dans cette table.
- `driver_dma.c` : ORA_STORAGE mutables, tokens privés, spans/bindings retenus,
  publication et ownership par slot, completions matérielles, recyclage.
- `driver_manager.c` : classe NETWORK, callbacks vers l'API netdev existante,
  transaction/lifetime de l'instance, budgets, catalogue de confiance et tests.

Le contrat de classe réseau passe les octets d'une trame via le scratch privé
du programme, limité à 1518 octets. INIT retourne un MAC, CONTROL un booléen de
lien réel ; READ retourne longueur/octet(s), WRITE accepte une trame copiée.
Le bridge copie les octets et compte les paquets ; la logique des registres,
indices, flags et descripteurs appartient au programme externe.

## Rings continus et protections

Deux pools de 64 KiB indépendants, deux rings de 32 descripteurs × 16 octets,
buffers de 2048 octets. Total physique engagé : **139264 octets (136 KiB)**.
Le natif réserve actuellement 66 pages : 270336 octets (264 KiB), sans que cette
comparaison de backings seuls soit présentée comme celle de toute la RAM/CPU.

Les opcodes restent génériques :

- DMA 10 `SLOT_ACQUIRE(ring,index)` : DD observé dans le champ STATUS approuvé,
  fence, vérification adresse/span/longueur, transfert en ownership CPU.
- DMA 11 `SLOT_POST(ring,index,longueur)` : index producteur attendu, span libre,
  adresse inchangée, longueur bornée, effacement des champs device-written,
  ownership device avant le doorbell. Le moteur calcule le tail (index ou next).
- DMA 12 `SLOT_STATUS(ring,index)` : snapshot du STATUS approuvé uniquement,
  aucun transfert d'ownership et aucune exposition d'adresse.

Un pool partagé par plusieurs slots est accessible CPU uniquement dans ses
spans non détenus par le device. Fermer un token ne libère jamais les buffers
retenus par un ring. Publication partielle/échec : refs retenues jusqu'au stop
prouvé. Les opérations DMA restent interdites en IRQ ; le worker Ethernet
existant consomme les frames, l'IRQ acquitte ICR puis le réveille.

Les guards MMIO interdisent RX.EN/TX.EN avant publication du ring correspondant,
et invalident ces publications lors d'un reset. Ils interdisent les écritures
RDH/TDH tant que le ring est publié. Aucun write scalaire/partiel ne peut accéder
aux adresses, longueurs ou tails DMA. Rings exactement 32 slots ; champs DEXT,
indirect descriptors, VLAN/TSO et tailles de buffers non représentées refusés.

Le contrat de reset impose également le délai d'accès minimal de 1 µs décrit
par Intel, puis observation des conditions de quiet et BME off avant release.
[Manuel Intel, CTRL.RST et initialisation](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf).

## Exclusivité et fallback

La transaction de contrôle reprend le gate réseau existant et un gate netdev.
Pins/exécution de l'instance sont protégés par son gate. Pas de verrou kernel
global. Changement de backend, IRQ, sends/receives et destruction ne peuvent
observer un provider libéré. Les leases natives sont libérées individuellement,
jamais en supprimant tous les contrôleurs portant l'owner natif historique.

L'arrêt bloque les nouveaux pins, masque l'IRQ, attend les exécutions bornées,
exécute STOP dans la VM, désactive BME, prouve le reset, reclaim les rings,
libère ORA, unmap avec shootdown, libère les leases, puis reprend le natif.
Au chargement, le natif est arrêté avant de réserver/initialiser le nouveau
backend. Un échec sûr restaure le natif ; un stop non prouvé reste quarantiné.
Une faute runtime non récupérable masque l'IRQ et exige un unload avec preuve
de stop : aucun « succès » après timeout d'une transaction partiellement publiée.

`driver netstats` mesure les deltas des véritables appels C TX/RX/IRQ depuis
l'activation du backend ; les zéros ne sont pas une constante d'affichage.
`driver netcycle` teste conflit, stop, programme illégal refusé avec fallback,
puis trois cycles de reload ; `driver netstats` doit montrer le .driver final.

## Corrections démontrées pendant l'intégration

1. Lien physique asynchrone : configuration/rings prêts ne signifie pas LU=1.
   INIT et le fallback natif cessent de brûler une boucle de carrier sous gate.
   CONTROL/LSC exposent et réveillent l'état réel du lien.
2. DHCP tentait son acquisition avant le lien, puis affectait une adresse
   VirtualBox fabriquée. DHCP est maintenant servi par le worker Ethernet :
   WAIT_LINK → DISCOVER/OFFER → REQUEST/ACK, deadlines sans polling actif ;
   timeout laisse l'interface sans adresse au lieu de simuler une attribution.
   Le cache de configuration existant reste distinct d'un nouvel ACK observé.
3. UDP calculait le pseudoheader RX avec l'IP locale et rejetait un broadcast
   valide après configuration. Le callback IPv4 interne transmet maintenant
   la destination réelle ; UDP et TCP la vérifient sans supprimer le checksum.
4. Une écriture ARP a pris 1224306 ns dans VirtualBox : le budget hérité de 1 ms
   interrompait le programme après un effet hardware et avant son index logiciel.
   Budgets NETWORK READ/WRITE bornés à 10 ms, INIT/STOP à 100 ms, fuel 20000 ;
   IRQ 256 instructions/200 µs et RTC/UART inchangés. Erreur fatale = quarantaine.

## Limites concrètes

Validé sur VirtualBox PIIX3/82540EM, sans IOMMU. Pas encore validé sur une carte
physique. Un NIC/firmware hostile n'est pas isolé par ces contrôles logiciels.
Une interface, PIC IRQ11 (topologie testée), IPv4/MTU1500, pas MSI/MSI-X,
jumbo/VLAN/TSO/offload annoncé. Le réseau garde ses limites antérieures, notamment
pas d'IPv6 ni réassemblage IPv4. DHCP ne constitue pas encore un client complet
de renouvellement/expiration RFC ; ce travail n'est pas présenté comme fait.
Les fenêtres MMIO sont bornées à huit slots privés, réutilisés uniquement après
unmap et shootdown acquitté ; la fenêtre native est également recyclée à l'arrêt.
Le driver reste interprété : aucune promesse de débit Gigabit ou de parité CPU
avec le C natif sur la base de la seule page HTTPS.

## Fichiers principaux

Ajoutés : `kernel/include/unos/driver_pci.h`, `kernel/src/driver_pci.c`,
`driver_pci_policy.c`, `drivers/e1000.driver.json` et son archive générée,
programme négatif `e1000-denied-test`, tests UDP/DHCP et scripts de validation
réseau/PCAP/pixels. Étendus : `driver_dma.h/.c`, `driver_vm.h/.c`,
`driver_manager.c`, broker `hardware_resources.h/.c`, `netdev.h/.c`,
`irq.c`, `main.c`, `ipv4.h/.c`, `udp.c`, `tcp.c`, `dhcp.h/.c`, `ethernet.c`,
packer/catalogue et suite host. ORA lui-même, WebKit/JSC, Suchu et le renderer
n'ont pas été refondus ou modifiés pour ce travail.

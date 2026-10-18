# `.driver` — état intégré et validation

## Réellement livré

Le kernel charge une archive compressée, valide son SHA/version/TOC/bytecode,
la lie uniquement à des ressources approuvées/exclusives et interprète son
protocole. Aucun ELF, dlopen kernel, native delegate ou fonction spécifique
UART/CMOS appelée par l'ISA. Les descriptions sont des fichiers externes
modifiables, compilés en bytecode par un outil indépendant du kernel.

| Partie | État et preuve |
|---|---|
| Archive RLE/SHA-256/version/TOC | Réel, parser strict sous ASan/UBSan |
| VM arithmétique/branches/PIO/indexed-PIO/scratch | Réel, descriptions RTC et UART exécutées |
| Fuel/deadline/refus opérations | Réel, host : budget, malformed/rights/unsupported |
| Ressources exclusives/PCI config partagé | Réel, 8 threads/40000 tentatives + conflit en VM |
| Native CLOCK | RTC CMOS réel repris après le fallback early-boot |
| Native CHARACTER | UART COM2 réelle en VM, TX externe/RX/IRQ/scheduler wait |
| IRQ attach/mask/detach et lifetime | Réel, VM 1/4 CPU, blocked reader unload/kill |
| Manager local/pins/install/hot load/unload | Réel, installation puis reboot/readback VexFS |
| Découverte PCI | Réelle, BDF/vendor/device/class ; pilotes natifs gardent les ressources |
| MMIO BAR et DMA pour packages | Refus explicite v1, pas simulés |
| USB/discovery hotplug du bus | Absent ; aucun host USB existant trouvé |
| Dépôt distant/signatures fournisseurs | Non implémenté, point de validation par pin prêt, aucun download |

Deux classes sont exposées en kernel : CLOCK (branchée à l'API RTC native et
donc time/FS/UI/TLS existants) et CHARACTER (API read/write/attente). Pas encore
d'ABI application pour ouvrir une capability de périphérique, ni d'interfaces
réseau/bloc/input/GPU portées sur le moteur. La découverte dynamique de fichiers
n'est pas un hotplug PCI/USB général : load/unload est sûr pour les deux devices
approuvés, pas une promesse de retrait arbitraire de tout matériel.

## Tests exécutés

Host : `scripts/test-driver-vm.sh` compile le vrai parser/VM/broker. SHA-256 est
celui de Mbed TLS du kernel. Conversion BCD/binary, 12h AM/PM/24h, UIP timeout,
instruction fuel/deadline, allocation fail, toutes troncatures et mutations
octet par octet du vrai CMOS, programmes malveillants avec SHA recalculé,
offset/resource/register/branch hors bornes, write non autorisé, MMIO/DMA
unsupported. Chaque erreur laisse zéro allocation du loader. Broker réel :
overlap, bounds/overflow, propriétaires natifs, release exact et 8 pthreads /
40000 tentatives, jamais deux propriétaires simultanés. ASan/UBSan passent.

VM VirtualBox 7.2.6, même kernel, 1 et 4 vCPU :

```text
driver-test: real clock 2026-10-10 00:19:23 epoch=1791591563
driver-test: UART external echo byte=75 IRQ=1 RX=1 result=ok
driver-test: empty receive=timeout byte=75 queue=0 total=1 IRQ=1
driver-test: stop=ok
driver-test: blocked reader unload result=ok
driver-test: blocked reader kill result=ok
driver-test: archive variant actual divisor=2
driver-test: PASS clock/UART/IRQ/timeout/conflict/stale/unload/reload
```

Le sender kernel dort deux ticks pour que le lecteur s'enregistre avant le TX.
Le pair TCP local reçoit le vrai flux sortant de l'UART émulée et le renvoie :
`RX 4b / TX 4b`. Le byte arrive via l'IRQ3 et le compteur natif. Aucune donnée
injectée directement dans le moteur, aucun polling de RX par le consumer.
Les waits n'empruntent aucune référence à l'instance au-delà du sommeil/kill.
La variante source `uart-slow.driver.json` programme DLL=2 ; le diagnostic de
l'archive relit ce registre matériel, sans changer/recompiler le kernel.

Installation réelle de la variante et pin : réussite, activation depuis
`/home/root/packages/uart-16550-57600-f8bd404535cc74bf.driver`. Test sur disque
VexFS 32 Mio neuf : install → poweroff/reboot → volume monté (24 entries) →
load de ce fichier réussit, instance active. Re-enrollment/activation explicites,
pas d'exécution automatique des fichiers simplement découverts. Utilisateur
`driverguest` sans SYSTEM_ADMIN : chargement refusé.

Non-régression : boot desktop, terminal, les deux Paint (couleurs/rayons/gomme,
clear maintenu, drag, resize compact, 9 assertions Suchu), réseau e1000 DHCP
10.0.2.15, DNS example.com 104.20.23.154 et vrai GET TLS/HTTPS avec réponse
`HTTP/1.1 200 OK` et contenu Example Domain. Pas de campagne Vulkan/WebKit
complète, ni de hardware physique testé. Aucun changement renderer/JIT/ABI.

Le dernier kernel (fermeture atomique des métadonnées d'instance) a repassé le
harness matériel en 1/4 CPU. Kernel SHA256 :
`0a3b51261aca32f295972d68992aed0ff066c755ce363ae70a1c57ed1e0c29fc`.
ISO SHA256 : `85e07d7f08c2e3212d16440b344e490532dcffb549ad5e0956eef5e31f78e088`.
VM laissée arrêtée, 4 CPU, UART2 désactivée et disque de test détaché. Le disque
reste dans `build/driver-tests/persistence.vdi` pour reproduire le readback.

## Défauts rencontrés et corrigés

- Le diagnostic utilisait `%llu`, absent du formatter kernel. Le `%s` suivant
  lisait alors un compteur comme pointeur et déclenchait #PF dans `append_string`.
  Corrigé dans le caller par conversion décimale explicite, sans refonte libc.
- Le loopback interne VirtualBox renvoie un NUL supplémentaire après K. La
  trace de protocole contient une unique émission K. Le callback
  `uartR3TxUnconnectedTimer` prépare bVal=0 puis le réinjecte sans condition
  sur cbRead, avant de tester la FIFO vide. [Source Oracle](https://raw.githubusercontent.com/VirtualBox/virtualbox/master/src/VBox/Devices/Serial/UartCore.cpp).
  Ce défaut du device de test reste documenté ; aucun filtrage de NUL dans
  KyIlOS. Validation finale en mode normal avec un pair externe.
- Le premier pair echo avait reflété le texte firmware. Le harness arme
  désormais l'echo seulement une fois le desktop prêt, avec budget trafic.
- Retrait d'instance : transfert atomique des métadonnées sous registry gate,
  puis libération du payload hors gate. Pas de lecteur voyant un manifest
  partiellement remis à zéro, ni de réutilisation de payload retraité.
- Ingestion : `fs_read_view_bounded` borne les fichiers avant matérialisation
  disque/ISO sous fs_gate, nécessaire pour ne pas charger un fichier massif
  avant de découvrir que son archive dépasse 64 Kio. `fs_read_view` garde
  son contrat et utilise la même fonction avec SIZE_MAX.

## Fichiers et reproduction

Core : `kernel/include/unos/driver_vm.h`, `kernel/src/driver_vm.c`.
Manager : `driver_manager.h`, `driver_manager.c`.
HAL : `hardware_resources.h/.c`, `irq.h/.c`, wrappers PCI dans disk/netdev/gpu.
Intégration : rtc.h/.c, main.c, shell_commands.c, fs.h/.c (bounded read).
Packaging : `drivers/*.driver.json`, archives et `boot.trust`, `pack-driver.py`,
`build-driver-packages.py`, build.ps1 et boot/limine.conf.
Tests : `driver_vm_test.c`, `hardware_resource_test.c`, `test-driver-vm.sh`,
`driver-uart-peer.py`, `run-driver-tests.ps1`.

```text
python scripts/build-driver-packages.py
bash scripts/test-driver-vm.sh
scripts/run-driver-tests.ps1 -CpuCount 1
scripts/run-driver-tests.ps1 -CpuCount 4
```

Les trois archives JSON restent les sources éditables. Le générateur initial
sert seulement à leur écriture initiale ; ni le runtime ni le build n'en dépendent.
Le packer ne contient aucun protocole matériel. Le build normal inclut les
archives ; mise à jour d'archive seule : packer + repack ISO, pas de compilation
kernel/engine. Consulter [le format](driver-format.md) et
[l'audit/plan](driver-integration-plan.md).

Preuves brutes : `build/driver-tests/{cpu1,cpu4}/serial.log`, peer logs,
`smp-validation.log`, `persistent-install.log`, `persistent-readback.log`,
`install-network.png`, `tls-result.png`, `permission.png`, `desktop-apps.log`.
Le dépôt contient déjà de nombreux changements antérieurs ; un diff global
ne constitue pas la liste de cette mission.

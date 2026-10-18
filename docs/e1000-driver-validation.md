# E1000 `.driver` — validation finale

Même kernel/ISO, VirtualBox PIIX3, 82540EM NAT, IOMMU désactivée, 1 puis 4 vCPU.
Le navigateur lancé est le vrai `/usr/bin/kyil-browser` Suchu avec WebKit/WPE.
URL : `https://example.com/?driver=e1000-cpuN` ; aucun document substitué,
aucun snapshot fabriqué et aucune délégation au datapath E1000 C.

## Résultats réellement exécutés

| Mesure finale | 1 vCPU | 4 vCPU |
|---|---:|---:|
| DHCP wire DISCOVER/OFFER/REQUEST/ACK | 1/2/3/5 | 1/2/3/5 |
| DNS réponse valide capturée | oui | oui |
| Navigateur HTTPS | 200, HTTP/2 | 200, HTTP/2 |
| TTFB | 22 ms | 26 ms |
| Transfert principal | 227 ms | 351 ms |
| DOMInteractive / DOMContentLoaded | 457 ms | 633 ms |
| DOM complete | 490 ms | 669 ms |
| First paint déclaré par WebKit | 510 ms | 686 ms |
| Paquets acceptés TX / RX du backend final | 54 / 30 | 53 / 29 |
| IRQ finales du backend | 68 | 70 |
| Delta appels natifs C TX/RX/IRQ pendant `.driver` | **0 / 0 / 0** | **0 / 0 / 0** |
| Compteur d'erreur de packet handler | ok | ok |
| Pixels sombres dans viewport réellement capturé | 21678 | 21678 |
| Couleurs distinctes dans viewport | 245 | 242 |
| Panic / quarantaine | aucun | aucun |

Les temps sont ceux de deux navigations distantes réelles, pas une mesure de
scaling 1→4 CPU ni un benchmark de débit contrôlé. Le transfert est petit ;
aucune parité CPU/Gigabit avec le natif n'est revendiquée. Les 136 KiB de backings
DMA de l'archive se comparent aux 264 KiB réservés par les buffers/rings natifs,
pas à la consommation de RAM de toute la pile.

La capture visuelle a été attendue après le finish réseau/DOM : une image
entièrement blanche ne passait pas le critère. Le screenshot final contient
le contenu WebKit `Example Domain`, 577 octets décodés selon Navigation Timing.
Le transport normal reste Surface/capabilities, pas la capture de validation.

## Lifecycle et adversaires

Dans chaque boot, `driver netcycle` a réellement exécuté :

1. Double chargement refusé avec CONFLICT.
2. Arrêt par programme STOP, BME off, quiescence, ORA→0, unmap/shootdown,
   libération exclusive et reprise du natif.
3. Chargement du vrai programme négatif `e1000-denied-test.driver` : tentative
   d'activer RX avant publication, refusée par le guard. Nouveau stop prouvé,
   aucun backing engagé, natif restauré. Ce n'est pas un test simulé de matériel.
4. Chargement du programme valide, trois cycles supplémentaires unload/reload.
   Chaque retrait logue `bytes=0 metadata=0`, puis chaque nouveau backend
   engage à nouveau exactement 139264 octets. Le dernier est celui du navigateur.

Le programme invalide et le valide s'exécutent avec le même kernel/moteur,
prouvant que le comportement protocolaire vient de l'archive externe. Les fenêtres
MMIO sont recyclées après shootdown et ne s'accumulent pas à chaque reload.

Autres vérifications :

- Build kernel complet, puis link de la version finale ; ISO reconstruite.
- Host ASan/UBSan : parser/VM, ORA, broker, DMA ; 8 contextes × 100 cycles et
  40000 tentatives d'exclusivité broker. Slot ownership, completions, spans,
  offsets/overflow, refus MMIO/adresses, invalidation reset, blocage head-write,
  références stale/étrangères, quiescence/OOM/rollback et dernière référence.
- Sources UDP/DHCP réelles en tests host : pseudoheader destination, mauvais
  checksum refusé, WAIT_LINK, deadlines, OFFER/REQUEST/ACK, ACK invalide et
  timeout sans IP fabriquée.
- VM 1 et 4 : RTC réel, UART TX/RX externe, IRQ, wait/timeout, conflits, stale
  IDs, unload/reload, kill d'un waiter et variante baudrate via archive.
- Le navigateur a été fermé proprement avant les compteurs finaux et le smoke
  mémoire ORA ; aucun nouveau chantier graphique ou JSC n'a été ouvert.

## Preuves brutes et reproduction

Sous `build/e1000-tests/cpu1/` et `cpu4/` :

- `serial.log`, `network.pcap`, `pcap-summary.json`, `result.json` ;
- `dns.png`, `browser-https.png`, `exclusive-counters.png`, `frame-metrics.json`.

Captures : 89 frames / 16 paquets commençant par un record TLS en CPU1 ;
87 frames / 15 en CPU4. Le parseur est une synthèse PCAP, pas un déchiffreur
TLS ou un compteur exhaustif de records reconstitués. WebKit confirme lui-même
la réponse 200 et le document chargé. Les deltas natifs proviennent des compteurs
incrémentés dans les vraies fonctions C, depuis l'activation du backend final.

Régressions UART/RTC : `build/driver-tests/e1000-cpu1/` et `e1000-cpu4/`.
Host : `build/driver-tests/e1000-host.log`.
Diagnostics conservés : `build/e1000-dhcp-rejection.log`,
`build/e1000-datapath-timeout.log`, premiers logs/captures sous `build/e1000-*`.

```
bash scripts/test-driver-vm.sh
powershell -File scripts/run-driver-tests.ps1 -CpuCount 1 -EvidenceLabel e1000-
powershell -File scripts/run-driver-tests.ps1 -CpuCount 4 -EvidenceLabel e1000-
powershell -File scripts/run-network-driver-tests.ps1 -CpuCount 1
powershell -File scripts/run-network-driver-tests.ps1 -CpuCount 4
python scripts/analyse-network-driver-pcap.py build/e1000-tests/cpu4/network.pcap
```

SHA-256 testé :

- Kernel : `fdb459d70d7caefc5d5bcea8ad94cb1307ef2284d3e9f28179a9da05844a98cb`.
- ISO : `e10d358aac2ea5f66d88ec318f088be3028ba25e08d6e09d7b707a883cfe0416`.
- e1000.driver : `0abbe52906537401a0227220c9c75628a31837fd76eb7964b5266fcf85c76598`.

VM laissée éteinte, PIIX3, 4 CPU, IOMMU none, UART2 off et NIC tracing off.
Les limites matérielles/protocolaires et les fichiers changés sont décrits dans
[l'architecture du driver](e1000-driver.md). Pas de validation sur carte physique,
pas de nouvelle campagne de conformité réseau ou graphique générale.

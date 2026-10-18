# Préparation DMA du moteur `.driver`

## Audit et frontière de sécurité

Le moteur 1.0 n'acceptait que PIO/indexed-PIO pour RTC et UART. `drv_io_t`
passait un numéro de ressource, une largeur et un offset aux callbacks ; ni
ce callback ni le manifeste ne conféraient une autorisation matérielle.
`driver_manager.c:policy` limitait l'autorité aux deux grants hôte. Les leases
PCI/MMIO existaient pour les pilotes natifs, mais pas de grant BAR pour les
archives, pas de DMA, pas d'IOMMU. Le E1000 natif programme des adresses PMM.

Décision utilisateur appliquée : pas d'IOMMU obligatoire, ORA reste le contrat
de stockage/lifetime. L'archive ne voit ni une adresse CPU, ni une adresse
physique/bus, ni un pointeur ORA. Elle manipule des tokens locaux vers des
`ORA_STORAGE` mutables, uniques et jamais canonicalisés par égalité d'octets.

Sans IOMMU, ce contrat protège contre le **programme de description** hostile,
pas contre un périphérique matériel hostile ou un firmware qui ignore son
protocole. Le matériel et la politique hôte définissant les champs/registres
sûrs restent dans la base de confiance. Un champ indirect/chaîné interprété
comme une nouvelle adresse par le matériel doit être interdit par cette
politique. Renommer un tel champ « scalar » ne le rend pas sûr.

## Implémentation réelle

`driver_dma.c` est le moteur générique de stockage, références, schémas et
ownership. `driver_dma_memory.c` fournit l'allocation PMM contiguë, le contrôle
du mapping VMM/HHDM kernel-only et le zeroing de toute l'allocation arrondie.
Les traductions bus restent privées au fournisseur ; la version x86 actuelle
utilise l'adresse physique sur le bus PCI cohérent. ORA appelle le fournisseur
de destruction exactement à la dernière référence. ORA lui-même est inchangé.

- Grants hôte copiés à l'initialisation : type BUFFER/RING, droits CPU R/W, largeurs,
  taille maximale, stride et schéma de champs non chevauchants.
- `drv_dma_attach_io` croise ces grants avec le manifeste avant tout callback :
  demandes excessives refusées, droits/largeurs/tailles réduits à la demande,
  grants non demandés inaccessibles. Impossible d'insérer des callbacks PIO bruts.
  Les registres de publication doivent tous appartenir à la plage MMIO demandée
  en écriture. Un unique aperture de registres est supporté dans cette version.
- Adresses 64 bits et longueurs associées sont générées ensemble par `BIND`
  à partir d'une référence, d'un offset et d'une longueur validés.
- Un ring retient chaque buffer lié indépendamment de son token créateur.
- Bytecode : aucune lecture du champ adresse ; aucune écriture adresse/length/
  status ; scalaires écrits uniquement avec largeur exacte et masque autorisé.
  Les copies scratch↔DMA concernent exclusivement les BUFFER, jamais les rings.
- Registres : liste positive, largeur exacte et masque ; registres d'adresse,
  longueur et doorbell réservés à `PUBLISH`. Aucune écriture brute partielle
  permettant d'atteindre un champ protégé. Une lecture de registre d'adresse
  est également refusée.
- `PUBLISH` impose un ring par endpoint et des bindings complets, gèle le ring
  et ses buffers **avant** la première écriture matérielle. Des erreurs partielles
  ne rendent jamais ces pages recyclables automatiquement.
- `RECLAIM` exige une preuve de quiescence de l'adaptateur hôte. Le programme
  ne peut pas remplacer cette preuve par un booléen. Barrières CPU et validation
  des adresses/longueurs écrites par le matériel avant retour en ownership CPU.
  Longueur reçue excessive : erreur IO et longueur neutralisée après arrêt prouvé.
- Fermeture d'un objet device-owned et destruction du contexte : BUSY. Les
  références ORA explicitement conservées côté kernel empêchent également la
  destruction du contexte/provider, même après fermeture de leur token local.
- Quotas : 64 tokens vivants, 4096 descripteurs/ring, 8 champs/descripteur,
  16 MiB physiques maximum/contexte (le grant peut réduire cette limite),
  4 MiB de métadonnées propres au moteur, en plus du bookkeeping ORA borné par
  le nombre de stockages. Échecs d'allocation/validation : rollback avant publication.

Toutes les opérations et les retain/release explicites d'un objet DMA exigent
un contexte de travail différé : les instructions DMA sont refusées dans
l'entrée IRQ, dont le budget ne peut pas borner une allocation/zeroing ou un
parcours de ring au milieu d'un callback. Les IRQ peuvent acquitter les
registres autorisés ; le futur adaptateur réseau devra réveiller son worker.
Ces opérations utilisent
la transaction du propriétaire (`driver_instance.gate` dans un adaptateur).
Les contextes distincts sont indépendants ; ORA reste SMP-safe. Ce contrat
privé ne crée pas d'ABI userspace DMA/export. Le callback `quiesced` doit prouver
la fin de l'activité matérielle et la visibilité des écritures/completions, pas
seulement l'état d'un flag fourni par le bytecode. Le backend mémoire actuel
est réservé aux machines x86 PCI à DMA cohérent ; pas de support non-cohérent
annoncé. Aucun sleep, polling scheduler ou traduction IOMMU ajouté.

## Archive 1.1, compatibilité 1.0

Layout des records inchangé. Minor=1 et engine_min=2 nécessaires à DMA.
Les anciennes archives 1.0 et numéros d'opcodes restent inchangés.
`DRV_DMA_OP` (23) : `aux` sélectionne l'opération, `immediate` sélectionne le
premier de huit registres d'entrée consécutifs (0..24), `dst` reçoit le résultat.
`a=b=0`. Aucun pointeur dans les arguments.

| Opération | aux | Registres d'entrée |
|---|---:|---|
| ALLOC | 1 | grant hôte demandé via selector d'une ressource DMA, taille |
| RELEASE | 2 | référence |
| READ | 3 | référence, offset, largeur |
| WRITE | 4 | référence, offset, largeur, valeur |
| BIND | 5 | ring, buffer, index descripteur, index champ adresse, offset buffer, longueur |
| PUBLISH | 6 | ring, endpoint autorisé |
| RECLAIM | 7 | ring |
| COPY_IN / COPY_OUT | 8 / 9 | buffer, offset buffer, offset scratch, longueur |

Les tokens comportent une identité de contexte et un compteur monotone ;
aucune réutilisation après fermeture ni wrapping accepté. Ce sont des références
locales à la VM, pas une nouvelle capability publique transférable. Une référence
connue est conservée sans hash. L'archive `dma-memory-test.driver` exerce
ALLOC/WRITE/READ/RELEASE avec le vrai stockage PMM/VMM/ORA sous KyIlOS.
Elle ne possède **aucun droit matériel** et n'est pas autoloadée comme périphérique.

## Ce qui n'est pas encore activé

**État historique de la passe de préparation.** Le support réseau suivant est
maintenant intégré ; voir [E1000 par archive](e1000-driver.md) et ses preuves.

Le manager continue à refuser MMIO/PCI/DMA dans ses grants RTC/UART. Le parser
admet DMA et MMIO 1.1 (MMIO exige également le contrat DMA/engine_min=2), mais
ne confère aucune autorité ; pas de callback = UNSUPPORTED.
Pas de changement du pilote réseau natif ou de son IRQ. Pas de `e1000.driver`
fonctionnel annoncé dans cette passe de préparation.

Pour connecter un E1000 : grants BAR/PCI exclusifs, politique hôte de registres
et descripteurs vérifiée contre le protocole, preuve de stop/completion, adaptation
réseau/IRQ et handoff du fallback. Le moteur actuel fournit un ownership de ring
entier ; la restitution indépendante des slots TX/RX d'un ring actif reste à
implémenter avant d'en faire un périphérique réseau continu. Les registres du
modèle endpoint actuel sont base low/high, longueur, doorbell à valeur autorisée
fixe ; les producteurs/consommateurs/tails dynamiques viendront avec cette gestion
des slots. Aucun E1000-specific opcode ne sera nécessaire.

## Validation

Tests host : `scripts/test-driver-vm.sh`, ASan/UBSan sur les sources de production
du parser, interpréteur, ORA, DMA et broker. Registres/DMA matériel simulés
uniquement dans le fixture de test, explicitement distingués de l'exécution VM.
Tests : archives 1.0, archive 1.1 réelle, schémas malformés, permissions, références
stale/étrangères, overflow, rétention/dernière référence, erreurs MMIO partielles,
quiescence refusée, longueur RX corrompue, OOM aux étapes d'allocation, 8 contextes
concurrents × 100 cycles. Broker : 8 threads, 40000 tentatives d'exclusivité.

Validation VM et hashes des artefacts : voir le rapport de résultats associé.

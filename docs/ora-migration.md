# MRAC → ORA : migration et validation

État du 4 octobre 2026. ORA signifie **Optimize RAM Allocation**.
Les sept étapes sont implémentées ; l'ABI utilisateur historique demeure.
Ce document distingue les contrats du code et les validations effectivement exécutées.

## Répartition finale des responsabilités

| Domaine | Identité et politique | Stockage et durée de vie |
|---|---|---|
| Allocations noyau | `kernel_allocator`, adresse et arène d'allocation | Allocation mutable brute, libération explicite ; le PID est un attribut de comptabilité, pas un destructeur de données vivantes |
| Contenu immutable | Le sous-système choisit de canonicaliser ou non | `ORA_CONTENT`, référence owning opaque, empreinte privée conservée, égalité exacte, destruction à la dernière référence |
| Backing mutable | Provider natif : SHM, mapping partagé/fichier, Surface, JIT | `ORA_STORAGE` conserve le provider et appelle son destructeur exactement une fois ; aucune déduplication par les octets |
| Clés/version applicatives | `versioned_store`, propriétaire process et clé explicite | Chaque version détient une référence de contenu ORA |
| Mémoire d'un process | `process_memory`, namespace, régions, VA, protections et provenance | Régions avec vues ORA owning et références de providers ; pages privées matérialisées détenues par l'inventaire/VMM |
| Fichiers/cache | Entrée FS/path exact, backend disque ou ISO | Référence de cache et leases de lecture owning ; une ancienne lease reste valide après remplacement |
| Blobs IPC nommés | Catalogue IPC propre, nom comparé exactement | Contenu immutable ORA, acquisition sous verrou puis lease |
| Préférences Spotlight | Catalogue Spotlight propre, tuple utilisateur/requête/path encodé sans ambiguïté | Score immutable actuel ORA, remplacement de la référence |

ORA n'est ni un registre de PID/VA, ni un gestionnaire de permissions, ni un namespace
universel. Une référence ORA est interne au kernel, **pas** un handle utilisateur.
Les capabilities des objets natifs, les droits et les mappings restent contrôlés par
leurs sous-systèmes. Le compteur ORA remplace le compteur de lifetime des providers
de pages, sans remplacer leurs algorithmes de pagination.

Les pages privées VMM, les mappings PTE et les backings mutables ne deviennent pas
des contenus immutables par leur simple existence. ORA ne canonicalise pas toute
la RAM ni les pages mutables du système. Les pages physiques restent allouées/libérées
par PMM/VMM ou par le destructeur de leur provider, avec un propriétaire unique.

## Les sept étapes et leurs commits

| Étape | Changement | Commit |
|---|---|---|
| 1 | Extraction de l'allocateur brut ; heap sans dépendance à MRAC ; séparation des plages heap/MMIO audio | `aa354da` |
| 2 | Moteur ORA immutable/mutable, refs, vues, empreinte mémorisée, comparaison exacte et publication atomique | `85fff52` |
| 3 | Service de clés et versions applicatives au-dessus d'ORA ; traduction des anciens syscalls sans changement de layout/numérotation | `609cc17` |
| 4 | Inventaire process-owned de namespaces/régions ; suppression des bindings MRAC comme source de vérité | `316565c` |
| 5 | Vues de contenu owning, providers ORA pour backings partagés/fichier, fork/splits/détachement des projections immutables | `beedc1d` |
| 6 | Identités FS/cache, blobs IPC et Spotlight distinctes ; migration des lecteurs et loaders vers des leases explicites | `7d8b019` |
| 7 | Suppression de `mrac.c`, `mrac.h`, index universel et APIs internes mortes | `5dafe8f` |

Chaque étape a été compilée et bootée dans le workspace. Les corrections de validation
qui suivent ces étapes durcissent les mêmes contrats ; elles n'introduisent pas une
huitième architecture.

Durcissement allocateur/inventaire/FS : `7e3317d`. Protection des buffers ABI et
smoke natif dédié : `a92e516`. Les ajustements des anciens smokes et ce rapport
sont regroupés dans le commit de validation final.

## Contrat de contenu et concurrence

`ora_create[_zero_extended]` copie un candidat privé et calcule FNV dans la même
passe sur **les octets effectivement copiés**. Le padding de la dernière page est
zéro. L'empreinte est conservée dans l'objet opaque ; les fonctions de fingerprint
sont privées et remplaçables. Aucun successeur de FNV, Merkle tree ou chunking ajouté.

La publication dans l'index canonical se fait sous verrou IRQ-safe/atomique. Une
correspondance d'empreinte et de longueur ne suffit jamais : `memcmp` départage
exactement. Deux créations concurrentes identiques convergent vers un seul contenu
publié. Un candidat en échec n'est jamais visible. `ORA_UNIQUE` contourne cet index.

`retain`, acquisition d'une vue, lookup d'une clé connue, rebind d'une référence,
rename de catalogue, split et fork d'une projection immutable ne recalculent pas H.
Une capture nouvelle d'octets mutables est une nouvelle ingestion et peut calculer H.
Deux SHM égales restent deux objets mutables indépendants.

Un appel à `retain` nécessite une référence déjà détenue ou un verrou de registre
qui la protège ; ORA n'offre pas de weak reference vers un pointeur déjà libéré.
Les vues de sortie doivent être vides/initialisées à zéro. Leur remplacement impose
de libérer l'ancienne référence. La dernière release retire l'objet de l'index sous
verrou puis détruit ses données/provider **hors** du verrou ORA, permettant les
destructeurs réentrants. Les compteurs saturés font échouer l'acquisition.

Les registres applicatif et nommés pinent les références sous leur propre verrou.
FS et `process_memory` utilisent des transactions récursives IRQ-safe/non dormantes.
Les handlers mmap/mprotect/munmap/heap couvrent publication d'inventaire et modifications
PTE avec la transaction mémoire, y compris entre threads partageant le process.
Les handlers du service versionné copient aussi leurs requêtes et tiennent cette
transaction entre validation des buffers et copie ; le retrait du mapping concurrent
ne peut donc pas invalider une adresse déjà validée sur ce chemin. L'ingestion ORA
lit chaque octet une seule fois par load atomique relaxed, et hash/copie la même valeur.

## Mémoire, partage et rollback

`process_memory` conserve régions PRIVATE/SHARED, réservations, lazy, fichier, Surface
et JIT avec leur provenance. Les offsets des vues et providers sont ajustés aux splits.
Tous les morceaux et références d'un split sont préparés avant sa publication.
Un changement de protection ne crée pas une région dans un trou d'inventaire.
Le retrait de régions entières compacte l'inventaire sans allocation supplémentaire :
le rollback d'une création reste disponible sous OOM.

Une projection immutable directe est read-only. Pour la rendre writable, les copies
physiques privées sont préparées avant de modifier l'ownership/PTE ; le contenu ORA
source reste pincé. Le nouvel ownership ne libère jamais les pages de l'ancien contenu.
Les projections partagées n'adoptent pas les pages physiques du contenu canonical.

Fork partage les vues immutables et les véritables providers SHARED ; les pages PRIVATE
engagées sont copiées indépendamment. Le fork sparse parcourt les pages matérialisées,
pas toutes les pages virtuelles. Les réservations 128 Gio et arènes JIT 1 Gio demeurent
sparse. Le quota JIT, les alias RW/NX et RX/RO, les capacités et l'exécution async
restent les contrats natifs précédents.

Un snapshot de namespace conserve l'inventaire et ses références ; il ne prétend pas
figer le contenu d'un backing mutable partagé. La capture explicite d'une région
mutable est distincte. Aucun nouveau COW Unix ni stop-the-world global introduit.

L'allocateur brut retire une arène lors du cleanup du propriétaire sans libérer ses
blocs vivants. Ils restent accessibles aux détenteurs légitimes, puis sont libérés
explicitement. Les blocs dédiés alignés ne sont pas recyclés comme petits blocs ;
leurs dernières libérations rendent les pages. Une plage VA noyau disjointe réserve
l'adressage du heap sans engagement physique massif.

## FS et lecteurs

Le FS garde son format disque, ses extents, ses métadonnées et ses contrôles d'accès.
Un write durable publie le cache seulement après le commit disque. Un cache OOM
invalide l'ancienne copie et laisse le fichier disque valide ; un write RAM-only en
OOM restaure l'ancienne entrée. Un fichier vide n'est pas une suppression.

Le fichier writable VexFS masque la copie ISO/residente du même path. Le fast path
resident refuse cette entrée pour laisser le caller utiliser le backend writable.
Les caches sont attachés à ces identités FS distinctes, pas à un hash de path MRAC.
`fs_read_view` et `fs_read_boot_module_view` fournissent une lease owning exacte.
Les FD résidents la gardent jusqu'à leur dernière référence ; les buffers de FD
mutables sont séparés. ELF/PE, installateur, éditeur, shell et autres lecteurs
libèrent leurs leases ; le loader ELF travaille sur sa copie privée avant relocation.

Le cache ISO reste borné à 64 Mio. Les métadonnées bootstrap de l'allocateur restent
un high-water cache avec recyclage des descripteurs ; ce n'est pas un nombre d'objets
ORA vivants. Les objets `/dev/shm` volatils ne deviennent pas des fichiers durables.

## Tests exécutés

### Hôte : vrai moteur ORA et vrais services

Commande : `scripts/test-ora-storage.ps1` (clang Windows).
Elle compile `ora.c`, `versioned_store.c` et `named_content.c` ; seul le provider
d'allocation hôte remplace PMM/IRQ, pas le moteur de stockage. Résultat : **PASS**.

- A/B → X puis rebind A → Y ; B et les anciennes leases conservent X.
- Collision forcée de deux contenus différents, égalité exacte et partage des égaux.
- 10 000 retain/release sans nouveau fingerprint ; vues/offsets/overflows et sortie occupée.
- Providers mutables égaux indépendants, disparition du créateur, destruction unique.
- Injection OOM à chaque allocation d'ingestion, de création de version et de catalogue ; rollback et compteur final d'allocations/objets à zéro.
- 8 threads × 4 000 ingestions avec ancre puis sans ancre : publication concurrente et dernière release/création concurrente.
- Ingestion avec source modifiée concurremment : empreinte correspondant au contenu copié, pas à une seconde lecture de la source.
- 8 threads × 2 000 bind/acquire/remove/destroy sur catalogue ; références connues sans hash et zéro allocation finale.
- Versions, isolation des propriétaires, rename, rebind et leases après remove/reap.

### VM VirtualBox : code kernel et binaires réels

Kernel reconstruit avec `scripts/build.ps1 -NoIso`, ISO repackée, boot avec
`auto_smoke`. Les nouveaux tests sont intégrés au chemin auto-smoke existant.
Le dernier durcissement a recompilé `ora.c`, `vex_syscall.c` et `main.c` avec les mêmes
flags, puis relinké le kernel et rejoué les smokes VM.
Le smoke Surface et le smoke glibc/JSC modifiés ont été reconstruits et stagés.
Le smoke natif du service versionné est construit par `scripts/build-ora-test.ps1`.
Les anciens binaires natifs restent valides : leur ABI n'a pas changé.

- ORA kernel : partage, vues projetées, zéro rehash, arènes retirées et 64 destructions de créateur/provider.
- Inventaire : splits/protections/unmap, 16 forks de vues immutables sans rehash, détachement writable, deux SHM égales indépendantes, deux MAP_SHARED read-only indépendants, libération des derniers providers sans toucher leurs voisins.
- Réservation lazy 128 Gio et fork avec seulement deux pages touchées : engagement borné contrôlé en pages PMM.
- FS : vraie ingestion residente, writable overlay, ancienne lecture conservée après remplacement et suppression, aucun objet ORA résiduel du test.
- ABI versionnée : 2 048 clés numériques alignées, put/get/replacement/remove.
- ABI ring 3 : versions/rebind/clé indépendante, ancienne version, metadata de buffer court, remove-current atomique, refus des requêtes/buffers kernel, invalides et readonly.
- Isolation user/kernel, PROT_NONE/NX, buffers invalides, lazy privé/partagé 128 Gio, JIT lazy 1 Gio et W^X/dual aliases.
- Threads/TLS/FPU, futex/kill, fork/exec, pipes/EOF, ownership mémoire, FD et spawn glibc.
- Signaux/fautes synchrones et sigreturn concurrent : 1 200 reprises exactes + 10 800 syscalls ordinaires.
- PE réel : `pe_probe.exe` atteint `Vex PE probe: done`.
- File-backed mmap, offsets, trous/ftruncate/pread/pwrite, gros mapping 128 Mio sparse.
- Surface : capability transfer, droits/mapping, lifetime ; SEQPACKET/SCM_RIGHTS.
- JSC réel : Baseline/DFG/FTL avec exécution vérifiée, plus compilation concurrente.
- Vulkan : compute/SPIR-V/descriptors/MSAA, ressources en vol, queues async, timelines, WSI multi-image et PMM stable sur 128 cycles d'event allocation/free.
- Boot graphique dédié : EGL/GLES **1 290 checks PASS**, six présentations Surface réelles ; clear/triangle/texture/FBO/blend/depth/stencil, contextes partagés, threads, resize/completion/readback et lifetime.

Traces conservées dans `build/ora-complete-serial.log` et
`build/ora-abi-final-serial.log` ainsi que `build/ora-gl-regression-serial.log` ;
reconstruction complète dans `build/ora-complete-build.log`.
Les traces intermédiaires `build/ora-step*-serial.log` et `build/ora-*-build.log`
conservent la progression et les échecs corrigés. Ces artifacts de build sont locaux.

La validation Vulkan finit toujours volontairement rouge sur son critère de couverture
globale « programmable renderer and WSI still incomplete ». Ce résultat n'est ni masqué
ni une déclaration de conformité. Les fautes userspace attendues des smokes restent
fatales/reprises selon leur contrat, sans panic kernel observé.

### Corrections révélées par les régressions

- Collision de la plage MMIO audio avec l'ancien heap ; espaces séparés.
- Faux ENOMEM de grosses allocations dû à l'étroite ancienne plage VA brute, malgré de la RAM libre ; plage noyau disjointe.
- Réutilisation d'un bloc dédié page-aligned comme petit bloc avec un header différent : lookup/realloc invalides, notamment ftruncate puis pwrite ; allocation/layout corrigés et reproduction kernel ajoutée.
- Inventaire actif/commit dépendant d'une copie allouée : sous OOM il pouvait perdre son identité ; queries/activation sans allocation.
- Rollback et changement de protection après splits : préparation globale et publication cohérente, retrait entier sans allocation.
- Test Surface obsolète : flags GPU utilisés dans le domaine Surface, unmap générique et fermeture du dernier handle encore mappé. Le test respecte maintenant le contrat typé, vérifie le refus de fermeture, le lifetime après disparition du producteur et la destruction finale.
- Smoke sigreturn lancé depuis boot PID 0 puis wait invalide : parent schedulé réel, statut enfant réellement collecté.
- Smoke JSC concurrent : boucle finie terminant avant la publication FTL. Le test continue du calcul réel avec oracle jusqu'à l'entrée effective dans le dernier tier, ou échoue sur deadline. Aucun changement JSC/threshold/protection.

## Limites de validation

Les races du moteur et des catalogues ont été stressées avec de vrais threads hôte.
La VM de validation est mono-vCPU : aucune validation SMP intégrale kernel n'est
revendiquée. L'OOM du moteur/services est injecté à l'hôte ; un épuisement physique
global de la VM et un failpoint à chaque allocation de page/VMM n'ont pas été réalisés.
La migration conserve le format PE et compile son loader, mais aucun programme PE
complexe n'est revendiqué sur ces runs. Le lancement réel WebKit est inclus dans les
smokes ; ce lot ne mesure pas une nouvelle campagne HTTP/HTTPS/navigation.

## Comparaison

**Avant :** allocateur brut, contenus, versions, clés process/VA/path et lifetime
réunis dans MRAC ; bindings et pointeurs empruntés servaient d'inventaires.

**Après :** allocateur mutable indépendant ; ORA stocke les contenus immutables et
les références de providers ; chaque sous-système détient ses identités/inventaires ;
publication et leases explicites ; application keys/versioning uniquement au-dessus
d'ORA ; absence d'index universel `(pid,key_hash)`.

Les noms publics `VEX_SYS_MRAC_*` sont conservés pour l'ABI historique. Les noms
`security_mrac_*` désignent encore la politique de sécurité existante, qui n'a pas
été remplacée par un moteur de stockage. Ils n'impliquent pas le retour de l'ancien MRAC.

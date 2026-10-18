# FlowSched — implémentation et validation, 5 octobre 2026

FlowSched remplace la sélection priorité statique/round-robin. Ce document
décrit le code et les observations, pas une garantie temps réel ni une
validation SMP. La VM de référence utilise un vCPU et 4 Gio.

## Politique et invariants

- Deux niveaux : une entité par `process_group`, puis ses threads exécutables.
  Les tâches noyau sans groupe de ressources ont une entité privée. Un clone
  thread rejoint l'entité de son groupe ; fork crée un groupe distinct.
- Service réellement écoulé en nanosecondes, comptabilité avant les changements
  d'état, horloges d'entitlement incrémentées selon le nombre d'entités actives.
  L'entitlement d'un groupe est sa part égale du CPU actif ; celui d'un thread
  est sa part du service effectivement consommé par son groupe.
- Éligibilité : service reçu inférieur ou égal au temps virtuel moyen des
  entités actives. Parmi les entités éligibles, choisir la première deadline
  virtuelle, d'abord pour le groupe puis pour le thread.
- Requête ordinaire de 2 ms. Une urgence raccourcit la requête/deadline, mais
  ne modifie ni le poids ni l'éligibilité. Les petites charges successives
  conservent une requête ordinaire non consommée.
- Un dormeur conserve sa dette ; son crédit au réveil est borné à 4 ms derrière
  le temps virtuel actif. L'urgence répétée ne crée pas de droits CPU supplémentaires.
- Treaps intrusifs déterministes, indexés par service/ordre, augmentés de la
  meilleure deadline ; requête de préfixe éligible et sélection attendue O(log N).
  Les sommes utilisent 128 bits pour éviter le débordement quand le nombre
  d'entités augmente. Pas de parcours de `process_list` pour sélectionner.
- Les timers ont leur propre index de deadlines. Les TID ont un index hash.
  Une liste de zombies remplace le parcours systématique des tâches vivantes
  par le reaper. L'inventaire reste utilisé pour des opérations froides
  (kill-group, rapports, unlink/destruction).
- Les contextes de flow portent identité, origine, naissance et deadline.
  INPUT, TIMER, I/O, IPC, EVENT et SURFACE restent des concepts internes.
  Leur durée de validité est bornée à 250 ms. Le budget de latence est représenté
  par `deadline_ns - born_ns`, pas par un crédit CPU additionnel.
- Une dépendance lie un waiter au bloqueur effectivement identifié. Les liens
  entrants/sortants propagent l'urgence transitive lors des événements ; ils
  sont retirés au réveil, à l'annulation et à la destruction. Cycles et chaînes
  dépassant la limite de 64 sont refusés. La comptabilité ne reconstruit pas
  de graphe global : elle utilise le contexte effectif mis en cache, sauf
  actualisation locale lors de son expiration.
  Un join natif dont la dépendance formerait un cycle retourne une erreur
  avant sommeil ; les IRQ input/I/O ne sont pas attribuées au TID interrompu.
- L'ordre des waiters combine urgence et ancienneté : après 8 ms, l'ancienneté
  devient décisive. Un propriétaire ne reçoit jamais une priorité permanente.

L'absence de starvation repose sur l'éligibilité/service avec un ensemble fini
d'entités et des passages noyau qui finissent. Ce n'est pas une promesse de
deadline dure face à un noyau non préemptible ou une panne du timer virtuel.

## Intégration réelle

`kernel/src/flowsched.c` contient la politique testée aussi en hosted.
`scheduler.c` gère l'accounting, les transitions, les index et les dépendances.
Les champs priority et Linux policy demeurent des métadonnées de compatibilité ;
ils ne pilotent plus le partage CPU. Linux SCHED_OTHER utilise FlowSched ;
FIFO/RR ne sont pas prétendus implémentés et retournent ENOTSUP après validation.

| Chemin | Intégration |
|---|---|
| Clavier/souris → GUI | Flow INPUT depuis IRQ, wakeup et demande de préemption ; aucun polling permanent |
| IRQ réseau → worker | Flow I/O distinct de celui de la tâche interrompue ; safe point après batch |
| Message IPC | Contexte conservé par message puis adopté à réception ; donation sur backpressure uniquement vers un consommateur observé et non ambigu |
| Événements/compteurs | Propagation du contexte lors du wake ; pas de propriétaire fictif pour un compteur sans serveur identifié |
| Surface/compositor | Contexte porté par la présentation, adopté au dequeue ; attente completion vers le véritable service GUI |
| Spawn, join, wait d'un enfant identifié | Donation vers le thread enfant/bloqueur connu, retrait au réveil ; inscription join/wait atomique vis-à-vis des IRQ |
| Futex ordinaire | Check/inscription atomiques ; wake fini ordonné par urgence/ancienneté ; broadcast en un parcours |
| Futex PI Linux | Ledger explicite PRIVATE, handoff, chaînes, owner-exit, unmap et kill ; aucune interprétation d'un futex ordinaire comme TID |

Le propriétaire d'un futex ordinaire ne peut pas être deviné depuis son contenu.
La traduction PI vit dans `compat_linux_futex_pi.c` : LOCK_PI, TRYLOCK_PI et
UNLOCK_PI privés non temporisés. Timed/shared PI et variantes non représentées
échouent explicitement. Le mot Linux WAITERS/OWNER_DIED/TID n'entre pas dans
l'ABI native. Le chemin unmap rapide d'une région entière a été corrigé pour
annuler aussi ses waiters PI. `exit_group` retire également les waiters
event-counter et Surface de ses threads arrêtés.

Les nouveaux threads adoptent le flow valide du créateur, sans son entitlement.
Un serveur inconnu ou plusieurs consommateurs ambigus ne reçoivent pas une
donation arbitraire. Les locks noyau IRQ-off non dormants n'ont pas un thread
propriétaire distinct à accélérer. Aucun deuxième moteur de locks/sauvegarde
de contexte n'a été créé.

## Temps, préemption et telemetry

`sched_clock.c` mesure le TSC via CPUID.15 lorsque le ratio/crystal est complet,
sinon via deux fronts de seconde RTC. Pas de fréquence CPU supposée pour le
scheduler. Le LAPIC est calibré sur cette horloge et demande une interruption
de 1 ms. VirtualBox n'assure pas cette précision effective : le retard des
réveils périodiques mesuré reste souvent de 10–20 ms.

Les ticks ABI historiques conservent leur unité de 10 ms mais suivent le temps
mesuré ; `lv_tick_inc` suit les millisecondes réellement écoulées, plus un
incrément arbitraire de 10 par IRQ. Les deadlines internes de sommeil sont
relatives au temps nanoseconde au moment de l'inscription. L'addition des ticks
est saturée et atomique vis-à-vis des IRQ. Le bug préexistant `nanosleep(req,req)`
qui écrasait la requête avant de la lire a également été corrigé et testé.

Un réveil peut préempter du code CPL3 après service/EOI de l'IRQ. En kernel,
préemption aux frontières syscall et safe points explicites sans transaction
en cours (GUI, worker réseau, construction ELF encore non publiée).
`kernel_guard` indique explicitement la profondeur non préemptible.
Le switch CR3/FS_BASE/FPU existant est conservé ; une mesure sur la pile entrante
est prise après sa restauration et avant la reprise.

`flowstats` écrit sur le log série : CPU et entitlement par groupe/thread,
histogrammes ready→selected et input→selected, maximum selected→running,
switches, propagations, dépendances créées, cycles refusés, cycles TSC du chemin
`schedule()` avant le switch, durée/adresse de la section non préemptible maximale.
Les compteurs d'inversions sont des edges de dépendance créés, pas une preuve
que chaque edge était une inversion observée en wall-clock.

Les percentiles noyau sont les bornes supérieures de classes logarithmiques,
pas des quantiles exacts. L'input mesuré est le plus ancien input pending
coalescé jusqu'à sélection GUI : ce n'est ni chaque paquet souris ni la latence
complète input→pixels. Le coût instrumenté inclut sélection/accounting/reaping,
pas tous les IRQ, wakes et coûts FPU/CR3. Le rapport parcourt les tâches hors
hot path et peut perturber la mesure ; la comparaison utilise la même telemetry.
`top` utilise CPU_ns/idle_ns sur une fenêtre réelle, tout en conservant les
structures/ticks de l'ABI d'inventaire publique.

## Mesures avant/après

Même VM 1 vCPU / 4 Gio. Le baseline réintroduit uniquement l'ancienne sélection
priorité/RR et sa tranche de 10 ms dans un kernel instrumenté utilisant la même
horloge et les mêmes hooks. Ce n'est pas une comparaison avec un ancien ISO
non instrumenté. Il reste sous `build/flowsched-baseline`, jamais dans le build
normal. Un premier essai avec le bug d'alias nanosleep a été invalidé ; les
chiffres ci-dessous ne l'utilisent pas.

Test réel glibc : deux processus, l'un avec un worker CPU, l'autre avec 30,
démarrage bloquant, données MAP_SHARED héritées, parent avec 200 attentes de
10 ms. Les latences sont triées exactement par le programme.

| Mesure | Ancienne politique | FlowSched, dernier run desktop |
|---|---:|---:|
| Itérations groupe mono-thread | 926 379 | 544 882 |
| Itérations groupe 30 threads | 29 319 583 | 540 913 |
| Rapport multi/mono | 31,65× | 0,993× |
| Durée des 200 attentes | 116,472 s | 4,126 s |
| Retard de réveil p50 | 584,010 ms | 10,829 ms |
| Retard p95 | 635,312 ms | 20,625 ms |
| Retard p99 | 648,445 ms | 20,836 ms |
| Retard maximum | 655,438 ms | 31,620 ms |

Un autre run final avait un maximum isolé de 361 ms malgré un p99 de 20,9 ms :
la queue de latence n'est donc pas éliminée. Les itérations sont un proxy de
débit ; CPU/entitlement en nanosecondes sont aussi enregistrés.
Le dernier smoke sur les dernières sources donne 543 548/544 870 itérations,
p99 31,579 ms et maximum 207,468 ms, avec tous les cas PI passants
(`flowsched-runtime-final-source.log`). La variation des queues de latence est
conservée dans le rapport, plutôt que de prétendre un plafond de 32 ms.
Le baseline dépassait le premier timeout du harness (90 s), puis terminait
correctement avec les deux exit_status à zéro. Le timeout est désormais 180 s.

Vrai `/webkit`, même recherche HTTPS DuckDuckGo `pomme fruit`, 600 événements
PS/2 alternés ±3 px injectés avec la même cadence hôte, pendant la navigation :

| Mesure | Ancienne politique | FlowSched |
|---|---:|---:|
| Input→selected GUI p50, borne | ≤1,024 ms | ≤0,032 ms |
| p95, borne | ≤32,768 ms | ≤8,192 ms |
| p99, borne | ≤131,072 ms | ≤131,072 ms |
| Maximum exact | 305,310 ms | 126,251 ms |
| TTFB document | 71 ms | 82 ms |
| Transfert document | 2 066 ms | 1 078 ms |
| DOMInteractive | 11 604 ms | 9 240 ms |
| DOMContentLoaded | 16 745 ms | 11 781 ms |
| First paint | 16 780 ms | 11 803 ms |
| Document complete | 35 059 ms | 19 800 ms |

Une seule navigation distante par politique, payloads proches mais non identiques
(7 625/7 660 octets encodés), réseau live, caches et charge hôte variables :
ne pas attribuer toute la variation du chargement au scheduler. Le p99 input
reste dans la même classe. Le WPE frame ACK atteint 8 et la capture montre les
résultats/images réellement rendus, pas un snapshot fabriqué.
Un nouveau lancement depuis le desktop normal après les dernières corrections
atteint aussi la recherche réelle : DOMInteractive 8 857 ms, first paint
11 192 ms, complete 20 007 ms. Il confirme le chemin, sans remplacer la paire
de comparaison ci-dessus.

## Tests exécutés et preuves

- `scripts/test-flowsched.ps1` : code exact du policy core, 1 vs 30, trois hogs,
  renouvellement malveillant d'urgence, donation A→B→C, cycle/teardown, timers,
  crédit dormeur borné, ordre âgé/urgent, rebind et somme virtuelle >INT64_MAX : PASS.
- Runtime VM : vraie glibc/pthreads, 1 vs 30 + réveils, alias nanosleep,
  chaîne PTHREAD_PRIO_INHERIT, EFAULT sur unmap pendant attente, handoff owner
  death, huit kills de processus avec waiter/owner vivants : PASS.
- Régressions VM : ORA inventory/storage/views/no-rehash, FS leases, ABI native,
  isolation/pointeurs/W^X/NX/PROT_NONE/lazy128Gio, private/shared/fork, TLS/FPU,
  futex, signaux 32/33, SEQPACKET/SCM_RIGHTS, spawn/pipe/exec/FD lifetime : PASS.
- Sigreturn : 1 200 reprises précises + 10 800 syscalls ordinaires : PASS.
  Les fautes volontaires dans ce smoke ne sont pas des panics kernel.
- JSC : vrais tiers Baseline/DFG/FTL via aliases W^X, trois modes/checksums : PASS.
- EGL/GLES : 1 290 checks, async/thread/lifetime et Surface : PASS.
- Vulkan : les sous-tests existants core/compute/async WSI passent ; le critère
  final de `vk_test` reste explicitement incomplet, exit=1. Aucun faux vert global.
- Hosted : ORA et les 14 groupes d'input/browser/window existants : PASS.
- Desktop : clavier réel (commandes `top`, `run`, `flowstats`, `paint`), Paint
  dessine, fenêtre déplacée/redimensionnée, navigateur déplacé/redimensionné,
  contenu visible. Ce sont des injections PS/2 et captures, pas une évaluation
  humaine de la fluidité perçue. `top` au repos : busy 0,2 %, idle 99,7 %, GUI 0,2 %.
- Dernier kernel : lancement navigateur, fermeture normale avec `graceful
  shutdown complete`, puis `ps` ne retrouve que les six tâches de base ; `top`
  mesure busy 0,1 %, idle 99,8 %. La telemetry de cette session startup/fermeture
  observe aussi un maximum input de 238,759 ms, hors scénario comparatif à 600
  événements. Il reste donc des pointes, même si le test comparatif s'améliore.

Preuves locales (fichiers build ignorés par Git) :
`flowsched-baseline/comparable-runtime.log`, `flowsched-baseline/webkit-navigation.log`,
`flowsched-runtime-final.log`, `flowsched-desktop-runtime.log`,
`flowsched-runtime-final-source.log`, `flowsched-final-browser.log`,
`flowsched-browser-cleanup.log`, `flowsched-close-ps-top.png`,
`flowsched-webkit-navigation.log`, `flowsched-webkit-interactions.log`,
`flowsched-regressions-vm.log`, `flowsched-graphics-vm.log`, `flowsched-host-input.log`,
`flowsched-webkit-navigation-presented.png`, `flowsched-idle-top.png`,
`flowsched-paint-final-drag.png`, `flowsched-paint-final-resize.png`.

Reproduction : `scripts/run-flowsched-tests.ps1` build hosted/runtime/kernel/ISO,
boot et collecte le résultat VM, puis arrête la VM. Cet outil laisse un ISO
de test : reconstruire avec `scripts/build.ps1 -SkipWebKit -BootMode Desktop`
pour le desktop normal. Le paramètre `-BootMode FlowSched` est explicite ;
un marker de test ancien ne doit pas sélectionner ce mode dans un build Desktop.

## Limites et travail restant

La politique complète est intégrée au scheduler réel, sans changer l'ABI native
publique ou l'ownership ORA/process/FD/Surface. Il n'y a pas encore migration,
load balancing ou activation SMP : `flow_cpu_t` isole les index futurs par CPU,
mais le dispatch effectif et les guards IRQ sont BSP. Pas de claim SMP testé.

Les longues transactions kernel ne deviennent pas préemptibles par magie.
Le maximum de ~554–624 ms mesuré depuis boot est symbolisé dans
`fs_register_boot_module` sous `kernel_guard` ; il inclut l'ingestion initiale,
pas nécessairement une transaction pendant navigation. Les pointes input
restantes n'ont pas toutes été attribuées à un site précis. Le reaper peut aussi
faire une destruction longue dans `schedule()`. Aucune transaction mutation
ORA/VMM/FS n'a été coupée arbitrairement pour obtenir une jolie latence.

Reader/writer : les octets d'une vue ORA immutable retenue peuvent déjà être
consommés après sortie de la transaction de metadata (`fs_read_view`). Publier,
rebind, retirer des références ou modifier un backing mutable exige une vraie
sérialisation. Ajouter des RW locks globaux sur BSP ne supprimerait pas ce coût
et risquerait de rendre des mutations préemptibles sans contrat ; cela n'a pas
été fait. Une future réduction des longues transactions devra distinguer travail
privé/préparation et publication atomique, avec tests d'ownership.

Les waiters sans propriétaire identifiable n'ont pas de proxy inventé. PI timed/
shared, robust-list générale et binding public d'un executor GPU ne sont pas
annoncés. Les deadlines de flow sont des objectifs d'urgence, pas des deadlines
temps réel garanties. Des shaders WebKit non supportés produisent toujours
une erreur explicite ; ce lot ne modifie ni renderer, ni JIT, ni réseau général.

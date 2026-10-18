# Desktop SMP dynamique — checkpoint de validation 1–4 CPU

## État et périmètre

Le dispatch général des applications est activé en production. Le kernel détecte
le nombre de CPU au boot ; aucune recompilation ni hypothèse de puissance de deux.
Le BSP reste propriétaire des IRQ périphériques existantes, pas des applications.
Ce checkpoint vise le desktop VirtualBox 1, 2, 3 et 4 vCPU. Il ne certifie ni tous
les syscalls ni tous les matériels. Lot 37/Vulkan reste en pause.

La clôture reste suspendue à un dernier blocage : le runtime FlowSched isolé
à deux CPU, avec calcul ring 3 sans syscalls, progresse anormalement lentement
sous VirtualBox/Hyper-V. Les deux threads ont IF actif et les timers LAPIC sont
armés. Un snapshot VirtualBox mesure ~503 s Virtual/TSC contre ~19 s VirtualSync,
horloge des timers émulés. Cela distingue le symptôme d'un simple wait futex,
mais ne prouve pas encore le défaut interne exact de VirtualBox. Le même ISO
passe sous QEMU TCG/UEFI à deux CPU : runtime de 3,476 s, réveil tardif maximal
de 16,014 ms, statuts enfants 0/0, chaîne PI et cycles unmap/owner-exit/kill PASS.
Le réglage NEM `UseRing0Runloop=0` n'apporte aucun gain et a été retiré. Ni le
scheduler ni les wakeups ne sont contournés. Les passages desktop décrits
ci-dessous restent des résultats acquis, pas une validation de ce cas CPU-bound.

## Architecture retenue

- `smp.c`, `gdt.c`, les entrées assembleur et `irq.c` : état CPU dynamique,
  GS/TSS/IST/pile/timer privés, IPI, invalidations TLB acquittées et références
  actives aux racines VMM. Les identités APIC et indices scheduler sont distincts.
- `scheduler_enable_smp_dispatch()` / `scheduler_publish_prepared()` : tâches
  userspace sans pin BSP, placement initial sur le CPU disponible le moins chargé.
  Les placements explicites restent respectés. `gui-loop` est migrable ; les
  services IRQ conservent leur affinité périphérique.
- `flow_pick()` / `flow_claim()` / `flow_migrate()` : préférence locale dans un
  groupe éligible, puis vol de travail non réclamé. Migration réelle des vues
  runqueue et timers ; conservation du service, deadlines et dépendances.
  Un seul entitlement global par Process, quel que soit son nombre de threads.
- `running_cpu` reste réclamé jusqu'à la sauvegarde effective du contexte et
  l'arrivée sur la nouvelle pile. L'ancien thread ne devient disponible ailleurs
  qu'après `scheduler_running()`. Kill distant demande un arrêt à une frontière
  sûre ; cleanup/retirement attendent cette exclusivité et les références restantes.
- Accounting/historique d'exécution par CPU ; `flowstats` produit `smp-app`.
  Historique RIP borné par CPU, snapshot atomique, `smp-jit` uniquement pour une
  instruction réellement échantillonnée dans une page JIT présente USER/RX/RO.
  Ces données sont diagnostiques, pas un moteur de scheduling parallèle.

Le gate commun FlowSched protège seulement les métadonnées. Il ne couvre ni
l'exécution des tâches ni les attentes/destructions. Il reste des sérialisations
PM/FS/GUI : ce n'est pas un verrou couvrant toute l'exécution du kernel.

## Corrections qui ont permis l'activation générale

- `usercopy.c`, `vex_syscall.c`, `compat_linux.c` : validation/materialisation et
  copie sous transaction mémoire ; requêtes natives figées ; buffers I/O, iovecs,
  msghdr/ancillary et poll possédés par le kernel pendant une attente. Copie de
  sortie revalidée après wake ; EFAULT si un sibling a unmap/protect. Allocations
  temporaires rattachées au thread et annulées à l'arrêt. Copies système/temps et
  chemins open/openat/chdir bornés ; un C-string snapshot conserve son terminateur
  même si l'application modifie simultanément ses octets.
- `vex_poll_kernel()` distingue un tableau kernel réellement possédé des données
  utilisateur. La validation supplémentaire du provider sur un bounce-buffer
  kernel provoquait des EFAULT et une boucle GIO ; elle n'est contournée que sur
  cet appel interne explicitement typé.
- FD/FS : références d'opération, clonage/fermeture/publication protégés, curseur
  partagé sérialisé avec les octets et le cache MAP_SHARED. Destruction après la
  dernière référence, et retrait des emprunts abandonnés avant libération de pile.
- `ipc.c` : waitqueues de pipes, EOF/EPIPE et backpressure réels. Un read vide
  faisait encore yield en boucle ; il bloque maintenant sans polling. Waiters
  retirés après kill, buffer gardé tant qu'une attente en détient une référence.
- Events/signaux : dispositions protégées, publication pending atomique, red zone
  et alignement conservés, siginfo/ucontext au-dessus de la pile du handler, FPU
  sauvegardée. Codec Linux pour le contexte asynchrone. Retour IRET renseigné avec
  RIP/RFLAGS, résultat syscall original conservé malgré les syscalls du handler,
  MXCSR validé, et IF non désactivable par un ucontext utilisateur.
- GUI/Surface/GPU/JIT : transactions et lifetime cohérents avec process_memory,
  ordre mémoire → scène/Surface, waiters GPU retirés au stop. Aucun yield sous
  verrou de scène. Les IRQ CPL0 d'input/timer/IPI n'accèdent pas aux inventaires
  PM/scène/Surface : ces gates autorisent ces IRQ tout en interdisant la préemption
  du contexte kernel. Les gates d'état IRQ, allocator et VMM gardent leur exclusion.
- Souris : coalescing dès la publication, en conservant press/release, wheel et
  reversals ; sommes contrôlées. Le coalescing uniquement à la consommation ne
  protégeait pas une queue retardée contre une rafale de rapports physiques.
- Paint : attente explicite du compteur d'événements privé de fenêtre. Le flag
  optionnel `VEX_APP_WINDOW_POLL_WAIT` réutilise le champ flags existant ; flags=0
  reste le poll legacy. Aucun verrou GUI/PM pendant l'attente. Notification sur
  input/configure/close, dernière référence libérée sur close/reap, annulation
  des waiters existants au kill. Ni sleep arbitraire ni boucle poll/yield au repos.
- Clavier/souris/RTC et ledger réseau existant : guards courts sur les états
  partagés. DNS pending nettoyé par TID arrêté. Aucune nouvelle stack réseau.
- Anciens diagnostics FPU synchrones remis en opt-in ; sauvegarde/restauration
  réelle FPU/XSAVE conservée. Le job diagnostique AP est une tâche FlowSched après
  activation : attendre exclusivement une fenêtre idle pouvait affamer le harness.

## Preuves VM et tests

`build/smp/platform-{1,2,3,4}.log` : plateforme, timers/IPI, PMM/ORA/VMM/TLB,
contextes/migration/stop distant, publication/retirement, shared-CR3, TLS/SSE,
allocations concurrentes, futex, 32 cycles RW→RX/unmap par thread, fork/exec,
FD offset (16 384 enregistrements exacts), emprunts FD/mémoire, counters et
2 000 races wait/register/wake sur SMP. `smp-automatic` exige que les threads
ordinaires non liés explicitement aient réellement exécuté sur tous les N CPU.
`smp-irq-memory` exige le progrès du timer pendant une transaction mémoire,
sans changement de CPU/TID du contexte protégé.

`build/smp/desktop-{1,2,3,4}.log` et PNG associés : boot Desktop normal, Terminal
avec vraie commande echo, Paint déplacé/redimensionné et dessin réel, ouverture
native du navigateur, HTTPS réel example.com (h2), puis deux Web Workers JSC.
Leurs sommes intégrales sont vérifiées indépendamment par exponentiation affine ;
le titre vert est le vrai DOM rendu après les DEUX résultats, pas une image injectée.
Les RIP JIT RX sont observés sur plusieurs CPU en SMP. GUI/NetworkProcess/WebProcess
ont des historiques d'exécution distribués, sans binding imposé par le test.

Le vrai JSC shell à quatre CPU passe ses modes Baseline, DFG, FTL et compilation
concurrente, avec `final_tier_executed=true` pour les tiers attendus. Le JIT utilise
les alias natifs RW/NX et RX/RO ; aucune permission RWX générique n'est ajoutée.

Après fermeture : sept tâches, aucun descendant WebKit, ~99,8–99,9 % idle.
Réouverture validée à deux CPU. Le logiciel renderer reste coûteux ; ceci n'est
pas un engagement de FPS ni de scaling des navigations Web.

Le dernier boot Desktop normal à quatre CPU utilise le kernel final ci-dessous :
Terminal echo, Paint déplacé/dessiné, HTTPS example.com h2, deux workers (892,6
et 882,4 millions de batches, sommes vérifiées), JIT RX échantillonné sur CPU
0/2/3, GUI et NetworkProcess sur les quatre CPU. Fermeture réelle du navigateur :
sept tâches et 99,9 % idle dans `desktop-4-closed-top.png`. Le probe Web conserve
une divergence distincte : champ texte vide et métriques resize/scroll incomplètes,
alors que click/hover/key/wheel sont reçus. Ce probe n'est pas déclaré PASS.
L'ISO finale revient au Desktop normal, sans marqueurs autorun ; la VM est
configurée à quatre CPU. La réserve VirtualBox CPU-bound reste ouverte.

Les checks EGL/GLES (1 290), les régressions Linux et les scénarios PI de chaîne,
unmap, mort du propriétaire et kill passent sur les runs runtime 1 et 4 CPU.
Les runs finaux `final-domain-fault-{1,4}.log` revalident les pointeurs Linux
kernel/invalides, le retour de faute synchrone (y compris IF forcé actif),
sigreturn concurrent et les 1 290 checks EGL/GLES. La matrice platform 1–4
repasse sur ce kernel final ; `build/vex-os` et le kernel stagé ont le SHA256
`C2CE40C12676F97A7F8EFC4BDB2C23AD61ABCF2DD8D0BCA32A5A131594CF64BE`.
Le smoke de couverture Vulkan conserve **exit=1 attendu**, pas un faux PASS.
`tests/flowsched_smp_test.c`, FD borrow et PI/counter hosted complètent les tests VM.
`mouse_queue_test.c` teste la vraie logique producteur/consommateur, notamment
20 000 rapports sans perdre les frontières des boutons ou les déplacements.

Le harness PS/2 vérifie le curseur dans le vrai framebuffer et tient compte du
clamp écran. Il confirme les déplacements bornés avant d'en envoyer d'autres :
VirtualBox peut tronquer les gros deltas accumulés de resets/targets artificiels.
Les premières tentatives échouées sont conservées ; callback reçu ≠ pixels validés.

## Comparaison CPU

Charge ring 3 fixe : 48 millions d'itérations, un Process, N threads, même CR3.
Deux mesures distinctes : dernier calcul fini, puis joins/cleanup. Les temps
isolés sont dans les logs platform ; ils ne sont pas remplacés par des mesures
mélangeant cette charge avec les smokes graphiques.

| CPU | Calcul, ms | Avec joins, ms |
| ---: | ---: | ---: |
| 1 | 71,60 | 71,63 |
| 2 | 35,39 | 35,84 |
| 3 | 45,94 | 46,14 |
| 4 | 24,95 | 25,26 |

Environ 2,87× de 1 à 4 sur ce calcul dans le run final. Trois CPU sont ici plus
lents que deux ; les runs précédents donnaient une autre tendance. Ce sont des
mesures ponctuelles de VM, pas un benchmark statistique ni une promesse linéaire.
Deux Web Workers fixes plafonnent naturellement à deux exécutions JS simultanées :
~1,08 milliard de batches comptés à un CPU, ~2,01 milliards à deux, ~1,87 milliard
à quatre dans le run antérieur (~1,77 milliard dans le dernier run), sur ~20 s
par worker. Le passage 2→4 ne donne donc pas un gain général.

## Travail restant — hors milestone desktop SMP 1–4

- Optimisation du gate/index de métadonnées FlowSched et du scaling au-delà de 4.
- Routage/balancing IRQ avancé ; pilotes matériels supplémentaires et contraintes
  des futurs callbacks IRQ (PM/scène/Surface modifiés dans un worker, pas depuis
  une IRQ CPL0 pendant la transaction interrompue).
- Réduction des longues transactions/rétentions et sérialisation résiduelle ;
  benchmarking statistique de latence sous charge, au-delà des runs ciblés.
- Couverture systématique de tous les ABI/pointeurs/erreurs/OOM, des dispositions
  de signaux et de leur livraison hors retours syscall ; ce chantier ne prétend
  pas à une certification exhaustive.
- Le probe Web interactif montre click/hover/focus/resize et navigation, mais
  certaines captures SMP ont une édition/présentation de champ texte ou des
  métriques DOM incomplètes/stales. Ce n'est pas déclaré entièrement validé ;
  le banc native echo/URL, le rendu, HTTPS et les vrais workers sont validés.
- Soup/HSTS peut signaler des erreurs de cache disque existantes ; HTTPS réel
  fonctionne. Pas de refonte FS/TLS/WebKit ni reprise Vulkan dans ce checkpoint.

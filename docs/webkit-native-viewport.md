# WebKit : mémoire et viewport natif — 1–2 octobre 2026

Lot 37/Vulkan reste en pause. Cette passe ne change ni le JIT/W^X, ni le réseau,
ni le contrat public graphique Surface. Le navigateur conserve son chrome Vex
natif ; le document HTML de test est seulement du contenu Web.

## Frontière et transport

`gui.c` effectue le dispatch après les contrôles natifs, modales et hit-tests
des autres fenêtres. `vex_browser_display.c` conserve le focus, les transitions
d'entrée/sortie et une file d'événements bornée. Les coordonnées sont relatives
au rectangle de contenu calculé par la fenêtre native, et non à l'écran complet.
Le syscall structuré `BROWSER_VIEWPORT` fournit QUERY, POLL et PRESENT au
processus propriétaire du navigateur. PRESENT choisit la destination native :
le renderer n'impose pas sa position sur l'écran.

Le launcher et le processus UI WebKit utilisent une paire message IPC Vex.
Le FD enfant est hérité au spawn natif, puis rendu CLOEXEC avant les sous-processus
WebKit. Le backend WPE traduit les événements Vex en événements WPE : pointeur,
boutons, wheel discret, keysyms/modifiers/up/down, activité/focus, taille et
visibilité. La source GIO attend la disponibilité du canal IPC ; aucun événement
DOM n'est fabriqué pour réussir le test.

Le rendu reste EGL/GLES → renderer commun → Surface. Le renderer transmet une
capability Surface sans écriture ; le processus UI la transmet au launcher avec
QUERY/READ/PRESENT/COMPOSE seulement. La capability locale est fermée après la
consommation réelle du compositor, avant ACK. ACK autorise alors la réutilisation
du backing par le producteur. Une frame reçue pendant que la fenêtre est masquée
reste pending jusqu'à présentation possible, sans faux ACK. La présentation
n'utilise plus une copie du snapshot dans l'ancien framebuffer SHM. Ce SHM garde
son layout historique borné pour les métadonnées de contrôle.

## Divergences démontrées et corrections

- Le shell LVGL déduisait indépendamment la page d'accueil de l'URL. Il reçoit
  désormais l'état natif réel, évitant de masquer les contrôles alors que WebKit
  présente déjà du contenu.
- Le décodeur souris traitait les deltas PS/2 comme des entiers signés de 8 bits.
  Les valeurs positives supérieures à 127 étaient mal décodées. Le signe 9 bits
  fourni dans l'en-tête PS/2 est désormais utilisé ; overflow/synchronisation
  restent validés. Le wheel IntelliMouse est décodé si le périphérique le permet.
- La propriété des boutons est distincte de leur état matériel : le test
  `build/browser-button-ownership-before.log` échoue sur le dispatch précédent,
  qui pouvait envoyer un release sans press Web. La correction conserve le
  destinataire du press. Une nouvelle interaction hors viewport reste exclue ;
  seul le release réel d'un drag déjà capturé par le Web est transmis avec ses
  coordonnées extérieures, pour ne pas conserver un bouton artificiellement
  enfoncé. Le test vérifie aussi que le release natif ne passe pas au site.
- Drainer tous les paquets souris avant le dispatch effaçait un down/up rapide.
  Le GUI conserve les transitions paquet par paquet. Les mouvements adjacents
  du viewport peuvent être coalescés, jamais les edges clavier/boutons.
- Le wheel matériel discret était envoyé comme axe WPE smooth. Le test recevait
  des événements DOM mais ne produisait pas le scroll attendu. La traduction en
  crans WPE discrets produit maintenant le scroll natif WebKit réel.
- Les touches up/down et les modificateurs sont conservés indépendamment du
  flux caractères legacy. La file associe les événements à leur propriétaire ;
  relâcher un Shift ne libère pas l'autre Shift encore enfoncé.
- MESSAGE_IO natif perdait errno en réutilisant une fonction POSIX retournant
  -1. Une lecture non bloquante vide devenait -1 au lieu de -EAGAIN : le launcher
  strict fermait son canal. La frontière structurée renvoie désormais l'erreur
  négative exacte, avec rollback des attachments. La convention legacy/POSIX
  -1 + errno est conservée.
- Le boot normal, avec la vraie application Files, révèle une divergence entre
  hit-test et image : la présentation directe WebKit publiait une couche basse
  au-dessus des fenêtres app_surface. La recomposition des fenêtres affectées
  et de leur fermeture de recouvrement suit désormais leur z-order avant
  scanout/completion/ACK. Les popovers natifs sont restaurés au-dessus également.
  Le splash legacy suit le même ordre. Aucun redraw complet de l'écran à chaque
  frappe et aucune modification de l'ABI Surface/driver ne sont introduits.
  `build/native-overlap-frame-before.png` conserve une frame réelle du défaut.

Une autre rétention réelle est identifiée au **close**, distincte de la pente
PMM par snapshot : tuer le launcher fermait son endpoint natif, mais le processus
UI WebKit retirait seulement sa source d'input et poursuivait les snapshots.
`build/native-browser-close-leak-before-vm.log` et
`build/native-browser-close-processes.png` montrent UI/Web/Network et leurs
threads encore vivants, avec frame 22 produite après la fermeture.
Le main UI observe maintenant HUP/ERR/NVAL de son endpoint avec GIO, arrête son
timer/boucle, demande l'arrêt réel du WebProcess par l'API WebKit et libère la
view et ses capabilities SHM. Le backend ferme aussi le renderer en cas d'EOF,
sans ACK fictif. Le GUI collecte son propre launcher terminé avec le wait natif
NOHANG ; un launcher appartenant au shell reste collectable par ce parent.

La fermeture du premier run de production (3465 snapshots livrés, zéro erreur
snapshot) a exposé une autre divergence : l'API
`webkit_web_view_terminate_web_process` émet bien `kill(WebProcess, SIGKILL)`.
`linux_signal_target` appelait `process_kill`, dont le contrat historique cible
un contexte d'exécution. Le leader mourait, mais ses workers continuaient et
WebKit émettait « WebProcess didn't exit as expected after the UI process
connection was closed ». Le nouveau helper interne `process_kill_group` termine
tous les contextes du Process avec les droits existants, retire leurs waiters,
ferme les FD process-wide une fois avant reap et réveille le parent. La
traduction SIGKILL, y compris tgkill visant un worker, utilise ce helper ; le
contrat natif task-targeted reste inchangé. Aucune ABI publique ne change.

Le test de réouverture a aussi révélé que le bouton rouge appelait directement
`gui_browser_end` sans détruire la session display. Le cache conservait l'ancien
PID ; le nouveau launcher obtenait un refus du viewport et fermait son canal.
Le bouton utilise désormais `ladybird_display_close`, qui nettoie owner,
surface, focus et file d'input. Le test hôte vérifie close/reopen avec un nouvel
owner et sans événements, boutons ni overflow hérités.

La validation pendant navigation est conservée dans
`build/webkit-native-final-close-vm.log` / capture
`build/webkit-native-final-close-processes.png` : 280 snapshots, zéro erreur,
UI/Web/Network et tous leurs threads disparaissent ; `ps` revient aux cinq
processus natifs de base. Contrairement au premier close, aucune erreur fatale
WebKit ni fault ne suit SIGKILL. Le test de minimisation a également révélé le
garde global `browser_active` dans `send_key_to_active_app`, qui rejetait les
touches même lorsque le terminal était visible et le navigateur minimisé.
Ce garde tient compte de la visibilité ; minimiser restitue la configuration
du terminal et retire aussi le focus de l'adresse native.

La vérification avec un véritable éditeur natif a révélé deux erreurs de
dispatch : le filtre d'entrée du shell supprimait le caractère legacy même
quand une fenêtre app_surface avait le focus, et la frappe ASCII `z` (0x7a)
était prise pour KEYBOARD_KEY_APP_SWITCH_NEXT (également 0x7a). Cette dernière
collision masquait le navigateur au lieu de saisir le caractère. Le dispatch
GUI reçoit maintenant un marqueur de raccourci distinct de la valeur legacy
(`keyboard_pollchar_kind`) ; les événements structurés sont filtrés d'après
le scancode/modifiers réel, jamais d'après cette collision de valeurs. L'ABI
legacy conserve ses octets historiques. Le test exécute le vrai décodeur et
distingue les press/release de `z` du raccourci F6.

`app_surface_has_keyboard_focus` permet d'appliquer la disponibilité du shell
uniquement à ses propres frappes ; le focus natif reprend le clavier pour le
dispatch GUI. La mise à jour incrémentale de la label terminal repeignait aussi
au-dessus du backing de l'éditeur. Elle recompose maintenant les fenêtres qui
recouvrent ce dommage, avec le mécanisme de fermeture de recouvrement déjà
testé, avant présentation. Aucun redraw global à chaque frappe n'est ajouté.
Les captures et traces précédant ces corrections sont conservées dans
`build/webkit-native-focus-divergence-vm.log` et
`build/webkit-native-focus-second-vm.log`.

Enfin, `app_surface_present` reconstruisait le bureau entier pour chaque
refresh d'un backing d'application. Une frappe dans l'éditeur effaçait donc
le contenu Web jusqu'à la frame suivante. Une présentation compose maintenant
la fenêtre concernée et ses recouvrements/overlays, sans reconstruire wallpaper
et fenêtres disjointes. Les peintres natifs travaillent encore par fenêtre
entière ; cette correction ne prétend pas les rendre tous pixel-incrémentaux.

Le test manuel `run vex_editor` a enfin montré un zombie natif conservé par
le shell. `load_elf_process(wait_for_exit=1)` appelait seulement `process_wait` :
un réveil quelconque pouvait faire retourner le chargeur, et même une sortie
réelle n'était jamais collectée. Ce chemin utilise maintenant
`process_wait_pid(created_pid, ..., 0, ...)`, primitive native déjà existante,
qui vérifie la sortie de l'enfant exact puis le collecte. Le résultat du
chargement reste distinct du statut de sortie de l'application, comme avant.
Le smoke kernel rejoue quatre sorties rapides puis un chargement inexistant,
et vérifie après chaque appel qu'aucun enfant ne reste à collecter. Le chemin
spawn asynchrone et la compatibilité Linux ne sont pas réécrits.
La validation réelle finale `run vex_editor → frappe z → fermeture → ps`
revient aux cinq processus natifs de base, sans zombie :
`build/native-editor-reap-final.png` / `build/native-editor-reap-final-vm.log`.

## Mesure mémoire

L'ancienne trace `build/webkit-curves-animation-64-vm.log` confirme exactement
157 pages perdues sur ses 24 derniers échantillons producteur, soit **628 Kio**.
Elle ne contient pas les catégories permettant d'attribuer chacune de ces pages
rétroactivement. Une attribution exacte à une fuite donnée serait inventée.

L'instrumentation nouvelle compte MRAC, backings partagés/file-backed, objets et
capabilities Surface, tables VMM et pages résidentes par inventaire process_memory.
Elle compte chaque process_group une fois, sans additionner artificiellement ses
threads partageant CR3. Les mappings lazy de 128 Gio ne sont pas parcourus page
par page. Les compteurs GL et mallinfo2 distinguent mémoire vivante et réserve
libre glibc. Les échantillons PMM comparés sont tous pris au même point du cycle
producteur PID 14, jamais mélangés aux lectures UI après matérialisation du bitmap.

Sur le run interactif, après échauffement :

| Catégorie | Observation |
| --- | --- |
| Surface | 1 objet, 1 capability, 1 backing, 3 329 896 octets pour 926×899 |
| Backings | 4 backings partagés, 3740 pages ; 813 pages WK2 bitmap actives |
| MRAC | 74739 pages ; 277035584 octets vivants, état stable en fin de run |
| GL | 13 programmes ; 66–68 objets ; ~26,2 Mo textures ; 311296 octets buffers |
| Allocateur WebProcess | mémoire vivante ~40,5 Mo ; arène 44,96 → 54,71 Mo, dont ~14,2 Mo libres réutilisables |
| Allocateur UI | arène 1859584 octets ; ~350 Ko vivants ; ~1,5 Mo libres réutilisables |

La hausse mesurée se situe dans les pages privées et l'échauffement des pages
file-backed, pas dans une accumulation de Surface/capabilities/bitmaps. L'arène
glibc augmente essentiellement sa réserve réutilisable tandis que les objets GL
vivants se stabilisent. Cela établit une rétention/high-water et une
matérialisation progressive ; ce n'est pas une preuve exhaustive d'absence de
toute fuite interne WebKit. Aucune fuite identifiable du transport n'a été
masquée par un trim forcé, une limite RAM relevée ou un faux compteur.

Le run long conservé contient **305 snapshots**, environ 45 minutes en VM avec
ce backend logiciel et ses diagnostics. Les 24 derniers échantillons perdent
196 Kio, les 64 derniers 388 Kio et les 128 derniers 1176 Kio. Le ralentissement
est mesuré ; un plateau PMM parfaitement horizontal ne l'est pas. Entre les
deux derniers inventaires, les régions et pages file-backed restent identiques,
les pages privées UI passent de 2352 à 2354 et celles du WebProcess de 18707 à
18720. L'instrumentation DOM elle-même sollicite le runtime JS du processus UI.
L'ancienne perte de 628 Kio est donc confirmée, mais son attribution individuelle
reste indéterminée faute de catégories dans l'ancienne trace. Les conclusions
portent sur la reproduction instrumentée, pas sur une certitude inventée.
`scripts/measure-webkit-memory.ps1` reproduit les fenêtres PMM et conserve les
catégories en JSON, sans déduire « fuite » de PMM seul.

La build de production, sans probes DOM/mémoire, a ensuite livré **3465
snapshots** du document d'accueil statique avant fermeture volontaire.
`build/webkit-native-production-long-vm.log` conserve ce run ; le compteur du
callback de terminaison dans `build/webkit-native-shutdown-first-vm.log` confirme
3466 demandes / 3465 frames / 0 erreurs. Les traces PMM de production sont
bornées aux 96 premiers échantillons producteur : leurs fenêtres finales de
24/64 échantillons donnent 20/56 Kio de perte. Elles ne prouvent pas une stabilité
RAM sur l'intégralité des 3465 frames. Le contenu statique n'est pas utilisé
pour prétendre valider une animation ; les pixels dynamiques ont été vérifiés
sur le document interactif du run précédent.

Les traces catégories sont opt-in (`VEX_BROWSER_MEMORY_DIAGNOSTIC=1` côté
kernel ; `VEX_MEMORY_DIAGNOSTIC` côté runtime GL). La variante
`WEBKIT_RENDER_SMOKE=1` active le document interactif et les observations DOM en
lecture seule ; la build normale n'exécute pas ces probes.

## Interactions observées, et non seulement callbacks

`build/webkit-native-moved.png` montre la valeur du champ `ab` après saisie `abC`
puis End/Backspace, deux clics, deux hover, les événements clavier/modifiers,
SVG/canvas et l'animation. Le window move décale l'origine du viewport de
(974,163) à (874,163) ; un clic au nouvel emplacement active encore le bon bouton.
Le wheel réel produit scrollY=120 pour trois crans descendants, puis scrollY=0
pour trois crans montants. Les observations DOM confirment activeElement/focus,
scrollHeight et les dimensions du document.

`build/webkit-native-scroll.png` montre la navigation par le vrai lien ancre,
scrollY=1419 et le resize 926×899 → 926×798. Le clic sur la barre d'adresse native
retire le focus Web : la saisie modifie `vex://homed` sans changer le compteur
clavier du document. Le wheel hors viewport n'augmente pas son compteur DOM.
L'entrée/sortie est vérifiée par le compteur du document et le test natif.
Le masquage puis la restauration via le menu Vex sont conservés dans
`build/webkit-native-hide-restore-vm.log` et les captures
`build/webkit-native-minimized.png` / `build/webkit-native-restored.png` : activité
visible/focus retirée, configuration 0×0 puis retour à 926×899, contenu restauré
et phase d'animation qui progresse sans fermeture du canal.

Ces interactions sont injectées au périphérique PS/2 de la VM par
`scripts/vm-browser-input.ps1`, pas par evaluate_javascript. Le probe JavaScript
lit seulement les états. Les snapshots ont des hashes variables et des pixels
SVG/canvas/textes réellement issus du renderer ; l'animation n'est pas un buffer
constant. Le backend logiciel est lent : attendre la réaction DOM réelle avant
de conclure qu'une action n'a pas fonctionné.

## Tests reproductibles

- `scripts/test-native-browser-input.sh` compile les vrais décodeurs/dispatch
  kernel pour l'hôte : PS/2 signé/wheel/overflow, bounds/coordonnées locales,
  chrome exclusion/focus/resize/hide, saturation explicite, touches/modifiers et
  séparation des propriétaires. Le quatrième exécutable vérifie la fermeture
  de recouvrement des dommages, y compris une seconde fenêtre hors du dommage
  initial, fenêtres cachées/disjointes et calculs de limites sans overflow.
  Le cinquième exécutable vérifie le focus d'une fenêtre native, y compris
  réactivation du même owner, fenêtre masquée/minimisée, close pending et
  identité absente. Il vérifie aussi le chemin de présentation réel avec
  frontières compositor instrumentées : rejet avant composition en cas
  d'erreur, dommage limité à la fenêtre, pas de reconstruction du bureau,
  completion seulement après flush. Les cinq exécutables passent ; la régression clavier
  couvre maintenant explicitement la collision `z` / F6.
- `linux_kill_group_smoke.S` effectue quatre cycles fork + worker bloqué sur
  futex, puis kill(Process) / tgkill(worker), vérifie EOF réel du pipe partagé,
  wait avec WTERMSIG=9 et ESRCH pour un PID absent. Le même binaire passe sur
  Linux réel sous WSL. Le sous-test de cancellation d'un seul waiter dans
  `linux_futex_smoke.S` utilisait jusque-là incorrectement le SIGKILL Linux ; il
  utilise maintenant explicitement le domaine natif PROCESS_KILL (thread),
  conservant sa vérification de retrait du waiter plutôt que supprimant le cas.
- `build/webkit-native-production-build.log` : 451 contrôles EGL/GLES hôte passent.
- `build/egl-native-input-asan.log` : mêmes 451 contrôles avec ASan/UBSan, sans erreur.
- Le test EGL/GLES VM ajoute aux transferts réels de capabilities les erreurs
  natives -EAGAIN pour une file vide et -EBADF pour un FD invalide.
  Il passe **1280 contrôles**, `gl-smoke: exit=0`.

La suite rejouée sur le kernel final incluant focus, collision `z` et
recouvrement du terminal est conservée dans
`build/native-browser-input-final-regressions-vm.log` : EGL/GLES 1280, nouveau
kill-group-smoke exit=0, quatre cycles native-load/reap + image absente,
17 régressions syscall vérifiées, tests Vulkan/Surface
existants, 800 Mio de churn heap recyclé et 128 cycles event-counter avec delta
PMM nul. Le critère final Vulkan incomplet conserve exit=1 ; aucun échec
intermédiaire n'est converti en succès. La première tentative est conservée dans
`build/native-shutdown-regression-first-vm.log` : assertion erronée du nouveau
test (exit encodé au lieu de statut signalé), et ancien futex smoke dépendant de
la mauvaise sémantique SIGKILL. Ces tests ont été corrigés explicitement, pas
supprimés.

## Points de code de cette passe

| Partie | Fichiers / fonctions |
| --- | --- |
| Entrées matérielles | `mouse.c: mouse_decode_packet/mouse_take_wheel`, `keyboard.c: keyboard_poll_event/keyboard_modifiers` |
| Exclusion clavier / terminal | `keyboard.c: keyboard_pollchar_kind`, `app_surface.c: app_surface_has_keyboard_focus/app_surface_focus`, `gui.c: gui_tick` |
| Dispatch natif | `gui.c: gui_loop`, `gui_browser.c: gui_browser_begin/gui_browser_end`, `vex_browser_display.c: browser_viewport_pointer/browser_viewport_key/browser_viewport_poll` |
| Session/focus | `vex_browser_display.c: ladybird_display_close/browser_runtime_reset`, `gui.c` bouton close, `vex_lvgl_browser_ui.c` état home natif |
| Frontière ABI / IPC | `vex_abi.h`, `browser_viewport.h`, `vex_syscall.c: VEX_SYS_BROWSER_VIEWPORT/vex_sys_message_io`, `vex_browser_protocol.h`, `webkit_launcher.c: viewport_pump` |
| Backend / WebKit | `vex_wpe_backend.c: view_receive_native/view_receive_frame/target_present_surface`, `webkit_linux_main.c: on_native_window_closed/on_snapshot_ready` |
| Occlusion | `window_damage.h`, `app_surface.c: app_surface_render_damage`, `gui.c: gui_compositor_restore_foreground`, `spotlight_ui.c` restauration overlay |
| Fin de Process | `scheduler.c: process_kill_group`, `compat_linux.c: linux_signal_target/linux_signal_thread_target`, `scheduler.h` API interne |
| Collecte native | `elf.c: load_elf_process`, `main.c: graphics_test_runner` smoke load/reap |
| Diagnostics mémoire | `memory_diagnostic.h`, `mrac.c`, `vmm.c`, `process_memory.c`, `compositor_surface.c`, `compat_linux.c`, `gles.c`, `egl.c`, `measure-webkit-memory.ps1` |

Les preuves de code et les tests ci-dessus couvrent la frappe/modifiers/keyup
et les touches spéciales testées, pas un système IME ou toutes les dispositions
de clavier. La fenêtre de test native est une fondation de dispatch ; les onglets
et menus d'un navigateur complet restent un chantier distinct.

La conformité Vulkan reste hors scope et n'est pas déclarée. Les fonctionnalités
non supportées continuent de retourner une erreur. Le JIT JSC reste non résolu ;
ce JavaScript tourne sans relâcher W^X.

Le dernier test de focus montre `abCz` dans le champ Web et uniquement `z` dans
l'éditeur natif (`build/webkit-native-typing-final.png`). Après le clic natif,
une frappe `q` donne `zq` dans l'éditeur sans nouvelle frappe Web. Les premières
frappes envoyées avant load-finished ne valident pas la saisie du document ; la
séquence validée est celle envoyée après chargement réel.
`build/webkit-native-minimize-terminal-final.png` montre `ps` exécuté pendant que
UI/Web/Network sont encore vivants. `build/webkit-native-close-final.png` et
`build/webkit-native-input-lifecycle-final-vm.log` montrent leur disparition après
65 snapshots / 66 demandes / zéro erreur, sans fatal WebKit. Cette capture
antérieure à la dernière correction montrait aussi le zombie natif du
`run vex_editor`, origine du correctif load/reap décrit plus haut ; il ne
s'agissait pas d'un processus WebKit résiduel.

La dernière correction de présentation est vérifiée sur le boot normal du
kernel 9AF46876, avant l'ajout ultérieur du correctif d'attente ELF synchrone :
`build/webkit-native-present-isolation-final.png` montre `zq`
dans l'éditeur, le viewport Web encore entièrement présenté et zéro frappe
Web dans le DOM. Ce run est distinct du mode headless des tests graphiques,
qui ne crée pas les fenêtres du bureau et ne constitue pas une validation
du hit-test visuel.

`build/webkit-native-present-reopen-final-vm.log` valide ensuite deux sessions
sur ce même kernel 9AF46876 sans reboot : owner 6 puis owner 49, 16 puis 17 snapshots
livrés, zéro erreur snapshot dans les deux cas. La seconde session atteint
load-finished et de vrais pixels SVG/canvas animés à 926×899, stride 3704
(`build/webkit-native-reopened-final.png`). Les deux fermetures déclenchent
l'arrêt du contenu et la terminaison du WebProcess sans abort/fault. La
réouverture n'hérite plus du PID, focus ou événements de l'ancienne session.

L'ISO livrée utilise ensuite le kernel C23CBD9B (même code graphique/input,
plus load/reap ELF et son smoke), avec toutes les régressions rejouées.
`build/webkit-native-final-manifest.json` conserve les hashes du kernel et du
main production identiques à leurs copies staging. Le kernel, le main et le
boot ont aussi été extraits de l'ISO finale : hashes correspondants, sans
module auto-test dans la configuration extraite. Le main production est
restauré, sans probes DOM de WEBKIT_RENDER_SMOKE ; aucun module auto-test ne
figure dans le boot final. La VM est laissée sur le bureau normal.

## Frontière non validée : navigation file:// et changement de WebProcess

La saisie native d'une vraie URL `file:///usr/linux-tests/webkit-render.html`
atteint bien le canal de commande, est acquittée et crée un nouveau WebProcess.
Dans `build/webkit-native-final-close-vm.log`, ce dernier atteint le chargement
des bibliothèques et la création de son renderer backend, mais aucun nouveau
load/commit ni target_create n'est observé pendant environ dix minutes. L'ancien
document continue à produire ses snapshots. La première divergence de ce
changement de processus n'est pas encore identifiée ; ce n'est pas présenté
comme une navigation réussie ni attribué arbitrairement au FS/réseau.

Le document interactif validant cette passe est réellement chargé/rendu par
WebKit avec load_html dans le harness existant. Ses boutons, champ, lien ancre,
scroll et JavaScript sont réels ; cette preuve ne valide pas load_uri ou la
navigation inter-documents d'un navigateur complet.

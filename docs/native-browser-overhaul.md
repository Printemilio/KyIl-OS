# Navigateur natif Vex — état vérifié, octobre 2026

Cette passe conserve WebKit/WPE et le renderer logiciel commun. Le chrome,
la page « New Page », les onglets et l'édition de l'adresse sont natifs Vex/LVGL.
Vulkan Lot 37 reste en pause. Ce document remplace les anciennes réserves
concernant `load_uri` dans `webkit-native-viewport.md`.

## Chemin réel

`desktop / gui_browser → /webkit (supervisor natif) → webkit_linux_main`

Le noyau conserve l'état du chrome, le hit-test et le viewport sélectionné.
Le superviseur transmet les commandes sur un endpoint message fiable. Chaque
page a son propre ID, WebKitWebView, historique, backend WPE et endpoint de
viewport. Huit pages au maximum ; elles partagent le WebContext/NetworkProcess,
pas leur état de navigation. Un onglet sélectionné reçoit les coordonnées
locales du viewport et les événements WPE ; le chrome ne les envoie pas au DOM.

Le rendu suit `WebKit/Skia → EGL/GLES → renderer Vex → Surface → capability IPC
→ ViewBackend → supervisor → compositor`. Les ACK suivent la consommation de
la Surface. Les snapshots ne servent pas au transport normal. Le bootstrap SHM
ne contient que les informations de démarrage/canal, pas une image fabriquée.

La fermeture notifie l'UI, arrête ses vrais WebProcesses, détruit les pages et
attend sa sortie ; les endpoints et capabilities sont fermés. La fermeture
après plusieurs changements de processus a laissé seulement les processus
système 0–5 (`browser-cross-site-clean-close.png`).

## Réseau réutilisé et complété

`NET_CONNECT` (bit 7) est distinct de `NET_ADMIN`. Il est accordé explicitement
au lancement natif du navigateur et hérité par ses descendants. Il autorise les
connexions, pas l'administration réseau et ne rend pas le processus admin.
Les sockets VSL utilisent TCP/UDP/DNS Vex ; aucune stack privée de navigateur.
Le NetworkProcess utilise le vrai module GIO/GnuTLS et le vrai trust store CA.
L'heure vient de l'horloge Vex/RTC UTC. Un certificat invalide reste refusé.

Les compléments génériques comprennent le connect TCP asynchrone, les envois
partiels/ACK/retransmissions/FIN et des datagrammes UDP réellement reçus. La
configuration DNS est issue du DHCP. Limites actuelles : IPv4, huit connexions
TCP, seize sockets UDP et files bornées, gestion TCP encore limitée des paquets
hors ordre et du TIME_WAIT. Ce n'est pas une stack Internet complète.

## Corrections et causes

* `vex_fd_table.inc` : l'ancien inventaire global de 128 FD saturait au troisième
  onglet et le fork perdait des FD. L'inventaire est maintenant process-wide
  (128 par Process), partagé entre threads ; copie complète ou rollback avant
  publication du fils, refs indépendantes pour dup/fork/IPC, CLOEXEC par FD.
* `process_memory` / spawn : un enfant empruntant le CR3 de son parent ne peut
  pas valider ses adresses sous un propriétaire différent. La validation utilise
  cet ownership jusqu'à exec. CHILD_SETTID/CLEARTID sont validés dans ce domaine.
* `syscall_entry.S` : le choix SYSRET/IRET était stocké par CPU et pouvait être
  écrasé par une autre invocation préemptée. Il appartient désormais au frame
  de syscall, avant STI ; sigreturn marque ce frame précis.
* `keyboard.c` : le même Ctrl+Tab était dispatché à l'IRQ puis au décodage raw.
  La métadonnée du ring indique le dispatch effectivement réussi. Le fallback
  reste disponible si la file IRQ est pleine ; les modifiers restent cohérents.
* `vex_posix_compat.c:poll_once` : FD négatifs ignorés, FD positifs invalides
  POLLNVAL, références correctement relâchées. Ce bug était réel mais n'était
  pas la cause de la navigation croisée (aucun FD négatif dans son poll).
* `vex_wpe_backend.c:view_backend_get_renderer_host_fd` : WebKit prend propriété
  du FD retourné et le ferme après transfert. Rendre l'endpoint possédé par le
  backend laissait un numéro stale : au prochain changement de WebProcess,
  SCM_RIGHTS échouait sur FD 8 avec EBADF avant CreateWebPage. Le callback rend
  maintenant F_DUPFD_CLOEXEC ; le backend conserve sa propre référence.
* `gui.c` / `browser_navigation.h` : buffer de saisie auparavant plus petit que
  son contrat. Limite unique 1024, insertion et normalisation bornées, pas de
  troncature silencieuse. Les URL file/HTTP/HTTPS gardent leurs vrais schemes.
* `gui_browser.c` / restauration compositor : une frame vide pouvait effacer
  l'erreur TLS native. L'état d'erreur demeure visible jusqu'à nouvelle navigation.
* `process_memory.c:process_memory_protect_region/store_split_region` : le run
  sans diagnostic a reproduit un panic MRAC pendant le mprotect RELRO d'un ELF
  Linux. Le numéro de processus et la backtrace distinguent cet appel du test
  de pointeur invalide affiché en parallèle. Un début de page précédant la
  région produisait un offset unsigned négatif ; une fin de page dépassant la
  région autorisait une lecture hors backing. L'inventaire et le snapshot sont
  désormais découpés à leur intersection réelle, sans élargir le backing ni
  changer la plage de permissions PTE. Chaque slice vérifie ses bounds.
* `webkit_linux_main` : notifications et lifecycle par page ; erreur réelle
  remontée au chrome, arrêt de WebProcess signalé, adresse/Stop conservés durant
  le lancement avant LOAD_STARTED. Pas de succès de navigation inventé.
* Smoke : le probe file-backed mmap était lancé deux fois sur le même fichier,
  dont un exemplaire tronquait celui de l'autre. Suppression du lancement doublon,
  maintien du probe attendu par vk_test.
* Les nouveaux probes ELF/poll/WPE sont attendus par le runner syscall utilisateur
  de vk_test. Un enfant du PID 0 est détaché dans le contrat actuel et peut être
  reaped avant le wait de boot : ce chemin produisait un faux échec malgré la
  sortie réussie du probe ELF. Les tests restent exécutés et leur statut est
  vérifié par un vrai parent ; la politique scheduler n'est pas modifiée.

Le backend WebKitWebViewBackend est adopté par la propriété `backend` ; son
destructeur reste géré par WebKit. Aucun unref artificiel n'a été ajouté.
Les journaux IPC/syscall et miroir journal WebKit utilisés pour le diagnostic
sont opt-in. Le runtime de production est reconstruit sans ce miroir.

## Preuves réelles

Les artefacts cités sont sous `build/`. Les interactions VM sont de vrais
événements souris/PS2 VirtualBox, pas des injections de DOM. La lecture du DOM
via Ctrl+Shift+D est uniquement un diagnostic demandé ; elle ne modifie pas les
URL, clics ou résultats de navigation.

| Parcours | Preuve |
|---|---|
| file A, formulaire vers B, valeur transmise, Back et scrollY=240 | `browser-wikipedia-cross-site-navigation.log` et captures de cette série |
| HTTPS example.com et trois pages indépendantes | `browser-two-tabs-real-https.png` |
| Vraie Wikipédia Pomme, texte/CSS et photos, HTTP 200 | `browser-wikipedia-rendered.png` |
| Certificat expired.badssl.com refusé et erreur native durable | `browser-tls-error-persistent.png` |
| URL file de 208 caractères, DOM exact | `browser-long-uri-loaded.png` |
| Resize 926×899 ↔ 1904×923, reflow réel | `browser-resize-completed.png` |
| `Alt+F10` maximise puis restaure la fenêtre native ; le DOM passe à `1904×923`, puis revient à `926×899` (`resize=2`) sur l'ISO finale propre | `browser-resize-final-max.png`, `browser-resize-final-restored.png` |
| Minimize, terminal utilisable, fermeture de trois pages | `browser-minimized-validation.png`, `browser-clean-close-processes.png` |
| file A → HTTP B → HTTPS example dans le même onglet | `browser-cross-site-fd-fix-validation.log`, `browser-file-http-https-same-tab.png` |
| Back HTTPS→HTTP→file et Suivant vers HTTP, load/commit/finish | `browser-cross-site-fd-fix-validation.log` |
| Même callback WPE, 1000 exports/close, CLOEXEC, stale/null | `browser-host-regressions-current.log`, `browser-wpe-fd-vm-test.png` |
| Cause avant correction | `browser-wpe-stale-fd-proof.log`, `browser-wpe-fd-before-fix.log` |
| Panic et backtrace mprotect/split/MRAC avant correction | `browser-production-smoke-panic.log`, `browser-panic-context-proof.log`, `browser-protect-edge-before-fix.log` |

Quatorze groupes host contrôlent notamment input, focus, viewport, damage,
édition/normalisation d'URL, FD, TCP/UDP et ownership WPE. Le runtime graphique
est reconstruit en passant ses 451 cas GL/EGL. Les smokes glibc FD (quatre enfants
et exec), sigreturn concurrent (1200 fautes reprises + 10800 syscalls), poll,
mémoire/lazy/file-backed, IPC/SCM_RIGHTS, TLS/futex et W^X sont aussi exécutables
sur la VM. Le global Vulkan ne doit pas être déclaré vert : son critère final
de couverture générale reste volontairement non satisfait ; l'ancien gpu_test
demande également un flag Surface obsolète. Ces limites ne sont pas masquées.

## Limites conservées

VSL refuse toujours RWX. JSC utilise désormais le JIT natif à aliases RW/NX et
RX/RO, avec Baseline/DFG/FTL exécutés en VM ; voir `jsc-vex-jit.md` pour les
preuves et timings. W^X n'est pas relâché. Le
renderer logiciel et son profil shader restent partiels : pas de promesse de
compatibilité avec tous les sites ou de performance GPU. WebGL, médias et audio
restent désactivés comme avant cette passe. Les warnings persistants de cache
libsoup/SQLite (notamment file-locking) ne sont pas présentés comme résolus.

Pas encore de gestion complète des popups/target=_blank, téléchargements,
permissions de site, dialogues/file chooser, profil persistant et menus complets.
La recherche de texte normalise vers une vraie URL de moteur ; son résultat
distant n'est pas fabriqué. Le chrome n'a pas une couverture typographique
Unicode complète. Les boucles de service 16 ms et certains waits provider
restent des pistes de performance ; aucune absence générale de polling n'est
annoncée. Les pages distantes lourdes peuvent encore être lentes.

Les tests positifs démontrent les parcours listés, pas une conformité globale
POSIX, TLS ou GLES ni l'absence de toute fuite sur tous les workloads.

Le contrat de référence WPE remet également une référence FD distincte :
[WPEBackend-fdo, ViewBackend::clientFd](https://github.com/Igalia/WPEBackend-fdo/blob/master/src/view-backend-private.cpp).

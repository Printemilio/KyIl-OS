# Applications desktop KyIlOS en Suchu

## Chemins normaux

| Application | Binaire | Sources applicatives |
|---|---|---|
| Terminal | `/usr/bin/kyil-terminal` | `terminal.suchu`, `terminal_model.suchu` |
| Files | `/usr/bin/kyil-files` | `files.suchu`, `files_model.suchu` |
| Applications | `/usr/bin/kyil-launcher` | `launcher.suchu` |
| Paint | `/usr/bin/kyil-paint` | Paint Suchu existant, canvas incrémental conservé |
| Editor | `/usr/bin/kyil-editor` | Éditeur Suchu existant, relinké |
| Browser | `/usr/bin/kyil-browser` | Chrome Suchu/WebKit existant, relinké |

Le menu desktop, Spotlight et les commandes `files`/`gfm`, `paint`, `launcher`
utilisent ces chemins. Les anciennes applications C/LVGL restent disponibles
pour comparaison/diagnostic ; le bureau/compositor, son overlay Spotlight et
les services noyau ne sont pas réécrits en Suchu. Aucun changement ne porte sur
le Suchu original du Desktop : seuls les sources intégrés sont utilisés.

## Files

Suchu possède chemins canoniques, tri, listing courant, sélection, navigation
back/forward/up, raccourcis, type-ahead, scroll, double clic, dialogues,
propriétés, clipboard local copy/cut, renommage, copie/déplacement, suppression,
confirmation et gestion des erreurs. Le modèle ne réutilise pas `gfm_fs.c` ni
`gfm_view.c`.

Les bridges `fileio` correspondent à lstat, open/read/write/fsync/close,
mkdir/rmdir/unlink/rename/chmod/readlink/symlink. Les boucles, choix de noms,
descente récursive, limites, vérification des collisions et cleanup sont Suchu.
Copies par chunks de 64 Kio, création exclusive, erreurs de flush/close
propagées. Un échec tente de retirer la copie partielle ; si ce cleanup échoue,
son chemin est signalé. Les liens sont copiés comme liens et ne sont pas suivis
par la suppression. La profondeur reste bornée à huit, comme le modèle GFM
précédent ; la suppression préflighte cette limite.

La vue n'est plus arbitrairement tronquée aux 128 entrées de l'ancien GFM.
Les limites VexFS de 127 bytes par chemin et les 64 bytes de nom créable de GFM
sont conservées. Le rafraîchissement à une seconde observe les modifications
externes/downloads, avec un vrai timer scheduler. Les gros changements de
fichiers restent synchrones, comme dans l'ancien GFM. Déplacement de répertoire
= copie complète puis suppression ; les opérations multi-fichiers ne sont pas
une transaction atomique ni une protection contre des mutations concurrentes
de tout l'arbre. Le clipboard de fichiers reste local à l'instance.

## Terminal et console native

Le Terminal Suchu est une application de rendu/interaction, pas un nouveau
shell caché dans un bridge. Suchu possède saisie, historique 100 commandes,
vue texte, CR/backspace, clear, scrollback borné, wrapping par métrique mono
réelle et resize/reflow mis en cache. Il utilise le même shell natif que le
desktop précédent : `top`, diagnostics, filesystem, `run`, shutdown/reboot,
etc. restent ses véritables commandes.

`VEX_SYS_CONSOLE=220` est une requête structurée native, dans le domaine natif
déjà séparé de legacy/Linux. Elle donne une lease exclusive liée à un Process
et à sa fenêtre, avec nonce opaque, validation UID/capabilities, permissions
USER/WRITE des buffers et snapshot de requête. Le shell existant est une
ressource système durable ; fermer son frontend ne le détruit pas. La fermeture
ou mort du propriétaire de fenêtre révoque la lease. Un autre PID/fork ou un
token invalide ne peut pas l'utiliser.

Le service ancien est administratif et dispose de tous les droits : son accès
exige donc UID 0 + **toutes** les capabilities, pas seulement SYSTEM_ADMIN.
Le bureau les délègue explicitement au binaire Terminal protégé. Un programme
ordinaire reste sans ces droits. Cette passe ne prétend pas offrir des shells
indépendants par utilisateur ni une PTY POSIX. Les programmes interactifs qui
exigent une PTY sont un chantier distinct.

Les sorties passent dans une queue bornée de 64 Kio ; un overrun est annoncé,
jamais caché. Les octets antérieurs à l'attachement sont conservés, notamment
pour les commandes déclenchées par Spotlight avant que la fenêtre soit prête.
`cls` produit un vrai clear de vue. Des notifications coalescées de ressource
(`VEX_APP_EVENT_NOTIFY`) utilisent la waitqueue/event counter de la fenêtre :
aucun timer de polling de sortie n'est installé.

La soumission de ligne existante du shell a été unifiée : publication atomique
d'au plus 254 bytes, puis consommation par le même chemin d'exécution. L'ancien
envoi d'un paquet de 176 bytes **par caractère** pouvait ne publier qu'un
préfixe lorsque son pipe était plein. Les caractères UTF-8 sont acceptés comme
bytes ; les contrôles non représentables sont refusés.

## Délégation/lifetime du lanceur

`PROCESS_SPAWN` conserve sa structure et flags=0 (aucun droit ajouté).
Une option de flags permet de demander explicitement un sous-ensemble des
capabilities actuelles. Une demande hors droits ou un flag inconnu échoue avec
PID=0. Le lanceur administratif demande ALL pour Terminal, NET_CONNECT pour
Browser et aucun droit supplémentaire pour les autres applications.

Suchu possède sa liste de PID et collecte réellement leurs sorties avec WAIT
NOHANG. Les lancements effectués par les services GUI/shell sans contrat wait
sont détachés, évitant de conserver des zombies sous ces parents permanents.

## Corrections génériques découvertes

- `set_text` d'un Input mettait son caret à la nouvelle fin mais conservait
  l'ancienne ancre. Effacer le champ à la soumission donnait une sélection hors
  limites, puis `Invalid_argument(String.sub)`. Caret/ancre/offset sont désormais
  cohérents ; un champ disabled ne continue pas à recevoir l'édition.
- Un Timer rendait automatiquement le runtime busy, même pour une période
  d'une seconde. Le runtime attend maintenant les événements avec la prochaine
  deadline de timer/caret ; les deadlines demandées par l'application se
  combinent par minimum. Les compteurs temporels suivent le temps monotone.
- Une fermeture de fenêtre depuis le stack d'un enfant attribuait le clavier
  physique à ce PID via `capture_current`, alors que le dispatcher GUI doit
  rester son lecteur. L'attribution désigne maintenant le service GUI.
- La compilation Suchu passait une archive absolue non échappée à `-cclib`,
  cassant les paths contenant des espaces. L'autolink cmxa utilise la même
  archive fraîchement staged sans cet argument redondant.

## Build et preuves

```sh
VEX_TOOLCHAIN_WORK="$PWD/build/toolchain-work" bash scripts/build-raylib.sh --c-only
VEX_TOOLCHAIN_WORK="$PWD/build/toolchain-work" bash scripts/build-suchu.sh
bash scripts/build-suchu-apps.sh
bash scripts/build-suchu-editor.sh
bash scripts/build-suchu-browser.sh
bash scripts/build-suchu-paint.sh
```

Dans le Terminal :

```
run /usr/native-tests/suchu_terminal_test
run /usr/native-tests/suchu_files_test
run /usr/native-tests/native_console_test
launcher
files
paint
```

Premiers résultats réels en VM BIOS Core2, 2 Gio, 1 vCPU : modèle Terminal
**10 PASS**, modèle filesystem Suchu **23 PASS** (dont 153000 bytes, liens,
copies/arbre/rename/move/permissions/non-overwrite/cleanup), frontière console
**10 PASS** (pointeurs kernel/invalide/range/readonly, fenêtre sans lease,
délégation interdite, flags inconnus/PID nul). L'interface Files a créé, renommé
et copié un dossier réel sous `/tmp` ; ses captures sont dans
`build/suchu-files-new-folder.png` et `build/suchu-files-rename-copy.png`.
Les tests sont exécutés sous KyIlOS, pas présentés à partir d'un benchmark hôte.

Validation finale 4 vCPU : frontière console/délégation **10 PASS** ; saisie
`echo focus-restored` après fermeture du child du smoke, sans clic de réparation ;
launch Applications puis Files, fermeture de Files puis clic Editor correctement
reçu ; texte réellement édité et confirmation des modifications non sauvegardées ;
Browser lancé avec NET_CONNECT=128 et WebKit jusqu'à `web view created`/main loop ;
tracé réellement dessiné dans Paint. Un deuxième lancement Terminal rejoint la
première console et ferme son frontend surnuméraire, dont le statut est collecté.

Le test `file:///usr/linux-tests/webkit-render.html` ne valide pas le rendu de
page : WebKit annonce MIME inconnu/unsupported et télécharge les 3621 octets.
Cette lacune est rapportée séparément ; aucune navigation simulée n'a été ajoutée.
Les grandes fenêtres préexistantes de Paint/Browser dépassent encore un écran
1024x768 ; cette passe ne refond pas la politique de placement du compositor.
Les améliorations de framework ne constituent pas une mesure comparative de
performances du langage contre C/LVGL ni une validation physique du D630.
Le shutdown natif a ensuite fermé le desktop de test et ses applications Suchu
via S5 à quatre vCPU. Le Kernel reste responsable du shell natif existant ;
la migration porte sur l'application Terminal et son modèle d'affichage.

L'ISO normale `build/vex-os.iso` a été mise à jour et est identique à
`build/kyilos-suchu.iso`. `scripts/run-virtualbox.ps1` charge donc directement
les nouvelles apps. Image BIOS USB-HDD séparée : `build/kyilos-suchu-usb.img`.
L'image Dell précédente est conservée. Le manifeste
`build/portable-iso-provenance.json` contient les SHA-256 vérifiés.
SHA-256 ISO normale : `0E92DCABEE4B60C43E86E65FA7967FCD9B34E1BE72FCA26A22D216E6A5E662DE`.
SHA-256 USB : `67C53EF0AF03E852D0EAFE1EAC4B192F13C17DAEC7643FE1844CABD44085EE88`.

Captures supplémentaires : `build/suchu-4cpu-focus-restored.png`,
`build/suchu-final-launcher.png`, `build/suchu-editor-text-4cpu.png`,
`build/suchu-paint-stroke-4cpu.png`, `build/suchu-console-ten-checks.png`.
Le log final est `build/suchu-desktop-4cpu.log`, avec le build dans
`build/suchu-desktop-kernel-final.log` et `build/suchu-all-apps-build.log`.

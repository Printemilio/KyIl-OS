# KyIl Editor — migration native Suchu

## État et architecture

L'application normale est `/usr/bin/kyil-editor`, compilée depuis
`userspace/suchu/examples/editor.suchu`. Le bureau (entrée Editor), Spotlight,
GFM/OPEN_PATH et le launcher utilisent ce chemin. L'ancien
`userspace/apps/vex_editor.c`/`vex_editor` reste un artefact de diagnostic ; sa
logique LVGL n'est ni appelée ni embarquée comme moteur de l'application Suchu.
L'ancien service `kernel/src/text_editor_ui.c` reste séparé/inactif dans ces
parcours normaux. Aucun fichier du Suchu original sur le Desktop n'a été modifié
ou compilé : tous les changements sont dans la copie intégrée.

L'ancien éditeur normal C/LVGL avait un seul document, un chargement tronqué à
128 Kio et pas de commande de sauvegarde. La migration conserve son ouverture
de fichiers et ajoute un véritable modèle éditable/enregistrable.

Le pipeline est :

```
entrée bureau/GFM → contexte natif ou argv → application Suchu compilée
  → modèle documentaire Suchu → Canvas/widgets Suchu → Raylib software
  → Surface native → compositor → framebuffer
```

Suchu possède documents, onglets, curseur/sélection, historique, commandes,
menus, hit-test, navigation, scroll, dialogues, publication des sauvegardes et
confirmations. Il n'y a aucun helper C/OCaml de piece table, édition, recherche,
undo, sélection ou politique documentaire.

`editor_model.suchu` utilise une piece table segmentée : source immutable,
ajouts de taille bornée pour la frappe, fragments `[source, offset, length,
newline-count]` fusionnés lorsqu'ils sont contigus. Chaque segment indexe ses
LF ; les opérations de ligne parcourent les fragments et font des recherches
binaires dans ces index. Les états d'historique partagent les octets, pas des
copies du document. L'historique est borné à 256 transactions et les segments
inaccessibles sont collectés périodiquement par Suchu.

Les bornes internes sont des offsets de **bytes UTF-8** ; les déplacements et
colonnes utilisent des code points et des colonnes visuelles (tabulations à 4).
LF/CRLF sont conservés byte pour byte, et CRLF est un atome de navigation. Une
édition qui couperait un caractère ou CRLF est refusée. Les métadonnées de
contenu sont construites avant publication d'un état complet. Les caches de
caret/clipping évitent de relire/recopier un long préfixe à chaque frappe. Seules
les lignes/colonnes visibles sont rasterisées. La caret est solide ; l'éditeur
n'installe pas de timer de polling pour son viewport.

## Bridges techniques

* `platform_backend.ml` : primitives génériques de bytes/UTF-8, descripteurs
  Unix natifs, read/write partiels, stat/permissions, fsync, rename et erreurs.
* `platform_stubs.c` : contexte OPEN_CONTEXT, observation du flux input Raylib,
  activation de la confirmation de fermeture et reset du latch Raylib.
* `rcore_vexos.c` : nouveau mode opt-in CLOSE_CONFIRM. Aucun modèle documentaire.

Le presse-papiers Vex système n'existe pas dans cette version. Le vrai clipboard
Raylib, **privé au processus**, est utilisé entre documents/onglets et signalé
comme local dans la barre de statut. Aucun partage inter-apps n'est annoncé.
Le picker Windows/zenity n'est pas invoqué : les dialogues de chemin sont des
widgets natifs Suchu ; GFM fournit aussi la sélection de fichiers existants.

## Sauvegarde et erreurs

La politique de sauvegarde est Suchu : création exclusive d'un temporaire
adjacent → écriture de tous les fragments → fsync → close → permissions du
fichier existant (0600 pour un nouveau) → rename VexFS. Le document ne devient
enregistré qu'après publication réussie. Une erreur laisse le texte modifié et
le fichier original intact ; le temporaire récupérable est conservé et son nom
est signalé. Une collision de temporaire est une erreur explicite, sans retry
arbitraire. La lecture boucle sur les retours partiels et vérifie la taille.

Le remplacement est sérialisé par VexFS, mais aucune atomicité contre une panne
au milieu de ses écritures multi-secteurs n'est revendiquée. La persistance
après reboot nécessite un backend disque. Sans HDD, le log Vex dit explicitement
`VexFS keeps every file in memory` : fsync ne transforme pas ce backend RAM en
stockage durable. Les tests utilisent un VDI dédié, pas un disque utilisateur.

## Corrections d'infrastructure démontrées pendant la validation

* Suchu GUI : `visible`/`enabled` étaient ignorés à la construction ; appliqués
  réellement et déclarés dans le checker. Une invalidation de layout en onDraw
  se termine avant la prochaine attente. `draw.measure` utilise le même spacing
  que `draw.text`. Le scroll d'un Input révélé/redimensionné est borné à la
  largeur de son contenu, empêchant une chaîne courte de rester hors champ.
* Compiler : le corps d'une lambda est délimité ; le wildcard d'arité ne peut
  plus devenir un handler du `try ... with` interne et masquer l'erreur réelle.
  Régression compilé/interprété `function_exceptions.suchu`.
* Une collision applicative entre le compteur `position` et la fonction globale
  homonyme du modèle a été éliminée (`offset_at`) ; régression exacte de clipping.
* Fermeture : flag natif opt-in CLOSE_CONFIRM, demande consommée par poll,
  fenêtre conservée jusqu'à acceptation par Suchu. Les autres apps gardent leur
  fermeture habituelle. Cancel ne détruit ni fenêtre ni document.
* Cadres LVGL : les redraws partiels consommaient un slot par appel jusqu'à
  saturer la limite de dix. Les frames natives réutilisent maintenant le
  **window_id**, avec ordre de stacking explicite, sans redraw desktop permanent.
* Focus : fermeture/reap du foreground transfère le focus à la prochaine fenêtre
  visible, évitant de perdre le clavier après fermeture de GFM.
* Desktop : Suchu Editor et GFM sont détachés du service GUI qui ne consomme pas
  de wait status. Le launcher collecte les statuts de ses propres enfants.

## Commandes

Raccourcis : Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+W, Ctrl+Tab/Shift+Tab,
Ctrl+A/C/X/V/Z/Y, Ctrl+Shift+Z, Ctrl+F/H, Ctrl+Home/End, Ctrl+Left/Right,
Shift+navigation, PageUp/Down, Tab/Shift+Tab. Escape quitte le panneau courant
et ne ferme pas arbitrairement la fenêtre. Menus File/Edit/View et commandes
directes sont natifs. Wheel scroll vertical ; Shift+wheel horizontal ; les
scrollbars peuvent être manipulées à la souris.

Build dans la copie, avec le toolchain existant :

```sh
scripts/build-raylib.sh --c-only
scripts/build-suchu.sh
bash scripts/build-suchu-editor.sh
bash scripts/build-suchu-browser.sh # relink de la non-régression
toolchain/bin/x86_64-unknown-vexos-gcc -O2 -g -Wall -Wextra \
  userspace/libc/tests/suchu_test.c -o usr/native-tests/suchu_test
```

Compiler les sources kernel touchées avec les flags du projet, stager
`build/vex-os`, `usr/bin/*`, `usr/lib/suchu/*`, les sources/fixtures Suchu et les
tests natifs, puis `scripts/repack-iso.ps1`. `scripts/build.ps1` stage déjà `usr`.
Le launcher C/LVGL est reconstruit via sa recette dans
`scripts/build-webkit-userspace.ps1` (ou ses objets ciblés + liblvgl).

Tests guest :

```
run /usr/native-tests/suchu_editor_test
run /usr/native-tests/suchu_test
```

## Preuves exécutées

Captures/logs dans `build/suchu-editor/`, comprenant aussi les échecs antérieurs
qui ont conduit aux corrections (pas comptés comme passes).

* `model-final-4.png`, `model-readback-1.png` : **3366 checks PASS**, dont oracle
  indépendant de bytes/lignes, edits aléatoires, undo/redo/no-op, UTF-8/CRLF,
  refus des splits invalides, permissions, sauvegarde vide/201 Kio/readback.
* `saved-manual-4.png`, `durable-editor-4.png` : saisie de deux lignes, Save as,
  fermeture/réouverture GFM, remplacement world→planet et **readback après reboot**.
* `copied-pasted-4.png`, `redo-4.png`, `undo-clean-4.png`, `replaced-all-4.png` :
  sélection clavier, clipboard réel local, undo/redo et remplacement.
* `two-tabs-4.png`, `window-close-confirm-4.png`, `close-cancel-retained-4.png` :
  contenu indépendant, dirty, confirmation, annulation avec texte conservé.
* `large-scroll-4.png` : 40001 lignes / 201 Kio, navigation EOF et wheel réels.
* `mouse-selection-delete-4.png`, `resize-editor-4.png` : plage multiligne à la
  souris, suppression/undo, déplacement/redimensionnement avec contenu conservé.
* `utf8-real-4.png`, `unicode-readback-1.png` : touche PS/2 é, colonne par caractère,
  Save as et readback UTF-8 après reboot en mono-CPU.
* `suchu-pass-1.png` / log : **32 checks, 0 failed**, six différentielles
  run/comp exécutées et compilation GUI dans le guest.
* Counter compilé dans KyIlOS et navigateur réel sont revalidés séparément.
  `counter-click-1.png` atteint Count: 1 ; `browser-painted-1.png` contient le
  document WebKit texte/CSS/SVG/canvas animé, pas son buffer initial blanc.
* Payload final 4 CPU : `final-focus-handoff-4.png` montre la saisie immédiate
  après fermeture de GFM, sans clic de reprise. `repeated-open-final-4.png`
  vérifie une autre ouverture propre ; `save-error-real-final-4.png` montre un
  refus de rename vers le système, avec texte conservé/Modified et temporaire
  signalé. `final-clean-idle-4.png` : **6 tâches système, 99.9% idle**, plus aucune
  fenêtre/processus Editor ou GFM. Aucun panic/erreur GUI dans le log final.

Le disque `build/suchu-editor/editor-validation.vdi` est un artefact de tests.
La configuration VM originale sans HDD est restaurée après validation. Pour
réutiliser le volume persistant :

```powershell
.\scripts\run-virtualbox.ps1 -CpuCount 4 -DataDisk "$PWD\build\suchu-editor\editor-validation.vdi"
```

Le launcher a été reconstruit avec sa collecte d'enfants ; son bouton Editor
n'a pas été exercé graphiquement dans cette passe. Le parcours normal bureau
et le parcours GFM ont été exercés, sur le vrai binaire staged.

Fichiers créés : `editor.suchu`, `editor_model.suchu`, `editor_test.suchu`,
`platform_backend.ml`, `platform_stubs.c`, `scripts/build-suchu-editor.sh`,
`function_exceptions.suchu` et ce rapport. Fichiers adaptés :
`gui_backend.ml`, `cli.ml`, `transpiler.ml`, `tools/export_browser.ml`,
`scripts/build-suchu.sh`, `userspace/raylib/rcore_vexos.c` et son header observer,
`kernel/include/unos/vex_abi.h`, `kernel/src/app_surface.c`, `kernel/src/gui.c`,
`kernel/src/gui_overlays.inc`, `kernel/src/vex_lvgl_ui.c`/header,
`userspace/apps/vex_launcher.c`, `userspace/libc/tests/suchu_test.c`.

Les scénarios graphiques ne constituent pas une certification exhaustive ni une
campagne d'injection OOM/pannes disque. Les accès simultanés à un même fichier
depuis des applications distinctes n'ont pas été stressés ici.

## Limites explicites

Clipboard inter-processus/IME natif absent ; navigation par code points, pas
par clusters de graphèmes/UAX29 ; le jeu de glyphes intégré couvre Latin,
grec/cyrillique et une partie des symboles, pas toutes les écritures Unicode.
Les octets UTF-8 non représentables par la police sont néanmoins conservés.
Recherche littérale sensible à la casse ; pas de regex/syntax highlighting.
Le coût de manipulation des fragments est linéaire dans leur nombre, malgré
les index/caches ; ce n'est pas encore un arbre équilibré de millions de pièces.

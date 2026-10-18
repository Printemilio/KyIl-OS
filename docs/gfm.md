# GFM — Graphical File Manager

GFM remplace l'ancien « Vex Files » / File Manager. C'est une application native Vex
(fenêtre `VEX_SYS_APP_WINDOW_*`, LVGL, binaire `/home/root/bin/gfm`), lancée par le menu
du bureau, Spotlight (« GFM »), le shell (`gfm`, ou `files`) ou le lanceur.

## Fonctions

* Navigation réelle : dossiers, parent, retour/avance (16 niveaux), champ de chemin
  (Entrée pour y aller, chemins relatifs et `..` acceptés, erreur claire sinon),
  « Places » : Root, Home, Desktop, Downloads (`/home/root/Downloads`, créé à la demande).
* Liste triée (dossiers d'abord), taille, type ; sélection, double-clic ou Entrée pour
  ouvrir, frappe de lettres pour sauter à une entrée, flèches haut/bas, Backspace = parent.
* Nouveau dossier, renommer, supprimer (avec confirmation et nombre d'éléments),
  copier / couper / coller (fichiers et arbres de dossiers ; noms `x copy`, `x copy 2`).
* Panneau Info : nom, type, taille, permissions, propriétaire, emplacement, contenu ;
  « Allow running » / « Forbid running » (bit exécutable) pour lancer un binaire
  téléchargé.
* Ouverture par le mécanisme Vex existant (`vex_open_path`) : programme exécutable →
  lancement, texte → Vex Editor. Un binaire sans bit exécutable est refusé avec un message.
* Rafraîchissement : après chaque opération, bouton ↻, et vérification périodique (1 s) :
  les fichiers qui apparaissent (téléchargements de Vex-Browser, y compris
  « Download in progress ») s'affichent sans action. Redimensionnement/maximisation
  re-disposent l'interface.

## Architecture (`userspace/apps/gfm/`)

| Fichier | Rôle |
|---|---|
| `gfm_sys.h`, `gfm_sys_vex.c` | couche fine sur les appels fichiers natifs (liste, stat, open, read, write, rename, mkdir, rmdir, chmod) |
| `gfm_fs.{h,c}` | modèle et opérations (chemins, noms, liste, copie, déplacement, suppression…), sans UI, testable sur l'hôte |
| `gfm_view.{h,c}` | vue LVGL : widgets, mise en page, états ; aucune logique FS |
| `gfm.c` | contrôleur : état, historique, presse-papiers, clavier, boucle d'événements |

Limites VexFS respectées : chemins de 127 octets, un dossier ne peut pas être renommé sur
place (renommer/déplacer un dossier = copie de l'arbre puis suppression), 64 entrées de
table au total, `rmdir` supprime tout l'arbre.

## Corrections noyau nécessaires

* `fs_access_path` : un dossier implicite (sans marqueur, ex. `/home/root`, `/home`) était
  listé mais refusé à l'ouverture ; il est maintenant lisible.
* `rename(2)` remplace un fichier existant (POSIX) — voir `browser-downloads.md`.
* `gui_spawn_with_context` : le contexte d'ouverture était enregistré *après* le spawn ;
  sur plusieurs CPU l'éditeur le réclamait avant (« New document » vide). Enregistrement
  préalable en slot « pending ».
* `vex_sys_app_window_resize` validait la requête native avec l'ancien test d'adresse ;
  aucune app ne pouvait être redimensionnée (le compositeur étirait l'image). Aligné sur
  `vex_sys_app_window_poll` ; un `CONFIGURE` de taille identique est accepté sans
  réallocation GPU.

## Tests

* Hôte : `userspace/linux_tests/gfm_fs_test.c` (+ `gfm_sys_host.c`), ASan/UBSan.
  `gcc -std=gnu11 -fsanitize=address,undefined userspace/linux_tests/gfm_fs_test.c
  userspace/linux_tests/gfm_sys_host.c userspace/apps/gfm/gfm_fs.c`
* VM (4 CPU), entrées PS/2 réelles : navigation (Root/Home/Downloads/chemin saisi,
  chemin invalide), création/renommage de dossier, copie/collage/déplacement,
  suppression confirmée, Info, bit exécutable + lancement, ouverture d'un texte dans
  l'éditeur, double-clic, clavier (flèches, Backspace, frappe), redimensionnement,
  téléchargements Vex-Browser visibles dans Downloads.

## Limites

* Une seule sélection (les événements fenêtre n'ont ni modificateurs ni clic droit) ;
  pas de molette (défilement par glisser et par flèches) ; pas de touche Suppr/F2/Ctrl+X
  (les apps ne reçoivent pas ces touches) : les boutons de la barre d'outils les remplacent.
* Pas de glisser-déposer, ni de dates (non exposées par l'API native de `stat`).
* Au plus 128 entrées affichées par dossier (limite de `VEX_SYS_DIRECTORY_LIST`).
* `kernel/src/file_manager_ui.c` (ancien gestionnaire intégré au noyau, non lancé) est
  conservé tel quel.

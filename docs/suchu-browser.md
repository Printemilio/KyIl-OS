# Navigateur KyIlOS en Suchu

Le lancement Browser du desktop/Spotlight/shell utilise `/usr/bin/kyil-browser`,
un ELF natif traduit depuis `userspace/suchu/examples/browser.suchu` puis compilé
par OCaml 5.4. Il ne lance pas l'ancien navigateur C pour son interface.
Le moteur reste le vrai `webkit_linux_main` WPE/WebKit/JSC, non modifié.
Le dossier Suchu original n'est ni compilé ni modifié.

## Responsabilités

* Suchu : liste/identité/sélection/fermeture des onglets, drafts d'adresse,
  commandes navigation/historique/reload/stop, focus et hit-test du contenu,
  routage clavier/pointeur/wheel, visibilité des pages et UI téléchargements.
  New Page, tabs et contrôles sont natifs Suchu/Raylib ; aucun chrome HTML.
* GUI Suchu/Raylib : layout, Input UTF-8/selection/caret, widgets, dessin du
  chrome, fenêtre RAW_INPUT et surface software native. Ajout de helpers
  `focus`, `blur`, `is_focused`, `get_bounds` et cleanup avant fermeture du contexte.
* `browser_backend.ml` : valeurs Suchu ↔ tuples/arrays OCaml, modules natifs,
  conversion des erreurs du transport et hook de fermeture.
* `browser_stubs.c` : bootstrap SHM/capabilities, processus WebKit, endpoints
  messages/attachments, validation des frames, texture importée et ACK.
  Aucune logique de tabs, éditeur d'adresse ou hit-test dans ce fichier.
  La normalisation d'adresse réutilise le helper natif testé existant.
* WebKit : vrais WebViews/historiques et processus, réseau, HTML/CSS/JS/DOM,
  Skia/EGL/GLES et surfaces produites. NET_CONNECT est explicitement accordé
  au lancement par le desktop et hérité ; aucune stack réseau privée Suchu.

## Pipeline et durée de vie

`WebKit → Surface/capability IPC → vue READ/NX → conversion BGRA/RGBA → texture
Raylib software → app Surface → compositor`.

Le stride Surface est en **pixels uint32_t**, le pitch source en octets vaut
`stride * 4`. Le helper vérifie bounds/padding/formats et alpha prémultiplié.
Pas de snapshot dans le transport normal. Il reste des copies nécessaires
avec le backend Raylib actuel : swizzle/upload, rendu/readback software et
présentation native. Ceci n'est pas annoncé zéro copie.

Une vue READ et une staging buffer sont réutilisées par page. La vue est
libérée avant CONFIGURE/resize, changement de backing ou destruction. Elle
n'est lue qu'après réception d'une nouvelle frame complétée. ACK suit la copie
réelle dans la texture indépendante ; le producteur peut alors réutiliser ses
octets sans affecter les draws différés Raylib. Texture, mapping, capability et
endpoint ont leur cleanup avant le contexte graphique, y compris après erreur.
Les queues gardent EAGAIN/backpressure ; URI trop longue ou protocole invalide
reste une erreur. Les reports READY retardés d'une page fermée relâchent leur FD.

L'observer Raylib est privé au processus et s'exécute sur son thread UI. Les
callbacks WebKit ne touchent aucun objet OCaml : ils restent dans la boucle
GLib du processus contenu. Les événements sont routés avant édition Input ;
Enter est soumis après édition, incluant les derniers caractères du batch.

## Corrections système causales

Ctrl+Tab était consommé globalement pour le navigateur noyau/switcher. Le
dispatcher conserve le shortcut legacy unique, publie les edges RAW et laisse
les apps RAW_INPUT les traiter. Alt+Tab et les autres shortcuts desktop restent
globaux. Pas de consultation GUI depuis le gate clavier.

Le chargement WebKit concurrent avec compilation native a exposé des buffers
ISO/DMA globaux non protégés. Le parcours ISO utilise un secteur privé ; cache
et chunk pread sont protégés séparément. Les backends disque/CD ont un gate
chacun autour de leur unique slot/bounce buffer. Les IRQ restent autorisées,
sans préemption du contexte protégé ni verrou global kernel. Une corruption
réelle faisait échouer ld sur libasmrun.a et disparaître des répertoires ISO.

## Validation acquise

Artefacts sous `build/suchu-browser/` et `build/vex-serial.log` :

* lancement depuis menu desktop, vrai Suchu/Raylib et vrai WebKit ;
* file:// : texte/CSS/SVG/canvas/animation, hashes différents de frames et pixels
  non noirs ; clic DOM, hover, focus et champ `suchu`, scroll réel (160 px) ;
* resize/déplacement : DOM `resize=2`, viewport `908x581`, champ conservé ;
* controls WM minimize/maximize/restore : geste consommé jusqu'au release,
  aucune création de page parasite ; le hit-test Suchu rejette les positions
  hors rectangle client, y compris le y négatif de la barre native ;
* pages distinctes, CREATE/SELECT/CLOSE, Ctrl+T/Ctrl+Tab/Ctrl+W et changement de
  pages dans le même boot ; BACK/FORWARD/RELOAD arrivent au vrai WebKit ;
* HTTPS example.com h2 : un run DOMInteractive 525 ms, complete 551 ms, FCP
  563 ms ; HTTP réel http/1.1 et historique, sans contenu substitué ;
* fermeture de deux pages/navigateur : 7 tâches système, 99,9 % idle ;
  réouverture d'une nouvelle session ;
* Suchu natif : PASS 29 checks, 0 failed, cinq différentielles run/comp et
  compilation GUI/counter (~13,8 s) ; Counter compilé dans l'OS lancé ;
* quatre lecteurs natifs vérifient 48 lectures complètes de libasmrun.a et
  libgiognutls.so contre les hashes exacts stagés, avec stat du répertoire,
  pendant le démarrage WebKit : PASS ;
* decoder clavier réel hosted : modifiers/edges/owner isolation/legacy fallback
  et Ctrl+Tab/Alt+Tab PASS ; pixels : stride/padding/alpha/formats/bounds PASS.

Le smoke ne constitue pas une campagne globale Vulkan/SMP/conformité ; aucune
nouvelle validation de l'ensemble des anciens lots n'est revendiquée ici.

## Build

Dans la copie KyIlOS, avec le cross toolchain existant :

```
scripts/build-raylib.sh --c-only
scripts/build-suchu.sh
bash scripts/build-suchu-browser.sh
python3 scripts/build-iso-reader-test.py # régression ISO, hashes du payload stagé
```

La traduction host déclare seulement les noms de modules disponibles ; le binaire
enregistrera leurs vraies implémentations target avant évaluation. Suchu run et
comp utilisent aussi le binding browser réel. L'ancien `/webkit` et son chrome
noyau sont conservés pour diagnostic ; le startup C est partagé via
`webkit_session.h`. Les noms internes/ABI Vex ne sont pas renommés massivement.

## Limites et validations restantes

L'interface conserve thème/disposition/fonctions ; le cadre est maintenant celui
d'une app native, avec son titre en plus de la barre d'onglets. Les commandes
minimize/maximize du module chrome Raylib restent limitées ; les boutons du
cadre sont gérés par le WM natif. GPU matériel, shaders Raylib programmables,
clipboard système partagé et audio restent les limites antérieures du port.
Les reports/cancel/open de téléchargements sont implémentés, mais un transfert
de téléchargement complet n'est pas encore validé dans cette passe. Aucun
benchmark statistique de navigation ou run prolongé de fuite n'est revendiqué.

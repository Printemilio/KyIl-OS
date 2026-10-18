# EGL/GLES et reprise WebKit — travail en cours

Lot 37 reste en pause, non terminé. Cette passe utilise sa fondation programmable
sans convertir GL vers les fonctions publiques Vulkan.

## État de référence après cette continuation

Le vrai `/webkit`, avec le document de régression opt-in, produit 64 snapshots
1280×720 stride 5120, 59 hashes distincts et un titre allant jusqu'à
`VexRender:139:image=true:sum=-1039155312`. Texte/font, HTML/CSS, SVG, canvas,
courbes, mutations DOM et timer JavaScript sont réellement visibles. Aucun
abort, panic ou échec de draw n'est observé dans ce run. Preuves :
`build/webkit-curves-animation-64-vm.log`, son JSON de validation et
`build/webkit-curves-fixed-screen.png`.

La version finale sans dumps temporaires passe 451 contrôles hôte sous
ASan/UBSan (`build/gl-final-asan.log`), 1278 contrôles EGL/GLES/VSL en VM,
83 cas sémantiques Vulkan et 17 régressions Linux
(`build/egl-1278-vulkan-final-vm.log`). Le churn heap natif de 800 MiB passe ;
128 cycles d'event counters donnent un delta PMM nul. Le smoke Vulkan finit
toujours avec exit=1 pour la couverture incomplète, suivi de suite completed.
Ce résultat n'est pas masqué ni assimilé à une conformité Vulkan.

L'ISO finale est reconstruite avec le document normal du navigateur et le
module auto_webkit. SHA256 runtime root/staging identique :
`67be64e9b887c223fd00abd13a572c3d5ba2fb1037cb94643eecdd83bf52b89f`.
Le document animé reste un test séparé, sans remplacement du runtime WebKit.

Limite de stabilité mémoire : les 24 derniers mmap de bitmap côté producteur
voient PMM libre passer de 916218 à 916061 pages, soit 628 KiB de baisse.
Cette dérive n'est ni une preuve de fuite ni une consommation constante ; sa
cause reste à mesurer par catégorie (caches/userspace/kernel/backings).
Les tests déterministes de dernière référence passent, mais ne suffisent pas
à conclure à l'absence de fuite sur une longue session animée.

## Chemin réel

`EGL/GLES → vex_render_core → moteur partagé SPIR-V/raster/sampling → pixels`.
Les fonctions communes se trouvent dans `vex_render_engine.h`,
`vex_render_quad.h` et `vex_render_types.h`. Vulkan conserve ses queues
asynchrones ; le frontend GL CPU exécute ses commandes sous son verrou de
contexte. La compilation GLSL utilise le vrai glslang Khronos 15.1 vendored,
reconstruit par `scripts/bootstrap-glslang.sh`. SPIR-V reste privé au runtime.

Une seule DSO `libvex_graphics.so.1` contient les états GL/EGL. Les alias
`libEGL.so.1`, `libGLESv2.so.2`, `libGL.so.1` dépendent de cette DSO : ils ne
créent pas plusieurs copies des contextes ou des espaces de noms.

EGL fournit display/config, contexte ES2, pbuffer, surface de fenêtre Vex,
make-current exclusif par thread, partage d'objets, destruction différée des
objets courants et détachement à la sortie du thread. La fenêtre WPE fournit un
descripteur privé de surface et des callbacks retain/release/present. Le backing
BGRA natif est présenté par capability Surface ; l'ACK suit la completion
compositor, avant réutilisation de ses pixels. Le resize alloue le remplacement
avant de remplacer l'ancien backing. Le snapshot WebKit demeure un chemin
distinct, avec ses propres pixels et transfert SHM.

## Couverture vérifiée, sans revendication de conformité

Shaders vertex/fragment compilés et exécutés, fonctions/matrices/uniform arrays,
attributs, triangles et textures 2D, filtres nearest/linear distincts min/mag,
repeat/clamp/mirror, FBO, copies de textures, blend, profondeur/stencil,
masques/scissor et lifecycle partagé. Les dérivées et LOD implicite passent par
quatre véritables invocations voisines avec helper lanes ; aucun gradient
constant n'est inventé. OpSelect accepte maintenant ses conditions booléennes
scalaires ou vectorielles conformes au type du résultat.

`GL_OES_rgb8_rgba8` possède de vrais renderbuffers et requêtes de précision.
RGB8 ne possède pas de canal alpha writable ; son padding opaque ne change pas
pendant clear/draw/blending. RGBA8 conserve son alpha. L'extension n'ajoute pas
implicitement ES3, MSAA ou texture-storage.

Preuves conservées :

- `build/gl-source-compiler-trace.log` : 327 contrôles hôte, compilateur reconstruit.
- `build/gl-source-asan.log` : 327 contrôles ASan/UBSan, frontend et moteur instrumentés.
- `build/egl-418-vulkan-vm.log` : 418 contrôles VM, dont six présentations et
  dessin conditionnel programmable, orientation et resize.
- `build/gl-rgb8-advertised.log` : 354 contrôles hôte avec RGB8/RGBA8.
- `build/egl-445-vulkan-vm.log` : RGB8/RGBA8, six présentations et 445 contrôles.
- `build/gl-rgb8-asan.log` : 358 contrôles hôte ASan/UBSan, dont requêtes des
  précisions du framebuffer actuellement lié.
- `build/egl-465-vulkan-vm.log` : 465 contrôles VM, dont transfert typé Surface,
  présentation refusée sans COMPOSE, réussie avec COMPOSE, aucune écriture
  autorisée par la capability importée, fermeture indépendante. Régressions
  Vulkan/syscall complètes ; delta PMM nul sur 128 cycles event-counter. Le
  critère final incomplet Vulkan reste volontairement rouge (exit 1).

## Limites actuelles

EGL expérimental 1.4, config `EGL_NON_CONFORMANT_CONFIG`, `EGL_CONFORMANT=0`.
ES3 et contextes desktop GL refusés. L'alias libGL n'est pas une implémentation
desktop GL. Pas de conformité ES2 revendiquée. Textures actuellement mono-mip,
sans cube/compression ; glGenerateMipmap échoue explicitement. Les textures par
défaut et création de noms à bind restent incomplètes. Les anciens formats
renderbuffer packed/depth doivent encore reproduire exactement leur précision,
pas seulement leur capacité de stockage. Les limites shader internes sont
bornées ; les opcodes/profile non supportés échouent, et les quads divergents
aux points de dérivées sont refusés. La présentation WPE directe est encore
distincte de l'intégration complète à la fenêtre du launcher.

## Historique des frontières WebKit et corrections

`build/webkit-skia-capabilities-vm.log` utilise le vrai launcher `/webkit` et les
nouvelles bibliothèques. Les contextes GL sont courants, Skia interroge leurs
capacités, BitmapTexture crée les textures. Le premier snapshot est encore
entièrement nul (`nonblack=0`). La peinture d'une tuile finit dans
`CoordinatedTileBuffer::canvas → SkSurface::getCanvas`, avec `this=NULL`
(`RDI=0`, faute de lecture à 0x28). Le callsite exact est l'offset 0x5e6877e de
la véritable libWPEWebKit, après un tail-call de `canvas`.

Skia ne considère pas RGBA8 rendable en ES2 sans une extension de format
appropriée. L'ancien runtime n'en exposait aucune. RGB8/RGBA8 sont maintenant
implémentés et testés ; le nouveau run WebKit doit établir si cette correction
résout la création de la surface, puis tracer la première divergence suivante.
Cette piste n'est pas encore une validation de pixels WebKit rendus. Les fautes
observées pendant cleanup restent à distinguer et diagnostiquer séparément.
W^X reste strict ; aucun JIT JSC fonctionnel n'est revendiqué.

Le run suivant `build/webkit-rgba8-present-rights-vm.log` dépasse la création des
surfaces : Skia crée/attache ses FBO, vérifie leur complétude et exécute des
clears. Deux snapshots restent entièrement nuls, donc ne constituent pas le
milestone. La frame arrive au ViewBackend, mais la présentation est refusée :
le backend ne transférait pas le droit COMPOSE, exigé par
`compositor_surface_pin` et `compositor_surface_compose`. Le backend accorde
maintenant explicitement QUERY/MAP_READ/PRESENT/COMPOSE, toujours sans
MAP_WRITE/EXPORT/RESIZE. Le test de cette correction passe en VM ; le nouveau
run WebKit doit établir la suite du chemin de rendu réel.

Le run `build/webkit-ack-enqueued-wrong-thread-vm.log` confirme la première
présentation et un ACK enfilé sur le même canal natif, sans réception renderer.
La cause est une confusion d'identités dans `message_waiter_arm_locked` :
`scheduler_current_pid()` désigne le propriétaire process, tandis que
`process_wait_event()` endort le thread courant. Le renderer TID 37 attendait,
mais l'ACK réveillait PID/TID 14. Les waiters IPC portent désormais séparément
PID (ownership/cancellation de groupe) et TID (wake, lifetime d'exécution).
Les waiters Surface avaient le même défaut et utilisent la même distinction.
La trace d'échantillonnage CPU utilise également le vrai TID.

La validation EGL/GLES en VM passe 609 contrôles : elle comprend 16 cycles
worker → frame avec capability Surface → réception principale → ACK sans
attachment → réveil/join du worker, sans polling ni timeout de contournement.
Cela ne prouve pas encore un contenu WebKit rendu : le navigateur doit être
relancé avec le réveil corrigé et les pixels vérifiés séparément.

`build/webkit-ack-fixed-skia-lod-bias-vm.log` prouve ensuite les ACK 1 et 2
ainsi que `target.frame_rendered`. Les snapshots restent noirs : le programme
Skia 23 est refusé au parseur SPIR-V (ancienne ligne 684). Sa source réelle
emploie `texture2D(uTextureSampler_0_S0, texCoord, -0.475)`, donc
OpImageSampleImplicitLod avec ImageOperands Bias. Cet operand est maintenant
validé (fragment GL, float scalaire 32 bits, masque exact) et exécuté : le biais
s'ajoute au LOD calculé à partir des quatre invocations voisines, avant le choix
minification/magnification. Il n'est pas supprimé ni ignoré. Le profil Vulkan
refuse toujours ce cas tant que son propre contrat ne l'implémente pas.

`build/gl-lod-bias.log` et `build/gl-lod-bias-asan.log` passent 380 contrôles
hôte, avec des pixels distinguant les filtres à biais positif/négatif et la
forme constante exacte produite par Skia. Le run VM suivant passe 631 contrôles
EGL/GLES. La validation de vrais pixels du navigateur demeure distincte.

## Découverte des polices et contrats VSL

`build/webkit-css-no-fonts-vm.log` et `build/webkit-bias-screen.png` montrent
le fond CSS #1a1a2e et la règle horizontale réellement peints, sans texte.
Les huit premiers snapshots ont précédé ce rendu ; leurs buffers noirs ne
prouvent pas que les suivants le restent. Les statistiques couvrent maintenant
les 64 premiers snapshots puis un échantillon sur 128.

Deux défauts génériques ont été établis : syscall Linux 79 renvoyait le pointeur
de l'API native au lieu du nombre d'octets incluant NUL ; glibc stocke ce retour
dans un `int`, ce qui fait échouer le getcwd sur pile de Fontconfig et ses glob
patterns. La traduction renvoie désormais le nombre d'octets, ERANGE pour un
buffer court et EFAULT pour une destination non writable, sans changer l'ABI
native. Ensuite, getdents ne parcourait que l'inventaire RAM, pas les fichiers
ISO. Le backend ISO sait maintenant énumérer ses enfants immédiats avec leurs
noms Rock Ridge. L'énumération VSL fusionne les backends dans une liste possédée
par l'objet FD, déduplique les noms, conserve son curseur entre petites lectures
et libère cette liste au dernier close. Un échec de construction déroule la
liste entière ; aucun pointeur vers un secteur ISO n'est conservé.

`build/egl-1156-vulkan-font-abi-vm.log` : 1156 contrôles EGL/GLES/VSL passent,
dont 24 cycles open/read/close de répertoires ISO, petits buffers, noms/types,
curseur partagé par dup, getcwd brut et glibc, ERANGE et pointeurs invalides.
83 cas sémantiques Vulkan et 17 régressions Linux passent ; 128 cycles event
allocation/free conservent un delta PMM nul. Le binaire Vulkan de ce run a le
même SHA256 que le staging. Son verdict final reste volontairement négatif
pour la couverture Vulkan encore incomplète : Lot 37 n'est pas clôturé.
`build/gl-font-abi-asan.log` : 385 contrôles hôte passent sous ASan/UBSan.
Le run WebKit après ces corrections reste à évaluer séparément.

`build/webkit-fonts-black-snapshot-vm.log` et `build/webkit-fonts-screen.png`
confirment ensuite le texte réellement rendu : titre, deux paragraphes, règle
et fond CSS. JavaScript termine le calcul et publie `VexJSC:-1039155312` dans
le titre. Les snapshots restent pourtant noirs jusqu'au numéro 64 au moins.
Ce résultat distingue le rendu de la Surface du retour WebKitImage.

`build/shared-file-remap-failure-vm.log` reproduit la première perte : deux
marqueurs écrits via MAP_SHARED disparaissent après munmap/remap du même FD
resté ouvert. La liste des pages partagées appartenait au backing d'un mapping,
qui la détruisait à sa dernière référence ; le FD conservait seulement son
buffer original. Les pages appartiennent désormais à l'objet FD kernel. Chaque
mapping conserve cet objet, comme les attachments IPC et les FD installés.
Le cache n'a pas de référence retour vers l'objet : il est détruit exactement
à sa dernière référence, sans cycle. Sa clé de page est l'offset fichier absolu,
ce qui permet aussi des alias à tailles/offsets différents. Read/pread/write
sur le même objet voient les pages matérialisées ; fsync/close d'un fichier
ordinaire réconcilient son buffer avant le flush réel. Les fichiers WK2 restent
volatils, jamais flushés vers le disque.

`build/egl-1175-vulkan-shared-cache-vm.log` passe 1175 contrôles, notamment la
conservation sans aucun mapping, aliases de sous-plages, cohérence read/write,
attachment en queue après close producteur, puis close destinataire avant son
dernier unmap. Un run précédent échouait au transfert parce que le nouveau test
demandait des droits 0 au lieu du droit FD DUP requis ; le test a été corrigé,
pas la validation des droits. Les régressions Vulkan/Linux passent également.

L'enquête fonts a aussi révélé que iso9660_read retournait son buffer PMM après
fs_register_boot_module, qui le libérait sans la restriction annoncée par son
ancien commentaire. La copie MRAC est désormais renvoyée ; la source ISO
temporaire est libérée entièrement par son seul propriétaire. La réservation
d'un slot de cache précède MRAC_store : un cache plein ne publie plus de binding
sans taille. L'échec se propage vers les vrais chemins open/stat streaming.
Cela ne change pas le modèle MRAC ni le format disque.

Limites de ce correctif mémoire : cache par objet FD existant, pas nouvelle
identité filesystem globale partagée entre toutes les ouvertures indépendantes.
Le shrink remet à zéro le contenu retiré ; la révocation des PTE déjà présentes
au-delà d'un EOF raccourci reste une limite préexistante, non revendiquée comme
une implémentation complète de SIGBUS sur truncate. Le run WebKit avec ce cache
est encore nécessaire pour prouver le retour de pixels dans WebKitImage.

`build/egl-1212-vulkan-shared-cache-vm.log` : 1212 contrôles passent après ajout
du shrink/extension (préfixe conservé, ancienne seconde page remise à zéro) et
de stat/open/pread des vrais headers TrueType de l'ISO. Les 83 cas sémantiques
Vulkan, 17 régressions Linux et delta PMM nul des 128 cycles event passent sur
ce même kernel. Le verdict final Vulkan incomplet reste négatif.

## Pixels WebKit et snapshots réellement validés

`build/webkit-real-snapshots-vm.log` contient au moins 64 snapshots, sans panic
ni abort observé. Les deux premiers sont blancs (document pas encore peint),
le troisième contient le fond CSS #1a1a2e, puis les snapshots 4 à 64 ont le
hash `b83137590795de9d`, 920415 pixels non noirs et 8278 différents du premier
pixel. `build/webkit-real-snapshot-screen.png` montre le titre, les deux
paragraphes et la règle. Ces octets viennent du vrai WebKitImage et traversent
ensuite le SHM framebuffer du launcher ; il ne s'agit plus d'un buffer noir
dimensionné ni d'un contenu fabriqué par le backend.

Le JavaScript du document publie `VexJSC:-1039155312`, égal au checksum
recalculé indépendamment. Cela prouve son exécution, pas un JIT fonctionnel :
RWX reste refusé et aucun nouveau contrat JIT n'a été introduit.
Sur les 24 dernières réservations de bitmap tracées, PMM libre varie entre
916423 et 916427 pages. Cette mesure courte exclut une fuite d'un bitmap entier
par snapshot ; elle ne constitue pas une preuve exhaustive d'absence de fuite.

La variante de test `WEBKIT_RENDER_SMOKE=1 scripts/build_and_stage_webkit.sh`
charge `userspace/linux_tests/webkit-render.html` par le vrai open/read et la
vraie API WebKit. Le document exerce texte, image SVG, canvas 2D, mutation DOM,
transform CSS et timer JavaScript. La source est gardée jusqu'à l'arrêt de la
boucle. Ce drapeau sert uniquement au test ; la build normale conserve le
document d'accueil. Les titres et hashes des snapshots doivent encore établir
la progression de ce workload animé.

## Validation des textures réellement utilisées

Le document animé révèle deux courbes manquantes (cercle SVG et arc canvas),
malgré le décodage de l'image et les timers JavaScript. Le run préalable est
conservé dans `build/webkit-unused-feedback-before-vm.log` et sa capture dans
`build/webkit-svg-canvas-animation-screen.png`. Il retourne GL_INVALID_OPERATION
sur les draws. Un test indépendant reproduit déjà un défaut avant toute
correction : `build/gl-unused-feedback-failure.log`, ligne 164 du pixel-test,
draw color-only vers une texture FBO toujours liée à une unité.

`gles_draw.h` validait toutes les textures liées, indépendamment des samplers
du programme. Cela refusait notamment un attachment inutilisé par les shaders.
La section 4.4.4 de la [spécification GLES 2.0](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf)
exige aussi que les shaders puissent échantillonner cette unité pour constituer
un feedback. La validation et la construction des ressources sources utilisent
désormais les unités des uniforms sampler du programme lié. Les samplers hors
limites et les feedbacks effectivement utilisés restent refusés ; aucun binding
WebKit n'est modifié pour contourner ce défaut.

`build/webkit-feedback-fix-build.log` et `build/gl-feedback-fix-asan.log` :
394 contrôles hôte passent, dont attachment lié sans sampler, attachment sur
une unité inutilisée et feedback réellement échantillonné refusé sans modifier
les pixels. ASan/UBSan ne signalent pas d'erreur. Ce défaut était réel, mais sa
correction seule ne suffit pas à faire apparaître les courbes.

Le run suivant (`build/webkit-length-before-vm.log`) atteint le shader Skia
de cercle, compte 24 vertices sur un FBO canvas 192×96 et échoue dans
l'interpréteur sur `GLSL.std.450 Length` (instruction 66). Les sources GLSL et
les vertices sont présents dans la trace ; `length(circleEdge.xy)` calcule la
couverture du cercle. L'ancien parser acceptait toutes les OpExtInst à longueur
plausible, alors que l'exécution ne supportait que Abs/Min/Max/Clamp/Mix.
`build/gl-length-before-failure.log` reproduit indépendamment ce défaut avec
une simple longueur de scalar/vector uniforme.

Le moteur commun exécute désormais Length float32 scalaire/vec2/vec3/vec4,
avec calcul mis à l'échelle et sqrt SSE pour éviter un overflow intermédiaire
et garder le renderer natif freestanding. Il valide type et largeur. Le parser
refuse dès le chargement une instruction étendue non implémentée, son mauvais
nombre d'arguments ou son import incorrect. Il ne s'agit pas d'une extension
de l'ABI shader Vex, ni d'un appel GL→Vulkan public.
`build/gl-length-fix-asan.log` passe 451 contrôles : longueurs 3/5/13/85, zéro,
valeur 1e30 pour chaque largeur, et vrai shader sin(angle) compilé GLSL mais
refusé explicitement au link du profil d'exécution. La VM WebKit avec cette
deuxième correction confirme visuellement les deux cercles dans
`build/webkit-curves-fixed-screen.png`. La mesure indépendante de cette capture,
`build/webkit-curves-pixel-validation.json`, compte 3096 pixels bleus pour le
SVG et 1716 pour le canvas, dans leurs régions respectives. Les centres, tailles,
rectangles et texte correspondent au vrai document. Ces pixels viennent des
shaders Skia, pas d'un test d'image injecté dans le backend. Les dumps temporaires
des shaders/vertices et erreurs de chaque return ont été retirés après diagnostic.

La build normale reste séparée du document animé. La couverture desktop GL,
input/scroll, réseau/TLS et intégration complète à la fenêtre native ne sont pas
validés par ces images. JSC exécute ce JavaScript sans pool JIT ; W^X reste strict.

## Passe suivante : viewport natif, input et mémoire

La validation ci-dessus est complétée par
[webkit-native-viewport.md](webkit-native-viewport.md). Le chrome reste natif,
WebKit reçoit le rectangle de contenu et de vrais événements PS/2 traduits par
WPE. Clic/hover/focus, champ texte/clavier/modifiers, wheel avec scroll DOM réel,
lien ancre, déplacement et resize sont vérifiés en VM. Le rendu Surface partagé
remplace la copie du snapshot vers l'ancien framebuffer SHM.
Les 451 contrôles hôte et ASan/UBSan passent ; la VM passe 1280 contrôles EGL/GLES,
dont les erreurs natives MESSAGE_RECV -EAGAIN/-EBADF. L'audit mémoire catégorisé
conserve un run de 305 snapshots : aucun empilement de Surface/capabilities/bitmap,
mais une matérialisation privée encore mesurable. Le rapport conserve cette
limite et ne prétend pas prouver un plateau PMM complet. Vulkan/Lot 37 reste en
pause et son critère final encore incomplet reste rouge.

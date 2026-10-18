# Régression de réactivité du bureau — 2 octobre 2026

## Causes confirmées

1. **Boot diagnostique involontaire.** Le fichier racine `auto_webkit` était
   automatiquement réimporté par chaque build. `main.c` sélectionnait le parcours
   isolé WebKit, qui initialise la GUI mais pas le shell. Le rectangle natif vide
   n'était donc pas un terminal ayant simplement perdu des caractères.
   `build.ps1` choisit désormais explicitement `-BootMode`, avec `Desktop` par
   défaut. Les anciens marqueurs ne sélectionnent plus un mode implicitement.
   Les scripts smoke/stress demandent leur mode ; VirtualBox est arrêté avant
   toute reconstruction de son ISO.

2. **Un fichier ELF traité comme répertoire ISO.** `open` vérifie si un chemin est
   un répertoire, notamment via le suffixe historique `.ladybird_dir`. Dans
   `iso_find_path`, un composant intermédiaire pouvait être un fichier régulier.
   Le lecteur parcourait alors l'extent d'une grosse bibliothèque comme un
   répertoire, secteur après secteur. Les échantillons RIP VirtualBox pointaient
   dans la boucle AHCI `disk_cdrom_read`, pas dans le rasterizer. Le lecteur exige
   maintenant un répertoire avant chaque séparateur, valide les records et
   rejette également `fichier/`. Le test fait échouer la traversée d'un fichier
   de 16 Mio après une seule lecture du répertoire racine.

3. **Amplification des lectures et diagnostics synchrones.** Un fault de 4 Kio
   faisait lire jusqu'à 128 Kio par `iso9660_pread`. Le budget est désormais celui
   des secteurs effectivement demandés, borné par l'EOF. Un cache de répertoires
   immuables est borné à 128 Kio et ne conserve pas les échecs I/O. Les modules de
   boot disponibles sont empruntés avant d'ouvrir un second flux ISO ; leurs
   mappings PRIVATE restent lazy et indépendants. Les traces détaillées
   syscall/futex/poll/paths sont opt-in (`-CompatTrace`), les traces GLES via
   `VEX_GL_CALL_TRACE`. Les erreurs ENOSYS restent réelles, avec répétitions
   journalisées à fréquence décroissante. Le calendrier RTC n'est plus lu à
   chaque tick GUI.

4. **Backlog souris et focus partagé incorrect.** `pop_event` consomme ensemble
   les mouvements consécutifs compatibles. Il conserve les transitions de
   boutons, wheel, inversions de direction et chemins de drag gauche. La lecture
   est protégée contre l'IRQ productrice. Le navigateur ne remplace plus l'identité
   du panneau natif par zéro et n'interdit plus son clavier. Un clic natif retire
   le focus du viewport ; celui-ci garde son propre hit-test/focus.

5. **Console et image présentée sans ownership durable.** Le launcher redirige
   stdout/stderr des descendants WebKit vers un vrai pipe hérité, drainé vers le
   diagnostic natif. Cela ne mélange plus les logs et la ligne du shell.
   Les snapshots ne servent pas au transport normal ; ils deviennent opt-in
   (`VEX_SNAPSHOT_DIAGNOSTIC=1`, ou build de test rendu).
   Un survol de bordure reconstruisait la scène et effaçait la Surface Web :
   `render_active_app` ne détenait pas la dernière image complète à recomposer.
   Le compositor conserve maintenant une version privée terminée avant l'ACK et
   avant de dessiner les fenêtres au premier plan. LVGL ne possède que le titre
   de la fenêtre contenant le viewport externe.

## Contrat de conservation

Le producteur EGL réutilise une Surface mutable après son ACK synchrone. Garder
son pointeur ou sa capability pour relire ultérieurement ne garantirait pas une
version immutable. Le compositor logiciel copie donc **une fois par présentation**
la région déjà composée dans son format framebuffer, et la réutilise lors des
recompositions natives. Cette copie ne provient pas d'un snapshot WebKit ni d'un
readback GPU et ne change pas les API publiques Surface/Vulkan.

`scene_image_store` réutilise l'allocation à dimensions constantes, remplace la
version lors d'un resize seulement après allocation réussie, et garde l'ancienne
sur OOM. La dernière version est libérée à la fermeture du navigateur. Le cache
est borné à une image du viewport, soit environ 3,18 Mio pour 926×899. Il ne
retient ni une référence producteur writable, ni une image par frame. Un OOM
échoue explicitement au lieu d'acquitter une version non conservée.

## Preuves exécutées

- Rebuild complet normal : `build/slow-final-full-build.log`.
- Hôte : les huit exécutables de `scripts/test-native-browser-input.sh` passent
  (ISO, backlog souris, image terminée/OOM, decode souris, viewport, clavier,
  dommages, focus). EGL/GLES : 451 contrôles dans
  `build/slow-production-no-snapshot-build.log`.
- VM, même kernel final : `build/slow-final-regressions-vm.log`.
  EGL/GLES 1280, kill-group exit=0, quatre cycles load/reap + image absente,
  17 régressions syscall vérifiées, tests Surface/Vulkan existants,
  800 Mio de churn heap et 128 cycles event-counter avec delta PMM nul.
  Les erreurs du shader négatif et du compute hors-bounds sont attendues et
  vérifiées. **Le critère final Vulkan incomplet conserve exit=1.**
- Bureau normal : `helpz`, Backspace, Entrée exécute `help` ; une commande
  `echo` de 145 caractères s'affiche et s'exécute avec retour à la ligne.
  Une rafale de 100 déplacements PS/2 atteint le curseur attendu dès la capture
  suivante. Le document reste présent après survol des bordures et focus natif.
  Captures : `build/slow-final-mouse-burst.png`,
  `build/slow-final-usable-desktop.png`.
- Dernier run de production **sans snapshot** : settings 2,76 s, main loop
  3,42 s, load-finished 10,47 s, ACK de la seconde Surface 14,95 s.
  Mesure depuis la fin du clic injecté, précision approximative 100 ms ; ce
  n'est pas une mesure p95 ni un benchmark de toutes les pages Web.
  `build/slow-final-no-snapshot-start-metrics.json` et capture
  `build/slow-final-no-snapshot-web.png` montrent le vrai document.
- Fermeture par le contrôle natif : la fenêtre disparaît et `ps` retrouve
  uniquement les cinq tâches natives initiales, aucun descendant WebKit restant.
  Capture `build/slow-final-browser-closed.png` et log
  `build/slow-final-no-snapshot-vm.log`.
- Kernel, launcher et runtime extraits de l'ISO : hashes identiques aux fichiers
  reconstruits dans `build/slow-final-iso-hashes.json`.

## Limites

Le software renderer reste coûteux : le démarrage Web n'est pas instantané.
Ce travail ne prétend pas rendre toutes les animations/pages fluides, ni établir
la conformité Vulkan. W^X et le contrat JIT sont inchangés ; Lot 37 reste en pause.
Le réglage ancien `WEBKIT_DISABLE_COMPOSITING_MODE` a été essayé comme suspect,
mais sa causalité n'a pas été démontrée : il est conservé. L'effacement reproduit
venait de la recomposition native sans dernière version, pas de ce réglage.

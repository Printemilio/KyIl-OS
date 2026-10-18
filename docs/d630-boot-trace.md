# D630 physique : blocage à 0 % et console de boot

## Ce que l'essai matériel démontre

L'utilisateur a effectivement démarré le Dell depuis `kyilos-legacy-usb.img`.
La barre 0 % est dessinée par `kmain` **après** le retour de `boot_time_init`,
`cpu_enable_sse`, GDT/GS/TSS, `pmm_init` et `tty_init`. Limine → kernel et
l'accès au framebuffer sont donc atteints. Le retour de PMM n'était cependant
pas contrôlé dans cet ancien boot ; un retour n'est pas une preuve de santé.

0 % couvrait : VMM → heap → allocator → SMP prepare → scheduler/FPU → users →
capabilities → syscalls → RTC → PS/2 → HDA → IDT/PIC/LAPIC → startup AP → TSC/RTC
et PIT/LAPIC calibration → timers AP → RNG. L'archive n'arrive qu'après le
stockage/VexFS, au jalon **35 %**. Rien ne permet donc de l'accuser au seul vu
de cet écran. La cause exacte sur le Dell n'est pas encore identifiée.

## Correction réelle, distincte de l'affichage

`vmm_init` mappait toutes les pages de 0 à `highest_physical_end`, incluant
les trous RESERVED/bad/MMIO. Une réservation firmware à 1 Tio causait un
parcours de 268 millions de pages et des tables inutiles pouvant consommer
la RAM. Ce blocage a été observé avant cette passe sous QEMU PC moderne.

Le HHDM bootstrap mappe désormais les plages réelles de RAM/loader/kernel/
modules, ACPI et framebuffer, avec des alias NX et supervisor. Les apertures
MMIO restent explicitement mappées par leurs propriétaires. Le framebuffer
de la réponse Limine est également mappé explicitement, même si le BIOS le
classe RESERVED. Le kernel garde son mapping exécutable distinct ; la pile
bootstrap est NX. Les tables allouées par PMM sont toujours couvertes par la
RAM réelle, de même que le bitmap, les modules, les structures Limine et les
piles AP. Les mappings user/process/lazy/JIT ne sont pas réimplémentés.

Chaque mapping bootstrap est vérifié ; overflow, absence de root ou OOM
ne permettent plus d'installer silencieusement un CR3 incomplet. La fenêtre
firmware 1 Tio reste dans la carte PMM (bitmap/counting inchangés dans ce lot)
mais ne produit plus un mapping linéaire 1 Tio.

## Console visible, sans dépendance au port série

La barre est supprimée. `boot_console` affiche directement dans le framebuffer
Limine, sans heap, GUI, scheduler, service série ou mesure temporelle. Glyphes
ASCII, pitch réel, scrolling et clipping ; supports 32/24/16 bits. Une étape
produit `[....] nom` avant exécution, `[DONE] nom` après retour. DONE indique
le retour de l'étape, pas un périphérique optionnel réellement disponible.

Les marqueurs incluent : CPU, GDT/GS/TSS, PMM, plages HHDM avec base/taille,
switch CR3, heap/ORA foundation, scheduler/FPU, probes matériels, SMP/AP,
calibration RTC/PIT/LAPIC, RNG/health/seed, archive/ORA, FS, drivers, réseau et IPC.
Les exceptions CPU sont prises en charge par une IDT installée avant PMM/VMM ;
une faute kernel pendant le boot affiche type/RIP/error et CR2 pour #PF, puis
s'arrête explicitement. Les échecs RNG, AP startup et calibration produisent
une ligne FAIL visible. Aucune seed faible ni fonctionnalité désactivée.

L'initialisation BSP efface également CR0.TS comme celle des AP : le scheduler
utilise FXSAVE/FXRSTOR systématiquement et n'a pas de mécanisme de FPU lazy #NM.
CR0 initial est affiché. Les sondes FPU de diagnostic des IRQ ne sont plus
exécutées dans #NM/#DF, afin de ne pas récursivement fauter avant de montrer
l'exception. Ce défaut d'initialisation est distinct du mapping de trous ;
aucune implication sur le Dell n'est revendiquée sans sa trace.

TTY est initialisé au passage au desktop pour ne pas effacer l'historique
de boot. La console cesse alors d'écrire dans le framebuffer ; elle ne devient
pas un second compositor ni une boucle de refresh. La VM fonctionne normalement.

## Tests réalisés

- Compilation kernel complète, puis relink de la protection NX de la pile.
- Host `boot_ranges_test` : 2 Gio + trou RESERVED 1 Tio, loader/FB conservés,
  bad memory exclue, overflow/NULL rejetés : PASS.
- Host `boot_console_test` : console sans services, pitch/scroll/clipping,
  sentinelles de mémoire hors écran conservées : PASS.
- QEMU BIOS + USB EHCI + image disque, PC moderne avec 1 Tio RESERVED :
  **bureau atteint**, alors que l'ancien kernel restait dans VMM.
- QEMU BIOS USB Legacy : bureau atteint, sans média CD.
- VirtualBox BIOS/Core2 T7600, 2 Gio/2 CPU : bureau ; deux CPU online ;
  provider `jitter/SHA3 + CTR-DRBG`. Ce n'est pas un test T7250 physique.
- Smoke `kmaptest` sur l'image finale : PASS (6 threads, 2 CR3s, root tardif,
  arbre kernel partagé, NX/USER, unmap, 32 cycles de destruction).
- Capture réelle avant CR3 : `build/d630-boot-console.png` ; bureau Core 2 :
  `build/d630-visible-core2-desktop.png`. Les JSON des runs contiennent les hashes.

Pas de résultat physique revendiqué pour la nouvelle image. Le bug VMM est
confirmé/corrigé ; son implication dans le blocage Dell demeure une hypothèse
que les marqueurs permettront de trancher.

## Essai suivant sur le Dell

Utiliser `build/kyilos-d630-trace-usb.img` dans Rufus en écriture brute/DD.
La précédente image reste disponible pour retour arrière. Aucune écriture du
disque ArchVista ni formatage automatique ; la préparation reste celle d'une
clé explicitement choisie par l'utilisateur. Le nouveau hash se trouve dans
`build/portable-iso-provenance.json`.

Image de cette passe : SHA256
`7625F6D1FC617E28F39CABE16AB63DD7ED786D495F421373DA392D8CB8F75E52`.
Kernel : `72E4D35785CEF08E8F60C7568973C42213898226934980AC643823B61085A5AA`.

Si le boot s'arrête, relever la **dernière ligne `[....]` non suivie de DONE**
et les éventuelles lignes FAIL / RIP / CR2. Une photo de l'écran complet permet
de conserver les étapes précédentes et les bornes mémoire, sans port série.
Une étape RNG health peut prendre du temps ; les sous-marqueurs distinguent
startup tests, allocation du collector et collecte de seed.

# Documentation KyIlOS

Le [README principal](../README.md) décrit le snapshot publié et son lancement.
Les documents de ce dossier comprennent des étapes historiques datées : leurs
anciens noms Vex/MRAC ou frontières de compatibilité ne remplacent pas l’état
actuel annoncé dans le README.

Entrées utiles :

- [Applications Suchu](suchu-desktop-apps.md) et [Browser](suchu-browser.md).
- [ORA](ora-migration.md), [FlowSched](flowsched.md), [SMP](smp-progress.md).
- [Ultra-Space](ultra-space.md), [capabilities de racine](native-fs-roots.md)
  et [rendez-vous natifs](native-rendezvous.md).
- [Format des pilotes](driver-format.md) et [DMA médié](driver-dma.md).
- [État Windows/Chrome du 18 octobre](windows-startup-progress-2026-10-18.md)
  et [frontière structurée](windows-chrome-main-frontier.json).
- [Portage Dell D630](d630-port.md) et [amorçage USB-HDD](usb-hdd-boot.md).

La dernière image publiée conserve son empreinte de validation. Les anciennes
traces citées sous `build/` sont des références du travail de développement ;
les volumineux journaux locaux ne sont pas inclus dans Git. La fiche
[`release/image.json`](../release/image.json) identifie exactement l’image de
la release.

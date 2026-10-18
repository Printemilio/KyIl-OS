# Windows — checkpoint mémoire / unwind / heap

## Statut

Chantier ouvert : **94 → 81 imports statiques encore absents de chrome_elf.dll**.
Le point d'entrée Chrome n'est pas atteint. Census exécuté : 384 bindings
(176 kernel32, 176 kernelbase, 29 ntdll, 3 version). Présence d'un export,
couverture complète et validation d'une fonctionnalité restent distinctes.

`windows-contract-frontier.{json,md}` contient les 94 lignes exactes : DLL,
symbole, importateur, famille, dépendances natives, complexité, priorité,
contrats associés et état. Générateur : scripts/windows-contract-plan.py.
Ce périmètre est distinct de chrome.exe+chrome_elf.dll et du graphe élargi.

## Implémentations

- Primitive native structurée 229 : query du memory-space courant depuis
  process_memory, accès/copie contrôlés, informations machine. Aucune adresse
  physique, référence ORA ou mémoire d'un autre processus n'est exposée.
  Query ne matérialise pas les pages. Copy est bornée à 1 Mio et conserve la
  transaction mémoire entre validations R/W, USER/PTE et copie.
- VirtualQuery utilise l'inventaire réel. VirtualProtect accepte les plages
  partielles/non alignées en conservant l'origine d'allocation et W^X.
  Read/WriteProcessMemory et VirtualProtectEx fonctionnent réellement pour
  le pseudo-handle self ; autres handles/processus restent explicitement refusés.
- Capture des vrais registres AMD64 CONTEXT et FXSAVE ; préservation des
  registres callee-saved Microsoft x64 lors de la backtrace.
- Recherche .pdata réelle, unwind V1/V2 : prologue partiel, petites/grandes
  allocations, frame pointer, sauvegardes GPR/XMM, machine frame, chaînage,
  épilogues, localisation des handlers/data. Lectures pile/xdata via native
  usercopy. Metadata invalide/non supportée : BAD_FUNCTION_TABLE explicite.
- Backtrace des fonctions PE via leurs tables, sans chaîne rbp. La frontière
  ELF native termine le walk ; cela peut produire une trace partielle.
- HeapLock/Unlock réentrants, exclusion réelle du concurrent, HeapWalk des
  blocs occupés, LocalAlloc/Free fixe avec zeroing/lifetime/stale checks.

Limites : reserve/commit séparés, decommit et comptabilité de commit garantie
restent absents. Le reserve+commit actuel utilise le lazy backing natif.
GlobalMemoryStatusEx n'est pas annoncé avec des limites de commit fictives.
VirtualQuery expose les segments de l'inventaire sans fusion arbitraire.
Mémoire distante, tables de fonctions dynamiques, DWARF ELF, propagation SEH,
RtlUnwind/Ex, RaiseException et language handlers restent à développer.
Local memory movable est refusée. FLS, registre, jobs, système/NLS et plusieurs
contrats I/O restent également dans la matrice : ce jalon ne résout pas les 94.

## Preuves

Scan des vrais binaires : chrome_elf.dll a 5980 entrées unwind V1 et 6 V2 ;
chrome.dll a 615149 V1 et 6 V2. Chaînage/EH/UH/EPILOG observés.
Script : build/windows/scan-unwind.py. Aucun binaire Chrome modifié.

Windows réel : RtlVirtualUnwind V1 body et V2 epilog PASS, dans
build/windows/unwind-host-reference.log. Référence uniquement, jamais un
chargement de NTDLL host dans la compatibilité KyIlOS.

VM 4 CPU : windows-memory-unwind-heap-4-cpu-20261012-060653.log.
VM 1 CPU : windows-memory-unwind-heap-1-cpu-20261012-061736.log.
Ces logs sont sous build/windows. Runner complet et census PASS en 1/4 CPU :

- protections partielles, origine/split, copie réelle, rejet readonly/kernel,
  exécution RW→RX renvoyant 42, 400 races et query sparse 128 Gio ;
- frame PE encore vivante, sauvegardes, vraie backtrace, prologue partiel,
  V2 epilog, chaîne et XMM6 ;
- heap enumeration, recursive locking, blocage du concurrent et LocalFree ;
- libc pthread (187 checks), Linux TLS/FPU, W^X/faults/mémoire, native console,
  root/IPC, process, IOCP, CFG/VEH et lifetimes des suites précédentes.

Les deux échecs initiaux du test unwind ont été conservés : PC du fixture
avant le push au lieu d'après ; puis fonctions C non-leaf sans .pdata.
Correction par label assembleur et -funwind-tables dans le test, vérifiée par
llvm-readobj. Aucun succès factice ou affaiblissement du test. L'ancien échec
intermittent native-execution n'est pas déclaré corrigé ; il n'est pas reproduit
dans les deux runners complets actuels.

## Fichiers et image

Kernel : vex_memory_view_abi.h, vex_abi.h, vex_legacy_compat.h,
process_memory.[ch], vex_syscall.c. Userspace : win32.c, heap.c,
exception_abi.h, exceptions.c, unwind.c, capture_context.S, windows.h.
Tests : memory_query_smoke.c, unwind_smoke.c, unwind_frame.S,
heap_walk_smoke.c, runner.c, kernel32.def. Scripts : build-windows-runtime.ps1,
test-windows-vm.ps1, audit-pe-graph.py, windows-contract-plan.py.
Pas de changement renderer/réseau/driver ni de conversion PE vers ELF.

Image dédiée : build/kyilos-windows-process.iso.

```
ISO      E2EE986F75C036D1C3B142BFB4E1DE5CC851473514F4CC1B95544EF7C3A1CE64
kernel   382B5E05A3477725012D13986B06EC9365A10EEA3A695A950BACBF93466027C6
runtime  970E539C7D748BB9A78BB6DC6D449D11C8B3D3B0DFD4C48EE0EF18EE37555FDC
```

Références : [x64 unwind](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64),
[encoding V2 LLVM](https://github.com/llvm/llvm-project/blob/main/llvm/lib/MC/MCWin64EH.cpp),
[VirtualAlloc commit](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc).

## Suite FLS / informations système

Le checkpoint ci-dessus est remplacé par **94 → 74 imports statiques absents**.
Census réel : 398 bindings (183 kernel32, 183 kernelbase, 29 ntdll, 3 version).
FLS et trois APIs machine ont été ajoutés sans succès de substitution.

FlsAlloc/Get/Set/Free stockent les valeurs dans les threads Windows existants.
Les valeurs sont détachées sous la transaction du loader avant les callbacks,
qui s'exécutent hors verrou. Free et sortie normale prennent chaque valeur une
seule fois. Tests : huit threads, deux cycles free/exit, 17 callbacks attendus,
réutilisation d'index et invalid/stale. Limites : 128 slots et fibre implicite
de chaque thread ; pas de moteur de fibres explicites ou garantie de callback
sur arrêt forcé d'un thread/processus.

GetSystemInfo/GetNativeSystemInfo sont alimentés par la primitive native machine
et CPUID : CPU en ligne, pages, adresses utilisateur, allocation granularity,
famille/model/stepping. IsProcessorFeaturePresent combine CPUID et l'état réel
EFER.NXE/CR4.OSFXSR, pas seulement le support matériel. Les processor groups
Windows au-delà de 64 CPU ne sont pas fournis ; ce cas est explicitement refusé.
Ces APIs ne prétendent pas implémenter la comptabilité de commit Windows.

Fichiers supplémentaires : environment.c, system.c, fls_smoke.c,
platform_smoke.c, windows.h et mise à jour de la primitive machine native.
Preuves finales 4 CPU : windows-platform-facts-4-cpu-20261012-063728.log ;
Preuves finales 1 CPU : windows-platform-facts-1-cpu-20261012-063855.log ;
FLS ciblé : windows-fls-4-cpu-20261012-062927.log, sous build/windows.
Runner complet PASS ; libc pthread toujours 187 checks / zéro échec.
Les deux boots montrent le refus statique avec 74 absents et aucun panic observé.

Image finale dédiée : build/kyilos-windows-process.iso.

```
ISO      2E7A539561959F9F9DE1ACF4DD18AB9DAB6AC4FE0A1F0FF428A86B6C0EAB5C8C
kernel   265F22B13516ED85BDBBD785644982F90DC4225E4403488D35D6123939483179
runtime  98E23CEB365307CF2CBFA5D40BCF05D68048B825742EE239AFFE9C3CD0D43205
```

Le premier obstacle observé reste une table statique incomplète de chrome_elf.dll,
avant toute exécution de son initialisation/point d'entrée. Le registre, la
propagation SEH, les jobs, NLS et d'autres contrats sont encore absents :
ce chantier ne doit pas être marqué terminé ni Chrome déclaré fonctionnel.

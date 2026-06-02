# Coherence Roadmap

This document records the current structural debt in a way that matches the codebase.

## 1. Documentation Drift

Previous state:

- the root README still described a first-boot-only kernel
- browser docs mixed product naming, source provenance, and future-port intent

Current direction:

- README now documents the real scope of the repository
- architecture and repository-map docs now exist
- browser status docs distinguish "runtime payload present" from "source tree present"

## 2. Public Naming Drift

Current problem:

- browser/runtime naming and file layout had drifted away from the supported stack
- that suggested the wrong product architecture to new readers

Safe current approach:

- keep compatibility localized in the shared runtime substrate
- prefer `vex_web_*` names at the compatibility facade level
- remove unsupported browser paths entirely instead of documenting them as peers

What should happen later:

1. move more launcher/browser code onto the `vex_web_*` surface directly
2. narrow direct inclusion of `vex_legacy_compat.h`
3. then continue internal translation-unit cleanup

## 3. Structural Hotspots

Largest files or responsibility clusters to split later:

- `kernel/src/gui.c`
- `kernel/src/shell.c`
- `kernel/src/vex_posix_compat.c`

Recently improved:

- `kernel/src/compat.c` has already been split into Linux and PE-focused units
  to reduce cross-domain coupling in one file
- `kernel/src/shell.c` now shares builtin command handling with `kernel/src/shell_commands.c`
- legacy-named compatibility files were renamed under `vex_*` filenames while keeping
  old symbol behavior intact

Recommended split order:

1. GUI browser shell / paint / switcher into separate units
2. Linux compat and PE compat into separate source files
3. shell builtins into command-oriented modules

## 4. Packaging Debt

Current problem:

- browser/runtime payload packaging is still driven heavily by boot-module staging
- `boot/limine.conf` doubles as a large manual package manifest

Reasonable short-term stance:

- keep it for now because it is explicit and working
- document it clearly
- avoid pretending it is already a generic package/runtime system

## 5. Risk Policy

For now, coherence work should prefer:

- documentation corrections
- facade headers and naming aliases
- file splits that do not change symbols or behavior

It should avoid:

- broad symbol renames across the whole tree
- boot-path rewrites without dedicated regression coverage
- changing runtime packaging semantics just for cleanliness

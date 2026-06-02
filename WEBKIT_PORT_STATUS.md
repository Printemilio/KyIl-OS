# Browser Runtime Status

This file answers a narrow question:

What browser/runtime technology is actually present in this repository today, and in
what form?

## Present In This Repository

- A product-facing browser shell inside Vex-OS (`Vex Browser`)
- A native launcher binary named `webkit`
- A Linux-ABI child runtime binary named `webkit_linux_main`
- Boot/runtime packaging for substantial browser-side shared libraries and payloads
- Legacy-named compatibility helpers and vendored browser-related source material

## Not Present As A Full In-Tree Port

- No full `vendor/webkit` source tree
- No full JavaScriptCore source tree maintained here as a source import
- No complete WebKit platform-port source integration owned directly by this repo
- No single, unified in-tree build system for a canonical upstream WebKit port

## Practical Meaning

Two statements can both be true:

1. Vex-OS already has a real browser runtime path wired into the product surface.
2. This repository does not yet contain a clean, complete, source-owned WebKit port.

That distinction is the source of most prior confusion.

## Current Interpretation

The browser stack should currently be understood as:

- a working runtime bridge and packaging arrangement
- plus compatibility-heavy browser experiments
- not yet a finalized, first-class engine port with clean source ownership boundaries

Supported runtime path today:

- `webkit` is expected to be the native launcher binary
- `webkit_linux_main` is expected to be the Linux-ABI WPE/WebKit child

## Naming Guidance

- `Vex Browser` is the product-facing name
- `webkit` is the current launcher/runtime binary name

See also:

- `docs/webkit-vexos-bridge.md`
- `docs/architecture.md`
- `docs/coherence-roadmap.md`

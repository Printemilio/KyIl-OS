# Vex App UI Architecture

Goal: Vex desktop applications should be normal userspace programs, not kernel-built-in UI modules.

Application policy:
- Non-preinstalled software must run as a separate process through the ELF ABI, the PE ABI, or a public Vex native API.
- Preinstalled Vex apps may use the native VexOS API directly, but they still must be separate userspace programs.
- Kernel-built-in GUI app logic is temporary migration debt, not the target architecture.
- Browser-specific code is not a UI foundation. The browser is just another app client.

The kernel/desktop owns:
- window chrome, focus, resize, minimize/maximize/close
- compositor scene and cursor
- routing input events to the focused app
- process lifetime and permissions

Userspace apps own:
- their framebuffer contents
- app state and file/business logic
- presenting dirty regions through the Vex ABI
- consuming configure, mouse, key, and close events

Current transition state:
- `VEX_SYS_APP_WINDOW_OPEN/PRESENT/POLL/CLOSE` is the public ABI entrypoint for new apps.
- `app_surface_*` owns a small generic per-process app surface registry keyed by `window_id`.
- The old browser display path remains separate compatibility debt for the browser client only.
- `gfm`, `vex_editor`, `vex_paint`, `vex_launcher`, and `vex_terminal` are preinstalled ELF userspace apps using the native Vex app API.
- Desktop menu entries now launch ELF apps for Files, Editor, Paint, and Terminal.
- Spotlight remains the primary system launcher/search overlay for now. It is treated as shell UI, not as a generic app window, until the native app API can reproduce its quality and integration.
- `vex_launcher` is a technical userspace launcher prototype, not the default Spotlight replacement.
- The boot console/TTY remains kernel-hosted until a PTY-backed userspace terminal can replace it fully.

Next migration steps:
- Add real focus/z-order and close/minimize/resize routing for app surfaces.
- Add argv or launch-context IPC so `vex_editor` can receive a clicked file path.
- Add a PTY/shell bridge so `vex_terminal` becomes the real terminal, not just a userspace shell host placeholder.
- Remove the old kernel GUI modules once the userspace apps reach feature parity.

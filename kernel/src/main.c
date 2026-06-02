#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/arp.h>
#include <unos/boot_time.h>
#include <unos/boot_requests.h>
#include <unos/mrac.h>
#include <unos/disk.h>
#include <unos/dhcp.h>
#include <unos/debug_log.h>
#include <unos/ethernet.h>
#include <unos/elf.h>
#include <unos/framebuffer.h>
#include <unos/gdt.h>
#include <unos/fs.h>
#include <unos/gpu.h>
#include <unos/gui.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/inst.h>
#include <unos/iso9660.h>
#include <unos/irq.h>
#include <unos/ipv4.h>
#include <unos/keyboard.h>
#include <unos/mouse.h>
#include <unos/netdev.h>
#include <unos/pmm.h>
#include <unos/rtc.h>
#include <unos/scheduler.h>
#include <unos/shell.h>
#include <unos/spotlight.h>
#include <unos/syscall.h>
#include <unos/timezone.h>
#include <unos/tty.h>
#include <unos/udp.h>
#include <unos/users.h>
#include <unos/vmm.h>
#include <unos/voice.h>

#define NET_LINK_WAIT_TIMEOUT_MS 1000ull
#define NET_LINK_STABLE_POLLS 4u
#define BOOT_BAR_WIDTH 320ull
#define BOOT_BAR_HEIGHT 18ull
#define GUI_PROCESS_PRIORITY 1u

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void cpu_enable_sse(void) {
    uint64_t cr0;
    uint64_t cr4;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ull << 2u); /* EM: allow FPU/SSE instructions. */
    cr0 |= (1ull << 1u);  /* MP: pair WAIT/FWAIT with TS correctly. */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 9u);  /* OSFXSR: enable FXSAVE/FXRSTOR and XMM regs. */
    cr4 |= (1ull << 10u); /* OSXMMEXCPT: enable SSE exceptions. */
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
}

static void print_u64(uint64_t value) {
    char digits[21];
    size_t count = 0;

    if (value == 0) {
        tty_putchar('0');
        return;
    }

    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0) {
        tty_putchar(digits[--count]);
    }
}

static uint64_t boot_step_begin(void) {
    return boot_time_ms();
}

static uint64_t boot_step_elapsed(uint64_t start_ms) {
    return boot_time_ms() - start_ms;
}

static void print_boot_step(const char *name, uint64_t elapsed_ms) {
    tty_puts("boot: ");
    tty_puts(name);
    tty_putchar(' ');
    print_u64(elapsed_ms);
    tty_puts("ms\n");
}

static uint32_t boot_scale_channel(uint8_t value, uint8_t mask_size) {
    if (mask_size == 0) {
        return 0;
    }

    return ((uint32_t)value * ((1u << mask_size) - 1u)) / 255u;
}

static uint32_t boot_color(struct limine_framebuffer *fb, uint8_t r, uint8_t g, uint8_t b) {
    return (boot_scale_channel(r, fb->red_mask_size) << fb->red_mask_shift)
        | (boot_scale_channel(g, fb->green_mask_size) << fb->green_mask_shift)
        | (boot_scale_channel(b, fb->blue_mask_size) << fb->blue_mask_shift);
}

static void boot_put_pixel(struct limine_framebuffer *fb, uint64_t x, uint64_t y, uint32_t color) {
    if (fb == NULL || fb->bpp != 32 || x >= fb->width || y >= fb->height) {
        return;
    }

    uint8_t *row = (uint8_t *)fb->address + y * fb->pitch;
    ((uint32_t *)row)[x] = color;
}

static void boot_fill_rect(struct limine_framebuffer *fb, uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    for (uint64_t py = y; py < y + h && py < fb->height; ++py) {
        for (uint64_t px = x; px < x + w && px < fb->width; ++px) {
            boot_put_pixel(fb, px, py, color);
        }
    }
}

static void boot_draw_border(struct limine_framebuffer *fb, uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (w == 0 || h == 0) {
        return;
    }

    boot_fill_rect(fb, x, y, w, 1, color);
    boot_fill_rect(fb, x, y + h - 1u, w, 1, color);
    boot_fill_rect(fb, x, y, 1, h, color);
    boot_fill_rect(fb, x + w - 1u, y, 1, h, color);
}

static void boot_loading_screen(uint32_t percent, const char *label) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count == 0) {
        return;
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    const uint32_t bg       = boot_color(fb, 8,  10, 16);
    const uint32_t text     = boot_color(fb, 230, 236, 248);
    const uint32_t muted    = boot_color(fb, 110, 120, 145);
    const uint32_t border   = boot_color(fb, 55,  62,  80);
    const uint32_t fill     = boot_color(fb, 60,  150, 255);
    const uint32_t hilite   = boot_color(fb, 140, 200, 255);
    const uint32_t accent   = boot_color(fb, 60,  150, 255);
    const uint64_t bar_w    = fb->width > BOOT_BAR_WIDTH ? BOOT_BAR_WIDTH : fb->width - 32u;
    const uint64_t x        = (fb->width - bar_w) / 2u;
    const uint64_t logo_x   = (fb->width - 48u) / 2u;
    const uint64_t logo_y   = fb->height / 2u - 36u;
    const uint64_t bar_y    = fb->height / 2u + 4u;
    const uint64_t filled   = ((bar_w - 4u) * percent) / 100u;

    boot_fill_rect(fb, 0, 0, fb->width, fb->height, bg);

    tty_draw_text_at(logo_x, logo_y, "Vex-OS", text, bg);
    /* Accent underline under the logo */
    boot_fill_rect(fb, logo_x, logo_y + 14u, 48u, 2u, accent);

    boot_draw_border(fb, x, bar_y, bar_w, BOOT_BAR_HEIGHT, border);
    if (filled > 0u) {
        boot_fill_rect(fb, x + 2u, bar_y + 2u, filled, BOOT_BAR_HEIGHT - 4u, fill);
        /* Top highlight stripe for depth */
        boot_fill_rect(fb, x + 2u, bar_y + 2u, filled, 2u, hilite);
    }

    if (label != NULL) {
        tty_draw_text_at((fb->width - 64u) / 2u, bar_y + 30u, label, muted, bg);
    }
}

static void wait_for_net_link(void) {
    const uint64_t start_ms = boot_time_ms();
    uint32_t stable_polls = 0;

    while (boot_time_ms() - start_ms < NET_LINK_WAIT_TIMEOUT_MS &&
           stable_polls < NET_LINK_STABLE_POLLS) {
        if (netdev_link_up()) {
            ++stable_polls;
        } else {
            stable_polls = 0;
        }

        __asm__ volatile ("pause");
    }
}

static void append_string(char *dst, unsigned long capacity, unsigned long *length, const char *src) {
    while (*src != '\0' && *length + 1u < capacity) {
        dst[*length] = *src;
        ++(*length);
        ++src;
    }

    dst[*length] = '\0';
}

static const char *path_basename(const char *path) {
    const char *name = path;

    if (path == NULL || path[0] == '\0') {
        return "module";
    }

    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' && cursor[1] != '\0') {
            name = cursor + 1;
        }
    }

    return name;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;

    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }

    return left[i] == right[i];
}

static int module_goes_to_bin(const char *name) {
    return string_equals(name, "hello")
        || string_equals(name, "alloc_stress")
        || string_equals(name, "net_test")
        || string_equals(name, "gpu_test")
        || string_equals(name, "vk_test")
        || string_equals(name, "mouse_test")
        || string_equals(name, "webkit")
        || string_equals(name, "webkit_linux_main")
        || string_equals(name, "WebKitWebProcess")
        || string_equals(name, "WebKitNetworkProcess")
        || string_equals(name, "WPEWebDriver")
        || string_equals(name, "jsc");
}

static int module_is_linux_interpreter(const char *name) {
    return string_equals(name, "ld-linux-x86-64.so.2");
}

static int path_contains_separator(const char *path) {
    size_t i = 0;

    if (path == NULL) {
        return 0;
    }

    while (path[i] != '\0') {
        if (path[i] == '/') {
            return 1;
        }
        ++i;
    }

    return 0;
}

static const char *module_virtual_path(const char *module_path) {
    static const char prefix[] = "boot():/";
    size_t i = 0;

    if (module_path == NULL) {
        return NULL;
    }

    /* Limine v8+ may strip the "boot():/" scheme and store just the
     * absolute path (e.g. "/lib/foo.so").  Handle both formats. */
    if (module_path[0] == '/') {
        return module_path + 1;   /* skip leading '/', return "lib/foo.so" */
    }

    while (prefix[i] != '\0') {
        if (module_path[i] != prefix[i]) {
            return NULL;
        }
        ++i;
    }

    return module_path + i;
}

static int boot_module_present(const char *name) {
    if (module_request.response == NULL || name == NULL) {
        return 0;
    }

    for (uint64_t i = 0; i < module_request.response->module_count; ++i) {
        const struct limine_file *module = module_request.response->modules[i];
        if (module == NULL || module->path == NULL) {
            continue;
        }

        if (string_equals(path_basename(module->path), name)) {
            return 1;
        }
    }

    return 0;
}

static void import_limine_modules(void) {
    if (module_request.response == NULL) {
        return;
    }

    for (uint64_t i = 0; i < module_request.response->module_count; ++i) {
        const struct limine_file *module = module_request.response->modules[i];
        char destination[128];
        unsigned long length = 0;

        if (module == NULL || module->address == NULL || module->size == 0) {
            continue;
        }

        const char *name = path_basename(module->path);
        const char *virtual_path = module_virtual_path(module->path);
        if (module_is_linux_interpreter(name)) {
            append_string(destination, sizeof(destination), &length, "/lib64/ld-linux-x86-64.so.2");
        } else if (virtual_path != NULL && virtual_path[0] != '\0' && path_contains_separator(virtual_path)) {
            append_string(destination, sizeof(destination), &length, "/");
            append_string(destination, sizeof(destination), &length, virtual_path);
        } else {
            append_string(destination, sizeof(destination), &length, module_goes_to_bin(name) ? "/home/root/bin/" : "/home/root/packages/imports/");
            append_string(destination, sizeof(destination), &length, name);
        }
        if (((module_is_linux_interpreter(name) ||
                (virtual_path != NULL && virtual_path[0] != '\0' && path_contains_separator(virtual_path))) &&
                fs_register_boot_module(destination, module->address, (size_t)module->size) != 0) ||
            ((!module_is_linux_interpreter(name) &&
                (virtual_path == NULL || virtual_path[0] == '\0' || !path_contains_separator(virtual_path))) &&
                (((size_t)module->size > (1u << 20u) && fs_register_boot_module(destination, module->address, (size_t)module->size) != 0) ||
                 ((size_t)module->size <= (1u << 20u) && fs_write(destination, module->address, (size_t)module->size) != 0)))) {
            debug_log_puts("boot: module import skipped ");
            debug_log_puts(destination);
            debug_log_puts("\n");
        }
    }
}

void kmain(void) {
    uint64_t step;
    uint64_t pmm_ms;
    uint64_t vmm_ms;
    uint64_t heap_ms;
    uint64_t core_ms;
    uint64_t disk_ms;
    uint64_t fs_ms;
    uint64_t net_ms;
    uint64_t dhcp_ms;
    uint64_t services_ms;
    uint64_t gui_ms;
    uint64_t shell_ms;
    const int auto_smoke = boot_module_present("auto_smoke");

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        halt_forever();
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0) {
        halt_forever();
    }

    if (memmap_request.response == NULL) {
        halt_forever();
    }

    boot_time_init();
    cpu_enable_sse();
    step = boot_step_begin();
    pmm_init(memmap_request.response);
    pmm_ms = boot_step_elapsed(step);

    tty_init();
    boot_loading_screen(0, "core");
    debug_log_init();
    debug_log_puts("boot: serial log online\n");

    step = boot_step_begin();
    vmm_init();
    vmm_ms = boot_step_elapsed(step);

    step = boot_step_begin();
    heap_init();
    heap_ms = boot_step_elapsed(step);

    step = boot_step_begin();
    MRAC_init();
    scheduler_init();
    users_init();
    gdt_init();
    syscall_init();
    rtc_init();
    keyboard_init();
    mouse_init();
    voice_init();
    irq_init();
    core_ms = boot_step_elapsed(step);

    boot_loading_screen(10, "disk");
    step = boot_step_begin();
    disk_init();
    disk_ms = boot_step_elapsed(step);

    boot_loading_screen(35, "fs");
    step = boot_step_begin();
    fs_init();
    import_limine_modules();
    iso9660_init();
    timezone_init();
    spotlight_index_build();
    fs_ms = boot_step_elapsed(step);

    boot_loading_screen(60, "net");
    step = boot_step_begin();
    netdev_init();
    wait_for_net_link();
    ethernet_init();
    arp_init();
    ipv4_init();
    udp_init();
    dhcp_init();
    net_ms = boot_step_elapsed(step);

    step = boot_step_begin();
    dhcp_start();
    dhcp_ms = boot_step_elapsed(step);

    boot_loading_screen(85, "services");
    step = boot_step_begin();
    ipc_init();
    inst_init();
    services_ms = boot_step_elapsed(step);

    if (auto_smoke) {
        step = boot_step_begin();
        gpu_init(framebuffer_request.response->framebuffers[0]);
        gui_ms = boot_step_elapsed(step);
        shell_ms = 0u;
        tty_puts("smoke: autorun begin\n");
        (void)elf_spawn("/home/root/bin/gpu_test");
        (void)elf_spawn("/home/root/bin/vk_test");
        tty_puts("smoke: autorun queued\n");
    } else {
        boot_loading_screen(100, "gui");
        step = boot_step_begin();
        gui_init();
        gui_ms = boot_step_elapsed(step);

        step = boot_step_begin();
        shell_init();
        gui_set_terminal_pid(shell_pid());
        shell_ms = boot_step_elapsed(step);
    }

    if (dhcp_config_loaded()) {
        tty_puts("dhcp: loading config\n");
    }
    if (dhcp_config_saved()) {
        tty_puts("dhcp: saving config\n");
    }

    print_boot_step("pmm", pmm_ms);
    print_boot_step("vmm", vmm_ms);
    print_boot_step("heap", heap_ms);
    print_boot_step("core", core_ms);
    print_boot_step("disk", disk_ms);
    print_boot_step("fs", fs_ms);
    print_boot_step("net", net_ms);
    print_boot_step("dhcp", dhcp_ms);
    print_boot_step("dhcp offer", dhcp_offer_wait_ms());
    print_boot_step("dhcp ack", dhcp_ack_wait_ms());
    print_boot_step("services", services_ms);
    print_boot_step("gui", gui_ms);
    print_boot_step("shell", shell_ms);

    tty_puts("boot: ready in ");
    print_u64(boot_time_ms());
    tty_puts("ms\n");

    if (!auto_smoke) {
        process_create(gui_loop, GUI_PROCESS_PRIORITY);
        tty_puts("boot: webkit installed, run 'browser' or 'webkit' to start\n");
    }
    irq_enable();
    schedule();

    halt_forever();
}

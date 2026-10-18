#include <unos/vex_legacy_compat.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/gui.h>
#include <unos/heap.h>
#include <unos/scheduler.h>
#include <unos/browser_viewport.h>
#include <unos/keyboard.h>
#include <unos/irq.h>

#define VEX_TRACE_BROWSER_GUI 0
static uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    unsigned short cs;__asm__ volatile("mov %%cs,%0":"=r"(cs));
    /* Host unit tests execute this same queue in ring 3. */
    if(!(cs&3))__asm__ volatile("cli" ::: "memory");
    return flags;
}
static void irq_restore(uint64_t flags) {
    unsigned short cs;__asm__ volatile("mov %%cs,%0":"=r"(cs));
    if(!(cs&3))__asm__ volatile("push %0; popfq" :: "r"(flags) : "memory","cc");
}

static ladybird_display_surface_t display_surface;
static uint8_t display_ready;
static uint8_t display_present_pending;
static uint8_t display_present_dirty_valid;
static uint32_t display_present_dirty_x;
static uint32_t display_present_dirty_y;
static uint32_t display_present_dirty_w;
static uint32_t display_present_dirty_h;

#define BROWSER_COMMAND_QUEUE_SIZE 32u
#define BROWSER_COMMAND_MAX (VEX_BROWSER_URI_SIZE + 48u)
#define BROWSER_RESULT_MAX 8u
#define BROWSER_RESULT_TITLE_MAX 96u
#define BROWSER_RESULT_URL_MAX 256u
#define BROWSER_RESULT_SNIPPET_MAX 192u

static char browser_command_queue[BROWSER_COMMAND_QUEUE_SIZE][BROWSER_COMMAND_MAX];
static uint8_t browser_command_head;
static uint8_t browser_command_tail;
static uint32_t browser_process_pid;
static uint32_t browser_process_parent_pid;
static vex_viewport_event_t viewport_events[256];
static uint32_t viewport_tab_ids[256], viewport_polled_tab, viewport_bounds_tab;
static uint32_t viewport_head,viewport_tail;
static uint8_t viewport_focus,viewport_inside;
static uint8_t viewport_overflow;
static uint32_t viewport_buttons;
static uint32_t viewport_owned_buttons;
static vex_rect_t viewport_last_bounds;

static void viewport_push(vex_viewport_event_t e) {
    uint64_t flags=irq_save();
    e.time=(uint32_t)(irq_timer_ticks()*10u);
    if(e.type==VEX_VIEWPORT_MOTION && viewport_head!=viewport_tail) {
        uint32_t last=(viewport_head+255u)%256u;
        if(viewport_events[last].type==VEX_VIEWPORT_MOTION && viewport_tab_ids[last]==gui_browser_active_tab()){viewport_events[last]=e;irq_restore(flags);return;}
    }
    uint32_t next=(viewport_head+1u)%256u;
    if(next==viewport_tail) {
        /* Motion can be coalesced, but transitions are never overwritten. */
        debug_log_puts("viewport: event queue exhausted\n");
        viewport_overflow=1;
        irq_restore(flags);
        return;
    }
    viewport_events[viewport_head]=e;viewport_tab_ids[viewport_head]=gui_browser_active_tab();viewport_head=next;
    irq_restore(flags);
}
int browser_viewport_bounds(vex_rect_t *bounds) {
    if(!display_ready || !gui_browser_viewport_visible() || !gui_content_width() || !gui_content_height())return 0;
    *bounds=(vex_rect_t){(int32_t)gui_content_x(),(int32_t)gui_content_y(),
        (int32_t)gui_content_width(),(int32_t)gui_content_height()};return 1;
}
void browser_viewport_blur(void) {
    if(viewport_focus) {viewport_focus=0;keyboard_focus_release();viewport_push((vex_viewport_event_t){.type=VEX_VIEWPORT_FOCUS});}
}
void browser_viewport_hide(void) {
    browser_viewport_blur();
    viewport_push((vex_viewport_event_t){.type=VEX_VIEWPORT_CONFIGURE});
}
void browser_viewport_pointer(int x,int y,uint32_t buttons,int wheel,int allowed) {
    vex_rect_t b={0};int inside=allowed && browser_viewport_bounds(&b) &&
        x>=b.x && y>=b.y && x<b.x+b.w && y<b.y+b.h;
    /* Motion precedes button edges in this queue. Its button mask must describe
     * the state before those edges; publishing the new mask early makes the
     * consumer observe a press/release in the motion event and can cancel the
     * corresponding DOM click. */
    uint32_t previous_buttons=viewport_buttons;
    vex_viewport_event_t e={.x=x-b.x,.y=y-b.y,.modifiers=keyboard_modifiers(),.hardware=previous_buttons};
    if((buttons&~viewport_buttons) && !inside)browser_viewport_blur();
    if(inside!=viewport_inside) {e.type=inside?VEX_VIEWPORT_ENTER:VEX_VIEWPORT_LEAVE;viewport_push(e);viewport_inside=inside;}
    if(inside) {
        e.type=VEX_VIEWPORT_MOTION;viewport_push(e);
        if((buttons&~viewport_buttons) && !viewport_focus) {
            viewport_focus=1;keyboard_focus_capture_current();
            viewport_push((vex_viewport_event_t){.type=VEX_VIEWPORT_FOCUS,.pressed=1});
        }
    }
    for(unsigned i=0;i<3;i++)if((buttons^viewport_buttons)&(1u<<i)) {
        uint32_t bit=1u<<i;
        if(buttons&bit) {
            if(!inside)continue;
            viewport_owned_buttons|=bit;
        }else {
            if(!(viewport_owned_buttons&bit))continue;
            viewport_owned_buttons&=~bit;
        }
        /* Only the widget that received a press receives its release. The
         * release of a captured Web drag keeps its real outside coordinates;
         * a new outside/chrome click is never delivered to the document. */
        e.type=VEX_VIEWPORT_BUTTON;e.value=i+1;e.pressed=(buttons>>i)&1u;
        e.hardware=viewport_owned_buttons;viewport_push(e);
    }
    if(inside && wheel){e.type=VEX_VIEWPORT_WHEEL;e.value=wheel;viewport_push(e);}
    viewport_buttons=buttons;
}
void browser_viewport_key(uint32_t key,uint32_t hardware,uint32_t pressed,uint32_t modifiers) {
    if(viewport_focus)viewport_push((vex_viewport_event_t){.type=VEX_VIEWPORT_KEY,.key=key,
        .hardware=hardware,.pressed=pressed,.modifiers=modifiers});
}
int browser_viewport_has_focus(void) {return viewport_focus != 0u;}
int browser_viewport_poll(vex_viewport_event_t *e) {
    uint64_t flags=irq_save();
    if(viewport_overflow){irq_restore(flags);return -105;} /* ENOBUFS: never silently lose a key/button edge. */
    vex_rect_t b={0};browser_viewport_bounds(&b);
    if(memcmp(&b,&viewport_last_bounds,sizeof(b)) || viewport_bounds_tab!=gui_browser_active_tab()) {
        viewport_bounds_tab=gui_browser_active_tab();viewport_polled_tab=viewport_bounds_tab;
        viewport_last_bounds=b;
        *e=(vex_viewport_event_t){.type=VEX_VIEWPORT_CONFIGURE,.x=b.x,.y=b.y,.width=b.w,.height=b.h};irq_restore(flags);return 1;
    }
    if(viewport_head==viewport_tail){irq_restore(flags);return 0;}
    *e=viewport_events[viewport_tail];viewport_polled_tab=viewport_tab_ids[viewport_tail];viewport_tail=(viewport_tail+1u)%256u;irq_restore(flags);return 1;
}
uint32_t browser_viewport_polled_tab(void) {return viewport_polled_tab;}
static char browser_results_query[BROWSER_COMMAND_MAX];
static char browser_result_titles[BROWSER_RESULT_MAX][BROWSER_RESULT_TITLE_MAX];
static char browser_result_urls[BROWSER_RESULT_MAX][BROWSER_RESULT_URL_MAX];
static char browser_result_snippets[BROWSER_RESULT_MAX][BROWSER_RESULT_SNIPPET_MAX];
static uint32_t browser_result_count;
static uint8_t browser_results_are_visible;

static void browser_runtime_reset(void) {
    browser_viewport_blur();
    viewport_head=viewport_tail=viewport_buttons=0;viewport_focus=viewport_inside=0;
    viewport_owned_buttons=0;
    viewport_overflow=0;
    viewport_bounds_tab=viewport_polled_tab=0;
    memset(&viewport_last_bounds,0,sizeof(viewport_last_bounds));
    browser_process_pid = 0u;
    browser_process_parent_pid = 0u;
    browser_command_head = 0u;
    browser_command_tail = 0u;
    browser_results_query[0] = '\0';
    browser_result_count = 0u;
    browser_results_are_visible = 0u;
    for (uint32_t i = 0u; i < BROWSER_RESULT_MAX; ++i) {
        browser_result_titles[i][0] = '\0';
        browser_result_urls[i][0] = '\0';
        browser_result_snippets[i][0] = '\0';
    }
}

static void copy_string_limited(char *dst, size_t capacity, const char *src) {
    size_t i = 0u;
    if (dst == NULL || capacity == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

void ladybird_debug_log(const char *text) {
    if (text == NULL) {
        return;
    }
    debug_log_puts(text);
}

static void debug_log_u32(uint32_t value) {
    char digits[11];
    size_t count = 0u;
    if (value == 0u) {
        debug_log_puts("0");
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u) {
        char out[2];
        out[0] = digits[--count];
        out[1] = '\0';
        debug_log_puts(out);
    }
}

static void display_reset_surface(void) {
    if (display_surface.pixels != NULL) {
        kfree(display_surface.pixels);
    }

    memset(&display_surface, 0, sizeof(display_surface));
    display_ready = 0;
    display_present_pending = 0;
    display_present_dirty_valid = 0;
    display_present_dirty_x = 0u;
    display_present_dirty_y = 0u;
    display_present_dirty_w = 0u;
    display_present_dirty_h = 0u;
    browser_runtime_reset();
}

static void draw_scaled_surface(void) {
    uint64_t screen_w;
    uint64_t screen_h;
    uint64_t target_w;
    uint64_t target_h;
    uint64_t origin_x;
    uint64_t origin_y;

    if (browser_results_are_visible || !display_ready || display_surface.pixels == NULL || display_surface.width == 0u || display_surface.height == 0u) {
        return;
    }

    const uint64_t client_x = gui_content_x();
    const uint64_t client_y = gui_content_y();
    screen_w = gui_content_width();
    screen_h = gui_content_height();
    if (screen_w == 0u || screen_h == 0u) {
        return;
    }

    target_w = screen_w;
    target_h = screen_h;
    origin_x = client_x;
    origin_y = client_y;

    if (display_present_dirty_valid) {
        gui_blit_argb_scaled_partial(
            origin_x,
            origin_y,
            target_w,
            target_h,
            display_surface.pixels,
            display_surface.width,
            display_surface.height,
            display_surface.stride,
            display_present_dirty_x,
            display_present_dirty_y,
            display_present_dirty_w,
            display_present_dirty_h
        );
    } else {
        gui_blit_argb_scaled(origin_x, origin_y, target_w, target_h, display_surface.pixels, display_surface.width, display_surface.height, display_surface.stride);
    }
}

int ladybird_display_open(uint32_t width, uint32_t height) {
    size_t pixels_count;

    if (display_ready &&
        display_surface.pixels != NULL &&
        display_surface.width == width &&
        display_surface.height == height) {
        if (browser_process_pid == 0u) {
            browser_process_pid = scheduler_current_process_id();
            browser_process_parent_pid = scheduler_current_ppid();
        }
        return 0;
    }

    display_reset_surface();
    if (width == 0u || height == 0u) {
        return -1;
    }

    pixels_count = (size_t)width * (size_t)height;
    display_surface.pixels = kmalloc(pixels_count * sizeof(uint32_t));
    if (display_surface.pixels == NULL) {
        return -1;
    }

    memset(display_surface.pixels, 0, pixels_count * sizeof(uint32_t));
    display_surface.width = width;
    display_surface.height = height;
    display_surface.stride = width;
    display_ready = 1;
    browser_process_pid = scheduler_current_process_id();
    browser_process_parent_pid = scheduler_current_ppid();
    ladybird_debug_log("LB display_open pid=");
    debug_log_u32(browser_process_pid);
    ladybird_debug_log("\n");
    gui_browser_begin();
    return 0;
}

void ladybird_display_resize(uint32_t width, uint32_t height) {
    if (display_surface.width == width && display_surface.height == height) {
        return;
    }

    (void)ladybird_display_open(width, height);
}

ladybird_display_surface_t *ladybird_display_surface(void) {
    return display_ready ? &display_surface : NULL;
}

void ladybird_display_present(void) {
    display_present_pending = 1u;
    gui_wake();
}

void ladybird_display_refresh(void) {
    display_present_pending = 1u;
    gui_wake();
}

void ladybird_display_close(void) {
    gui_browser_end();
    display_reset_surface();
}

void ladybird_display_set_pending_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    display_present_dirty_x = x;
    display_present_dirty_y = y;
    display_present_dirty_w = w;
    display_present_dirty_h = h;
    display_present_dirty_valid = (w != 0u && h != 0u) ? 1u : 0u;
}

void ladybird_display_clear_pending_dirty(void) {
    display_present_dirty_valid = 0u;
    display_present_dirty_x = 0u;
    display_present_dirty_y = 0u;
    display_present_dirty_w = 0u;
    display_present_dirty_h = 0u;
}

void ladybird_display_pump(void) {
    if (display_present_pending == 0u) {
        return;
    }
#if VEX_TRACE_BROWSER_GUI
    ladybird_debug_log("GUI pump: begin\n");
    if (display_present_dirty_valid) {
        ladybird_debug_log("GUI pump: partial dirty\n");
    }
#endif
    display_present_pending = 0u;
    draw_scaled_surface();
    gui_browser_frame_ready();
    vex_rect_t damage={(int32_t)gui_content_x(),(int32_t)gui_content_y(),
        (int32_t)gui_content_width(),(int32_t)gui_content_height()};
    gui_compositor_restore_foreground(&damage);
    display_present_dirty_valid = 0u;
#if VEX_TRACE_BROWSER_GUI
    ladybird_debug_log("GUI pump: end\n");
#endif
}

void ladybird_browser_yield_to_process(void) {
    if (browser_process_pid == 0u) {
        return;
    }
    ladybird_debug_log("GUI yield_to: begin\n");
    /* GUI callers may hold a scene transaction. Publish urgency/IPI now;
       the GUI safe point after composition performs any local switch. */
    scheduler_wake_flow(browser_process_pid,scheduler_flow_current());
    ladybird_debug_log("GUI yield_to: resumed\n");
}

uint32_t ladybird_browser_process_pid(void) {
    return browser_process_pid;
}

uint32_t ladybird_browser_parent_pid(void) {
    return browser_process_parent_pid;
}

int ladybird_browser_command_push(const char *command) {
    size_t i = 0u;

    if (command == NULL || command[0] == '\0') {
        ladybird_debug_log("Q push: empty\n");
        return -1;
    }
    if(strlen(command)>=BROWSER_COMMAND_MAX)return -7;
    const uint64_t flags=irq_save();
    const uint8_t next=(uint8_t)((browser_command_head+1u)%BROWSER_COMMAND_QUEUE_SIZE);
    if(next==browser_command_tail){irq_restore(flags);return -11;}

    while (command[i] != '\0' && i + 1u < BROWSER_COMMAND_MAX) {
        browser_command_queue[browser_command_head][i] = command[i];
        ++i;
    }
    browser_command_queue[browser_command_head][i] = '\0';
    browser_command_head = next;
    irq_restore(flags);
    ladybird_debug_log("Q push: ok\n");
    if (browser_process_pid != 0u) {
        ladybird_debug_log("Q push: target pid=");
        debug_log_u32(browser_process_pid);
        ladybird_debug_log(" state=");
        ladybird_debug_log(scheduler_state_name(browser_process_pid));
        ladybird_debug_log("\n");
        scheduler_set_process_priority(browser_process_pid, 3u);
        process_wake(browser_process_pid);
        ladybird_debug_log("Q push: after wake state=");
        ladybird_debug_log(scheduler_state_name(browser_process_pid));
        ladybird_debug_log("\n");
        ladybird_debug_log("Q push: wake\n");
    }
    return 0;
}

int ladybird_browser_command_pop(char *buffer, size_t size) {
    size_t i = 0u;

    if (buffer == NULL || size == 0u) {
        return -1;
    }
    const uint64_t flags=irq_save();
    if (browser_command_tail == browser_command_head) {
        buffer[0] = '\0';
        irq_restore(flags);
        return 0;
    }

    while (browser_command_queue[browser_command_tail][i] != '\0' && i + 1u < size) {
        buffer[i] = browser_command_queue[browser_command_tail][i];
        ++i;
    }
    buffer[i] = '\0';
    if(browser_command_queue[browser_command_tail][i]){irq_restore(flags);return -7;}
    browser_command_tail = (uint8_t)((browser_command_tail + 1u) % BROWSER_COMMAND_QUEUE_SIZE);
    irq_restore(flags);
    scheduler_set_process_priority(scheduler_current_tid(), 1u);
    ladybird_debug_log("Q pop: ok\n");
    return 1;
}

void ladybird_browser_results_begin(const char *query) {
    browser_result_count = 0u;
    browser_results_are_visible = 0u;
    copy_string_limited(browser_results_query, sizeof(browser_results_query), query);
    for (uint32_t i = 0u; i < BROWSER_RESULT_MAX; ++i) {
        browser_result_titles[i][0] = '\0';
        browser_result_urls[i][0] = '\0';
        browser_result_snippets[i][0] = '\0';
    }
}

void ladybird_browser_results_add(const char *title, const char *url, const char *snippet) {
    if (browser_result_count >= BROWSER_RESULT_MAX || url == NULL || url[0] == '\0') {
        return;
    }
    copy_string_limited(browser_result_titles[browser_result_count], BROWSER_RESULT_TITLE_MAX, title);
    copy_string_limited(browser_result_urls[browser_result_count], BROWSER_RESULT_URL_MAX, url);
    copy_string_limited(browser_result_snippets[browser_result_count], BROWSER_RESULT_SNIPPET_MAX, snippet);
    ++browser_result_count;
}

void ladybird_browser_results_show(void) {
    browser_results_are_visible = 1u;
}

void ladybird_browser_results_hide(void) {
    browser_results_are_visible = 0u;
}

uint8_t ladybird_browser_results_visible(void) {
    return browser_results_are_visible;
}

uint32_t ladybird_browser_results_count(void) {
    return browser_result_count;
}

const char *ladybird_browser_results_query(void) {
    return browser_results_query;
}

const char *ladybird_browser_result_title(uint32_t index) {
    return index < browser_result_count ? browser_result_titles[index] : "";
}

const char *ladybird_browser_result_url(uint32_t index) {
    return index < browser_result_count ? browser_result_urls[index] : "";
}

const char *ladybird_browser_result_snippet(uint32_t index) {
    return index < browser_result_count ? browser_result_snippets[index] : "";
}

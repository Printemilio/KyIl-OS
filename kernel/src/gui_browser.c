#include "gui_private.h"

#include <stddef.h>
#include <stdint.h>
#include <unos/gui.h>
#include <unos/elf.h>
#include <unos/app_surface.h>
#include <unos/security.h>
#include <unos/browser_viewport.h>
#include <unos/browser_chrome.h>
#include <unos/browser_downloads.h>
#include <unos/browser_edit.h>
#include <unos/browser_navigation.h>
#include <unos/keyboard.h>
#include <unos/debug_log.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/vex_logo.h>
#include <unos/vex_legacy_compat.h>
#include <unos/vex_lvgl_ui.h>
#include "../include/vex_browser_wordmark.h"
#include "../include/vex_browser_hex.h"

#define GUI_BROWSER_TAB_HEIGHT 36ull

static vex_browser_state_t browser_tabs[VEX_BROWSER_MAX_TABS];
static uint32_t browser_selected, browser_next_id, browser_closing_pid;
static uint32_t browser_closing_parent_pid;
static uint8_t browser_address_selected, browser_reopen_requested;
static uint64_t browser_address_cursor;
uint32_t gui_browser_active_tab(void) {return browser_tabs[browser_selected].id;}
static int browser_action(const char *action,uint32_t id,const char *argument);
static void browser_select_tab(uint32_t index);

static uint64_t browser_shell_height(void) {
    return browser_native_home_visible ? GUI_BROWSER_TAB_HEIGHT
                                       : GUI_BROWSER_SHELL_HEIGHT;
}

static size_t browser_string_length(const char *text) {
    size_t length = 0u;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void browser_copy_string(char *dst, size_t capacity, const char *src) {
    size_t i = 0u;
    if (dst == NULL || capacity == 0u) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void browser_append_string(char *dst, size_t capacity, const char *src) {
    size_t length = browser_string_length(dst);
    size_t i = 0u;
    while (src != NULL && src[i] != '\0' && length + 1u < capacity) {
        dst[length++] = src[i++];
    }
    dst[length] = '\0';
}

void browser_shell_set_address(const char *address) {
    browser_address_len = 0u;
    if (address == NULL) {
        browser_address[0] = '\0';
        browser_address_cursor=0;return;
    }

    while (address[browser_address_len] != '\0' && browser_address_len + 1u < sizeof(browser_address)) {
        browser_address[browser_address_len] = address[browser_address_len];
        ++browser_address_len;
    }
    browser_address[browser_address_len] = '\0';
    browser_address_cursor=browser_address_len;
}

void draw_browser_loading_state(void) {
    const uint64_t x = gui_content_x();
    const uint64_t y = gui_content_y();
    const uint64_t w = gui_content_width();
    const uint64_t h = gui_content_height();
    const uint32_t bg = pack_color(20, 20, 20);
    const uint32_t panel = pack_color(30, 30, 30);
    const uint32_t border = pack_color(72, 49, 86);
    const uint32_t text = pack_color(255, 255, 255);
    const uint32_t muted = pack_color(104, 104, 104);
    const uint64_t panel_w = w > 280u ? (w > 760u ? 760u : w - 48u) : w;
    const uint64_t panel_h = 112u;
    const uint64_t panel_x = x + (w > panel_w ? (w - panel_w) / 2u : 0u);
    const uint64_t panel_y = y + (h > 220u ? (h - panel_h) / 3u : 28u);

    if (!browser_active || browser_minimized || w == 0u || h == 0u) {
        return;
    }

    if (vex_lvgl_ui_ready() && x <= UINT32_MAX && y <= UINT32_MAX &&
        w <= UINT32_MAX && h <= UINT32_MAX) {
        vex_lvgl_ui_draw_browser_loading((uint32_t)x, (uint32_t)y,
            (uint32_t)w, (uint32_t)h, browser_loading_text, (browser_tabs[browser_selected].flags&VEX_BROWSER_FAILED)!=0);
        return;
    }

    fill_rect(x, y, w, h, bg);
    fill_rounded_rect(panel_x - 2u, panel_y - 2u, panel_w + 4u, panel_h + 4u, 12u, border);
    fill_rounded_rect(panel_x, panel_y, panel_w, panel_h, 10u, panel);
    gui_font_text(panel_x + 24u, panel_y + 26u, browser_tabs[browser_selected].flags&VEX_BROWSER_FAILED?"Navigation failed":"Loading...", text, 16u);
    draw_browser_text_clipped(panel_x + 24u, panel_y + 58u,
        browser_loading_text[0] == '\0' ? "Waiting for WebKit frame" : browser_loading_text,
        muted,
        panel_w > 48u ? panel_w - 48u : 0u);
    compositor_mark_dirty(x, y, w, h);
}

void draw_browser_shell(void) {
    const uint64_t x = browser_win_x + 8u;
    const uint64_t y = browser_win_y + GUI_TITLE_HEIGHT + 6u;
    const uint64_t w = browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    /* Figma Frame 16 shell: flat #141414, "New Page" tab + divider + "+",
       then a nav row with the address field. */
    const uint32_t shell = pack_color(20, 20, 20);
    const uint32_t sep   = pack_color(72, 49, 86);
    const uint32_t address = browser_address_focused ? pack_color(30, 30, 30) : pack_color(25, 25, 25);
    const uint32_t text  = pack_color(255, 255, 255);
    const uint32_t muted = pack_color(104, 104, 104);
    const uint64_t address_x = x + 100u;
    const uint64_t address_w = w > 148u ? w - 148u : 80u;
    const uint64_t shell_h = browser_shell_height();

    if (!browser_active || browser_minimized || w == 0u) {
        return;
    }

    if (vex_lvgl_ui_ready() && x <= UINT32_MAX && y <= UINT32_MAX && w <= UINT32_MAX) {
        vex_lvgl_ui_draw_browser_shell((uint32_t)x, (uint32_t)y,
            (uint32_t)w, browser_address, browser_address_focused,
            browser_tabs,browser_tab_count,browser_selected,browser_native_home_visible,browser_address_cursor,browser_address_selected);
        return;
    }

    fill_rect(x, y, w, shell_h, shell);
    /* Tab row */
    unsigned tab_w=browser_chrome_tab_width(w,browser_tab_count);
    for(unsigned i=0;i<browser_tab_count;i++) {
        uint64_t tx=x+10+i*tab_w;
        if(i==browser_selected)fill_rect(tx,y+32,tab_w-2,2,pack_color(163,89,227));
        draw_browser_text_clipped(tx+8,y+10,browser_tabs[i].title[0]?browser_tabs[i].title:"New Page",text,tab_w>36?tab_w-36:1);
        gui_font_text(tx+tab_w-20,y+8,"x",muted,16);
        fill_rect(tx+tab_w-2,y+5,1,25,sep);
    }
    gui_font_text(x+18+tab_w*browser_tab_count,y+8,"+",text,16);
    fill_rect(x, y + 35u, w, 1u, sep);                       /* row divider */

    if (!browser_native_home_visible) {
        /* Navigation stays available on loaded pages; the home mockup keeps
         * only the tab strip and uses its central search field. */
        gui_font_text(x + 14u, y + 44u, "<", text, 16u);
        gui_font_text(x + 40u, y + 44u, ">", text, 16u);
        gui_font_text(x + 66u, y + 44u, "C", text, 16u);

        fill_rounded_rect(address_x, y + 40u, address_w, 26u, 10u, address);
        draw_browser_text_clipped(address_x + 16u, y + 49u,
            browser_address_len == 0u ? "Search what you want ..." : browser_address,
            browser_address_len == 0u ? muted : text,
            address_w > 76u ? address_w - 76u : 0u);
        if (browser_address_focused) {
            const uint64_t cursor_px = address_x + 18u + browser_address_len * 8u;
            if (cursor_px < address_x + address_w - 12u) {
                fill_rect(cursor_px, y + 47u, 1u, 12u, text);
            }
        }

        /* Vex logo at the right of the address field */
        gui_draw_vex_logo(address_x + address_w - 30u, y + 43u, 20u);
        gui_font_text(x + w - 32u, y + 44u, "...", text, 16u);
    }
    compositor_mark_dirty(x, y, w, shell_h);
}

void draw_browser_text_clipped(uint64_t x, uint64_t y, const char *text, uint32_t color, uint64_t max_width) {
    /* Inter 16 with pixel clipping (call sites were tuned for a 16px cell,
       Inter's cell is 20 — nudge up 2px to stay visually centred) */
    gui_font_text_clipped(x, y > 2u ? y - 2u : y, text, color, 16u, max_width);
}

uint64_t gui_content_x(void) {
    if (browser_active) {
        return browser_win_x + 8u;
    }
    return terminal_win_x + 8u;
}

uint64_t gui_content_y(void) {
    if (browser_active) {
        return browser_win_y + GUI_TITLE_HEIGHT + 6u + browser_shell_height();
    }
    return terminal_win_y + GUI_TITLE_HEIGHT + 6u;
}

uint64_t gui_content_width(void) {
    if (browser_active) {
        if (browser_minimized) {
            return 0u;
        }
        return browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    }
    return terminal_win_w > 16u ? terminal_win_w - 16u : terminal_win_w;
}

uint64_t gui_content_height(void) {
    if (browser_active) {
        if (browser_minimized) {
            return 0u;
        }
        const uint64_t shell_h = browser_shell_height();
        return browser_win_h > GUI_TITLE_HEIGHT + shell_h + 12u
            ? browser_win_h - GUI_TITLE_HEIGHT - shell_h - 12u
            : 0u;
    }
    return terminal_win_h > GUI_TITLE_HEIGHT + 12u ? terminal_win_h - GUI_TITLE_HEIGHT - 12u : terminal_win_h;
}

void draw_browser_native_home(void) {
    const uint64_t x = gui_content_x();
    const uint64_t y = gui_content_y();
    const uint64_t w = gui_content_width();
    const uint64_t h = gui_content_height();
    /* Figma Frame 16 home: #141414, centred Vex-Brwser wordmark (hexagon
       as the missing "o"), 1028×50 search bar, gray hint bottom-right. */
    const uint32_t bg = pack_color(20, 20, 20);
    const uint32_t text = pack_color(255, 255, 255);
    const uint32_t muted = pack_color(104, 104, 104);
    const uint32_t field = pack_color(15, 15, 15);
    uint64_t title_x;
    uint64_t title_y;
    uint64_t search_w;
    uint64_t search_x;
    uint64_t search_y;

    if (!browser_active || browser_minimized || w == 0u || h == 0u) {
        return;
    }

    if (vex_lvgl_ui_ready() && x <= UINT32_MAX && y <= UINT32_MAX &&
        w <= UINT32_MAX && h <= UINT32_MAX) {
        vex_lvgl_ui_draw_browser_home((uint32_t)x, (uint32_t)y,
            (uint32_t)w, (uint32_t)h,browser_address,browser_address_focused,browser_address_cursor,browser_address_selected);
        return;
    }

    fill_rect(x, y, w, h, bg);
    title_y = y + (h > 400u ? (h * 33u) / 100u : 118u);
    title_x = x + (w > VEX_BROWSER_WORDMARK_W ? (w - VEX_BROWSER_WORDMARK_W) / 2u : 24u);
    if (w > VEX_BROWSER_WORDMARK_W && h > 240u) {
        gui_blit_rgba(title_x, title_y,
                      vex_browser_wordmark, VEX_BROWSER_WORDMARK_W, VEX_BROWSER_WORDMARK_H);
        /* Hexagon "o" is aligned on the wordmark baseline, inside its gap. */
        gui_blit_rgba(title_x + 207u, title_y + 3u,
                      vex_browser_hex, VEX_BROWSER_HEX_W, VEX_BROWSER_HEX_H);
    } else {
        gui_font_text(title_x, title_y, "Vex - Browser", text, 20u);
    }

    search_w = browser_chrome_search_width(w);
    search_x = x + (w > search_w ? (w - search_w) / 2u : 16u);
    search_y = y+browser_chrome_search_y(h);

    fill_rounded_rect(search_x, search_y, search_w, 50u, 10u, field);
    gui_font_text_clipped(search_x + 19u, search_y + (50u - gui_font_cell_height(16u)) / 2u, browser_address[0]?browser_address:"Search or enter an address", text, 16u,search_w>70?search_w-70:1);
    gui_draw_vex_logo(search_x + search_w - 40u, search_y + 12u, 26u);
    gui_font_text(search_x + search_w - 160u, search_y + 56u, "Enter to navigate", muted, 13u);
    compositor_mark_dirty(x, y, w, h);
}

void draw_browser_native_results(void) {
    const uint64_t x = gui_content_x();
    const uint64_t y = gui_content_y();
    const uint64_t w = gui_content_width();
    const uint64_t h = gui_content_height();
    const uint32_t panel = pack_color(22, 16, 40);
    const uint32_t panel_hi = pack_color(32, 22, 58);
    const uint32_t border = pack_color(100, 56, 180);
    const uint32_t accent = pack_color(140, 60, 220);
    const uint32_t text = pack_color(228, 220, 248);
    const uint32_t muted = pack_color(150, 130, 185);
    const uint32_t link = pack_color(160, 100, 255);
    const uint32_t chip = pack_color(40, 28, 70);
    const uint32_t icon_bg = pack_color(36, 24, 62);
    const uint32_t icon_fg = pack_color(160, 80, 255);
    const uint32_t count = ladybird_browser_results_count();
    uint64_t page_x;
    uint64_t page_w;
    uint64_t row_y;
    uint64_t row_h = 84u;
    uint64_t gap = 14u;

    if (!browser_active || browser_minimized || w == 0u || h == 0u) {
        return;
    }

    if (vex_lvgl_ui_ready() && x <= UINT32_MAX && y <= UINT32_MAX &&
        w <= UINT32_MAX && h <= UINT32_MAX) {
        vex_lvgl_ui_item_t items[VEX_LVGL_UI_MAX_ITEMS];
        uint32_t visible = count > VEX_LVGL_UI_MAX_ITEMS
            ? VEX_LVGL_UI_MAX_ITEMS : count;

        for (uint32_t i = 0u; i < visible; ++i) {
            items[i].title = ladybird_browser_result_title(i);
            items[i].subtitle = ladybird_browser_result_url(i);
            items[i].meta = ladybird_browser_result_snippet(i);
            items[i].kind = VEX_LVGL_UI_ITEM_FILE;
            items[i].selected = 0u;
        }
        vex_lvgl_ui_draw_browser_results((uint32_t)x, (uint32_t)y,
            (uint32_t)w, (uint32_t)h, ladybird_browser_results_query(),
            items, visible);
        return;
    }

    /* Figma Frame 17 results: flat #141414, favicon disc + title + URL +
       snippet per result, no card borders. */
    (void)panel; (void)panel_hi; (void)border; (void)chip; (void)icon_bg; (void)icon_fg; (void)accent;
    fill_rect(x, y, w, h, pack_color(20, 20, 20));

    page_x = x + (w > 96u ? 72u : 18u);
    page_w = w > 144u ? w - 144u : (w > 24u ? w - 24u : w);
    draw_browser_text_clipped(page_x, y + 42u, ladybird_browser_results_query(), pack_color(255, 255, 255), page_w > 40u ? page_w - 40u : 0u);
    fill_rect(x, y + 66u, w, 1u, pack_color(72, 49, 86));

    row_y = y + 92u;
    for (uint32_t i = 0u; i < count && i < 6u; ++i) {
        const uint64_t card_y = row_y + (uint64_t)i * (row_h + gap);
        if (card_y + row_h > y + h - 16u) {
            break;
        }
        /* favicon disc */
        fill_circle(page_x + 15u, card_y + 16u, 15u, pack_color(217, 217, 217));
        /* source + url (Figma: white source name, muted url on one line) */
        draw_browser_text_clipped(page_x + 40u, card_y + 2u, ladybird_browser_result_url(i), muted, page_w > 248u ? page_w - 248u : 0u);
        draw_browser_text_clipped(page_x + 40u, card_y + 22u, ladybird_browser_result_title(i), link, page_w > 248u ? page_w - 248u : 0u);
        draw_browser_text_clipped(page_x, card_y + 52u, ladybird_browser_result_snippet(i), text, page_w > 40u ? page_w - 40u : 0u);
    }

    if (count == 0u) {
        draw_browser_text_clipped(page_x, row_y + 24u, "No result", text, page_w > 56u ? page_w - 56u : 0u);
        draw_browser_text_clipped(page_x, row_y + 48u, "The network answered, but no usable link was extracted.", muted, page_w > 56u ? page_w - 56u : 0u);
    }

    compositor_mark_dirty(x, y, w, h);
}

void browser_submit_address_to_web(const char *address) {
    char pending[GUI_BROWSER_ADDRESS_MAX];

    if (address == NULL || address[0] == '\0') {
        return;
    }

    int normalized=browser_navigation_normalize(address,pending,sizeof(pending));
    if(normalized) {
        if(normalized==-2){
            browser_loading_visible=1;
            browser_copy_string(browser_loading_text,sizeof(browser_loading_text),"Address exceeds the supported URI length");
            render_active_app();gui_flush_scene_and_cursor();
        }
        return;
    }
    if(browser_action("navigate",gui_browser_active_tab(),pending)!=0)return;
    browser_copy_string(browser_tabs[browser_selected].uri, sizeof(browser_tabs[0].uri),pending);
    browser_tabs[browser_selected].flags=VEX_BROWSER_LOADING;
    gui_browser_suspend_tab_presented(gui_browser_active_tab());

    ladybird_debug_log("GUI submit: push\n");
    browser_shell_set_address(pending);
    browser_address_focused = 0u;
    keyboard_focus_capture_current();
    browser_native_home_visible = 0u;
    browser_loading_visible = 1u;
    browser_copy_string(browser_loading_text, sizeof(browser_loading_text), pending);
    ladybird_browser_results_hide();
    browser_results_drawn = 0u;
    ladybird_debug_log("GUI submit: draw\n");
    render_active_app();
    gui_flush_scene_and_cursor();
    ladybird_debug_log("GUI submit: done\n");
}

static void append_u64_decimal(char *buffer, size_t capacity, size_t *length, uint64_t value) {
    char digits[20];
    size_t count = 0u;

    if (buffer == NULL || length == NULL || capacity == 0u) {
        return;
    }
    if (value == 0u) {
        if (*length + 1u < capacity) {
            buffer[(*length)++] = '0';
            buffer[*length] = '\0';
        }
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

void browser_submit_content_click(uint64_t x, uint64_t y) {
    char command[48];
    size_t length = 0u;

    command[0] = '\0';
    command[length++] = 'c';
    command[length++] = 'l';
    command[length++] = 'i';
    command[length++] = 'c';
    command[length++] = 'k';
    command[length++] = ':';
    command[length] = '\0';
    append_u64_decimal(command, sizeof(command), &length, x);
    if (length + 1u < sizeof(command)) {
        command[length++] = ':';
        command[length] = '\0';
    }
    append_u64_decimal(command, sizeof(command), &length, y);
    (void)ladybird_browser_command_push(command);
    ladybird_browser_yield_to_process();
}

static int browser_action(const char *action,uint32_t id,const char *argument) {
    char command[GUI_BROWSER_ADDRESS_MAX+48];size_t length=0;
    browser_copy_string(command,sizeof(command),"browser:");
    browser_append_string(command,sizeof(command),action);
    browser_append_string(command,sizeof(command),":");length=browser_string_length(command);
    append_u64_decimal(command,sizeof(command),&length,id);
    browser_append_string(command,sizeof(command),":");
    browser_append_string(command,sizeof(command),argument?argument:"");
    int result=ladybird_browser_command_push(command);
    if(result){browser_copy_string(browser_loading_text,sizeof(browser_loading_text),"Browser command queue full; retry");browser_loading_visible=1;}
    return result;
}
static void browser_select_tab(uint32_t index) {
    if(index>=browser_tab_count)return;
    browser_viewport_hide(); /* Ordered old-page blur/hide before identity changes. */
    browser_selected=index;browser_address_focused=browser_address_selected=0;
    vex_browser_state_t *tab=&browser_tabs[index];
    browser_native_home_visible=!tab->uri[0];
    if(browser_window_focused){keyboard_focus_capture_current();browser_address_focused=browser_native_home_visible;}
    browser_shell_set_address(tab->uri);
    browser_loading_visible=!browser_native_home_visible &&
        (((tab->flags&VEX_BROWSER_LOADING)!=0u) || !gui_browser_has_presented(tab->id));
    browser_copy_string(browser_loading_text,sizeof(browser_loading_text),tab->error[0]?tab->error:tab->uri);
    (void)browser_action("select",tab->id,NULL);
    render_active_app();gui_flush_scene_and_cursor();
}
static void browser_new_tab(void) {
    if(browser_tab_count>=VEX_BROWSER_MAX_TABS){
        browser_copy_string(browser_loading_text,sizeof(browser_loading_text),"Maximum of eight pages reached");
        browser_loading_visible=1;return;
    }
    uint32_t id=browser_next_id+1;
    if(browser_action("create",id,NULL))return;
    browser_next_id=id;
    browser_tabs[browser_tab_count]=(vex_browser_state_t){.id=id};
    browser_copy_string(browser_tabs[browser_tab_count].title,sizeof(browser_tabs[0].title),"New Page");
    browser_select_tab(browser_tab_count++);
}
void gui_browser_cycle_tab(int backwards){
    if(browser_tab_count)browser_select_tab((browser_selected+browser_tab_count+(backwards?-1:1))%browser_tab_count);
}
static void browser_close_tab(unsigned index) {
    if(index>=browser_tab_count)return;
    if(browser_tab_count==1){gui_browser_end();return;}
    uint32_t id=browser_tabs[index].id;
    if(browser_action("close",id,NULL))return;
    gui_browser_release_tab_presented(id);
    browser_viewport_blur();
    for(unsigned i=index;i+1<browser_tab_count;i++)browser_tabs[i]=browser_tabs[i+1];
    --browser_tab_count;
    if(index<browser_selected)--browser_selected;
    browser_select_tab(browser_selected>=browser_tab_count?browser_tab_count-1:browser_selected);
}
int gui_browser_shortcut(const keyboard_event_t *event) {
    if(!browser_active || browser_minimized)return 0;
    if(!browser_window_focused)return 0;
    uint32_t key=event->key,mods=event->modifiers;
    if(key>='A' && key<='Z')key+='a'-'A';
    int handled=0;
    if(mods&1u){
        handled=key=='l'||key=='t'||key=='w'||key=='r'||(key=='a' && browser_address_focused);
#if VEX_BROWSER_DIAGNOSTIC
        handled|=key=='d' && (mods&2u);
#endif
        if(!handled || !event->pressed)return handled;
        if(key=='l'){browser_viewport_blur();browser_address_focused=browser_address_selected=1;
            browser_address_cursor=browser_address_len;keyboard_focus_capture_current();}
        else if(key=='t')browser_new_tab();
        else if(key=='w')browser_close_tab(browser_selected);
        else if(key=='r')(void)browser_action("reload",gui_browser_active_tab(),NULL);
        else if(key=='a')browser_address_selected=1;
#if VEX_BROWSER_DIAGNOSTIC
        else if(key=='d')(void)browser_action("probe",gui_browser_active_tab(),NULL);
#endif
    }else if(mods&4u){
        if(key==0x11004bu || key==0x11004du){
            handled=1;if(event->pressed)(void)browser_action(key==0x11004bu?"back":"forward",gui_browser_active_tab(),NULL);
        }
    }else if(key==0x11003fu){handled=1;if(event->pressed)(void)browser_action("reload",gui_browser_active_tab(),NULL);}
    if(handled){draw_browser_shell();if(browser_native_home_visible)draw_browser_native_home();gui_flush_scene_and_cursor();}
    return handled;
}
int gui_browser_download_cancel(uint32_t id) {
    /* Downloads are session-wide, so the command is not tied to a page. */
    return browser_action("download-cancel",id,NULL);
}
int gui_browser_update_state(const vex_browser_state_t *state) {
    for(unsigned i=0;i<browser_tab_count;i++)if(browser_tabs[i].id==state->id) {
        const uint8_t was_failed=(browser_tabs[i].flags&VEX_BROWSER_FAILED)!=0;
        const uint8_t loading_overlay_was_visible=browser_loading_visible;
        /* Home is native; an about:blank WebKit bootstrap is not navigation. */
        if(!browser_tabs[i].uri[0] && !state->uri[0]) {
            browser_tabs[i].flags=state->flags;
            browser_copy_string(browser_tabs[i].error,sizeof(state->error),state->error);
        }else browser_tabs[i]=*state;
        if(state->flags&VEX_BROWSER_DIVERTED) {
            /* The navigation became a download. Show again what was there
             * before: the committed page, or the native home of an empty tab. */
            if(state->uri[0])gui_browser_resume_tab_presented(state->id);
            if(i==browser_selected) {
                browser_loading_visible=0;browser_loading_text[0]='\0';
                if(!state->uri[0]) {
                    browser_viewport_hide();
                    browser_native_home_visible=1;browser_address_selected=0;
                    browser_shell_set_address("");
                    if(browser_window_focused){keyboard_focus_capture_current();browser_address_focused=1;}
                } else if(!browser_address_focused)browser_shell_set_address(browser_tabs[i].uri);
                render_active_app();gui_flush_scene_and_cursor();
            }
            return 0;
        }
        if(i==browser_selected) {
            if(!browser_address_focused)browser_shell_set_address(browser_tabs[i].uri);
            if(state->flags&VEX_BROWSER_FAILED) {
                browser_loading_visible=1;
                browser_copy_string(browser_loading_text,sizeof(browser_loading_text),state->error);
            } else if(!(state->flags&VEX_BROWSER_LOADING)) {
                /* Stop/cancel may produce no new frame. Remove the native
                 * loading layer when WebKit reports that the request ended. */
                browser_loading_visible=0;
                browser_loading_text[0]='\0';
            } else if(browser_loading_visible) {
                browser_copy_string(browser_loading_text,sizeof(browser_loading_text),state->uri);
            }
            if(state->flags&VEX_BROWSER_FAILED)render_active_app();
            else if(was_failed || (loading_overlay_was_visible &&
                     (state->flags&VEX_BROWSER_LOADING)==0))render_active_app();
            else draw_browser_shell();
            gui_flush_scene_and_cursor();
        }
        return 0;
    }
    return -2; /* late report for a closed tab; never recreate it */
}

void browser_shell_handle_key(char key) {
    if (!browser_active || !browser_address_focused || key == 0) {
        return;
    }

    if (key == '\n' || key == '\r') {
        browser_results_drawn = 0u;
        if (browser_address_len == 0u) {
            browser_address_focused = 0u;
            keyboard_focus_capture_current();
            browser_shell_set_address(browser_tabs[browser_selected].uri);
            render_active_app();
            gui_flush_scene_and_cursor();
            return;
        }
        browser_submit_address_to_web(browser_address);
        return;
    }

    if(key==KEYBOARD_KEY_LEFT || key==KEYBOARD_KEY_RIGHT) {
        if(browser_address_selected){browser_address_cursor=key==KEYBOARD_KEY_LEFT?0:browser_address_len;browser_address_selected=0;}
        else if(key==KEYBOARD_KEY_LEFT && browser_address_cursor)browser_address_cursor--;
        else if(key==KEYBOARD_KEY_RIGHT && browser_address_cursor<browser_address_len)browser_address_cursor++;
        draw_browser_shell();
        if(browser_native_home_visible)draw_browser_native_home();
        gui_flush_scene_and_cursor();
        return;
    }
    if (key == '\b') {
        browser_edit_backspace(browser_address,&browser_address_len,&browser_address_cursor,&browser_address_selected);
        draw_browser_shell();
        if(browser_native_home_visible)draw_browser_native_home();
        gui_flush_scene_and_cursor();
        return;
    }

    if (key >= 32 && key <= 126 && browser_address_len + 1u < sizeof(browser_address)) {
        browser_results_drawn = 0u;
        if(browser_edit_insert(browser_address,sizeof(browser_address),&browser_address_len,&browser_address_cursor,&browser_address_selected,key))return;
        draw_browser_shell();
        if(browser_native_home_visible)draw_browser_native_home();
        gui_flush_scene_and_cursor();
    }
}

gui_window_control_t browser_control_hit(uint64_t x, uint64_t y) {
    if (!browser_active || browser_minimized) {
        return GUI_WINDOW_CONTROL_NONE;
    }

    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + browser_win_w - 22u)) {
        return GUI_WINDOW_CONTROL_CLOSE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + browser_win_w - 48u)) {
        return GUI_WINDOW_CONTROL_MINIMIZE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + browser_win_w - 74u)) {
        return GUI_WINDOW_CONTROL_MAXIMIZE;
    }

    return GUI_WINDOW_CONTROL_NONE;
}

void set_browser_minimized(uint8_t minimized) {
    if (gui_framebuffer == NULL || !browser_active || browser_minimized == minimized) {
        return;
    }

    if (minimized) {
        browser_window_focused=0;
        browser_viewport_blur();
        browser_address_focused = 0u;
        keyboard_focus_release();
        browser_minimized = 1u;
        if (browser_previous_paint && paint_active) {
            paint_minimized = 0u;
            active_pid = 0u;
        } else if (active_pid == 0u) {
            active_pid = browser_previous_pid != 0u ? browser_previous_pid : terminal_pid_value;
        }
        send_app_config(terminal_pid_value,active_pid==terminal_pid_value,0u);
        render_active_app();
        draw_taskbar_static();
        return;
    }

    browser_minimized = 0u;
    browser_window_focused=1;keyboard_focus_capture_current();
    if (browser_previous_paint && paint_active) {
        paint_minimized = 1u;
    }
    if (paint_previous_browser && paint_active) {
        paint_minimized = 1u;
        paint_drawing = 0u;
    }
    render_active_app();
    draw_taskbar_static();
}

int browser_handle_shell_click(uint64_t x, uint64_t y) {
    const uint64_t content_x = gui_content_x();
    const uint64_t content_y = gui_content_y();
    const uint64_t content_w = gui_content_width();
    const uint64_t content_h = gui_content_height();
    const uint64_t shell_x = browser_win_x + 8u;
    const uint64_t shell_y = browser_win_y + GUI_TITLE_HEIGHT + 6u;
    const uint64_t shell_w = browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    const uint64_t address_x = shell_x + 100u;
    const uint64_t address_w = shell_w > 148u ? shell_w - 148u : 80u;

    if (!browser_active || browser_minimized || content_w == 0u || content_h == 0u) {
        return 0;
    }

    unsigned tab_w=browser_chrome_tab_width(shell_w,browser_tab_count);
    if(y>=shell_y && y<shell_y+36 && x>=shell_x+10) {
        unsigned offset=(unsigned)(x-shell_x-10),index=offset/tab_w;
        if(index<browser_tab_count) {
            if(offset%tab_w>=tab_w-26) {
                browser_close_tab(index);
            }else if(index!=browser_selected)browser_select_tab(index);
        }else if(offset<tab_w*browser_tab_count+34) {
            browser_new_tab();
        }
        render_active_app();gui_flush_scene_and_cursor();
        return 1;
    }

    if(!browser_native_home_visible && y>=shell_y+36 && y<shell_y+67 && x>=shell_x && x<shell_x+96) {
        unsigned control=(x-shell_x)/32;
        uint32_t flags=browser_tabs[browser_selected].flags;
        if(control==0 && (flags&VEX_BROWSER_CAN_BACK))(void)browser_action("back",gui_browser_active_tab(),NULL);
        if(control==1 && (flags&VEX_BROWSER_CAN_FORWARD))(void)browser_action("forward",gui_browser_active_tab(),NULL);
        if(control==2)(void)browser_action(flags&VEX_BROWSER_LOADING?"stop":"reload",gui_browser_active_tab(),NULL);
        browser_viewport_blur();return 1;
    }

    if (!browser_native_home_visible &&
        y >= shell_y + 40u && y < shell_y + 66u &&
        x >= address_x && x < address_x + address_w) {
        browser_viewport_blur();
        browser_address_focused = 1u;
        browser_address_selected = 1u;
        browser_address_cursor=browser_address_len;
        keyboard_focus_capture_current();
        draw_browser_shell();
        gui_flush_scene_and_cursor();
        return 1;
    }

    if (x >= content_x && y >= content_y && x < content_x + content_w && y < content_y + content_h) {
        if (ladybird_browser_results_visible()) {
            const uint64_t page_x = content_x + (content_w > 96u ? 72u : 18u);
            const uint64_t page_w = content_w > 144u ? content_w - 144u : (content_w > 24u ? content_w - 24u : content_w);
            const uint64_t row_y = content_y + 120u;
            const uint64_t row_h = 84u;
            const uint64_t gap = 14u;
            const uint32_t count = ladybird_browser_results_count();
            if (x >= page_x && x < page_x + page_w && y >= row_y) {
                const uint64_t offset = y - row_y;
                const uint32_t index = (uint32_t)(offset / (row_h + gap));
                const uint64_t local_y = offset % (row_h + gap);
                if (index < count && local_y < row_h) {
                    const char *url = ladybird_browser_result_url(index);
                    if (url != NULL && url[0] != '\0') {
                        ladybird_browser_results_hide();
                        browser_results_drawn = 0u;
                        browser_submit_address_to_web(url);
                        return 1;
                    }
                }
            }
            return 1;
        }
        if (!browser_native_home_visible) {
            browser_address_focused=0u;
            return 1;
        }
        uint64_t search_w=browser_chrome_search_width(content_w);
        uint64_t search_x=content_x+(content_w-search_w)/2;
        uint64_t search_y=content_y+browser_chrome_search_y(content_h);
        if (x>=search_x && x<search_x+search_w && y>=search_y && y<search_y+50) {
            browser_viewport_blur();
            browser_address_focused = 1u;
            browser_address_selected=1u;
            browser_address_cursor=browser_address_len;
            keyboard_focus_capture_current();
            draw_browser_shell();
            draw_browser_native_home();
            gui_flush_scene_and_cursor();
            return 1;
        }
        return 1;
    }

    return 0;
}

void gui_browser_begin(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    if (!browser_active && !terminal_minimized && active_pid == terminal_pid_value) {
        save_terminal_snapshot();
    }

    browser_previous_paint = (paint_active && !paint_minimized) ? 1u : 0u;
    if (browser_previous_paint) {
        paint_minimized = 1u;
        paint_drawing = 0u;
    }
    browser_previous_pid = active_pid;
    /* Browser focus is carried by the chrome/viewport state. The independent
     * native panel must keep its application and logical terminal alive. */
    browser_active = 1u;
    browser_window_focused=1;
    browser_minimized = 0u;
    browser_address_focused = 0u;
    browser_native_home_visible = 1u;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
    gui_browser_release_presented();
    browser_tab_count=1;browser_selected=0;browser_next_id=1;
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)browser_tabs[i]=(vex_browser_state_t){0};
    browser_tabs[0].id=1;
    browser_copy_string(browser_tabs[0].title,sizeof(browser_tabs[0].title),"New Page");
    browser_shell_set_address("");browser_address_selected=0;
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void gui_browser_launch(void) {
    /* Suchu owns the browser's chrome and session. The desktop only launches
     * or raises its ordinary native app window. Keep legacy /webkit available
     * for diagnostics; don't initialize its parallel kernel chrome here. */
    static uint32_t app_pid;
    if(app_pid && scheduler_process_alive(app_pid)){
        (void)app_surface_raise_owner(app_pid);return;
    }
    uint32_t pid=0;
    if(elf_spawn_pid_caps("/usr/bin/kyil-browser",&pid,VEX_CAP_NET_CONNECT))
        tty_puts("KyIl Browser launch failed\n");
    else {
        app_pid=pid;
        /* Desktop launches have no wait-status consumer. Leaving the GUI as
         * parent retained the entire Suchu address space as an uncollected
         * zombie after CloseWindow. The existing detach contract permits final
         * retirement without changing ownership of the browser's children. */
        scheduler_detach_process(pid);
    }
}

void gui_browser_poll_lifecycle(void) {
    if(browser_closing_pid){int status=0;uint32_t collected=0u;
        int result=process_reap_child_pid(browser_closing_parent_pid,
                                          browser_closing_pid,&status,&collected);
        if(result!=0){
            browser_closing_pid=0;
            browser_closing_parent_pid=0;
            if(browser_reopen_requested){browser_reopen_requested=0;gui_browser_launch();}
        }
    }
}
int gui_browser_lifecycle_pending(void) { return browser_closing_pid != 0u; }
void gui_browser_end(void) {
    uint32_t browser_pid;

    if (gui_framebuffer == NULL || !browser_active) {
        return;
    }

    browser_pid = ladybird_browser_process_pid();
    if(browser_pid!=0u){
        /* The runtime cancels its transfers while shutting down and its final
         * reports are no longer read: record the outcome here. */
        browser_downloads_session_ended();
        if(browser_action("shutdown",gui_browser_active_tab(),NULL)!=0)return;
        browser_closing_pid=browser_pid;
        browser_closing_parent_pid=ladybird_browser_parent_pid();
    }
    browser_active = 0u;
    browser_window_focused=0;keyboard_focus_release();
    gui_browser_release_presented();
    browser_minimized = 0u;
    if (browser_previous_paint && paint_active) {
        paint_minimized = 0u;
        active_pid = 0u;
    } else {
        terminal_minimized = 0u;
        active_pid = browser_previous_pid != 0u ? browser_previous_pid : terminal_pid_value;
    }
    browser_previous_paint = 0u;
    browser_address_focused = 0u;
    browser_native_home_visible = 0u;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
    browser_results_drawn = 0u;
    render_active_app();
    draw_taskbar_static();
    send_app_config(terminal_pid_value, active_pid == terminal_pid_value, 0u);
    gui_flush_scene_and_cursor();
}

void gui_browser_results_changed(void) {
    if (!browser_active || browser_minimized) {
        return;
    }
    browser_native_home_visible = 0u;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
    browser_results_drawn = 0u;
}

void gui_browser_frame_ready(void) {
    if(browser_tabs[browser_selected].flags&VEX_BROWSER_FAILED)return;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
}

void gui_browser_web_content_ready(void) {
    if(!browser_native_home_visible && !(browser_tabs[browser_selected].flags&VEX_BROWSER_FAILED))browser_loading_visible=0;
}
int gui_browser_viewport_visible(void) {
    return browser_active && !browser_minimized && !browser_native_home_visible;
}

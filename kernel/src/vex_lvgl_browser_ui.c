#include <unos/vex_lvgl_ui.h>
#include <unos/browser_chrome.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>

#include "vex_browser_hex.h"
#include "vex_browser_wordmark.h"
#include "vex_icon_hex.h"

#define BROWSER_SHELL_HEIGHT 67
#define BROWSER_TAB_HEIGHT 36
#define BROWSER_RESULT_MAX VEX_LVGL_UI_MAX_ITEMS

typedef struct browser_result_row {
    lv_obj_t *root;
    lv_obj_t *icon;
    lv_obj_t *url;
    lv_obj_t *title;
    lv_obj_t *snippet;
} browser_result_row_t;

typedef struct browser_renderer {
    uint8_t initialized;
    lv_obj_t *shell;
    lv_obj_t *tab;
    lv_obj_t *tabs[VEX_BROWSER_MAX_TABS];
    lv_obj_t *tab_labels[VEX_BROWSER_MAX_TABS];
    lv_obj_t *tab_closes[VEX_BROWSER_MAX_TABS];
    lv_obj_t *new_tab;
    lv_obj_t *tab_divider;
    lv_obj_t *row_divider;
    lv_obj_t *back;
    lv_obj_t *forward;
    lv_obj_t *reload;
    lv_obj_t *address_box;
    lv_obj_t *address;
    lv_obj_t *address_caret;
    lv_obj_t *brand;

    lv_obj_t *home;
    lv_obj_t *home_title;
    lv_obj_t *home_hex;
    lv_obj_t *home_search;
    lv_obj_t *home_search_text;
    lv_obj_t *home_caret;
    lv_obj_t *home_brand;
    lv_obj_t *home_hint;

    lv_obj_t *loading;
    lv_obj_t *loading_card;
    lv_obj_t *loading_title;
    lv_obj_t *loading_status;

    lv_obj_t *results;
    lv_obj_t *results_query;
    lv_obj_t *results_divider;
    browser_result_row_t result_rows[BROWSER_RESULT_MAX];
} browser_renderer_t;

static browser_renderer_t browser;
static uint8_t browser_wordmark_bgra[VEX_BROWSER_WORDMARK_W * VEX_BROWSER_WORDMARK_H * 4u];
static uint8_t browser_hex_bgra[VEX_BROWSER_HEX_W * VEX_BROWSER_HEX_H * 4u];
static uint8_t browser_icon_bgra[VEX_ICON_HEX_W * VEX_ICON_HEX_H * 4u];
static lv_image_dsc_t browser_wordmark_dsc;
static lv_image_dsc_t browser_hex_dsc;
static lv_image_dsc_t browser_icon_dsc;

static void make_passive(lv_obj_t *obj);

static void init_image(lv_image_dsc_t *descriptor, uint8_t *destination,
                       const uint8_t *source, uint32_t width, uint32_t height) {
    const size_t pixels = (size_t)width * height;
    for (size_t i = 0u; i < pixels; ++i) {
        destination[i * 4u + 0u] = source[i * 4u + 2u];
        destination[i * 4u + 1u] = source[i * 4u + 1u];
        destination[i * 4u + 2u] = source[i * 4u + 0u];
        destination[i * 4u + 3u] = source[i * 4u + 3u];
    }
    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor->header.cf = LV_COLOR_FORMAT_ARGB8888;
    descriptor->header.flags = 0u;
    descriptor->header.w = width;
    descriptor->header.h = height;
    descriptor->header.stride = width * 4u;
    descriptor->header.reserved_2 = 0u;
    descriptor->data_size = (uint32_t)(pixels * 4u);
    descriptor->data = destination;
    descriptor->reserved = NULL;
    descriptor->reserved_2 = NULL;
}

static lv_obj_t *image_create(lv_obj_t *parent, const lv_image_dsc_t *source) {
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, source);
    make_passive(image);
    return image;
}

static void make_passive(lv_obj_t *obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
}

static lv_obj_t *clean_object(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    make_passive(obj);
    return obj;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text == NULL ? "" : text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    make_passive(label);
    return label;
}

static void style_panel(lv_obj_t *obj, uint32_t color, int32_t radius) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
}

static void build_shell(lv_obj_t *screen) {
    browser.shell = clean_object(screen);
    style_panel(browser.shell, 0x141414u, 0);
    lv_obj_add_flag(browser.shell, LV_OBJ_FLAG_HIDDEN);
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++) {
        browser.tabs[i]=clean_object(browser.shell);
        browser.tab_labels[i]=label_create(browser.tabs[i],"New Page",&lv_font_montserrat_16,0xeeeeee);
        browser.tab_closes[i]=label_create(browser.tabs[i],"x",&lv_font_montserrat_16,0x998aa3);
        lv_obj_set_pos(browser.tab_labels[i],8,8);
    }

    browser.tab = label_create(browser.shell, "New Page", &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.tab, 71, 8);
    lv_obj_set_size(browser.tab, 160, 22);
    browser.new_tab = label_create(browser.shell, "+", &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.new_tab, 252, 8);
    browser.tab_divider = clean_object(browser.shell);
    style_panel(browser.tab_divider, 0x483156u, 0);
    lv_obj_set_pos(browser.tab_divider, 242, 4);
    lv_obj_set_size(browser.tab_divider, 1, 28);
    browser.row_divider = clean_object(browser.shell);
    style_panel(browser.row_divider, 0x483156u, 0);
    lv_obj_set_pos(browser.row_divider, 0, 35);
    lv_obj_set_size(browser.row_divider, LV_PCT(100), 1);
    browser.back = label_create(browser.shell, LV_SYMBOL_LEFT, &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.back, 14, 43);
    browser.forward = label_create(browser.shell, LV_SYMBOL_RIGHT, &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.forward, 40, 43);
    browser.reload = label_create(browser.shell, LV_SYMBOL_REFRESH, &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.reload, 66, 43);

    browser.address_box = clean_object(browser.shell);
    style_panel(browser.address_box, 0x191919u, 10);
    browser.address = label_create(browser.address_box, "", &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.address, 16, 4);
    browser.address_caret = clean_object(browser.address_box);
    lv_obj_set_style_bg_opa(browser.address_caret,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(browser.address_caret,lv_color_hex(0xc5afff),0);
    browser.brand = image_create(browser.address_box, &browser_icon_dsc);
    lv_obj_set_size(browser.brand, VEX_ICON_HEX_W, VEX_ICON_HEX_H);
}

static void build_home(lv_obj_t *screen) {
    browser.home = clean_object(screen);
    style_panel(browser.home, 0x141414u, 0);
    lv_obj_add_flag(browser.home, LV_OBJ_FLAG_HIDDEN);

    browser.home_title = label_create(browser.home,"Vex-Browser",&lv_font_montserrat_40,0xa492ff);
    browser.home_hex = image_create(browser.home, &browser_hex_dsc);
    browser.home_search = clean_object(browser.home);
    style_panel(browser.home_search, 0x0f0f0fu, 10);
    lv_obj_set_style_border_width(browser.home_search, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(browser.home_search, lv_color_hex(0x483156u), LV_PART_MAIN);
    browser.home_search_text = label_create(browser.home_search, "Search what you want ...",
                                             &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.home_search_text, 19, 15);
    browser.home_caret = clean_object(browser.home_search);
    lv_obj_set_style_bg_opa(browser.home_caret,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(browser.home_caret,lv_color_hex(0xc5afff),0);
    browser.home_brand = image_create(browser.home_search, &browser_icon_dsc);
    lv_obj_set_size(browser.home_brand, VEX_ICON_HEX_W, VEX_ICON_HEX_H);
    lv_obj_align(browser.home_brand, LV_ALIGN_RIGHT_MID, -14, 0);
    browser.home_hint = label_create(browser.home, "Ctrl + Space to talk", &lv_font_montserrat_12, 0x686868u);
}

static void build_loading(lv_obj_t *screen) {
    browser.loading = clean_object(screen);
    style_panel(browser.loading, 0x141414u, 0);
    lv_obj_add_flag(browser.loading, LV_OBJ_FLAG_HIDDEN);
    browser.loading_card = clean_object(browser.loading);
    style_panel(browser.loading_card, 0x1e1e1eu, 10);
    lv_obj_set_style_border_width(browser.loading_card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(browser.loading_card, lv_color_hex(0x483156u), LV_PART_MAIN);
    browser.loading_title = label_create(browser.loading_card, "Loading...", &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(browser.loading_title, 24, 24);
    browser.loading_status = label_create(browser.loading_card, "Waiting for WebKit frame",
                                           &lv_font_montserrat_14, 0x686868u);
    lv_obj_set_pos(browser.loading_status, 24, 58);
}

static void build_results(lv_obj_t *screen) {
    browser.results = clean_object(screen);
    style_panel(browser.results, 0x141414u, 0);
    lv_obj_add_flag(browser.results, LV_OBJ_FLAG_HIDDEN);
    browser.results_query = label_create(browser.results, "", &lv_font_montserrat_16, 0xffffffu);
    browser.results_divider = clean_object(browser.results);
    style_panel(browser.results_divider, 0x483156u, 0);

    for (uint32_t i = 0u; i < BROWSER_RESULT_MAX; ++i) {
        browser_result_row_t *row = &browser.result_rows[i];
        row->root = clean_object(browser.results);
        row->icon = clean_object(row->root);
        lv_obj_set_size(row->icon, 30, 30);
        style_panel(row->icon, 0xd9d9d9u, LV_RADIUS_CIRCLE);
        row->url = label_create(row->root, "", &lv_font_montserrat_12, 0x9682b9u);
        row->title = label_create(row->root, "", &lv_font_montserrat_16, 0xa064ffu);
        row->snippet = label_create(row->root, "", &lv_font_montserrat_14, 0xe4dcf8u);
        lv_obj_add_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    }
}

static int ensure_browser(void) {
    lv_obj_t *screen;
    if (browser.initialized != 0u) {
        return 0;
    }
    if (!vex_lvgl_ui_ready()) {
        return -1;
    }
    screen = lv_screen_active();
    if (screen == NULL) {
        return -1;
    }
    init_image(&browser_wordmark_dsc, browser_wordmark_bgra, vex_browser_wordmark,
               VEX_BROWSER_WORDMARK_W, VEX_BROWSER_WORDMARK_H);
    init_image(&browser_hex_dsc, browser_hex_bgra, vex_browser_hex,
               VEX_BROWSER_HEX_W, VEX_BROWSER_HEX_H);
    init_image(&browser_icon_dsc, browser_icon_bgra, vex_icon_hex,
               VEX_ICON_HEX_W, VEX_ICON_HEX_H);
    build_shell(screen);
    build_home(screen);
    build_loading(screen);
    build_results(screen);
    browser.initialized = 1u;
    return 0;
}

static void refresh(lv_obj_t *obj) {
    lv_display_t *display = lv_display_get_default();
    if (obj == NULL || display == NULL) {
        return;
    }
    vex_lvgl_ui_paint_object(obj);
}

void vex_lvgl_browser_begin_scene(void) {
    if (browser.initialized == 0u) {
        return;
    }
    lv_obj_add_flag(browser.shell, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(browser.home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(browser.loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(browser.results, LV_OBJ_FLAG_HIDDEN);
}

/* The native editor owns the logical caret. Show the same text window and
 * selection, including long URLs, without altering the URL buffer. */
static void editor_visual(lv_obj_t *label,lv_obj_t *cursor,const char *text,
                          uint8_t focused,uint32_t caret,uint8_t select_all,
                          int32_t width,int32_t x,int32_t y) {
    const lv_font_t *font=&lv_font_montserrat_16;
    size_t length=text?strlen(text):0;
    if(caret>length)caret=length;
    size_t start=0;int32_t before=0;
    for(size_t i=0;i<caret;i++)before+=lv_font_get_glyph_width(font,(unsigned char)text[i],i+1<length?(unsigned char)text[i+1]:0);
    while(start<caret && before>width-3){before-=lv_font_get_glyph_width(font,(unsigned char)text[start],start+1<length?(unsigned char)text[start+1]:0);start++;}
    lv_label_set_long_mode(label,focused?LV_LABEL_LONG_CLIP:LV_LABEL_LONG_DOT);
    if(focused)lv_label_set_text(label,length?text+start:"");
    lv_obj_set_width(label,width>0?width:1);
    lv_obj_set_style_bg_color(label,lv_color_hex(0x654582),LV_PART_SELECTED);
    lv_obj_set_style_text_color(label,lv_color_hex(0xffffff),LV_PART_SELECTED);
    lv_label_set_text_selection_start(label,focused&&select_all&&length?0:LV_LABEL_TEXT_SELECTION_OFF);
    lv_label_set_text_selection_end(label,focused&&select_all&&length?(uint32_t)(length-start):LV_LABEL_TEXT_SELECTION_OFF);
    if(focused&&!select_all){lv_obj_remove_flag(cursor,LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(cursor,x+before,y);lv_obj_set_size(cursor,1,font->line_height);}
    else lv_obj_add_flag(cursor,LV_OBJ_FLAG_HIDDEN);
}

void vex_lvgl_ui_draw_browser_shell(uint32_t x, uint32_t y, uint32_t width,
                                    const char *address, uint8_t focused,
                                    const vex_browser_state_t *tabs,uint32_t tab_count,
                                    uint32_t selected,uint8_t native_home,uint32_t caret,uint8_t select_all) {
    int32_t address_w;
    const uint8_t home = native_home;
    if (ensure_browser() != 0 || width == 0u) {
        return;
    }
    lv_obj_set_pos(browser.shell, (int32_t)x, (int32_t)y);
    lv_obj_set_size(browser.shell, (int32_t)width,
                    home ? BROWSER_TAB_HEIGHT : BROWSER_SHELL_HEIGHT);
    lv_obj_remove_flag(browser.shell, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(browser.tab,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(browser.tab_divider,LV_OBJ_FLAG_HIDDEN);
    unsigned tab_w=browser_chrome_tab_width(width,tab_count);
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++) {
        if(i>=tab_count){lv_obj_add_flag(browser.tabs[i],LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(browser.tabs[i],LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(browser.tabs[i],10+i*tab_w,0);
        lv_obj_set_size(browser.tabs[i],tab_w-2,34);
        style_panel(browser.tabs[i],i==selected?0x211926:0x141414,4);
        lv_obj_set_style_border_width(browser.tabs[i],i==selected?1:0,0);
        lv_obj_set_style_border_color(browser.tabs[i],lv_color_hex(0x76508e),0);
        lv_label_set_text(browser.tab_labels[i],tabs[i].title[0]?tabs[i].title:"New Page");
        lv_obj_set_size(browser.tab_labels[i],tab_w>38?tab_w-38:1,22);
        lv_obj_set_pos(browser.tab_closes[i],tab_w-22,8);
    }
    lv_obj_set_pos(browser.new_tab,18+tab_count*tab_w,8);
    uint32_t flags=tab_count?tabs[selected].flags:0;
    lv_obj_set_style_text_color(browser.back,lv_color_hex(flags&VEX_BROWSER_CAN_BACK?0xeeeeee:0x5c5464),0);
    lv_obj_set_style_text_color(browser.forward,lv_color_hex(flags&VEX_BROWSER_CAN_FORWARD?0xeeeeee:0x5c5464),0);
    lv_label_set_text(browser.reload,flags&VEX_BROWSER_LOADING?LV_SYMBOL_CLOSE:LV_SYMBOL_REFRESH);
    if (home) {
        lv_obj_add_flag(browser.back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(browser.forward, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(browser.reload, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(browser.address_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(browser.back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(browser.forward, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(browser.reload, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(browser.address_box, LV_OBJ_FLAG_HIDDEN);
    }

    address_w = width > 148u ? (int32_t)width - 148 : 80;
    lv_obj_set_pos(browser.address_box, 100, 40);
    lv_obj_set_size(browser.address_box, address_w, 26);
    style_panel(browser.address_box, focused != 0u ? 0x1e1e1eu : 0x191919u, 10);
    lv_label_set_text(browser.address,
        address == NULL || address[0] == '\0' ? "Search what you want ..." : address);
    lv_obj_set_style_text_color(browser.address,
        lv_color_hex(address == NULL || address[0] == '\0' ? 0x686868u : 0xffffffu), LV_PART_MAIN);
    lv_obj_set_width(browser.address, address_w > 60 ? address_w - 60 : 1);
    editor_visual(browser.address,browser.address_caret,address,focused,caret,select_all,address_w-60,16,4);
    lv_obj_align(browser.brand, LV_ALIGN_RIGHT_MID, -10, 0);
    refresh(browser.shell);
}

void vex_lvgl_ui_draw_browser_home(uint32_t x, uint32_t y, uint32_t width,
                                   uint32_t height,const char *address,uint8_t focused,uint32_t caret,uint8_t select_all) {
    int32_t search_w;
    int32_t title_y;
    if (ensure_browser() != 0 || width == 0u || height == 0u) {
        return;
    }
    lv_obj_set_pos(browser.home, (int32_t)x, (int32_t)y);
    lv_obj_set_size(browser.home, (int32_t)width, (int32_t)height);
    lv_obj_remove_flag(browser.home, LV_OBJ_FLAG_HIDDEN);

    title_y = height > 400u ? (int32_t)(height * 33u / 100u) : 72;
    lv_obj_set_size(browser.home_title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(browser.home_title, LV_ALIGN_TOP_MID, 0, title_y);
    lv_obj_add_flag(browser.home_hex,LV_OBJ_FLAG_HIDDEN);
    search_w = browser_chrome_search_width(width);
    lv_obj_set_size(browser.home_search, search_w, 50);
    lv_obj_align(browser.home_search, LV_ALIGN_TOP_MID, 0,browser_chrome_search_y(height));
    lv_label_set_text(browser.home_search_text,address&&address[0]?address:"Search or enter an address");
    lv_obj_set_width(browser.home_search_text,search_w>70?search_w-70:1);
    editor_visual(browser.home_search_text,browser.home_caret,address,focused,caret,select_all,search_w-70,19,15);
    lv_obj_set_style_border_color(browser.home_search,lv_color_hex(focused?0xa363da:0x483156),0);
    lv_obj_set_style_border_width(browser.home_search,1,0);
    lv_label_set_text(browser.home_hint,"Enter to navigate");
    lv_obj_set_size(browser.home_hint, 170, 18);
    lv_obj_align_to(browser.home_hint, browser.home_search, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 6);
    refresh(browser.home);
}

void vex_lvgl_ui_draw_browser_loading(uint32_t x, uint32_t y, uint32_t width,
                                      uint32_t height, const char *status, uint8_t failed) {
    int32_t card_w;
    if (ensure_browser() != 0 || width == 0u || height == 0u) {
        return;
    }
    lv_obj_set_pos(browser.loading, (int32_t)x, (int32_t)y);
    lv_obj_set_size(browser.loading, (int32_t)width, (int32_t)height);
    lv_obj_remove_flag(browser.loading, LV_OBJ_FLAG_HIDDEN);
    card_w = width > 760u ? 760 : (int32_t)width - 48;
    if (card_w < 200) card_w = (int32_t)width;
    lv_obj_set_size(browser.loading_card, card_w, 112);
    lv_obj_align(browser.loading_card, LV_ALIGN_TOP_MID, 0, height > 220u ? (int32_t)(height - 112u) / 3 : 28);
    lv_label_set_text(browser.loading_title,failed?"Navigation failed":"Loading...");
    lv_label_set_text(browser.loading_status,
        status == NULL || status[0] == '\0' ? "Waiting for WebKit frame" : status);
    lv_obj_set_width(browser.loading_status, card_w > 48 ? card_w - 48 : 1);
    refresh(browser.loading);
}

void vex_lvgl_ui_draw_browser_results(uint32_t x, uint32_t y, uint32_t width,
                                      uint32_t height, const char *query,
                                      const vex_lvgl_ui_item_t *items,
                                      uint32_t count) {
    int32_t page_x;
    int32_t page_w;
    if (ensure_browser() != 0 || width == 0u || height == 0u) {
        return;
    }
    if (count > BROWSER_RESULT_MAX) count = BROWSER_RESULT_MAX;
    lv_obj_set_pos(browser.results, (int32_t)x, (int32_t)y);
    lv_obj_set_size(browser.results, (int32_t)width, (int32_t)height);
    lv_obj_remove_flag(browser.results, LV_OBJ_FLAG_HIDDEN);

    page_x = width > 96u ? 72 : 18;
    page_w = width > 144u ? (int32_t)width - 144 : (int32_t)width - 24;
    if (page_w < 1) page_w = 1;
    lv_label_set_text(browser.results_query, query == NULL ? "" : query);
    lv_obj_set_pos(browser.results_query, page_x, 40);
    lv_obj_set_width(browser.results_query, page_w);
    lv_obj_set_pos(browser.results_divider, 0, 66);
    lv_obj_set_size(browser.results_divider, (int32_t)width, 1);

    for (uint32_t i = 0u; i < BROWSER_RESULT_MAX; ++i) {
        browser_result_row_t *row = &browser.result_rows[i];
        const int32_t row_y = 92 + (int32_t)i * 98;
        if (i >= count || row_y + 84 > (int32_t)height - 16) {
            lv_obj_add_flag(row->root, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(row->root, page_x, row_y);
        lv_obj_set_size(row->root, page_w, 84);
        lv_obj_set_pos(row->icon, 0, 1);
        lv_obj_set_pos(row->url, 40, 0);
        lv_obj_set_pos(row->title, 40, 21);
        lv_obj_set_pos(row->snippet, 0, 51);
        lv_obj_set_width(row->url, page_w > 40 ? page_w - 40 : 1);
        lv_obj_set_width(row->title, page_w > 40 ? page_w - 40 : 1);
        lv_obj_set_width(row->snippet, page_w);
        lv_label_set_text(row->url, items == NULL || items[i].subtitle == NULL ? "" : items[i].subtitle);
        lv_label_set_text(row->title, items == NULL || items[i].title == NULL ? "" : items[i].title);
        lv_label_set_text(row->snippet, items == NULL || items[i].meta == NULL ? "" : items[i].meta);
        lv_obj_remove_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    }
    refresh(browser.results);
}

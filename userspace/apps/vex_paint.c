#include <stddef.h>
#include <stdint.h>

#include "vex_lvgl.h"
#include "vex_runtime.h"
#include "vex_syscall.h"
#ifdef PAINT_BENCHMARK
#include "vex_bench.h"
#include <string.h>
#endif
static lv_timer_t *clear_timer;
static uint8_t clear_due;
static void document_clear(void);
static void clear_deadline(lv_timer_t *timer){(void)timer;clear_timer=0;document_clear();clear_due=1;}

#define PAINT_W 1200u
#define PAINT_H 720u
#define DOCUMENT_W 640u
#define DOCUMENT_H 360u
#define MAX_STAMPS 4096u

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t radius;
    uint32_t color;
} paint_stamp_t;

static paint_stamp_t stamps[MAX_STAMPS];
static uint32_t stamp_count;
static lv_obj_t *paint_screen;
static lv_obj_t *toolbar;
static lv_obj_t *canvas;
static lv_obj_t *toolbar_title;
static lv_obj_t *toolbar_dividers[2];
static lv_obj_t *color_buttons[3];
static lv_obj_t *size_buttons[3];
static lv_obj_t *eraser_button;
static uint32_t paint_color = 0xffff0000u;
static uint32_t brush_radius = 11u;
static uint8_t eraser_enabled;
static uint8_t drawing;
static int32_t last_x;
static int32_t last_y;

static const uint32_t palette[3] = {
    0xffff0000u,
    0xff000000u,
    0xff3820ffu,
};

static const uint32_t brush_sizes[3] = { 5u, 11u, 18u };
static const int32_t brush_visual_sizes[3] = { 12, 20, 28 };

static void document_clear(void) {
    stamp_count = 0u;
    if (canvas != 0) {
        lv_obj_invalidate(canvas);
    }
}

static void set_circle_style(lv_obj_t *obj, uint32_t color, uint8_t selected) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(color & 0x00ffffffu), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, selected ? 3 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_white(), LV_PART_MAIN);
}

static void refresh_tool_state(void) {
    for (uint32_t i = 0u; i < 3u; ++i) {
        set_circle_style(color_buttons[i], palette[i],
            (uint8_t)(!eraser_enabled && paint_color == palette[i]));
        set_circle_style(size_buttons[i], 0xd9d9d9u,
            (uint8_t)(brush_radius == brush_sizes[i]));
    }
    lv_obj_set_style_bg_color(eraser_button,
        lv_color_hex(eraser_enabled ? VEX_UI_ACCENT : 0x292929u), LV_PART_MAIN);
}

static void color_clicked(lv_event_t *event) {
    const uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    if (index < 3u) {
        paint_color = palette[index];
        eraser_enabled = 0u;
        refresh_tool_state();
    }
}

static void size_clicked(lv_event_t *event) {
    const uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    if (index < 3u) {
        brush_radius = brush_sizes[index];
        eraser_enabled = 0u;
        refresh_tool_state();
    }
}

static void eraser_clicked(lv_event_t *event) {
    (void)event;
    eraser_enabled = (uint8_t)!eraser_enabled;
    refresh_tool_state();
}

static void clear_clicked(lv_event_t *event) {
    (void)event;
    document_clear();
}
static void eraser_deadline_input(lv_event_t *event) {
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED){
        if(clear_timer)lv_timer_delete(clear_timer);
        clear_timer=lv_timer_create(clear_deadline,400,NULL);
        if(clear_timer)lv_timer_set_repeat_count(clear_timer,1);
    } else if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
        if(clear_timer){lv_timer_delete(clear_timer);clear_timer=NULL;}
    }
}

static void stamp(int32_t x, int32_t y) {
    const uint32_t color = eraser_enabled ? 0xffd9d9d9u : paint_color;

    if (stamp_count >= MAX_STAMPS || x < 0 || y < 0 ||
        x >= (int32_t)DOCUMENT_W || y >= (int32_t)DOCUMENT_H) {
        return;
    }
    stamps[stamp_count].x = (int16_t)x;
    stamps[stamp_count].y = (int16_t)y;
    stamps[stamp_count].radius = (uint16_t)brush_radius;
    stamps[stamp_count].color = color;
    ++stamp_count;
}

static void canvas_draw(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    int32_t width;
    int32_t height;

    if (layer == 0 || canvas == 0) {
        return;
    }
    lv_obj_get_coords(canvas, &area);
    width = lv_area_get_width(&area);
    height = lv_area_get_height(&area);

    for (uint32_t index = 0u; index < stamp_count; ++index) {
        const paint_stamp_t *item = &stamps[index];
        const int32_t x = area.x1 + (int32_t)((int64_t)item->x * width / DOCUMENT_W);
        const int32_t y = area.y1 + (int32_t)((int64_t)item->y * height / DOCUMENT_H);
        int32_t radius = (int32_t)((int64_t)item->radius * width / DOCUMENT_W);
        lv_area_t dot;
        lv_draw_rect_dsc_t descriptor;

        if (radius < 1) {
            radius = 1;
        }
        dot.x1 = x - radius;
        dot.y1 = y - radius;
        dot.x2 = x + radius;
        dot.y2 = y + radius;
        lv_draw_rect_dsc_init(&descriptor);
        descriptor.bg_color = lv_color_hex(item->color & 0x00ffffffu);
        descriptor.bg_opa = LV_OPA_COVER;
        descriptor.radius = LV_RADIUS_CIRCLE;
        lv_draw_rect(layer, &descriptor, &dot);
    }
}

static void stroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    const int32_t dx = x1 - x0;
    const int32_t dy = y1 - y0;
    int32_t steps = dx < 0 ? -dx : dx;
    const int32_t abs_y = dy < 0 ? -dy : dy;

    if (abs_y > steps) {
        steps = abs_y;
    }
    if (steps == 0) {
        stamp(x1, y1);
        return;
    }
    for (int32_t i = 0; i <= steps; ++i) {
        stamp(x0 + dx * i / steps, y0 + dy * i / steps);
    }
}

static int canvas_document_point(lv_point_t point, int32_t *document_x, int32_t *document_y) {
    lv_area_t area;
    int32_t local_x;
    int32_t local_y;
    int32_t width;
    int32_t height;

    lv_obj_get_coords(canvas, &area);
    width = lv_area_get_width(&area);
    height = lv_area_get_height(&area);
    local_x = point.x - area.x1;
    local_y = point.y - area.y1;
    if (local_x < 0 || local_y < 0 || local_x >= width || local_y >= height ||
        width <= 0 || height <= 0) {
        return 0;
    }
    *document_x = (int32_t)((int64_t)local_x * DOCUMENT_W / width);
    *document_y = (int32_t)((int64_t)local_y * DOCUMENT_H / height);
    return 1;
}

static void canvas_input(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;
    int32_t x;
    int32_t y;

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        drawing = 0u;
        return;
    }
    if ((code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) || indev == 0) {
        return;
    }
    lv_indev_get_point(indev, &point);
    if (!canvas_document_point(point, &x, &y)) {
        return;
    }
    if (!drawing) {
        drawing = 1u;
        stamp(x, y);
    } else {
        stroke(last_x, last_y, x, y);
    }
    last_x = x;
    last_y = y;
    lv_obj_invalidate(canvas);
}

static lv_obj_t *tool_circle(lv_obj_t *parent, int32_t size) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void paint_layout(vex_lvgl_app_t *app,
                         const vex_ui_layout_t *layout,
                         void *user_data) {
    const uint8_t compact = layout->width < 520u || layout->height < 330u;

    (void)app;
    (void)user_data;
    lv_obj_set_style_pad_all(paint_screen, 0, LV_PART_MAIN);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_size(canvas, LV_PCT(100), LV_PCT(100));

    if (compact) {
        const int32_t bar_w = layout->width > 16u ? (int32_t)layout->width - 16 : 1;
        lv_obj_set_pos(toolbar, 8, (int32_t)layout->height - 60);
        lv_obj_set_size(toolbar, bar_w, 52);
        lv_obj_add_flag(toolbar_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(toolbar_dividers[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(toolbar_dividers[1], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(eraser_button, 8, 8);
        lv_obj_set_size(eraser_button, 34, 34);
        for (uint32_t i = 0u; i < 3u; ++i) {
            lv_obj_set_pos(color_buttons[i], 52 + (int32_t)i * 42, 11);
            lv_obj_set_size(color_buttons[i], 28, 28);
            lv_obj_set_pos(size_buttons[i], bar_w - 122 + (int32_t)i * 40,
                           26 - brush_visual_sizes[i] / 2);
            lv_obj_set_size(size_buttons[i], brush_visual_sizes[i],
                            brush_visual_sizes[i]);
        }
    } else {
        const int32_t toolbar_h = layout->height > 346u ? 326 :
            ((int32_t)layout->height - 16);
        lv_obj_set_pos(toolbar, 10, (int32_t)layout->height - toolbar_h - 8);
        lv_obj_set_size(toolbar, 44, toolbar_h);
        lv_obj_remove_flag(toolbar_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(toolbar_dividers[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(toolbar_dividers[1], LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_pos(toolbar_title, 28, 12);
        lv_obj_set_size(toolbar_title, 112, 16);
        lv_obj_set_style_transform_pivot_x(toolbar_title, 0, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(toolbar_title, 0, LV_PART_MAIN);
        lv_obj_set_style_transform_rotation(toolbar_title, 900, LV_PART_MAIN);
        lv_obj_set_pos(toolbar_dividers[0], 6, 109);
        lv_obj_set_size(toolbar_dividers[0], 32, 1);
        lv_obj_set_pos(eraser_button, 5, 116);
        lv_obj_set_size(eraser_button, 34, 32);
        for (uint32_t i = 0u; i < 3u; ++i) {
            lv_obj_set_pos(color_buttons[i], 12, 155 + (int32_t)i * 39);
            lv_obj_set_size(color_buttons[i], 20, 20);
        }
        lv_obj_set_pos(toolbar_dividers[1], 6, 259);
        lv_obj_set_size(toolbar_dividers[1], 32, 1);
        for (uint32_t i = 0u; i < 3u; ++i) {
            const int32_t diameter = brush_visual_sizes[i];
            const int32_t center_y = 273 + (int32_t)i * 22;
            lv_obj_set_pos(size_buttons[i], (44 - diameter) / 2,
                           center_y - diameter / 2);
            lv_obj_set_size(size_buttons[i], diameter, diameter);
        }
    }
    lv_obj_move_foreground(toolbar);
}

static void build_paint_ui(vex_lvgl_app_t *app) {
    paint_screen = vex_lvgl_screen(app);
    vex_ui_prepare_screen(paint_screen);

    canvas = lv_obj_create(paint_screen);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_style_bg_color(canvas, lv_color_hex(0xd9d9d9u), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, canvas_draw, LV_EVENT_DRAW_MAIN, 0);
    lv_obj_add_event_cb(canvas, canvas_input, LV_EVENT_ALL, 0);

    toolbar = lv_obj_create(paint_screen);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(0x292929u), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(toolbar, 4, LV_PART_MAIN);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

    toolbar_title = vex_ui_label(toolbar, "Other Options",
                                 &lv_font_montserrat_12, 0xf1edf5u);
    for (uint32_t i = 0u; i < 2u; ++i) {
        toolbar_dividers[i] = lv_obj_create(toolbar);
        lv_obj_remove_style_all(toolbar_dividers[i]);
        lv_obj_set_style_bg_color(toolbar_dividers[i], lv_color_hex(0x684574u), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(toolbar_dividers[i], LV_OPA_COVER, LV_PART_MAIN);
    }

    eraser_button = vex_ui_button(toolbar, "+");
    lv_obj_set_style_radius(eraser_button, 3, LV_PART_MAIN);
    lv_obj_add_event_cb(eraser_button, eraser_clicked, LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(eraser_button, clear_clicked, LV_EVENT_LONG_PRESSED, 0);
    lv_obj_add_event_cb(eraser_button, eraser_deadline_input, LV_EVENT_ALL, 0);

    for (uint32_t i = 0u; i < 3u; ++i) {
        color_buttons[i] = tool_circle(toolbar, 20);
        lv_obj_add_event_cb(color_buttons[i], color_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        size_buttons[i] = tool_circle(toolbar, brush_visual_sizes[i]);
        lv_obj_add_event_cb(size_buttons[i], size_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }

    document_clear();
    refresh_tool_state();
    vex_lvgl_app_set_layout_handler(app, paint_layout, 0);
    vex_lvgl_app_apply_layout(app);
}

#ifdef PAINT_BENCHMARK
static void benchmark(vex_lvgl_app_t *app) {
    vex_bench_run();
    for(unsigned phase=2;phase<=5;++phase)for(int f=0;f<16;++f) {
        vex_bench_tag(phase);vex_bench_begin();
        if(f==0 && phase!=4)document_clear();
        eraser_enabled=phase==4;
        paint_color=phase==3?0xff3820ffu:phase==5?0xff000000u:0xffff0000u;
        brush_radius=phase==3?18:phase==5?5:11;
        int y=40+f*16;
        if(phase==2 || phase==4)stroke(80,y,160,y+8);
        if(phase==3)stroke(80,y,240,y);
        if(phase==5)stroke(f%2?260:40,y,f%2?40:260,300-y);
        refresh_tool_state();vex_lvgl_app_render(app);vex_bench_end();
    }
    uint64_t h=0;
    for(unsigned i=0;i<stamp_count;++i)h=(h*33+stamps[i].x+stamps[i].y*641+
        stamps[i].radius*230400+(stamps[i].color&0xffffffu))%2147483647ull;
    vex_bench_report(stamp_count,h);
}
#endif
int main(int argc,char **argv) {
    vex_lvgl_app_t app;
    vex_app_window_event_t event;
#ifdef PAINT_BENCHMARK
    const int measured=argc>1 && strcmp(argv[1],"--bench")==0;
    if(measured){vex_bench_start("lvgl");vex_bench_begin();}
#else
    (void)argc;(void)argv;
#endif
    if (vex_lvgl_app_open(&app, "Vex Paint", PAINT_W, PAINT_H,
            VEX_APP_WINDOW_FLAG_RESIZABLE) != 0) {
        return 1;
    }
    build_paint_ui(&app);
    vex_lvgl_app_render(&app);
#ifdef PAINT_BENCHMARK
    if(measured)vex_bench_end();
#endif

    for (;;) {
        if ((clear_timer?vex_app_window_wait_timeout(&app.window,&event,1):
             vex_app_window_wait(&app.window, &event)) != 0) {
            vex_lvgl_app_close(&app);return 1;
        }
        {
            if (event.type == VEX_APP_EVENT_CLOSE) {
                break;
            }
            lv_timer_handler();
            if(clear_due){clear_due=0;vex_lvgl_app_render(&app);}
#ifdef PAINT_BENCHMARK
            if(measured && event.type==VEX_APP_EVENT_KEY && event.key=='i'){
                vex_bench_idle();vex_bench_begin();vex_lvgl_app_render(&app);vex_bench_end();continue;
            }
            if(measured && event.type==VEX_APP_EVENT_KEY && event.key=='b'){benchmark(&app);continue;}
#endif
            if (vex_lvgl_app_handle_event(&app, &event) && event.type == VEX_APP_EVENT_CONFIGURE) {
                vex_lvgl_app_render(&app);
            }
        }
    }
    vex_lvgl_app_close(&app);
    return 0;
}

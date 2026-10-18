#ifndef UNOS_BROWSER_VIEWPORT_H
#define UNOS_BROWSER_VIEWPORT_H
#include <unos/vex_abi.h>
#include <unos/browser_state.h>
int browser_viewport_poll(vex_viewport_event_t *event);
void browser_viewport_pointer(int x,int y,uint32_t buttons,int wheel,int allowed);
void browser_viewport_key(uint32_t key,uint32_t hardware,uint32_t pressed,uint32_t modifiers);
void browser_viewport_blur(void);
void browser_viewport_hide(void);
int browser_viewport_has_focus(void);
int browser_viewport_bounds(vex_rect_t *bounds);
void gui_browser_web_content_ready(void);
int gui_browser_viewport_visible(void);
uint32_t gui_browser_active_tab(void);
int gui_browser_update_state(const vex_browser_state_t *state);
uint32_t browser_viewport_polled_tab(void);
#endif

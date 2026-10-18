#ifndef KYIL_RAYLIB_INPUT_OBSERVER_H
#define KYIL_RAYLIB_INPUT_OBSERVER_H
#include <unos/vex_abi.h>
/* Process-local observation; Raylib retains ownership. UI thread only. */
typedef void (*vex_raylib_input_observer_t)(const vex_app_window_event_t *,void *);
void vex_raylib_set_input_observer(vex_raylib_input_observer_t,void *);
void vex_raylib_set_close_confirmation(int enabled);
void vex_raylib_cancel_close(void);
/* One-shot wake deadline in scheduler ticks, for the next event wait. */
void vex_raylib_set_event_deadline(uint16_t ticks);
uint32_t vex_raylib_window_id(void);
#endif

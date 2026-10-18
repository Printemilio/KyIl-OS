#ifndef UNOS_KEYBOARD_H
#define UNOS_KEYBOARD_H
#include <stdint.h>
typedef struct keyboard_event {uint32_t key,hardware,pressed,modifiers;} keyboard_event_t;
int keyboard_poll_event(keyboard_event_t *event);
uint32_t keyboard_modifiers(void);

#define KEYBOARD_KEY_UP ((char)0x11)
#define KEYBOARD_KEY_SHIFT_LEFT ((char)0x12)
#define KEYBOARD_KEY_SHIFT_RIGHT ((char)0x13)
#define KEYBOARD_KEY_LEFT ((char)0x14)
#define KEYBOARD_KEY_RIGHT ((char)0x15)
#define KEYBOARD_KEY_DOWN ((char)0x17)
#define KEYBOARD_KEY_SUPER ((char)0x19)
#define KEYBOARD_KEY_CTRL_C ((char)0x03)
#define KEYBOARD_KEY_CTRL_V ((char)0x16)
#define KEYBOARD_KEY_CTRL_S ((char)0x18)
#define KEYBOARD_KEY_CTRL_SPACE ((char)0x1a)
#define KEYBOARD_KEY_ALT_TAB ((char)0x1b)
#define KEYBOARD_KEY_ALT_SHIFT_TAB ((char)0x1c)
#define KEYBOARD_KEY_CTRL_TAB ((char)0x7e)
#define KEYBOARD_KEY_CTRL_SHIFT_TAB ((char)0x7f)
#define KEYBOARD_KEY_APP_SWITCH_NEXT ((char)0x7a)
#define KEYBOARD_KEY_APP_SWITCH_PREV ((char)0x7b)
#define KEYBOARD_KEY_BROWSER_FOCUS ((char)0x1d)
#define KEYBOARD_KEY_BROWSER_NEW_TAB ((char)0x1e)
#define KEYBOARD_KEY_ALT_F10 ((char)0x7c)

void keyboard_init(void);
void keyboard_irq_handler(void);
char keyboard_getchar(void);
char keyboard_pollchar(void);
char keyboard_pollchar_kind(uint8_t *shortcut);
int keyboard_input_pending(void);
void keyboard_inject_char(char value);
void keyboard_focus_capture_current(void);
void keyboard_focus_capture_pid(uint32_t pid); /* Internal physical-input dispatcher ownership. */
void keyboard_focus_release(void);

#endif

#include <unos/keyboard.h>
#include <unos/gui.h>

#include <stdbool.h>
#include <stdint.h>
#include <unos/scheduler.h>
#include <unos/syscall.h>
#ifndef VEX_KEYBOARD_HOST_TEST
#include <unos/kernel_guard.h>
static kernel_gate_t keyboard_gate=KERNEL_GATE_INIT;
#endif
static inline uint64_t irq_save(void);
static inline void irq_restore(uint64_t flags);

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_AUX_DATA 0x20u
#define SCANCODE_RELEASE 0x80u
#define SCANCODE_EXTENDED 0xe0u
#define SCANCODE_LEFT_SHIFT 0x2au
#define SCANCODE_RIGHT_SHIFT 0x36u
#define SCANCODE_CTRL 0x1du
#define SCANCODE_ALT 0x38u
#define SCANCODE_TAB 0x0fu
#define SCANCODE_UP 0x48u
#define SCANCODE_DOWN 0x50u
#define SCANCODE_LEFT 0x4bu
#define SCANCODE_RIGHT 0x4du
#define SCANCODE_SUPER 0x5bu
#define SCANCODE_F6 0x40u
#define SCANCODE_F10 0x44u
#define SCANCODE_C 0x2eu
#define SCANCODE_V 0x2fu
#define SCANCODE_SPACE 0x39u
#define KEYBOARD_SCANCODE_BUFFER_SIZE 128u
#define KEYBOARD_INJECTED_BUFFER_SIZE 256u
#define KEYBOARD_HOTKEY_BUFFER_SIZE 32u

extern uint32_t scheduler_current_pid(void);
static bool extended_scancode;
static bool shift_pressed;
static bool ctrl_pressed;
static bool alt_pressed;
static bool hotkey_extended_scancode;
static bool hotkey_shift_pressed;
static bool hotkey_ctrl_pressed;
static bool hotkey_alt_pressed;
static uint8_t hotkey_shift_down,hotkey_ctrl_down,hotkey_alt_down;
static uint32_t focus_owner_pid;
static uint32_t focus_waiting_pid;
static volatile uint32_t blocking_reader_pid;
static volatile uint8_t scancode_buffer[KEYBOARD_SCANCODE_BUFFER_SIZE];
/* The IRQ hotkey and raw decoder share one physical key event. */
static volatile uint8_t scancode_shortcut_delivered[KEYBOARD_SCANCODE_BUFFER_SIZE];
static volatile uint8_t scancode_head;
static volatile uint8_t scancode_tail;
static volatile char injected_buffer[KEYBOARD_INJECTED_BUFFER_SIZE];
static volatile uint8_t injected_head;
static volatile uint8_t injected_tail;
static volatile char hotkey_buffer[KEYBOARD_HOTKEY_BUFFER_SIZE];
static volatile uint8_t hotkey_head;
static volatile uint8_t hotkey_tail;
static keyboard_event_t key_events[128];
static uint32_t key_event_owner[128];
static uint8_t key_head,key_tail;
static uint32_t down_keys[256];
static uint8_t decoded_shortcut;

static void keyboard_guard_end(uint64_t *flags){irq_restore(*flags);}
uint32_t keyboard_modifiers(void) {
    uint64_t input_guard __attribute__((cleanup(keyboard_guard_end)))=irq_save();
    return (ctrl_pressed?1u:0u)|(shift_pressed?2u:0u)|(alt_pressed?4u:0u);
}
int keyboard_poll_event(keyboard_event_t *e) {
    uint64_t input_guard __attribute__((cleanup(keyboard_guard_end)))=irq_save();
    while(key_head!=key_tail) {
        unsigned index=key_tail;key_tail=(key_tail+1u)%128u;
        if(key_event_owner[index]==scheduler_current_pid()){*e=key_events[index];return 1;}
    }
    return 0;
}

static const uint8_t scancode_ascii_normal[128] = {
    [0x01] = 27,
    [0x02] = '&', [0x03] = 0xe9, [0x04] = '"', [0x05] = '\'',
    [0x06] = '(', [0x07] = '-', [0x08] = 0xe8, [0x09] = '_',
    [0x0a] = 0xe7, [0x0b] = 0xe0, [0x0c] = ')', [0x0d] = '=',
    [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'a', [0x11] = 'z', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '^', [0x1b] = '$',
    [0x1c] = '\n',
    [0x1e] = 'q', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = 'm', [0x28] = 0xf9, [0x29] = 0xb2,
    [0x2b] = '*',
    [0x2c] = 'w', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = ',', [0x33] = ';',
    [0x34] = ':', [0x35] = '!', [0x39] = ' ',
};

static const uint8_t scancode_ascii_shift[128] = {
    [0x01] = 27,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = 0xb0, [0x0d] = '+',
    [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'A', [0x11] = 'Z', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = 0xa8, [0x1b] = 0xa3,
    [0x1c] = '\n',
    [0x1e] = 'Q', [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = 'M', [0x28] = '%', [0x29] = '~',
    [0x2b] = 0xb5,
    [0x2c] = 'W', [0x2d] = 'X', [0x2e] = 'C', [0x2f] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = '?', [0x33] = '.',
    [0x34] = '/', [0x35] = 0xa7, [0x39] = ' ',
};

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint64_t irq_save(void) {
#ifdef VEX_KEYBOARD_HOST_TEST
    return 0;
#else
    kernel_guard_t guard=kernel_guard_enter(&keyboard_gate);return guard.flags;
#endif
}

static inline void irq_restore(uint64_t flags) {
#ifdef VEX_KEYBOARD_HOST_TEST
    (void)flags;
#else
    kernel_guard_t guard={&keyboard_gate,flags};kernel_guard_leave(&guard);
#endif
}

static void set_blocking_reader(uint32_t pid) {
    const uint64_t flags = irq_save();
    blocking_reader_pid = pid;
    irq_restore(flags);
}

static uint32_t get_blocking_reader(void) {
    uint32_t pid;
    const uint64_t flags = irq_save();
    pid = blocking_reader_pid;
    irq_restore(flags);
    return pid;
}

static void push_scancode(uint8_t scancode, uint8_t shortcut_delivered) {
    const uint64_t flags = irq_save();
    const uint8_t next = (uint8_t)((scancode_head + 1u) % KEYBOARD_SCANCODE_BUFFER_SIZE);
    if (next == scancode_tail) {
        irq_restore(flags);
        return;
    }

    scancode_buffer[scancode_head] = scancode;
    scancode_shortcut_delivered[scancode_head] = shortcut_delivered;
    scancode_head = next;
    irq_restore(flags);
}

static int pop_scancode(uint8_t *scancode, uint8_t *shortcut_delivered) {
    const uint64_t flags = irq_save();

    if (scancode_tail == scancode_head) {
        irq_restore(flags);
        return 0;
    }

    *scancode = scancode_buffer[scancode_tail];
    *shortcut_delivered = scancode_shortcut_delivered[scancode_tail];
    scancode_tail = (uint8_t)((scancode_tail + 1u) % KEYBOARD_SCANCODE_BUFFER_SIZE);
    irq_restore(flags);
    return 1;
}

static int pop_injected_char(char *value) {
    const uint64_t flags = irq_save();

    if (injected_tail == injected_head) {
        irq_restore(flags);
        return 0;
    }

    *value = injected_buffer[injected_tail];
    injected_tail = (uint8_t)((injected_tail + 1u) % KEYBOARD_INJECTED_BUFFER_SIZE);
    irq_restore(flags);
    return 1;
}

static int push_hotkey_char(char value) {
    const uint64_t flags = irq_save();
    const uint8_t next = (uint8_t)((hotkey_head + 1u) % KEYBOARD_HOTKEY_BUFFER_SIZE);
    if (value == 0 || next == hotkey_tail) {
        irq_restore(flags);
        return 0;
    }

    hotkey_buffer[hotkey_head] = value;
    hotkey_head = next;
    irq_restore(flags);
    return 1;
}

static int pop_hotkey_char(char *value) {
    const uint64_t flags = irq_save();

    if (hotkey_tail == hotkey_head) {
        irq_restore(flags);
        return 0;
    }

    *value = hotkey_buffer[hotkey_tail];
    hotkey_tail = (uint8_t)((hotkey_tail + 1u) % KEYBOARD_HOTKEY_BUFFER_SIZE);
    irq_restore(flags);
    return 1;
}

static int track_hotkey_scancode(uint8_t scancode) {
    if(scancode==SCANCODE_EXTENDED){hotkey_extended_scancode=true;return 0;}
    const uint8_t key=scancode&~SCANCODE_RELEASE;
    const uint8_t released=(scancode&SCANCODE_RELEASE)!=0;
    const uint8_t extended=hotkey_extended_scancode;
    hotkey_extended_scancode=false;
    uint8_t *down=0,bit=extended?2u:1u;
    if(!extended && (key==SCANCODE_LEFT_SHIFT || key==SCANCODE_RIGHT_SHIFT)) {
        down=&hotkey_shift_down;bit=key==SCANCODE_LEFT_SHIFT?1u:2u;
    }else if(key==SCANCODE_CTRL)down=&hotkey_ctrl_down;
    else if(key==SCANCODE_ALT)down=&hotkey_alt_down;
    if(down){
        if(released)*down&=(uint8_t)~bit;else *down|=bit;
        hotkey_shift_pressed=hotkey_shift_down!=0;
        hotkey_ctrl_pressed=hotkey_ctrl_down!=0;
        hotkey_alt_pressed=hotkey_alt_down!=0;
        return 0;
    }
    if(released || extended)return 0;
    if((hotkey_alt_pressed || hotkey_ctrl_pressed) && key==SCANCODE_TAB)
        return push_hotkey_char(hotkey_alt_pressed?
            (hotkey_shift_pressed?KEYBOARD_KEY_APP_SWITCH_PREV:KEYBOARD_KEY_APP_SWITCH_NEXT):
            (hotkey_shift_pressed?KEYBOARD_KEY_CTRL_SHIFT_TAB:KEYBOARD_KEY_CTRL_TAB));
    if(key==SCANCODE_F6)
        return push_hotkey_char(hotkey_shift_pressed?KEYBOARD_KEY_APP_SWITCH_PREV:KEYBOARD_KEY_APP_SWITCH_NEXT);
    if(hotkey_alt_pressed && key==SCANCODE_F10)
        return push_hotkey_char(KEYBOARD_KEY_ALT_F10);
    return 0;
}

static int decode_scancode_legacy(uint8_t scancode) {
    if (scancode == SCANCODE_EXTENDED) {
        extended_scancode = true;
        return 0;
    }

    const uint8_t key = (uint8_t)(scancode & ~SCANCODE_RELEASE);
    if ((scancode & SCANCODE_RELEASE) != 0) {
        if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
            shift_pressed = false;
        } else if (key == SCANCODE_CTRL) {
            ctrl_pressed = false;
        } else if (key == SCANCODE_ALT) {
            alt_pressed = false;
        }
        extended_scancode = false;
        return 0;
    }

    if (extended_scancode) {
        extended_scancode = false;
        if (scancode == SCANCODE_SUPER) {
            return KEYBOARD_KEY_SUPER;
        }
        if (scancode == SCANCODE_UP) {
            return KEYBOARD_KEY_UP;
        }
        if (scancode == SCANCODE_DOWN) {
            return KEYBOARD_KEY_DOWN;
        }
        if (scancode == SCANCODE_LEFT) {
            return KEYBOARD_KEY_LEFT;
        }
        if (scancode == SCANCODE_RIGHT) {
            return KEYBOARD_KEY_RIGHT;
        }
        if (shift_pressed && scancode == SCANCODE_LEFT) {
            return KEYBOARD_KEY_SHIFT_LEFT;
        }
        if (shift_pressed && scancode == SCANCODE_RIGHT) {
            return KEYBOARD_KEY_SHIFT_RIGHT;
        }
        return 0;
    }

    if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
        shift_pressed = true;
        return 0;
    }

    if (key == SCANCODE_CTRL) {
        ctrl_pressed = true;
        return 0;
    }
    if (key == SCANCODE_ALT) {
        alt_pressed = true;
        return 0;
    }

    if (alt_pressed && key == SCANCODE_TAB) {
        return shift_pressed ? KEYBOARD_KEY_APP_SWITCH_PREV : KEYBOARD_KEY_APP_SWITCH_NEXT;
    }
    if (ctrl_pressed && key == SCANCODE_TAB) {
        return shift_pressed ? KEYBOARD_KEY_CTRL_SHIFT_TAB : KEYBOARD_KEY_CTRL_TAB;
    }
    if (key == SCANCODE_F6) {
        return shift_pressed ? KEYBOARD_KEY_APP_SWITCH_PREV : KEYBOARD_KEY_APP_SWITCH_NEXT;
    }
    if (alt_pressed && key == SCANCODE_F10) {
        return KEYBOARD_KEY_ALT_F10;
    }

    if (ctrl_pressed && key == SCANCODE_C) {
        return KEYBOARD_KEY_CTRL_C;
    }
    if (ctrl_pressed && key == SCANCODE_V) {
        return KEYBOARD_KEY_CTRL_V;
    }
    if (ctrl_pressed && key == 0x1fu) {
        return KEYBOARD_KEY_CTRL_S;
    }
    if (ctrl_pressed && key == SCANCODE_SPACE) {
        return KEYBOARD_KEY_CTRL_SPACE;
    }

    const int value = shift_pressed ? scancode_ascii_shift[key] : scancode_ascii_normal[key];
    return value;
}

static int decode_scancode(uint8_t scancode) {
    uint32_t hardware=(scancode&0x7fu)+(extended_scancode?128u:0u);
    int value=decode_scancode_legacy(scancode);
    if(scancode==SCANCODE_EXTENDED || scancode==0xe1u)return value;
    uint32_t key=0,base=scancode&0x7fu;
    if(hardware>=128)key=0x110000u+base;
    else if(base<128)key=shift_pressed?scancode_ascii_shift[base]:scancode_ascii_normal[base];
    if(base==0x2au || base==0x36u || base==0x1du || base==0x38u)key=0x110000u+hardware;
    /* Keys without a character (F1-F12, Caps Lock...) still have edges. */
    if(!key && hardware<128u)key=0x110000u+hardware;
    if(scancode&0x80u){key=down_keys[hardware];down_keys[hardware]=0;}else down_keys[hardware]=key;
    /* Releasing one of two held modifiers must not release the other. */
    shift_pressed=down_keys[0x2a] || down_keys[0x36];
    ctrl_pressed=down_keys[0x1d] || down_keys[128+0x1d];
    alt_pressed=down_keys[0x38] || down_keys[128+0x38];
    /* Desktop shortcuts are consumed by the native window dispatcher. */
    decoded_shortcut = (scancode & 0x80u) == 0u &&
        ((hardware == SCANCODE_F6) ||
         (hardware == SCANCODE_TAB && (ctrl_pressed || alt_pressed)) ||
         (hardware == SCANCODE_F10 && alt_pressed) ||
         hardware == 128u + SCANCODE_SUPER ||
         (hardware == SCANCODE_SPACE && ctrl_pressed));
    /* Ctrl+Tab has a legacy shadow shortcut, but raw applications must still
     * receive its physical edges. GUI chooses the target, outside this gate.
     * Alt+Tab remains desktop-owned. */
    if(decoded_shortcut && !(hardware==SCANCODE_TAB && ctrl_pressed && !alt_pressed)){
        key=0;down_keys[hardware]=0;
    }
    uint8_t next=(key_head+1u)%128u;
    if(key && next!=key_tail) {
        key_events[key_head]=(keyboard_event_t){key,hardware,(scancode&0x80u)==0,keyboard_modifiers()};
        key_event_owner[key_head]=scheduler_current_pid();key_head=next;
    }
    return value;
}

static int keyboard_try_read(void) {
    uint64_t input_guard __attribute__((cleanup(keyboard_guard_end)))=irq_save();
    uint8_t scancode, shortcut_delivered;
    char injected;

    if (pop_injected_char(&injected)) {
        decoded_shortcut = 0u;
        return (unsigned char)injected;
    }

    if (pop_scancode(&scancode, &shortcut_delivered)) {
        const int value = decode_scancode(scancode);
        /* Keep modifier/key state updated, but deliver a desktop shortcut
           only once. If the hotkey queue was full, the raw path remains. */
        return shortcut_delivered && decoded_shortcut ? 0 : value;
    }

    return -1;
}

static void keyboard_syscall_handler(void *args) {
    char *out = args;
    if (out != 0) {
        *out = keyboard_getchar();
    }
}

void keyboard_init(void) {
    extended_scancode = false;
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    hotkey_extended_scancode = false;
    hotkey_shift_pressed = false;
    hotkey_ctrl_pressed = false;
    hotkey_alt_pressed = false;
    hotkey_shift_down=hotkey_ctrl_down=hotkey_alt_down=0;
    focus_owner_pid = 0;
    focus_waiting_pid = 0;
    blocking_reader_pid = 0;
    scancode_head = 0;
    scancode_tail = 0;
    injected_head = 0;
    injected_tail = 0;
    hotkey_head = 0;
    hotkey_tail = 0;
    syscall_register("keyboard_getchar", keyboard_syscall_handler, SYSCALL_PRIORITY_CRITICAL);
}

void keyboard_irq_handler(void) {
    uint64_t input_guard __attribute__((cleanup(keyboard_guard_end)))=irq_save();
    const uint8_t status = inb(PS2_STATUS_PORT);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0 || (status & PS2_STATUS_AUX_DATA) != 0) {
        return;
    }

    {
        const uint8_t scancode = inb(PS2_DATA_PORT);
        const uint8_t shortcut_delivered = track_hotkey_scancode(scancode);
        push_scancode(scancode, shortcut_delivered);
    }
    gui_input_wake();
}

char keyboard_getchar(void) {
    const uint32_t pid = scheduler_current_pid();

    set_blocking_reader(pid);

    for (;;) {
        if (focus_owner_pid != 0 && focus_owner_pid != pid) {
            focus_waiting_pid = pid;
            process_wait();
            continue;
        }

        /* Poll the PS/2 port directly so input works even when IRQ1
           delivery is unreliable.  The IRQ handler itself is also
           registered and will fill the buffer when interrupts do fire. */
        keyboard_irq_handler();

        const int value = keyboard_try_read();
        if (value > 0) {
            set_blocking_reader(0);
            process_wake(pid);
            return (char)value;
        }

        process_yield();
    }
}

char keyboard_pollchar_kind(uint8_t *shortcut) {
    const uint32_t pid = scheduler_current_pid();
    const uint32_t blocking_pid = get_blocking_reader();
    char hotkey;
    if (shortcut != NULL) *shortcut = 0u;

    if (pop_hotkey_char(&hotkey)) {
        if (shortcut != NULL) *shortcut = 1u;
        return hotkey;
    }

    if (focus_owner_pid != 0 && focus_owner_pid != pid) {
        return 0;
    }

    if (blocking_pid != 0 && blocking_pid != pid && focus_owner_pid != pid) {
        return 0;
    }

    keyboard_irq_handler();

    {
        const int value = keyboard_try_read();
        if (value > 0) {
            if (shortcut != NULL) *shortcut = decoded_shortcut;
            return (char)value;
        }
    }

    return 0;
}

int keyboard_input_pending(void) {
    const uint64_t flags = irq_save();
    const uint32_t pid = scheduler_current_pid();
    const int may_read = (focus_owner_pid == 0 || focus_owner_pid == pid) &&
        (blocking_reader_pid == 0 || blocking_reader_pid == pid || focus_owner_pid == pid);
    const int pending = hotkey_head != hotkey_tail ||
        (may_read && (scancode_head != scancode_tail || injected_head != injected_tail));
    irq_restore(flags);
    return pending;
}

char keyboard_pollchar(void) {
    return keyboard_pollchar_kind(NULL);
}

void keyboard_inject_char(char value) {
    const uint64_t flags = irq_save();
    const uint8_t next = (uint8_t)((injected_head + 1u) % KEYBOARD_INJECTED_BUFFER_SIZE);
    const uint32_t reader = blocking_reader_pid;

    if (value == 0 || next == injected_tail) {
        irq_restore(flags);
        return;
    }

    injected_buffer[injected_head] = value;
    injected_head = next;
    irq_restore(flags);

    gui_input_wake();
    if (reader != 0) {
        process_wake(reader);
    }
}

void keyboard_focus_capture_pid(uint32_t pid) {
    uint64_t input_guard __attribute__((cleanup(keyboard_guard_end)))=irq_save();
    focus_owner_pid = pid;
}
void keyboard_focus_capture_current(void) {keyboard_focus_capture_pid(scheduler_current_pid());}

void keyboard_focus_release(void) {
    uint64_t input_guard __attribute__((cleanup(keyboard_guard_end)))=irq_save();
    focus_owner_pid = 0;
    if (focus_waiting_pid != 0) {
        process_wake(focus_waiting_pid);
        focus_waiting_pid = 0;
    }
}

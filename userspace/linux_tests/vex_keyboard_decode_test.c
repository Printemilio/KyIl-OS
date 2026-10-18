/* Host test executes the actual decoder, never IRQ/port instructions. */
#define VEX_KEYBOARD_HOST_TEST 1
#include "../../kernel/src/keyboard.c"
extern int printf(const char *,...);
static uint32_t test_pid=7;
uint32_t scheduler_current_pid(void){return test_pid;}
#define CHECK(x) do{if(!(x)){printf("keyboard FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){keyboard_event_t e;
    decode_scancode(0x2a);decode_scancode(0x36);decode_scancode(0xaa);
    CHECK(keyboard_modifiers()==2);decode_scancode(0x10);
    CHECK(down_keys[0x10]=='A');decode_scancode(0x90);decode_scancode(0xb6);
    CHECK(keyboard_modifiers()==0);
    unsigned presses=0,releases=0;
    while(keyboard_poll_event(&e)){if(e.hardware==0x10){CHECK(e.key=='A' && e.modifiers==2);if(e.pressed)++presses;else ++releases;}}
    CHECK(presses==1 && releases==1);
    decode_scancode(0xe0);decode_scancode(0x4d);
    CHECK(keyboard_poll_event(&e) && e.hardware==128+0x4d && e.key==0x11004d && e.pressed);
    decode_scancode(0xe0);decode_scancode(0xcd);CHECK(keyboard_poll_event(&e) && !e.pressed);
    test_pid=8;decode_scancode(0x30);test_pid=7;CHECK(!keyboard_poll_event(&e));
    decode_scancode(0x1d);decode_scancode(0x10);CHECK(keyboard_modifiers()==1);
    CHECK(keyboard_poll_event(&e) && e.hardware==0x1d);
    CHECK(keyboard_poll_event(&e) && e.key=='a' && e.modifiers==1);
    decode_scancode(0x9d);while(keyboard_poll_event(&e)) {}
    /* Same IRQ ingestion + raw consumption as a physical Ctrl+Tab. The
       IRQ hotkey must survive another app owning focus, without a second
       switch when the raw scancodes are eventually decoded by the GUI. */
    const uint8_t sequence[]={0x1d,0x0f,0x8f,0x9d};
    for(unsigned i=0;i<sizeof(sequence);++i)
        push_scancode(sequence[i],track_hotkey_scancode(sequence[i]));
    char hotkey=0;CHECK(pop_hotkey_char(&hotkey) && hotkey==KEYBOARD_KEY_CTRL_TAB);
    unsigned duplicates=0;
    for(unsigned i=0;i<sizeof(sequence);++i)if(keyboard_try_read()>0)duplicates++;
    CHECK(!duplicates && !pop_hotkey_char(&hotkey) && !keyboard_modifiers());
    unsigned raw_tabs=0;while(keyboard_poll_event(&e))if(e.hardware==0x0f){CHECK(e.modifiers&1u);raw_tabs++;}
    CHECK(raw_tabs==2);
    /* A full hotkey queue must retain the single raw fallback delivery. */
    hotkey_head=31;hotkey_tail=0;
    for(unsigned i=0;i<sizeof(sequence);++i)
        push_scancode(sequence[i],track_hotkey_scancode(sequence[i]));
    unsigned fallback=0;
    for(unsigned i=0;i<sizeof(sequence);++i)if(keyboard_try_read()==KEYBOARD_KEY_CTRL_TAB)fallback++;
    CHECK(fallback==1 && !keyboard_modifiers());hotkey_head=hotkey_tail=0;
    raw_tabs=0;while(keyboard_poll_event(&e))if(e.hardware==0x0f){CHECK(e.modifiers&1u);raw_tabs++;}
    CHECK(raw_tabs==2);
    const uint8_t shifts[]={0x1d,0x2a,0x36,0xaa,0x0f,0x8f,0xb6,0x9d};
    for(unsigned i=0;i<sizeof(shifts);++i)
        push_scancode(shifts[i],track_hotkey_scancode(shifts[i]));
    CHECK(pop_hotkey_char(&hotkey) && hotkey==KEYBOARD_KEY_CTRL_SHIFT_TAB);
    for(unsigned i=0;i<sizeof(shifts);++i)CHECK(keyboard_try_read()==0);
    CHECK(!hotkey_shift_pressed && !hotkey_ctrl_pressed && !keyboard_modifiers());
    raw_tabs=0;while(keyboard_poll_event(&e))if(e.hardware==0x0f){CHECK((e.modifiers&3u)==3u);raw_tabs++;}
    CHECK(raw_tabs==2);
    CHECK(decode_scancode(0x11)=='z' && !decoded_shortcut);
    CHECK(keyboard_poll_event(&e) && e.key=='z' && e.pressed);
    decode_scancode(0x91);CHECK(keyboard_poll_event(&e) && e.key=='z' && !e.pressed);
    CHECK(decode_scancode(0x40)==KEYBOARD_KEY_APP_SWITCH_NEXT && decoded_shortcut);
    CHECK(!keyboard_poll_event(&e));decode_scancode(0xc0);
    decode_scancode(0x1d);while(keyboard_poll_event(&e)){}
    CHECK(decode_scancode(0x0f)==KEYBOARD_KEY_CTRL_TAB && decoded_shortcut);
    CHECK(keyboard_poll_event(&e) && e.hardware==0x0f && e.pressed && (e.modifiers&1u));
    decode_scancode(0x8f);
    CHECK(keyboard_poll_event(&e) && e.hardware==0x0f && !e.pressed && (e.modifiers&1u));
    decode_scancode(0x2a);while(keyboard_poll_event(&e)){}
    CHECK(decode_scancode(0x0f)==KEYBOARD_KEY_CTRL_SHIFT_TAB && decoded_shortcut);
    CHECK(keyboard_poll_event(&e) && e.hardware==0x0f && e.pressed && (e.modifiers&3u)==3u);
    decode_scancode(0x8f);
    CHECK(keyboard_poll_event(&e) && e.hardware==0x0f && !e.pressed);
    decode_scancode(0xaa);decode_scancode(0x9d);
    while(keyboard_poll_event(&e)){}
    decode_scancode(0x38);while(keyboard_poll_event(&e)){}
    CHECK(decode_scancode(0x0f)==KEYBOARD_KEY_APP_SWITCH_NEXT && decoded_shortcut);
    decode_scancode(0x8f);decode_scancode(0xb8);
    decode_scancode(0x38);while(keyboard_poll_event(&e)){}
    CHECK(decode_scancode(0x44)==KEYBOARD_KEY_ALT_F10 && decoded_shortcut);
    CHECK(!keyboard_poll_event(&e));
    decode_scancode(0xc4);
    decode_scancode(0xb8);while(keyboard_poll_event(&e)){}
    CHECK(!keyboard_modifiers());
    printf("native keyboard: modifiers, key up/down, extended keys, owner isolation, z vs F6 PASS\n");return 0;
}

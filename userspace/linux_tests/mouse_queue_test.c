#include <assert.h>
#include <stdio.h>
#define VEX_MOUSE_HOST_TEST 1
#include "../../kernel/src/mouse.c"
void gui_input_wake(void) {}
void debug_log_puts(const char *text) {(void)text;}
int main(void) {
    int x, y; uint8_t b;
    /* An IRQ burst far beyond ring capacity must retain the full motion,
       then preserve press, drag, reversal and release boundaries. */
    for(int i=0;i<10000;++i)push_event(1,2,0,0);
    push_event(0,0,1,0);
    for(int i=0;i<10000;++i)push_event(2,1,1,0);
    push_event(-10,-20,1,0);push_event(0,0,0,0);
    assert(pop_event(&x,&y,&b) && x==10000 && y==20000 && !b);
    assert(pop_event(&x,&y,&b) && !x && !y && b==1);
    assert(pop_event(&x,&y,&b) && x==20000 && y==10000 && b==1);
    assert(pop_event(&x,&y,&b) && x==-10 && y==-20 && b==1);
    assert(pop_event(&x,&y,&b) && !x && !y && !b);
    for (int i = 0; i < 100; ++i) push_event(1, 2, 0, 0);
    push_event(0, 0, 1, 0);
    push_event(3, 4, 1, 0);
    push_event(0, 0, 0, 0);
    assert(pop_event(&x,&y,&b) && x == 100 && y == 200 && !b);
    /* Only the next button run was consumed: no new IRQ is required to
     * notice the unread click/motion/release tail before a GUI wait. */
    assert(mouse_events_pending());
    assert(pop_event(&x,&y,&b) && b == 1 && !x);
    assert(pop_event(&x,&y,&b) && b == 1 && x == 3);
    assert(pop_event(&x,&y,&b) && !b);

    /* Drag packets with a stable held-button state are one GUI update, while
     * the release edge remains distinct. This prevents redraw cost from
     * making the pointer replay a stale motion backlog. */
    push_event(0, 0, 1, 0);
    for (int i = 0; i < 80; ++i) push_event(2, 1, 1, 0);
    push_event(0, 0, 0, 0);
    assert(pop_event(&x, &y, &b) && b == 1 && x == 0 && y == 0);
    assert(pop_event(&x, &y, &b) && b == 1 && x == 160 && y == 80);
    assert(pop_event(&x, &y, &b) && b == 0 && x == 0 && y == 0);

    push_event(10, 0, 0, 0); push_event(-10, 0, 0, 0);
    assert(pop_event(&x,&y,&b) && x == 10);
    assert(pop_event(&x,&y,&b) && x == -10);
    push_event(1, 0, 0, 1); push_event(2, 0, 0, 0);
    assert(pop_event(&x,&y,&b) && x == 1 && mouse_take_wheel() == 1);
    assert(pop_event(&x,&y,&b) && x == 2 && mouse_take_wheel() == 0);
    assert(!pop_event(&x,&y,&b));
    assert(!mouse_events_pending());
    puts("Mouse motion backlog/button edges/drag/reversal/wheel: PASS");
}

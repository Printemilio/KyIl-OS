#include <unos/window_damage.h>
extern int printf(const char *,...);
extern int puts(const char *);
extern void abort(void);
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);abort();}}while(0)
int main(void) {
    /* Lower content damages the right edge of window 0. Repainting window 0
     * also touches window 1 outside the original dirty rect. Window 2 is
     * unrelated, and window 3 represents a hidden window with zero extent. */
    vex_rect_t windows[]={{0,0,100,100},{-40,20,50,50},{200,0,50,50},{80,0,0,100}};
    CHECK(window_damage_closure(windows,4,(vex_rect_t){90,20,20,20})==3);
    CHECK(window_damage_closure(windows,4,(vex_rect_t){100,0,50,10})==0);
    CHECK(window_damage_closure(windows,4,(vex_rect_t){210,0,1,1})==4);
    CHECK(window_damage_closure(windows,4,(vex_rect_t){90,20,0,20})==0);
    CHECK(window_damage_intersects((vex_rect_t){INT32_MAX-10,0,20,10},(vex_rect_t){INT32_MAX-2,0,1,1}));
    /* The closure is independent of enumeration order. */
    vex_rect_t reverse[]={{-40,20,50,50},{0,0,100,100}};
    CHECK(window_damage_closure(reverse,2,(vex_rect_t){90,20,20,20})==3);
    puts("native compositor damage: overlap closure, hidden/untouched windows, edges and overflow PASS");
    return 0;
}

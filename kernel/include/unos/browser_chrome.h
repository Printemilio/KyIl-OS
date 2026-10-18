#ifndef UNOS_BROWSER_CHROME_H
#define UNOS_BROWSER_CHROME_H
#include <unos/browser_state.h>
/* Shared by native painting and hit testing: a control has one geometry. */
static inline unsigned browser_chrome_tab_width(unsigned width,unsigned count) {
    unsigned available=width>54?width-54:1;
    unsigned size=available/(count?count:1);
    return size>180?180:(size?size:1);
}
static inline unsigned browser_chrome_search_width(unsigned width) {
    unsigned size=width>320?width*66/100:(width>32?width-32:1);
    if(size>1028)size=1028;
    return size;
}
static inline unsigned browser_chrome_search_y(unsigned height) {
    return (height>400?height*33/100:72)+68;
}
#endif

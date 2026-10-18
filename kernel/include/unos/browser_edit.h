#ifndef UNOS_BROWSER_EDIT_H
#define UNOS_BROWSER_EDIT_H
#include <stdint.h>
#include <stddef.h>
/* Native address editor: length, caret and terminator describe one buffer. */
static inline int browser_edit_insert(char *text,size_t capacity,uint64_t *length,
                                     uint64_t *caret,uint8_t *selected,char key) {
    if(!capacity || *length>=capacity || *caret>*length)return -1;
    if(*selected){*length=*caret=0;*selected=0;text[0]=0;}
    if(*length+1>=capacity)return -1;
    for(uint64_t i=*length+1;i>*caret;i--)text[i]=text[i-1];
    text[(*caret)++]=key;(*length)++;return 0;
}
static inline void browser_edit_backspace(char *text,uint64_t *length,uint64_t *caret,uint8_t *selected) {
    if(*selected){*length=*caret=0;*selected=0;text[0]=0;}
    else if(*caret && *caret<=*length){(*caret)--;for(uint64_t i=*caret;i<*length;i++)text[i]=text[i+1];(*length)--;}
}
#endif

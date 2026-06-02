#include <unos/clipboard.h>

#include <stddef.h>

#define CLIPBOARD_CAPACITY 1024u

static char clipboard_text[CLIPBOARD_CAPACITY];

void clipboard_set(const char *text) {
    size_t i = 0;

    if (text == 0) {
        clipboard_text[0] = '\0';
        return;
    }

    while (text[i] != '\0' && i + 1u < CLIPBOARD_CAPACITY) {
        clipboard_text[i] = text[i];
        ++i;
    }
    clipboard_text[i] = '\0';
}

const char *clipboard_get(void) {
    return clipboard_text;
}

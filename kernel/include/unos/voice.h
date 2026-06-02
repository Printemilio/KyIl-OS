#ifndef UNOS_VOICE_H
#define UNOS_VOICE_H

#include <stddef.h>
#include <stdint.h>

typedef enum voice_command {
    VOICE_COMMAND_NONE = 0,
    VOICE_COMMAND_OPEN_TERMINAL = 1,
    VOICE_COMMAND_OPEN_BROWSER = 2,
} voice_command_t;

void voice_init(void);
int voice_hda_available(void);
int voice_listen_once(char *out_text, size_t capacity);
voice_command_t voice_command_from_text(const char *text);
const char *voice_status(void);

#endif

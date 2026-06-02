#include <unos/shell.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/clipboard.h>
#include <unos/gui.h>
#include <unos/gui_app_ipc.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/irq.h>
#include <unos/keyboard.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/users.h>

#include "shell_internal.h"

#define SHELL_BUFFER_SIZE 256u
#define SHELL_HISTORY_DEPTH 10u
#define SHELL_PRIORITY 1u

static char command_buffer[SHELL_BUFFER_SIZE];
static char **history;
static size_t history_count;
static size_t selection_start;
static size_t selection_end;
static uint32_t shell_process_pid;
static int shell_command_pipe = -1;
static uint8_t shell_prompt_drawn;

static size_t string_length(const char *str) {
    size_t length = 0;

    while (str[length] != '\0') {
        ++length;
    }

    return length;
}

static void copy_string(char *dst, const char *src, size_t capacity) {
    size_t i = 0;

    if (capacity == 0) {
        return;
    }

    while (i + 1u < capacity && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

static void prompt(void) {
    tty_puts(user_current_username());
    tty_puts("@Vex-OS> ");
}

static int shell_try_next_key(char *out_key) {
    gui_app_command_t command;

    while (pipe_read(shell_command_pipe, &command, sizeof(command)) == (int)sizeof(command)) {
        if (command.type == GUI_APP_CMD_CONFIGURE) {
            tty_set_region(command.x, command.y, command.w, command.h);
            tty_set_colors_rgb(230, 236, 248, 12, 14, 22);
            tty_set_stream_output_enabled(command.flags != 0u && gui_terminal_surface_visible());
            if (command.flags == 0u) {
                shell_prompt_drawn = 0u;
            }
            if (command.flags != 0u && !shell_prompt_drawn) {
                prompt();
                shell_prompt_drawn = 1u;
            }
            continue;
        }

        if (command.type == GUI_APP_CMD_KEY) {
            *out_key = command.key;
            return 1;
        }
    }

    return 0;
}

static void shell_discard_pending_input(void) {
    gui_app_command_t command;

    while (pipe_read(shell_command_pipe, &command, sizeof(command)) == (int)sizeof(command)) {
        if (command.type == GUI_APP_CMD_CONFIGURE) {
            tty_set_region(command.x, command.y, command.w, command.h);
            tty_set_colors_rgb(230, 236, 248, 12, 14, 22);
            tty_set_stream_output_enabled(command.flags != 0u && gui_terminal_surface_visible());
            if (command.flags == 0u) {
                shell_prompt_drawn = 0u;
            }
        }
    }
}

static void redraw_current_line(const char *line, size_t *length) {
    while (*length > 0) {
        tty_putchar('\b');
        --(*length);
    }

    copy_string(command_buffer, line, SHELL_BUFFER_SIZE);
    *length = string_length(command_buffer);
    tty_puts(command_buffer);
}

static void clear_selection(void) {
    selection_start = 0;
    selection_end = 0;
}

static int has_selection(void) {
    return selection_end > selection_start;
}

static void copy_selection_or_line(void) {
    char text[SHELL_BUFFER_SIZE];
    size_t out = 0;
    const size_t start = has_selection() ? selection_start : 0;
    const size_t end = has_selection() ? selection_end : string_length(command_buffer);

    for (size_t i = start; i < end && out + 1u < sizeof(text); ++i) {
        text[out++] = command_buffer[i];
    }
    text[out] = '\0';
    clipboard_set(text);
}

static void paste_clipboard(size_t *length) {
    const char *text = clipboard_get();

    while (text != NULL && *text != '\0' && *length + 1u < SHELL_BUFFER_SIZE) {
        command_buffer[*length] = *text;
        ++(*length);
        command_buffer[*length] = '\0';
        tty_putchar(*text);
        ++text;
    }
}

static void selection_extend_left(size_t length) {
    if (selection_end == 0) {
        selection_end = length;
        selection_start = length;
    }
    if (selection_start > 0) {
        --selection_start;
    }
}

static void selection_extend_right(size_t length) {
    if (selection_end == 0 && selection_start == 0) {
        selection_end = selection_start = length;
        return;
    }
    if (selection_end < length) {
        ++selection_end;
    }
}

static void history_store(const char *line) {
    if (line[0] == '\0' || history == NULL) {
        return;
    }

    const size_t slot = history_count < SHELL_HISTORY_DEPTH ? history_count : SHELL_HISTORY_DEPTH - 1u;

    if (history_count >= SHELL_HISTORY_DEPTH) {
        for (size_t i = 1; i < SHELL_HISTORY_DEPTH; ++i) {
            copy_string(history[i - 1u], history[i], SHELL_BUFFER_SIZE);
        }
    }

    copy_string(history[slot], line, SHELL_BUFFER_SIZE);

    if (history_count < SHELL_HISTORY_DEPTH) {
        ++history_count;
    }
}

static const char *history_last(void) {
    if (history == NULL || history_count == 0) {
        return NULL;
    }

    return history[history_count - 1u];
}

void shell_execute_line(const char *line) {
    if (line == NULL || line[0] == '\0') {
        return;
    }

    tty_puts(line);
    tty_putchar('\n');
    shell_execute_command(line);
    prompt();
    shell_prompt_drawn = 1u;
}

void shell_submit_line(const char *line) {
    gui_app_command_t command;

    if (line == NULL || shell_command_pipe < 0) {
        return;
    }

    command.type = GUI_APP_CMD_KEY;
    command.flags = 0u;
    command.x = 0u;
    command.y = 0u;
    command.w = 0u;
    command.h = 0u;
    command.path[0] = '\0';

    for (size_t i = 0; line[i] != '\0'; ++i) {
        command.key = line[i];
        (void)pipe_write(shell_command_pipe, &command, sizeof(command));
    }

    command.key = '\n';
    (void)pipe_write(shell_command_pipe, &command, sizeof(command));
}

void shell_init(void) {
    history = kmalloc(sizeof(char *) * SHELL_HISTORY_DEPTH);
    if (history == NULL) {
        return;
    }

    for (size_t i = 0; i < SHELL_HISTORY_DEPTH; ++i) {
        history[i] = kmalloc(SHELL_BUFFER_SIZE);
        if (history[i] == NULL) {
            return;
        }
        history[i][0] = '\0';
    }

    history_count = 0;
    shell_prompt_drawn = 0;
    shell_command_pipe = pipe_create();
    if (shell_command_pipe < 0) {
        return;
    }
    shell_process_pid = process_create(shell_run, SHELL_PRIORITY);
}

uint32_t shell_pid(void) {
    return shell_process_pid;
}

void shell_app_send(const gui_app_command_t *command) {
    if (command == NULL || shell_command_pipe < 0) {
        return;
    }

    (void)pipe_write(shell_command_pipe, command, sizeof(*command));
}

void shell_run(void) {
    for (;;) {
        size_t length = 0;
        command_buffer[0] = '\0';
        clear_selection();

        for (;;) {
            char c = 0;

            while (!shell_try_next_key(&c)) {
                process_yield();
            }

            if (c == KEYBOARD_KEY_UP) {
                const char *last = history_last();
                if (last != NULL) {
                    redraw_current_line(last, &length);
                    clear_selection();
                }
                continue;
            }

            if (c == KEYBOARD_KEY_SHIFT_LEFT) {
                selection_extend_left(length);
                continue;
            }

            if (c == KEYBOARD_KEY_SHIFT_RIGHT) {
                selection_extend_right(length);
                continue;
            }

            if (c == KEYBOARD_KEY_CTRL_C) {
                copy_selection_or_line();
                continue;
            }

            if (c == KEYBOARD_KEY_CTRL_V) {
                paste_clipboard(&length);
                clear_selection();
                continue;
            }

            if (c == '\b') {
                if (length > 0) {
                    --length;
                    command_buffer[length] = '\0';
                    tty_putchar('\b');
                    clear_selection();
                }
                continue;
            }

            if (c == '\n') {
                tty_putchar('\n');
                break;
            }

            if (c >= ' ') {
                if (length + 1u >= SHELL_BUFFER_SIZE) {
                    tty_putchar('\n');
                    tty_puts("shell: command buffer full\n");
                    break;
                }

                command_buffer[length] = c;
                ++length;
                command_buffer[length] = '\0';
                tty_putchar(c);
                clear_selection();
            }
        }

        history_store(command_buffer);
        shell_execute_command(command_buffer);
        command_buffer[0] = '\0';
        shell_discard_pending_input();
        clear_selection();
        prompt();
        shell_prompt_drawn = 1u;
    }
}

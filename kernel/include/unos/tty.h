#ifndef UNOS_TTY_H
#define UNOS_TTY_H

void tty_init(void);
void tty_putchar(char c);
void tty_puts(const char *str);
void tty_clear(void);
void tty_set_region(unsigned long x, unsigned long y, unsigned long width, unsigned long height);
void tty_set_stream_output_enabled(int enabled);
void tty_set_colors_rgb(unsigned char fg_r, unsigned char fg_g, unsigned char fg_b, unsigned char bg_r, unsigned char bg_g, unsigned char bg_b);
void tty_draw_text_at(unsigned long x, unsigned long y, const char *str, unsigned int foreground, unsigned int background);
void tty_draw_text_transparent_at(unsigned long x, unsigned long y, const char *str, unsigned int foreground);
void tty_draw_text_clipped_at(unsigned long x, unsigned long y, unsigned long max_width, const char *str, unsigned int foreground, unsigned int background);
void tty_draw_text_transparent_clipped_at(unsigned long x, unsigned long y, unsigned long max_width, const char *str, unsigned int foreground);

#endif

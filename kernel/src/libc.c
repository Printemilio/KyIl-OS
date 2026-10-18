#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unos/heap.h>
#include <unos/memory_copy.h>

void *memcpy(void *dest, const void *src, size_t n) {
    return kyil_memory_copy(dest,src,n);
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;

    if (d < s) {
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) {
            d[i - 1u] = s[i - 1u];
        }
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    /* No SIMD/FPU state belongs to kernel memory primitives. REP stores also
     * avoid the scalar byte loop on large page/backing initialisations. x86
     * permits unaligned stores; the tail never touches bytes outside [s,s+n). */
    uint8_t *bytes = s;
    const uint64_t value = UINT64_C(0x0101010101010101) * (uint8_t)c;
    size_t words = n / sizeof(value);
    size_t tail = n % sizeof(value);
    __asm__ volatile("cld; rep stosq" : "+D"(bytes), "+c"(words) : "a"(value) : "memory", "cc");
    __asm__ volatile("rep stosb" : "+D"(bytes), "+c"(tail) : "a"((uint8_t)c) : "memory");
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = s1;
    const uint8_t *b = s2;

    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }

    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;

    while (s[len] != '\0') {
        ++len;
    }

    return len;
}

char *strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        ++s;
    }

    return c == 0 ? (char *)s : NULL;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        ++s1;
        ++s2;
    }

    return (int)(uint8_t)*s1 - (int)(uint8_t)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (s1[i] == '\0' || s1[i] != s2[i]) {
            return (int)(uint8_t)s1[i] - (int)(uint8_t)s2[i];
        }
    }

    return 0;
}

void *calloc(size_t count, size_t size) {
    if (size != 0 && count > ((size_t)-1) / size) {
        return NULL;
    }

    void *ptr = kmalloc(count * size);
    if (ptr != NULL) {
        memset(ptr, 0, count * size);
    }

    return ptr;
}

void free(void *ptr) {
    kfree(ptr);
}

void abort(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void append_char(char *str, size_t size, size_t *written, char c) {
    if (str != NULL && size != 0 && *written + 1u < size) {
        str[*written] = c;
    }

    ++(*written);
}

static void append_string(char *str, size_t size, size_t *written, const char *src) {
    if (src == NULL) {
        src = "(null)";
    }

    while (*src != '\0') {
        append_char(str, size, written, *src++);
    }
}

static void append_unsigned(char *str, size_t size, size_t *written, unsigned int value, unsigned int base, int uppercase, int width, char pad) {
    char digits[16];
    size_t count = 0;
    const char *alphabet = uppercase != 0 ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0) {
        digits[count++] = '0';
    }

    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = alphabet[value % base];
        value /= base;
    }

    while ((int)count < width) {
        append_char(str, size, written, pad);
        --width;
    }

    while (count > 0) {
        append_char(str, size, written, digits[--count]);
    }
}

int vsnprintf(char *str, size_t size, const char *format, __builtin_va_list ap) {
    size_t written = 0;

    if (format == NULL) {
        if (str != NULL && size != 0) {
            str[0] = '\0';
        }
        return 0;
    }

    for (size_t i = 0; format[i] != '\0'; ++i) {
        if (format[i] != '%') {
            append_char(str, size, &written, format[i]);
            continue;
        }

        ++i;
        if (format[i] == '\0') {
            break;
        }

        char pad = ' ';
        int width = 0;
        if (format[i] == '0') {
            pad = '0';
            ++i;
        }

        while (format[i] >= '0' && format[i] <= '9') {
            width = (width * 10) + (format[i] - '0');
            ++i;
        }

        switch (format[i]) {
            case '%':
                append_char(str, size, &written, '%');
                break;
            case 'c':
                append_char(str, size, &written, (char)__builtin_va_arg(ap, int));
                break;
            case 's':
                append_string(str, size, &written, __builtin_va_arg(ap, const char *));
                break;
            case 'd':
            case 'i': {
                int value = __builtin_va_arg(ap, int);
                if (value < 0) {
                    append_char(str, size, &written, '-');
                    value = -value;
                }
                append_unsigned(str, size, &written, (unsigned int)value, 10u, 0, width, pad);
                break;
            }
            case 'u':
                append_unsigned(str, size, &written, __builtin_va_arg(ap, unsigned int), 10u, 0, width, pad);
                break;
            case 'x':
                append_unsigned(str, size, &written, __builtin_va_arg(ap, unsigned int), 16u, 0, width, pad);
                break;
            case 'X':
                append_unsigned(str, size, &written, __builtin_va_arg(ap, unsigned int), 16u, 1, width, pad);
                break;
            default:
                append_char(str, size, &written, '%');
                append_char(str, size, &written, format[i]);
                break;
        }
    }

    if (str != NULL && size != 0) {
        str[written < size ? written : size - 1u] = '\0';
    }

    return (int)written;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, format);
    const int ret = vsnprintf(str, size, format, ap);
    __builtin_va_end(ap);
    return ret;
}

time_t time(time_t *timer) {
    if (timer != NULL) {
        *timer = 0;
    }

    return 0;
}

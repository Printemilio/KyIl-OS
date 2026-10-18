#include <stdint.h>

#include <unos/vex_legacy_compat.h>

extern int main(int argc, char** argv);

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

/* Every ELF produced for the native VexOS ABI carries an intrinsic note.  The
 * loader uses this byte-level tag (rather than the pathname) to distinguish a
 * Vex executable from a foreign Linux ELF that happens to be stored beside
 * it. */
__attribute__((section(".note.vexos"), used, aligned(4)))
static const struct {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    char name[8];
    uint32_t desc;
} vexos_abi_note = {
    6u, 4u, 1u, {'V','E','X','O','S','\0','\0','\0'}, 0x5645584eu
};

static void run_init_array(void) {
    for (void (**ctor)(void) = __init_array_start; ctor < __init_array_end; ++ctor) {
        if (*ctor != 0) {
            (*ctor)();
        }
    }
}

/* The kernel enters with the System V initial stack: %rsp points at argc,
 * followed by argv[], NULL, envp[], NULL and the auxiliary vector. */
void vex_crt0_start(uint64_t *initial) {
    int argc = (int)initial[0];
    char **argv = (char **)(initial + 1);

    run_init_array();
    exit(main(argc, argv));
    for (;;) {
    }
}

__asm__(
    ".text\n"
    ".global _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "    xor %rbp, %rbp\n"
    "    mov %rsp, %rdi\n"
    "    andq $-16, %rsp\n"
    "    call vex_crt0_start\n"
    "    ud2\n");

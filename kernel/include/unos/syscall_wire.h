#ifndef VEX_SYSCALL_WIRE_H
#define VEX_SYSCALL_WIRE_H

/* SYSCALL preserves the same argument registers for both domains. The upper
 * half of RAX selects the structured native domain BEFORE inspecting arguments;
 * the lower half is its independent call number. Untagged calls retain their
 * historical process-personality ABI (legacy Vex, Linux, or PE envelope).
 * This is a wire selector, not an ELF personality or a capability. */
#define VEX_SYSCALL_NATIVE_TAG  0x5645584e00000000ull
#define VEX_SYSCALL_DOMAIN_MASK 0xffffffff00000000ull
#define VEX_SYSCALL_NUMBER_MASK 0x00000000ffffffffull
/* Frozen compatibility interval; new native calls must not extend it. */
#define VEX_SYSCALL_LEGACY_NATIVE_FIRST 4ull
#define VEX_SYSCALL_LEGACY_NATIVE_LAST  196ull

#endif

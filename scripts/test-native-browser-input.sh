#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
gcc -O2 -Wall -Wextra -Werror -idirafter kernel/include \
    userspace/linux_tests/cursor_overlay_test.c -o build/cursor_overlay_test
build/cursor_overlay_test
for name in mouse_queue iso_read_budget scene_image tcp_transport udp_transport browser_edit browser_navigation fd_inventory; do
    gcc -O2 -Werror -DVEX_HOSTED_BUILD -ffunction-sections -fdata-sections -idirafter kernel/include -I vendor/limine-protocol/include \
        userspace/linux_tests/${name}_test.c -Wl,--gc-sections -o build/${name}_test
    build/${name}_test
done
gcc -O2 -Werror -DVEX_HOSTED_BUILD -ffunction-sections -fdata-sections -DVEX_NETWORK_HOST_TEST=1 \
    -idirafter kernel/include userspace/linux_tests/dns_wait_test.c \
    kernel/src/arp.c kernel/src/dns.c -Wl,--gc-sections -o build/dns_wait_test
build/dns_wait_test
gcc -O2 -Werror -DVEX_HOSTED_BUILD -ffunction-sections -fdata-sections -idirafter kernel/include \
    userspace/linux_tests/vex_input_decode_test.c kernel/src/mouse.c \
    -Wl,--gc-sections -o build/vex_input_decode_test
build/vex_input_decode_test
gcc -O2 -Werror -ffunction-sections -fdata-sections -I kernel/include \
    userspace/linux_tests/browser_viewport_test.c kernel/src/vex_browser_display.c \
    -Wl,--gc-sections -o build/browser_viewport_test
build/browser_viewport_test
gcc -O2 -Werror -DVEX_HOSTED_BUILD -ffunction-sections -fdata-sections -Iuserspace/libvex/include \
    userspace/linux_tests/wpe_input_translation_test.c -Wl,--gc-sections \
    -o build/wpe_input_translation_test
build/wpe_input_translation_test
gcc -O2 -Werror -ffunction-sections -fdata-sections -I kernel/include \
    userspace/linux_tests/vex_keyboard_decode_test.c \
    -Wl,--gc-sections -o build/vex_keyboard_decode_test
build/vex_keyboard_decode_test
gcc -O2 -Werror -DVEX_HOSTED_BUILD -idirafter kernel/include userspace/linux_tests/window_damage_test.c -o build/window_damage_test
build/window_damage_test
gcc -O2 -Werror -ffunction-sections -fdata-sections -I kernel/include -I vendor/lvgl \
    userspace/linux_tests/app_focus_test.c -Wl,--gc-sections -o build/app_focus_test
build/app_focus_test
gcc -O2 -Werror -DVEX_HOSTED_BUILD -std=c11 -DVEX_HOSTED_BUILD -ffunction-sections -fdata-sections \
    -Iuserspace/wpe_stub -Iuserspace/libvex/include -idirafter kernel/include \
    $(pkg-config --cflags glib-2.0 gio-unix-2.0) \
    userspace/linux_tests/wpe_renderer_fd_test.c -Wl,--gc-sections -o build/wpe_renderer_fd_test
build/wpe_renderer_fd_test

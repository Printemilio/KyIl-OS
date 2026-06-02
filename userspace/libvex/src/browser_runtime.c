#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>

#include "browser_runtime.h"
#include "vex_syscall.h"

int browser_display_open(uint32_t width, uint32_t height)
{
    return vex_web_display_open(width, height);
}

void browser_display_resize(uint32_t width, uint32_t height)
{
    vex_web_display_resize(width, height);
}

browser_display_surface_t* browser_display_surface(void)
{
    return vex_web_display_surface();
}

void browser_display_present(void)
{
    vex_web_display_present();
}

void browser_display_close(void)
{
    vex_web_display_close();
}

int browser_command_read(char* buffer, size_t size)
{
    return vex_web_browser_command_pop(buffer, size);
}

void browser_results_begin(const char* query)
{
    vex_web_browser_results_begin(query);
}

void browser_results_add(const char* title, const char* url, const char* snippet)
{
    vex_web_browser_results_add(title, url, snippet);
}

void browser_results_show(void)
{
    vex_web_browser_results_show();
}

void browser_results_hide(void)
{
    vex_web_browser_results_hide();
}

void browser_debug_log(const char* text)
{
    vex_web_debug_log(text);
}

void browser_yield(void)
{
    vex_web_browser_yield_to_process();
}

uint32_t browser_process_pid(void)
{
    return vex_web_browser_process_pid();
}

int browser_process_spawn(const char* path, uint32_t* pid_out)
{
    vex_process_spawn_req_t request = { path, 0u, 0u };
    uint64_t result;

    if (!path)
        return -1;

    result = vex_syscall1(VEX_SYS_PROCESS_SPAWN, (uint64_t)&request);
    if (pid_out)
        *pid_out = request.pid;
    return (int)result;
}

int browser_process_wait(int32_t pid, int* status, uint32_t flags)
{
    vex_process_wait_req_t request = { pid, status, flags };
    return (int)vex_syscall1(VEX_SYS_PROCESS_WAIT, (uint64_t)&request);
}


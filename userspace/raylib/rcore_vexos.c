/**********************************************************************************************
*
*   rcore_vexos - raylib platform backend for Vex OS
*
*   PLATFORM: VEXOS
*       - One native Vex app window (VEX_SYS_APP_WINDOW_*), opened with
*         VEX_APP_WINDOW_FLAG_RAW_INPUT: key press/release edges, pointer
*         position, three buttons and wheel, focus changes
*       - Software renderer (rlsw, GRAPHICS_API_OPENGL_SOFTWARE): every frame
*         is read back as BGRA and presented to the compositor
*
*   LIMITATIONS:
*       - No GPU path: shaders are accepted and ignored by rlsw
*       - Clipboard is private to the process (Vex has no clipboard syscall yet)
*       - No cursor shapes, no window icon, no monitor queries
*
*   Included by rcore.c when PLATFORM_VEXOS is defined (see
*   third_party/patches/raylib-ocaml). Written for Vex; zlib/libpng like raylib.
*
**********************************************************************************************/

#include <time.h>
#include <unistd.h>

// Built against the Vex C library: the native ABI headers must not declare
// their legacy freestanding libc on top of it.
#ifndef VEX_HOSTED_BUILD
    #define VEX_HOSTED_BUILD 1
#endif
#include "vex_app.h"
#include "vex_input_observer.h"
#include "rlsw_canvas_cache.inc"

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
#define VEX_DEFERRED_MAX 64

typedef struct {
    vex_app_window_t window;
    unsigned int *pixels;           // BGRA presentation buffer, render.width x render.height
    int pixelsWidth;
    int pixelsHeight;
    bool focused;
    // Releases that arrived in the same poll as their press: applied on the
    // next poll so that IsKeyPressed()/IsMouseButtonPressed() see the press.
    int deferredKeys[VEX_DEFERRED_MAX];
    int deferredKeyCount;
    int deferredButtons;            // bit mask of MouseButton
    char pressedThisPoll[MAX_KEYBOARD_KEYS];
    int buttonsPressedThisPoll;
    int buttons;                    // last button mask reported by the kernel
    char *clipboard;
} PlatformData;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
extern CoreData CORE;                   // Global CORE state context

static PlatformData platform = { 0 };   // Platform specific data
static uint16_t event_deadline_ticks;
void vex_raylib_set_event_deadline(uint16_t ticks){if(!ticks || !event_deadline_ticks || ticks<event_deadline_ticks)event_deadline_ticks=ticks;}
uint32_t vex_raylib_window_id(void){return platform.window.id;}

//----------------------------------------------------------------------------------
// Module Internal Functions Declaration
//----------------------------------------------------------------------------------
int InitPlatform(void);
void ClosePlatform(void);
static int vex_close_confirmation;
void vex_raylib_set_close_confirmation(int enabled){vex_close_confirmation=enabled!=0;}
void vex_raylib_cancel_close(void){CORE.Window.shouldClose=false;}

static void VexResizeBuffers(int width, int height);
static void VexHandleEvent(const vex_app_window_event_t *event);

//----------------------------------------------------------------------------------
// Module Functions Definition: Window and Graphics Device
//----------------------------------------------------------------------------------

bool WindowShouldClose(void)
{
    if (CORE.Window.ready) return CORE.Window.shouldClose;
    else return true;
}

void ToggleFullscreen(void)
{
    TRACELOG(LOG_WARNING, "ToggleFullscreen() not available on target platform");
}

void ToggleBorderlessWindowed(void)
{
    TRACELOG(LOG_WARNING, "ToggleBorderlessWindowed() not available on target platform");
}

// The compositor owns maximize/minimize (window chrome buttons); a program
// asking for it is not an error, it simply keeps the current state.
void MaximizeWindow(void) { }
void MinimizeWindow(void) { }
void RestoreWindow(void) { }

void SetWindowState(unsigned int flags)
{
    FLAG_SET(CORE.Window.flags, flags & (FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_ALWAYS_RUN));
}

void ClearWindowState(unsigned int flags)
{
    FLAG_CLEAR(CORE.Window.flags, flags & (FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_ALWAYS_RUN));
}

void SetWindowIcon(Image image) { }
void SetWindowIcons(Image *images, int count) { }

void SetWindowTitle(const char *title)
{
    CORE.Window.title = title;
}

void SetWindowPosition(int x, int y) { }
void SetWindowMonitor(int monitor) { }

void SetWindowMinSize(int width, int height)
{
    CORE.Window.screenMin.width = width;
    CORE.Window.screenMin.height = height;
}

void SetWindowMaxSize(int width, int height)
{
    CORE.Window.screenMax.width = width;
    CORE.Window.screenMax.height = height;
}

// The window size follows the compositor (CONFIGURE events); a program
// request is not forwarded yet.
void SetWindowSize(int width, int height)
{
    TRACELOG(LOG_WARNING, "SetWindowSize() not available on target platform");
}

void SetWindowOpacity(float opacity) { }
void SetWindowFocused(void) { }

void *GetWindowHandle(void)
{
    return &platform.window;
}

int GetMonitorCount(void) { return 1; }
int GetCurrentMonitor(void) { return 0; }
Vector2 GetMonitorPosition(int monitor) { return (Vector2){ 0, 0 }; }
int GetMonitorWidth(int monitor) { return CORE.Window.display.width; }
int GetMonitorHeight(int monitor) { return CORE.Window.display.height; }
int GetMonitorPhysicalWidth(int monitor) { return 0; }
int GetMonitorPhysicalHeight(int monitor) { return 0; }
int GetMonitorRefreshRate(int monitor) { return 60; }
const char *GetMonitorName(int monitor) { return "Vex"; }
Vector2 GetWindowPosition(void) { return (Vector2){ 0, 0 }; }
Vector2 GetWindowScaleDPI(void) { return (Vector2){ 1.0f, 1.0f }; }

void SetClipboardText(const char *text)
{
    RL_FREE(platform.clipboard);
    platform.clipboard = NULL;
    if (text != NULL)
    {
        size_t length = strlen(text);
        platform.clipboard = (char *)RL_MALLOC(length + 1);
        if (platform.clipboard != NULL) memcpy(platform.clipboard, text, length + 1);
    }
}

const char *GetClipboardText(void)
{
    return platform.clipboard;
}

Image GetClipboardImage(void)
{
    Image image = { 0 };
    return image;
}

void ShowCursor(void) { CORE.Input.Mouse.cursorHidden = false; }
void HideCursor(void) { CORE.Input.Mouse.cursorHidden = true; }

void EnableCursor(void)
{
    CORE.Input.Mouse.cursorHidden = false;
    CORE.Input.Mouse.cursorLocked = false;
}

void DisableCursor(void)
{
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);
    CORE.Input.Mouse.cursorHidden = true;
    CORE.Input.Mouse.cursorLocked = true;
}

void SwapScreenBuffer(void)
{
    if ((platform.pixels == NULL) || (platform.window.id == 0)) return;

    rlCopyFramebuffer(0, 0, platform.pixelsWidth, platform.pixelsHeight, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, platform.pixels);
    // The compositor blends: make every pixel opaque.
    const size_t count = (size_t)platform.pixelsWidth*(size_t)platform.pixelsHeight;
    for (size_t i = 0; i < count; i++) platform.pixels[i] |= 0xff000000u;
    (void)vex_app_window_present(&platform.window, platform.pixels, (uint32_t)platform.pixelsWidth, NULL);
    extern void vex_bench_end(void) __attribute__((weak));
    if(vex_bench_end)vex_bench_end();
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Misc
//----------------------------------------------------------------------------------

double GetTime(void)
{
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long int nanoSeconds = (unsigned long long int)ts.tv_sec*1000000000LLU + (unsigned long long int)ts.tv_nsec;
    return (double)(nanoSeconds - CORE.Time.base)*1e-9;
}

void OpenURL(const char *url)
{
    TRACELOG(LOG_WARNING, "OpenURL() not available on target platform");
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Inputs
//----------------------------------------------------------------------------------

int SetGamepadMappings(const char *mappings) { return 0; }
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration) { }

void SetMousePosition(int x, int y)
{
    CORE.Input.Mouse.currentPosition = (Vector2){ (float)x, (float)y };
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
}

void SetMouseCursor(int cursor)
{
    CORE.Input.Mouse.cursor = cursor;
}

const char *GetKeyName(int key)
{
    return "";
}

// PS/2 set-1 scancode (+128 for E0 keys) to raylib key, US positions
static int VexKeyFromScancode(unsigned int hardware)
{
    static const int base[0x59] = {
        [0x01] = KEY_ESCAPE, [0x02] = KEY_ONE, [0x03] = KEY_TWO, [0x04] = KEY_THREE,
        [0x05] = KEY_FOUR, [0x06] = KEY_FIVE, [0x07] = KEY_SIX, [0x08] = KEY_SEVEN,
        [0x09] = KEY_EIGHT, [0x0a] = KEY_NINE, [0x0b] = KEY_ZERO, [0x0c] = KEY_MINUS,
        [0x0d] = KEY_EQUAL, [0x0e] = KEY_BACKSPACE, [0x0f] = KEY_TAB,
        [0x10] = KEY_Q, [0x11] = KEY_W, [0x12] = KEY_E, [0x13] = KEY_R, [0x14] = KEY_T,
        [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I, [0x18] = KEY_O, [0x19] = KEY_P,
        [0x1a] = KEY_LEFT_BRACKET, [0x1b] = KEY_RIGHT_BRACKET, [0x1c] = KEY_ENTER,
        [0x1d] = KEY_LEFT_CONTROL,
        [0x1e] = KEY_A, [0x1f] = KEY_S, [0x20] = KEY_D, [0x21] = KEY_F, [0x22] = KEY_G,
        [0x23] = KEY_H, [0x24] = KEY_J, [0x25] = KEY_K, [0x26] = KEY_L,
        [0x27] = KEY_SEMICOLON, [0x28] = KEY_APOSTROPHE, [0x29] = KEY_GRAVE,
        [0x2a] = KEY_LEFT_SHIFT, [0x2b] = KEY_BACKSLASH,
        [0x2c] = KEY_Z, [0x2d] = KEY_X, [0x2e] = KEY_C, [0x2f] = KEY_V, [0x30] = KEY_B,
        [0x31] = KEY_N, [0x32] = KEY_M, [0x33] = KEY_COMMA, [0x34] = KEY_PERIOD,
        [0x35] = KEY_SLASH, [0x36] = KEY_RIGHT_SHIFT, [0x37] = KEY_KP_MULTIPLY,
        [0x38] = KEY_LEFT_ALT, [0x39] = KEY_SPACE, [0x3a] = KEY_CAPS_LOCK,
        [0x3b] = KEY_F1, [0x3c] = KEY_F2, [0x3d] = KEY_F3, [0x3e] = KEY_F4, [0x3f] = KEY_F5,
        [0x40] = KEY_F6, [0x41] = KEY_F7, [0x42] = KEY_F8, [0x43] = KEY_F9, [0x44] = KEY_F10,
        [0x45] = KEY_NUM_LOCK, [0x46] = KEY_SCROLL_LOCK,
        [0x47] = KEY_KP_7, [0x48] = KEY_KP_8, [0x49] = KEY_KP_9, [0x4a] = KEY_KP_SUBTRACT,
        [0x4b] = KEY_KP_4, [0x4c] = KEY_KP_5, [0x4d] = KEY_KP_6, [0x4e] = KEY_KP_ADD,
        [0x4f] = KEY_KP_1, [0x50] = KEY_KP_2, [0x51] = KEY_KP_3, [0x52] = KEY_KP_0,
        [0x53] = KEY_KP_DECIMAL, [0x57] = KEY_F11, [0x58] = KEY_F12,
    };

    if (hardware < 128) return (hardware < sizeof(base)/sizeof(base[0]))? base[hardware] : KEY_NULL;

    switch (hardware - 128)
    {
        case 0x1c: return KEY_KP_ENTER;
        case 0x1d: return KEY_RIGHT_CONTROL;
        case 0x35: return KEY_KP_DIVIDE;
        case 0x37: return KEY_PRINT_SCREEN;
        case 0x38: return KEY_RIGHT_ALT;
        case 0x47: return KEY_HOME;
        case 0x48: return KEY_UP;
        case 0x49: return KEY_PAGE_UP;
        case 0x4b: return KEY_LEFT;
        case 0x4d: return KEY_RIGHT;
        case 0x4f: return KEY_END;
        case 0x50: return KEY_DOWN;
        case 0x51: return KEY_PAGE_DOWN;
        case 0x52: return KEY_INSERT;
        case 0x53: return KEY_DELETE;
        case 0x5b: return KEY_LEFT_SUPER;
        case 0x5c: return KEY_RIGHT_SUPER;
        case 0x5d: return KEY_KB_MENU;
        default: return KEY_NULL;
    }
}

// Letters follow the layout (Ctrl+A is the key that types 'a', as on the
// keyboard the person sees); every other key keeps its physical position.
static int VexKeyFromEvent(unsigned int key, unsigned int hardware)
{
    if ((key >= 'a') && (key <= 'z')) return KEY_A + (int)(key - 'a');
    if ((key >= 'A') && (key <= 'Z')) return KEY_A + (int)(key - 'A');
    return VexKeyFromScancode(hardware);
}

static void VexKeyDown(int key, bool repeat)
{
    if ((key <= 0) || (key >= MAX_KEYBOARD_KEYS)) return;

    if (repeat || CORE.Input.Keyboard.currentKeyState[key])
    {
        CORE.Input.Keyboard.keyRepeatInFrame[key] = 1;
    }
    else
    {
        CORE.Input.Keyboard.currentKeyState[key] = 1;
        platform.pressedThisPoll[key] = 1;
        if (CORE.Input.Keyboard.keyPressedQueueCount < MAX_KEY_PRESSED_QUEUE)
        {
            CORE.Input.Keyboard.keyPressedQueue[CORE.Input.Keyboard.keyPressedQueueCount++] = key;
        }
    }

    if ((CORE.Input.Keyboard.exitKey != KEY_NULL) && (key == CORE.Input.Keyboard.exitKey)) CORE.Window.shouldClose = true;
}

static void VexKeyUp(int key)
{
    if ((key <= 0) || (key >= MAX_KEYBOARD_KEYS)) return;

    if (platform.pressedThisPoll[key])
    {
        if (platform.deferredKeyCount < VEX_DEFERRED_MAX) platform.deferredKeys[platform.deferredKeyCount++] = key;
        else CORE.Input.Keyboard.currentKeyState[key] = 0;
    }
    else CORE.Input.Keyboard.currentKeyState[key] = 0;
}

static void VexReleaseAll(void)
{
    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++) CORE.Input.Keyboard.currentKeyState[i] = 0;
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) CORE.Input.Mouse.currentButtonState[i] = 0;
    platform.buttons = 0;
    platform.deferredKeyCount = 0;
    platform.deferredButtons = 0;
}

static void VexSetButtons(int buttons)
{
    static const int map[3] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE };

    for (int i = 0; i < 3; i++)
    {
        const int bit = 1 << i;
        const int button = map[i];

        if ((buttons & bit) && !(platform.buttons & bit))
        {
            CORE.Input.Mouse.currentButtonState[button] = 1;
            platform.buttonsPressedThisPoll |= 1 << button;
        }
        else if (!(buttons & bit) && (platform.buttons & bit))
        {
            if (platform.buttonsPressedThisPoll & (1 << button)) platform.deferredButtons |= 1 << button;
            else CORE.Input.Mouse.currentButtonState[button] = 0;
        }
    }
    platform.buttons = buttons;
}

static vex_raylib_input_observer_t inputObserver;
static void *inputObserverData;
void vex_raylib_set_input_observer(vex_raylib_input_observer_t observer,void *data)
{ inputObserver=observer; inputObserverData=data; }
static void VexHandleEvent(const vex_app_window_event_t *event)
{
    if(inputObserver)inputObserver(event,inputObserverData);
    switch (event->type)
    {
        case VEX_APP_EVENT_CLOSE: CORE.Window.shouldClose = true; break;
        case VEX_APP_EVENT_CONFIGURE:
        {
            if ((event->w == 0) || (event->h == 0)) break;
            if (vex_app_window_accept_configure(&platform.window, event) != 0) break;
            VexResizeBuffers((int)event->w, (int)event->h);
        } break;
        case VEX_APP_EVENT_FOCUS:
        {
            platform.focused = (event->flags != 0);
            if (platform.focused) FLAG_CLEAR(CORE.Window.flags, FLAG_WINDOW_UNFOCUSED);
            else
            {
                FLAG_SET(CORE.Window.flags, FLAG_WINDOW_UNFOCUSED);
                VexReleaseAll();
            }
        } break;
        case VEX_APP_EVENT_KEY_RAW:
        {
            const unsigned int key = (unsigned int)event->x;
            const unsigned int hardware = (unsigned int)event->y;
            const bool pressed = (event->flags & VEX_APP_KEY_RAW_PRESSED) != 0;
            const unsigned int modifiers = (event->flags >> 8) & 0xffu;
            const int rlKey = VexKeyFromEvent(key, hardware);

            if (pressed)
            {
                VexKeyDown(rlKey, false);
                // Text input: printable characters, not Ctrl/Alt chords
                if ((key >= 32) && (key < 0x110000u) && (key != 127) && !(modifiers & 5u) &&
                    (CORE.Input.Keyboard.charPressedQueueCount < MAX_CHAR_PRESSED_QUEUE))
                {
                    CORE.Input.Keyboard.charPressedQueue[CORE.Input.Keyboard.charPressedQueueCount++] = (int)key;
                }
            }
            else VexKeyUp(rlKey);
        } break;
        case VEX_APP_EVENT_POINTER:
        {
            const float x = (float)(int64_t)event->x;
            const float y = (float)(int64_t)event->y;
            const int wheel = (int)(int64_t)event->w;

            if (!CORE.Input.Mouse.cursorLocked)
            {
                CORE.Input.Mouse.currentPosition = (Vector2){ x, y };
            }
            else
            {
                // Locked (DisableCursor): accumulate motion as a relative position
                CORE.Input.Mouse.currentPosition.x += x - CORE.Input.Mouse.lockedPosition.x;
                CORE.Input.Mouse.currentPosition.y += y - CORE.Input.Mouse.lockedPosition.y;
            }
            CORE.Input.Mouse.lockedPosition = (Vector2){ x, y };
            CORE.Input.Mouse.cursorOnScreen = (x >= 0) && (y >= 0) &&
                (x < CORE.Window.screen.width) && (y < CORE.Window.screen.height);
            // PS/2 wheel counts are positive towards the user (scroll down)
            if (wheel != 0) CORE.Input.Mouse.currentWheelMove.y -= (float)wheel;
            VexSetButtons((int)(event->flags & 7u));
        } break;
        default: break;   // legacy MOUSE_CLICK/KEY are not sent to RAW_INPUT windows
    }
}

// Handle every queued event. A window whose close was requested answers
// CLOSE to every poll from then on, so CLOSE ends the drain.
static int VexDrainEvents(void)
{
    vex_app_window_event_t event;
    int handled = 0;

    while (vex_app_window_poll(&platform.window, &event) == 0)
    {
        if (event.type == VEX_APP_EVENT_NONE) break;
        VexHandleEvent(&event);
        handled++;
        if (event.type == VEX_APP_EVENT_CLOSE) break;
    }
    return handled;
}

void PollInputEvents(void)
{
#if SUPPORT_GESTURES_SYSTEM
    UpdateGestures();
#endif

    CORE.Input.Keyboard.keyPressedQueueCount = 0;
    CORE.Input.Keyboard.charPressedQueueCount = 0;
    CORE.Input.Gamepad.lastButtonPressed = 0;

    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++)
    {
        CORE.Input.Keyboard.previousKeyState[i] = CORE.Input.Keyboard.currentKeyState[i];
        CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;
    }
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) CORE.Input.Mouse.previousButtonState[i] = CORE.Input.Mouse.currentButtonState[i];

    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
    CORE.Input.Mouse.previousWheelMove = CORE.Input.Mouse.currentWheelMove;
    CORE.Input.Mouse.currentWheelMove = (Vector2){ 0.0f, 0.0f };
    CORE.Window.resizedLastFrame = false;

    // Releases deferred from the previous poll
    for (int i = 0; i < platform.deferredKeyCount; i++) CORE.Input.Keyboard.currentKeyState[platform.deferredKeys[i]] = 0;
    platform.deferredKeyCount = 0;
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++)
    {
        if (platform.deferredButtons & (1 << i)) CORE.Input.Mouse.currentButtonState[i] = 0;
    }
    platform.deferredButtons = 0;
    memset(platform.pressedThisPoll, 0, sizeof(platform.pressedThisPoll));
    platform.buttonsPressedThisPoll = 0;

    int handled = VexDrainEvents();

    // EnableEventWaiting(): sleep until the next event when nothing happened
    if (CORE.Window.eventWaiting && (handled == 0) && !CORE.Window.shouldClose)
    {
        vex_app_window_event_t event;

        uint16_t deadline=event_deadline_ticks;event_deadline_ticks=0;
        if ((deadline?vex_app_window_wait_timeout(&platform.window,&event,deadline):
             vex_app_window_wait(&platform.window, &event)) == 0) VexHandleEvent(&event);
        if (!CORE.Window.shouldClose) VexDrainEvents();
    }
}

//----------------------------------------------------------------------------------
// Module Internal Functions Definition
//----------------------------------------------------------------------------------

static void VexResizeBuffers(int width, int height)
{
    if ((width <= 0) || (height <= 0)) return;
    if ((width == platform.pixelsWidth) && (height == platform.pixelsHeight) && (platform.pixels != NULL)) return;

    unsigned int *pixels = (unsigned int *)RL_CALLOC((size_t)width*(size_t)height, sizeof(unsigned int));
    if (pixels == NULL)
    {
        TRACELOG(LOG_WARNING, "VEXOS: Failed to allocate %ix%i presentation buffer", width, height);
        return;
    }
    RL_FREE(platform.pixels);
    platform.pixels = pixels;
    platform.pixelsWidth = width;
    platform.pixelsHeight = height;

    if (CORE.Window.ready)
    {
        CORE.Window.screen.width = width;
        CORE.Window.screen.height = height;
        SetupViewport(width, height);
        CORE.Window.currentFbo.width = width;
        CORE.Window.currentFbo.height = height;
        swResize(width, height);
        CORE.Window.resizedLastFrame = true;
    }
}

int InitPlatform(void)
{
    if (rlGetVersion() != RL_OPENGL_SOFTWARE)
    {
        TRACELOG(LOG_FATAL, "VEXOS: Platform requires the software renderer (GRAPHICS_API_OPENGL_SOFTWARE)");
        return -1;
    }

    if (CORE.Window.screen.width <= 0) CORE.Window.screen.width = 800;
    if (CORE.Window.screen.height <= 0) CORE.Window.screen.height = 450;

    uint32_t flags = VEX_APP_WINDOW_FLAG_RAW_INPUT;
    if(vex_close_confirmation)flags |= VEX_APP_WINDOW_FLAG_CLOSE_CONFIRM;
    if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_RESIZABLE)) flags |= VEX_APP_WINDOW_FLAG_RESIZABLE;

    const char *title = ((CORE.Window.title != NULL) && (CORE.Window.title[0] != '\0'))? CORE.Window.title : "raylib";
    if (vex_app_window_open(&platform.window, title, (uint32_t)CORE.Window.screen.width,
            (uint32_t)CORE.Window.screen.height, flags) != 0)
    {
        TRACELOG(LOG_FATAL, "VEXOS: Failed to open window (is the desktop running?)");
        return -1;
    }

    VexResizeBuffers(CORE.Window.screen.width, CORE.Window.screen.height);
    if (platform.pixels == NULL) return -1;

    CORE.Window.display.width = CORE.Window.screen.width;
    CORE.Window.display.height = CORE.Window.screen.height;
    CORE.Window.render.width = CORE.Window.screen.width;
    CORE.Window.render.height = CORE.Window.screen.height;
    CORE.Window.currentFbo.width = CORE.Window.render.width;
    CORE.Window.currentFbo.height = CORE.Window.render.height;
    platform.focused = true;

    TRACELOG(LOG_INFO, "DISPLAY: Device initialized successfully");
    TRACELOG(LOG_INFO, "    > Screen size:  %i x %i", CORE.Window.screen.width, CORE.Window.screen.height);

    CORE.Window.ready = true;

    InitTimer();
    CORE.Storage.basePath = GetWorkingDirectory();

    TRACELOG(LOG_INFO, "PLATFORM: VEXOS: Initialized successfully");
    return 0;
}

void ClosePlatform(void)
{
    vex_app_window_close(&platform.window);
    RL_FREE(platform.pixels);
    platform.pixels = NULL;
    RL_FREE(platform.clipboard);
    platform.clipboard = NULL;
}

// EOF

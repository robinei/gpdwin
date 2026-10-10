#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * LD_PRELOAD for games that read the raw SDL joystick API (SDL_JoystickGetAxis) with an Xbox 360 pad:
 * the triggers (axes 2 and 5) rest at -32768 there, so the game sees them as always fully pressed and
 * ignores the pad (Unepic). Remap them to 0..32767 like a half axis, like SDL's game controller layer does.
 * PADSHIM_SWAP_HAT_UD=1 also swaps D-pad up and down: Unepic reads hat bits 2 and 0 as up and down
 * (SDL: bit 0 = up, bit 2 = down), so the D-pad moves the wrong way vertically.
 * PADSHIM_XINPUT_LAYOUT=1 presents the Linux xpad layout in the order SDL gives on Windows (what such
 * games' default bindings assume): axes LX LY RX RY LT RT instead of LX LY LT RX RY RT, and the buttons
 * L3/R3 at 8/9 instead of 9/10 (xpad has the Guide button at 8).
 */
static int env_on(const char *name)
{
    const char *e = getenv(name);
    return e && *e == '1';
}

int16_t SDL_JoystickGetAxis(void *joy, int axis)
{
    static int16_t (*real)(void *, int);
    static int (*num_axes)(void *);
    static int xin = -1;
    static const int xmap[6] = { 0, 1, 3, 4, 2, 5 };
    if (!real) {
        real = dlsym(RTLD_NEXT, "SDL_JoystickGetAxis");
        num_axes = dlsym(RTLD_NEXT, "SDL_JoystickNumAxes");
    }
    if (xin < 0)
        xin = env_on("PADSHIM_XINPUT_LAYOUT");
    int six = num_axes && num_axes(joy) == 6;
    if (xin && six && axis >= 0 && axis < 6)
        axis = xmap[axis];
    int16_t v = real(joy, axis);
    if ((axis == 2 || axis == 5) && six)
        return (int16_t)(((int32_t)v + 32768) / 2);
    return v;
}

uint8_t SDL_JoystickGetButton(void *joy, int button)
{
    static uint8_t (*real)(void *, int);
    static int xin = -1;
    if (!real)
        real = dlsym(RTLD_NEXT, "SDL_JoystickGetButton");
    if (xin < 0)
        xin = env_on("PADSHIM_XINPUT_LAYOUT");
    if (xin && (button == 8 || button == 9))
        button++;
    else if (xin && button == 10)
        button = 8;
    return real(joy, button);
}

uint8_t SDL_JoystickGetHat(void *joy, int hat)
{
    static uint8_t (*real)(void *, int);
    static int swap = -1;
    if (!real)
        real = dlsym(RTLD_NEXT, "SDL_JoystickGetHat");
    if (swap < 0)
        swap = env_on("PADSHIM_SWAP_HAT_UD");
    uint8_t v = real(joy, hat);
    if (swap)
        v = (uint8_t)((v & ~5) | ((v & 1) << 2) | ((v >> 2) & 1));
    return v;
}

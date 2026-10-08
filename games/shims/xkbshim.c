#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKBstr.h>
/* old GLFW calls XkbGetKeyboard(all components); Xwayland fails that: assemble it from map + names */
XkbDescPtr XkbGetKeyboard(Display *dpy, unsigned int which, unsigned int spec)
{
    XkbDescPtr d = XkbGetMap(dpy, XkbAllMapComponentsMask, spec);
    if (!d) return NULL;
    XkbGetNames(dpy, XkbKeyNamesMask | XkbKeyAliasesMask | XkbAllNamesMask, d);
    return d;
}

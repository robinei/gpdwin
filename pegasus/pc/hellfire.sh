#!/bin/sh
# SDL's opengles2 renderer: the default opengl renderer's per-frame texture upload triggers the DSI
# display flashes/splits (kernel/dsi-investigation/FINDINGS.md "Reproducer"); opengles2 did not in tests.
SDL_RENDER_DRIVER=opengles2 exec devilutionx --hellfire "$@"

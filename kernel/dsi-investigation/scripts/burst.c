// DSI flash reproducer: renders like DevilutionX (SDL renderer, fullscreen, vsync, a CPU-drawn 853x480
// picture uploaded every frame and scaled to the window with linear filtering), so that the GPU does a short
// burst per frame and idles in between. Each mode leaves out a part of the work, to find which part makes
// the DSI DPI FIFO underrun (count them in `journalctl -k | grep classified`; dsi-trace.py gives timing).
//   cc -O2 -o burst burst.c $(pkg-config --cflags --libs sdl3)
//   WAYLAND_DISPLAY=wayland-1 ./burst [-m full|static|upload|clear] [-t seconds] [-s WxH] [-r renderer] [-n]
//                                     [-d ms] [-k n] [-p ms] [-u ms] [-l file]
//     full    upload a changed picture every frame and draw it (DevilutionX)
//     static  draw the same picture every frame, no upload
//     upload  upload every frame, draw only a clear (texture never sampled)
//     clear   no texture at all, clear + present
//     -n      no vsync (present as fast as possible)
//     -d ms   wait this long before rendering each frame (moves the GPU burst within the display frame)
//     -k n    split the frame's work into n GPU submissions (n draws, each flushed)
//     -u ms   wait this long between the upload and the rest of the frame
//     -l file write CLOCK_MONOTONIC times per frame: upload start, upload end, present start, present end
//             (same clock as the ftrace dumps of dsi-trace.py)
//     -p ms   no vsync, one frame every ms (e.g. 16.5 against the panel's 16.61: the burst sweeps through
//             all positions in the display frame every ~2.5 s; with vsync the compositor fixes the position)
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
	const char *mode = "full", *rname = NULL;
	int secs = 300, w = 853, h = 480, vsync = 1, k = 1;
	double delay_ms = 0, period_ms = 0, gap_ms = 0;
	const char *logname = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-m") && i + 1 < argc) mode = argv[++i];
		else if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-s") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
		else if (!strcmp(argv[i], "-r") && i + 1 < argc) rname = argv[++i];
		else if (!strcmp(argv[i], "-n")) vsync = 0;
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) delay_ms = atof(argv[++i]);
		else if (!strcmp(argv[i], "-k") && i + 1 < argc) k = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-u") && i + 1 < argc) gap_ms = atof(argv[++i]);
		else if (!strcmp(argv[i], "-l") && i + 1 < argc) logname = argv[++i];
		else if (!strcmp(argv[i], "-p") && i + 1 < argc) { period_ms = atof(argv[++i]); vsync = 0; }
		else { fprintf(stderr, "usage: see the comment at the top\n"); return 1; }
	}
	int upload = !strcmp(mode, "full") || !strcmp(mode, "upload");
	int draw = !strcmp(mode, "full") || !strcmp(mode, "static");
	if (!upload && !draw && strcmp(mode, "clear")) { fprintf(stderr, "unknown mode %s\n", mode); return 1; }

	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	SDL_Window *win = SDL_CreateWindow("burst", 1280, 720, SDL_WINDOW_FULLSCREEN);
	SDL_Renderer *r = win ? SDL_CreateRenderer(win, rname) : NULL;
	if (!r) { fprintf(stderr, "window/renderer: %s\n", SDL_GetError()); return 1; }
	SDL_SetRenderVSync(r, vsync);
	SDL_SetRenderLogicalPresentation(r, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
	SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
	Uint32 *px = malloc((size_t)w * h * 4);
	for (int i = 0; i < w * h; i++) px[i] = 0x203040 + (i % w) * 0x010101 % 0x808080;
	SDL_UpdateTexture(tex, NULL, px, w * 4);
	printf("renderer %s, mode %s, %dx%d, vsync %d, delay %.1f ms, %d submissions/frame, %d s\n",
	       SDL_GetRendererName(r), mode, w, h, vsync, delay_ms, k, secs);
	fflush(stdout);

	Uint64 end = SDL_GetTicks() + (Uint64)secs * 1000, frames = 0, t1 = SDL_GetTicks();
	Uint64 next = SDL_GetTicksNS();
	FILE *lf = logname ? fopen(logname, "w") : NULL;
	while (SDL_GetTicks() < end) {
		SDL_Event e;
		while (SDL_PollEvent(&e))
			if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_KEY_DOWN) end = 0;
		if (period_ms > 0) {
			next += (Uint64)(period_ms * 1e6);
			Uint64 now = SDL_GetTicksNS();
			if (next > now)
				SDL_DelayPrecise(next - now);
		}
		if (delay_ms > 0)
			SDL_DelayPrecise((Uint64)(delay_ms * 1e6));
		double tu0 = mono(), tu1 = tu0;
		if (upload) {
			// change a band of rows like a scrolling game view, then upload the whole picture
			int band = (int)(frames % h);
			for (int y = band; y < band + 8 && y < h; y++)
				for (int x = 0; x < w; x++)
					px[y * w + x] ^= 0x00ffffff;
			SDL_UpdateTexture(tex, NULL, px, w * 4);
			tu1 = mono();
			if (gap_ms > 0)
				SDL_DelayPrecise((Uint64)(gap_ms * 1e6));
		}
		SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
		SDL_RenderClear(r);
		for (int j = 0; j < k; j++) {
			if (draw)
				SDL_RenderTexture(r, tex, NULL, NULL);
			if (k > 1)
				SDL_FlushRenderer(r);
		}
		double tp0 = mono();
		SDL_RenderPresent(r);
		if (lf)
			fprintf(lf, "%.6f %.6f %.6f %.6f\n", tu0, tu1, tp0, mono());
		frames++;
		if (SDL_GetTicks() - t1 >= 10000) {
			printf("%.1f fps\n", frames * 1000.0 / (SDL_GetTicks() - (end - (Uint64)secs * 1000)));
			fflush(stdout);
			t1 = SDL_GetTicks();
		}
	}
	if (lf)
		fclose(lf);
	printf("%llu frames\n", (unsigned long long)frames);
	SDL_Quit();
	return 0;
}

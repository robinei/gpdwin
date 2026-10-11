// R4: the render engine reads a source BO that WE allocate (snooped or not, persistent or fresh per frame),
// imported into GL as a dma-buf EGLImage; each frame the CPU writes the BO, then glCopyTexSubImage2D copies it
// (blorp blit on the render engine, like the texture-upload staging path) into a Y-tiled texture, then clear + swap.
//   cc -O2 -o r4egl r4egl.c $(pkg-config --cflags --libs sdl3) -lEGL
//   r4egl [-S] [-F] [-t s] [-d ms] [-s WxH] [-n]   -S snooped source, -F fresh BO per frame, -n no CPU write
#include <SDL3/SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/i915_drm.h>

typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei;
typedef unsigned int GLbitfield; typedef float GLfloat; typedef unsigned char GLubyte;
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define DRM_FORMAT_ARGB8888 0x34325241

static void (*p_glGenTextures)(GLsizei, GLuint *);
static void (*p_glDeleteTextures)(GLsizei, const GLuint *);
static void (*p_glBindTexture)(GLenum, GLuint);
static void (*p_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*p_glCopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
static void (*p_glTexParameteri)(GLenum, GLenum, GLint);
static void (*p_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*p_glClear)(GLbitfield);
static void (*p_glViewport)(GLint, GLint, GLsizei, GLsizei);
static GLenum (*p_glGetError)(void);
static void (*p_glGenFramebuffers)(GLsizei, GLuint *);
static void (*p_glDeleteFramebuffers)(GLsizei, const GLuint *);
static void (*p_glBindFramebuffer)(GLenum, GLuint);
static void (*p_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static GLenum (*p_glCheckFramebufferStatus)(GLenum);
static void (*p_glEGLImageTargetTexture2DOES)(GLenum, void *);
static EGLImageKHR (*p_eglCreateImageKHR)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint *);
static EGLBoolean (*p_eglDestroyImageKHR)(EGLDisplay, EGLImageKHR);
#define LOAD(n) do { *(void **)&p_##n = SDL_GL_GetProcAddress(#n); if (!p_##n) { fprintf(stderr, "missing %s\n", #n); return 1; } } while (0)

static int fd;
static int xioctl(unsigned long req, void *arg)
{
	int r;
	do r = ioctl(fd, req, arg); while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

struct src { uint32_t handle; int dmabuf; void *map; EGLImageKHR img; GLuint tex, fbo; };

static int make_src(struct src *s, EGLDisplay dpy, int w, int h, uint64_t size, int snoop)
{
	struct drm_i915_gem_create c = { .size = size };
	if (xioctl(DRM_IOCTL_I915_GEM_CREATE, &c)) { perror("GEM_CREATE"); return 1; }
	s->handle = c.handle;
	struct drm_i915_gem_set_domain sd = { .handle = s->handle, .read_domains = I915_GEM_DOMAIN_CPU };
	xioctl(DRM_IOCTL_I915_GEM_SET_DOMAIN, &sd);
	if (snoop) {
		struct drm_i915_gem_caching ca = { .handle = s->handle, .caching = I915_CACHING_CACHED };
		if (xioctl(DRM_IOCTL_I915_GEM_SET_CACHING, &ca)) { perror("SET_CACHING"); return 1; }
	}
	struct drm_i915_gem_mmap_offset mo = { .handle = s->handle, .flags = snoop ? I915_MMAP_OFFSET_WB : I915_MMAP_OFFSET_WC };
	if (xioctl(DRM_IOCTL_I915_GEM_MMAP_OFFSET, &mo)) { perror("MMAP_OFFSET"); return 1; }
	s->map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mo.offset);
	if (s->map == MAP_FAILED) { perror("mmap"); return 1; }
	struct drm_prime_handle ph = { .handle = s->handle, .flags = DRM_CLOEXEC | DRM_RDWR };
	if (xioctl(DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph)) { perror("PRIME_HANDLE_TO_FD"); return 1; }
	s->dmabuf = ph.fd;
	EGLint attrs[] = {
		EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ARGB8888,
		EGL_DMA_BUF_PLANE0_FD_EXT, s->dmabuf, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0, EGL_DMA_BUF_PLANE0_PITCH_EXT, w * 4,
		EGL_NONE
	};
	s->img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
	if (!s->img) { fprintf(stderr, "eglCreateImageKHR failed 0x%x\n", eglGetError()); return 1; }
	p_glGenTextures(1, &s->tex);
	p_glBindTexture(GL_TEXTURE_2D, s->tex);
	p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, s->img);
	p_glGenFramebuffers(1, &s->fbo);
	p_glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex, 0);
	if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo incomplete\n"); return 1; }
	p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	GLenum e = p_glGetError();
	if (e) { fprintf(stderr, "GL error 0x%x in make_src\n", e); return 1; }
	return 0;
}

static void free_src(struct src *s, EGLDisplay dpy, uint64_t size)
{
	p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	p_glDeleteFramebuffers(1, &s->fbo);
	p_glDeleteTextures(1, &s->tex);
	p_eglDestroyImageKHR(dpy, s->img);
	close(s->dmabuf);
	munmap(s->map, size);
	struct drm_gem_close c = { .handle = s->handle };
	xioctl(DRM_IOCTL_GEM_CLOSE, &c);
}

int main(int argc, char **argv)
{
	int w = 853, h = 480, secs = 120, snoop = 0, fresh = 0, nowrite = 0;
	double delay_ms = 10;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-S")) snoop = 1;
		else if (!strcmp(argv[i], "-F")) fresh = 1;
		else if (!strcmp(argv[i], "-n")) nowrite = 1;
		else if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) delay_ms = atof(argv[++i]);
		else if (!strcmp(argv[i], "-s") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
		else { fprintf(stderr, "usage: see the comment at the top\n"); return 1; }
	}
	fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("renderD128"); return 1; }
	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_Window *win = SDL_CreateWindow("r4egl", 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
	SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : NULL;
	if (!ctx) { fprintf(stderr, "window/context: %s\n", SDL_GetError()); return 1; }
	SDL_GL_SetSwapInterval(1);
	LOAD(glGenTextures); LOAD(glDeleteTextures); LOAD(glBindTexture); LOAD(glTexImage2D); LOAD(glCopyTexSubImage2D);
	LOAD(glTexParameteri); LOAD(glClearColor); LOAD(glClear); LOAD(glViewport); LOAD(glGetError); LOAD(glGenFramebuffers); LOAD(glDeleteFramebuffers);
	LOAD(glBindFramebuffer); LOAD(glFramebufferTexture2D); LOAD(glCheckFramebufferStatus); LOAD(glEGLImageTargetTexture2DOES);
	*(void **)&p_eglCreateImageKHR = (void *)SDL_EGL_GetProcAddress("eglCreateImageKHR");
	*(void **)&p_eglDestroyImageKHR = (void *)SDL_EGL_GetProcAddress("eglDestroyImageKHR");
	EGLDisplay dpy = (EGLDisplay)SDL_EGL_GetCurrentDisplay();
	if (!p_eglCreateImageKHR || dpy == EGL_NO_DISPLAY) { fprintf(stderr, "no EGL image support / display\n"); return 1; }
	int ww, wh;
	SDL_GetWindowSizeInPixels(win, &ww, &wh);
	p_glViewport(0, 0, ww, wh);

	uint64_t size = ((uint64_t)w * h * 4 + 4095) & ~4095ull;
	unsigned *px = malloc(size);
	for (int i = 0; i < w * h; i++) px[i] = 0x203040 + (i % w) * 0x010101 % 0x808080;

	GLuint tex;
	p_glGenTextures(1, &tex);
	p_glBindTexture(GL_TEXTURE_2D, tex);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);

	struct src s;
	if (!fresh) {
		if (make_src(&s, dpy, w, h, size, snoop)) return 1;
		memcpy(s.map, px, (size_t)w * h * 4);
	}
	printf("r4egl: %dx%d snoop %d fresh %d write %d delay %.1f ms %d s\n", w, h, snoop, fresh, !nowrite, delay_ms, secs);
	fflush(stdout);

	Uint64 end = SDL_GetTicks() + (Uint64)secs * 1000, frames = 0, t0 = SDL_GetTicks(), t1 = t0;
	while (SDL_GetTicks() < end) {
		SDL_Event e;
		while (SDL_PollEvent(&e))
			if (e.type == SDL_EVENT_QUIT) end = 0;
		if (delay_ms > 0)
			SDL_DelayPrecise((Uint64)(delay_ms * 1e6));
		int band = (int)(frames % h);
		for (int y = band; y < band + 8 && y < h; y++)
			for (int x = 0; x < w; x++)
				px[y * w + x] ^= 0x00ffffff;
		if (fresh && make_src(&s, dpy, w, h, size, snoop)) return 1;
		if (!nowrite || fresh)
			memcpy(s.map, px, (size_t)w * h * 4);
		p_glBindTexture(GL_TEXTURE_2D, tex);
		p_glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
		p_glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
		p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		p_glClearColor(0, 0, 0, 1);
		p_glClear(GL_COLOR_BUFFER_BIT);
		SDL_GL_SwapWindow(win);
		if (fresh)
			free_src(&s, dpy, size);
		frames++;
		if (frames == 2) { GLenum ge = p_glGetError(); if (ge) { fprintf(stderr, "GL error 0x%x\n", ge); return 1; } }
		if (SDL_GetTicks() - t1 >= 10000) {
			printf("%.1f fps\n", frames * 1000.0 / (SDL_GetTicks() - t0));
			fflush(stdout);
			t1 = SDL_GetTicks();
		}
	}
	printf("%llu frames\n", (unsigned long long)frames);
	SDL_Quit();
	return 0;
}

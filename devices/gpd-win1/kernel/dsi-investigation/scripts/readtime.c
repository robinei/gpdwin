// Readback timing for the Mesa patch (crocus: upload staging textures not snooped, readback staging still
// snooped): times glReadPixels from an RGBA8 framebuffer and glGetTexImage of a GL_RGBA8 texture, each
// read as GL_BGRA (a format conversion, so Mesa goes through a staging texture) and as GL_RGBA. Surfaceless
// EGL, desktop GL, no window. Compare the system Mesa with the patched one: the numbers should match.
//   cc -O2 -o readtime readtime.c -lEGL -lGL
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W 853
#define H 480
#define N 100

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static void report(const char *name, double *t)
{
	qsort(t, N, sizeof(double), cmp);
	printf("%-28s median %6.2f ms  p90 %6.2f ms\n", name, t[N / 2], t[N * 9 / 10]);
}

int main(void)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 2; }
	eglBindAPI(EGL_OPENGL_API);
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
		fprintf(stderr, "context failed\n"); return 2;
	}
	printf("renderer: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	unsigned char *buf = malloc(W * H * 4);
	for (int i = 0; i < W * H * 4; i++) buf[i] = i * 7;
	GLuint tex, fb;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo\n"); return 2; }

	double t[N];
	struct { const char *name; int get; GLenum fmt; } tests[] = {
		{ "glReadPixels GL_BGRA", 0, GL_BGRA }, { "glReadPixels GL_RGBA", 0, GL_RGBA },
		{ "glGetTexImage GL_BGRA", 1, GL_BGRA }, { "glGetTexImage GL_RGBA", 1, GL_RGBA },
	};
	for (unsigned k = 0; k < sizeof(tests) / sizeof(tests[0]); k++) {
		for (int i = 0; i < N; i++) {
			glClearColor((i & 7) / 7.0f, 0.3f, 0.6f, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			glFinish();
			double t0 = now();
			if (tests[k].get)
				glGetTexImage(GL_TEXTURE_2D, 0, tests[k].fmt, GL_UNSIGNED_BYTE, buf);
			else
				glReadPixels(0, 0, W, H, tests[k].fmt, GL_UNSIGNED_BYTE, buf);
			t[i] = now() - t0;
		}
		report(tests[k].name, t);
	}
	printf("GL error 0x%x\n", glGetError());
	return 0;
}

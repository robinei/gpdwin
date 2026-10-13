// Headless GPU load for the screen on/off race test: a surfaceless EGL/GLES2 context draws into a
// small renderbuffer and calls glFinish() in a loop, so the GPU completes many small requests per second
// (each one a GT interrupt) independently of any output being on. Runs until killed.
//   cc -O2 -o gpuload gpuload.c -lEGL -lGLESv2
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <time.h>

int main(void)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
		fprintf(stderr, "context failed\n"); return 1;
	}
	GLuint fb, rb;
	glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, 256, 256);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	printf("renderer: %s\n", glGetString(GL_RENDERER)); fflush(stdout);
	unsigned long n = 0;
	time_t t0 = time(NULL);
	for (;;) {
		glClearColor((n & 255) / 255.0f, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glFinish();
		if (++n % 20000 == 0) {
			printf("%lu finishes, %.0f/s\n", n, n / (double)(time(NULL) - t0 + 1)); fflush(stdout);
		}
	}
}

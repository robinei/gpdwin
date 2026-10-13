// Channel-order check for the Mesa patch 0001 (GL_RGBA8 texture filled with GL_BGRA data, as SDL's opengl
// renderer does): upload four pure-colour blocks, draw the texture into an offscreen RGBA8 framebuffer with
// a textured quad (desktop GL, surfaceless EGL, no window) and read the result back with GL_RGBA. Prints
// PASS if every block comes back as the colour that was uploaded. Run with and without MESA_ST_MATCH_SIZED=1.
//   cc -O2 -o colorcheck colorcheck.c -lEGL -lGL
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <string.h>

#define W 64
#define H 64

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

	// BGRA bytes in memory: B, G, R, A. Quadrants: red, green, blue, white (alpha 255).
	static unsigned char src[H][W][4];
	const unsigned char col[4][4] = { { 0, 0, 255, 255 }, { 0, 255, 0, 255 }, { 255, 0, 0, 255 }, { 255, 255, 255, 255 } };
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			memcpy(src[y][x], col[(y >= H / 2) * 2 + (x >= W / 2)], 4);

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, src);

	GLuint fb, rb;
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo\n"); return 2; }

	glViewport(0, 0, W, H);
	glClearColor(0.5f, 0.5f, 0.5f, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();

	static unsigned char out[H][W][4];
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
	const char *name[4] = { "red", "green", "blue", "white" };
	const unsigned char want[4][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 } };
	int ok = 1;
	for (int q = 0; q < 4; q++) {
		int x = (q & 1) ? W * 3 / 4 : W / 4, y = (q & 2) ? H * 3 / 4 : H / 4;
		unsigned char *p = out[y][x];
		int good = p[0] == want[q][0] && p[1] == want[q][1] && p[2] == want[q][2];
		printf("%-5s -> R %3d G %3d B %3d A %3d %s\n", name[q], p[0], p[1], p[2], p[3], good ? "ok" : "WRONG");
		ok &= good;
	}
	GLint fmt = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
	printf("internal format reported: 0x%04x (GL_RGBA8 = 0x8058); GL error 0x%x\n", fmt, glGetError());
	printf("%s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}

// Draw-call overhead benchmark (offscreen, surfaceless EGL, desktop GL compatibility profile, no window):
// per frame, N tiny draws (one small quad each, 8x8 pixels: negligible fill), each with a uniform change
// and its vertices from client memory (so the driver streams them), into a 256x256 RGBA8 framebuffer,
// glFinish per frame. Prints median CPU ms per frame and draws/s. Measures driver CPU cost per draw.
//   cc -O2 -o drawbench drawbench.c -lEGL -lGL
//   ./drawbench [draws_per_frame=2000] [frames=200]
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	return s;
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 2000, frames = argc > 2 ? atoi(argv[2]) : 200;
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 2; }
	eglBindAPI(EGL_OPENGL_API);
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
		fprintf(stderr, "context failed\n"); return 2;
	}
	GLuint fb, rb;
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 256, 256);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	glViewport(0, 0, 256, 256);

	GLuint p = glCreateProgram();
	glAttachShader(p, shader(GL_VERTEX_SHADER,
		"#version 120\nattribute vec2 pos; uniform vec2 off; void main(){ gl_Position = vec4(pos + off, 0.0, 1.0); }"));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER,
		"#version 120\nuniform vec4 col; void main(){ gl_FragColor = col; }"));
	glBindAttribLocation(p, 0, "pos");
	glLinkProgram(p);
	glUseProgram(p);
	GLint uoff = glGetUniformLocation(p, "off"), ucol = glGetUniformLocation(p, "col");
	static const float quad[8] = { 0, 0, 0.0625f, 0, 0, 0.0625f, 0.0625f, 0.0625f };
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);   // client memory: streamed per draw

	double *t = malloc(sizeof(double) * frames);
	for (int f = -10; f < frames; f++) {
		double t0 = now();
		glClear(GL_COLOR_BUFFER_BIT);
		for (int i = 0; i < n; i++) {
			glUniform2f(uoff, (i % 30) / 16.0f - 1, (i / 30 % 30) / 16.0f - 1);
			glUniform4f(ucol, (i & 7) / 7.0f, 0.5f, 0.2f, 1);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		glFinish();
		if (f >= 0)
			t[f] = now() - t0;
	}
	qsort(t, frames, sizeof(double), cmp);
	printf("%s: %d draws/frame, median %.2f ms/frame (p90 %.2f) = %.0f draws/s, GL error 0x%x\n",
	       (const char *)glGetString(GL_VERSION), n, t[frames / 2], t[frames * 9 / 10],
	       n / t[frames / 2] * 1000, glGetError());
	return 0;
}

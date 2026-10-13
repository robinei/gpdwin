// Buffer update patterns (R7), offscreen (surfaceless EGL, desktop GL): what wined3d, SDL renderers and
// RetroArch do with vertex/index/uniform buffers per frame. Per frame: K updates of SZ bytes into a VBO that
// the previous frame's draws used (so the buffer is busy unless the frame was finished), each followed by a
// small draw reading the updated range; glFinish per frame. Prints median/p90 CPU ms per frame and total.
//   cc -O2 -o bufbench bufbench.c -lEGL -lGL
//   ./bufbench MODE [-k updates=64] [-z bytes=1024] [-b buffer bytes=1048576] [-n frames] [-q]
// modes:
//   subdata     glBufferSubData at increasing offsets (wined3d static/managed buffers, Qt)
//   orphan      glBufferData(NULL) + glBufferSubData of the whole buffer (SDL opengl renderer pattern)
//   mapunsync   glMapBufferRange(WRITE|UNSYNCHRONIZED|INVALIDATE_RANGE) (wined3d dynamic buffers NOOVERWRITE)
//   mapinval    glMapBufferRange(WRITE|INVALIDATE_BUFFER) at offset 0 (D3D DISCARD)
//   persistent  one glBufferStorage(PERSISTENT|COHERENT) buffer mapped once, written at increasing offsets
//   uniform     glUniform4fv of 64 floats per draw (constant updates without a buffer)
//   client      vertex data from client memory per draw (immediate-mode style, driver streams it)
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	const char *mode = argc > 1 ? argv[1] : "subdata";
	int k = 64, sz = 1024, bufsz = 1 << 20, frames = 200, quiet = 0;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-k") && i + 1 < argc) k = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-z") && i + 1 < argc) sz = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-b") && i + 1 < argc) bufsz = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-n") && i + 1 < argc) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-q")) quiet = 1;
	}
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 2; }
	eglBindAPI(EGL_OPENGL_API);
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) { fprintf(stderr, "context failed\n"); return 2; }
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
		"#version 120\nattribute vec2 pos; uniform vec4 u[64]; void main(){ gl_Position = vec4(pos * 0.05 + u[int(mod(pos.x, 64.0))].xy * 0.0, 0.0, 1.0); }"));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER, "#version 120\nvoid main(){ gl_FragColor = vec4(1.0); }"));
	glBindAttribLocation(p, 0, "pos");
	glLinkProgram(p);
	glUseProgram(p);
	GLint uu = glGetUniformLocation(p, "u");
	glEnableVertexAttribArray(0);

	float *data = malloc(bufsz);
	for (int i = 0; i < bufsz / 4; i++) data[i] = (i % 7) * 0.1f;
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	void *pmap = NULL;
	int persistent = !strcmp(mode, "persistent");
	if (persistent) {
		glBufferStorage(GL_ARRAY_BUFFER, bufsz, data, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
		pmap = glMapBufferRange(GL_ARRAY_BUFFER, 0, bufsz, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
		if (!pmap) { fprintf(stderr, "persistent map failed\n"); return 2; }
	} else {
		glBufferData(GL_ARRAY_BUFFER, bufsz, data, GL_DYNAMIC_DRAW);
	}
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	int nverts = sz / 8 > 3 ? sz / 8 : 3;
	int client = !strcmp(mode, "client");

	double *tc = malloc(sizeof(double) * frames), *tt = malloc(sizeof(double) * frames);
	for (int f = -10; f < frames; f++) {
		double t0 = now();
		glClear(GL_COLOR_BUFFER_BIT);
		int off = 0;
		for (int i = 0; i < k; i++) {
			data[i] += 0.001f;
			if (!strcmp(mode, "subdata")) {
				glBufferSubData(GL_ARRAY_BUFFER, off, sz, data);
			} else if (!strcmp(mode, "orphan")) {
				glBufferData(GL_ARRAY_BUFFER, bufsz, NULL, GL_DYNAMIC_DRAW);
				glBufferSubData(GL_ARRAY_BUFFER, 0, bufsz, data);
				off = 0;
			} else if (!strcmp(mode, "mapunsync")) {
				void *m = glMapBufferRange(GL_ARRAY_BUFFER, off, sz, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
				memcpy(m, data, sz);
				glUnmapBuffer(GL_ARRAY_BUFFER);
			} else if (!strcmp(mode, "mapinval")) {
				void *m = glMapBufferRange(GL_ARRAY_BUFFER, 0, sz, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
				memcpy(m, data, sz);
				glUnmapBuffer(GL_ARRAY_BUFFER);
				off = 0;
			} else if (persistent) {
				memcpy((char *)pmap + off, data, sz);
			} else if (!strcmp(mode, "uniform")) {
				glUniform4fv(uu, 64, data);
				off = 0;
			} else if (client) {
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, data + (off / 4));
			} else { fprintf(stderr, "unknown mode %s\n", mode); return 2; }
			if (!client)
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)(long)off);
			glDrawArrays(GL_TRIANGLES, 0, nverts);
			off += sz;
			if (off + sz > bufsz) off = 0;
		}
		double t1 = now();
		glFinish();
		double t2 = now();
		if (f >= 0) { tc[f] = t1 - t0; tt[f] = t2 - t0; }
	}
	qsort(tc, frames, sizeof(double), cmp);
	qsort(tt, frames, sizeof(double), cmp);
	if (quiet)
		printf("%.3f %.3f %.3f %.3f\n", tc[frames / 2], tc[frames * 9 / 10], tt[frames / 2], tt[frames * 9 / 10]);
	else
		printf("bufbench %-10s k=%d sz=%d buf=%d: cpu median %.3f ms (p90 %.3f) = %.1f us/update, frame median %.3f ms (p90 %.3f), err 0x%x\n",
		       mode, k, sz, bufsz, tc[frames / 2], tc[frames * 9 / 10], tc[frames / 2] * 1000 / k, tt[frames / 2], tt[frames * 9 / 10], glGetError());
	return 0;
}

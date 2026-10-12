// Texture/readback pattern benchmark (R7), offscreen (surfaceless EGL, desktop GL compatibility profile).
// Each mode mimics what a program on the GPD does per frame; prints median/p90 CPU ms per frame (until the
// GL calls return) and total ms per frame (with glFinish), so GPU-side costs show in the difference.
//   cc -O2 -o texbench texbench.c -lEGL -lGL
//   ./texbench MODE [-s WxH] [-n frames] [-k count] [-z tile] [-q] [-t] [-o file] (dump the last readback) (-t: also GPU time of the frame from
//   GL_TIMESTAMP queries around the frame's GL calls; the GPU runs at 200 MHz in these synchronous loops)
// modes:
//   sub-rgba     glTexSubImage2D(GL_RGBA, UNSIGNED_BYTE) into GL_RGBA8 (matching format: direct path)
//   sub-bgra     glTexSubImage2D(GL_BGRA, UNSIGNED_INT_8_8_8_8_REV) into GL_RGBA8 (wined3d, SDL opengl)
//   pbo-rgba     the same from a PBO filled with glBufferData(NULL)+glBufferSubData (st_pbo shader path)
//   pbo-bgra     the same with BGRA
//   pbomap-bgra  PBO filled through glMapBufferRange(WRITE|INVALIDATE_BUFFER) (wined3d)
//   subsmall     -k glTexSubImage2D of -z x -z (default 64x64) RGBA tiles into a WxH texture (atlas updates)
//   create       per frame: create a WxH GL_RGBA8 texture, render to it (clear + 1 draw), sample it, delete it
//   rtt          render to a WxH texture (clear to black + 1 draw), then draw it to the framebuffer (every frame)
//   rtt-noclear  the same without the clear
//   clear0       clear a WxH FBO to (0,0,0,1) (fast-clear candidate) + 1 draw
//   clear5       clear to (0.5,0.5,0.5,1) (no fast clear) + 1 draw
//   mipmap       glGenerateMipmap of a WxH texture
//   readpixels   glReadPixels WxH GL_RGBA from an FBO
//   readbgra     glReadPixels WxH GL_BGRA (staging + conversion)
//   readsmall    glReadPixels 16x16 GL_RGBA from a WxH FBO
//   getbuf       glGetBufferSubData 64 B from a 4 MiB VBO used by the previous draw
//   mapread      glMapBufferRange(READ) 64 B of a 4 MiB VBO
//   pboread      glReadPixels WxH into a PBO, then glMapBufferRange(READ) the PBO (wined3d readback)
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
static GLuint make_tex(int w, int h, const void *data)
{
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	return t;
}
static GLuint make_fbo(GLuint tex)
{
	GLuint f;
	glGenFramebuffers(1, &f);
	glBindFramebuffer(GL_FRAMEBUFFER, f);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo incomplete\n"); exit(2); }
	return f;
}
static void draw_quad(float s)
{
	static const float q[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	float v[8];
	for (int i = 0; i < 8; i++) v[i] = q[i] * s;
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "sub-rgba";
	int W = 1280, H = 720, frames = 200, k = 16, quiet = 0, gputime = 0, tile = 64;
	const char *dump = NULL;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-s") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
		else if (!strcmp(argv[i], "-n") && i + 1 < argc) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-k") && i + 1 < argc) k = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-z") && i + 1 < argc) tile = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-o") && i + 1 < argc) dump = argv[++i];
		else if (!strcmp(argv[i], "-q")) quiet = 1;
		else if (!strcmp(argv[i], "-t")) gputime = 1;
	}
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 2; }
	eglBindAPI(EGL_OPENGL_API);
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) { fprintf(stderr, "context failed\n"); return 2; }

	// the "window": an RGBA8 texture FBO of WxH (like an EGL window buffer)
	GLuint wintex = make_tex(W, H, NULL), winfbo = make_fbo(wintex);
	glViewport(0, 0, W, H);
	GLuint p = glCreateProgram();
	glAttachShader(p, shader(GL_VERTEX_SHADER,
		"#version 120\nattribute vec2 pos; varying vec2 uv; void main(){ gl_Position = vec4(pos, 0.0, 1.0); uv = pos * 0.5 + 0.5; }"));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER,
		"#version 120\nuniform sampler2D tex; varying vec2 uv; void main(){ gl_FragColor = texture2D(tex, uv); }"));
	glBindAttribLocation(p, 0, "pos");
	glLinkProgram(p);
	glUseProgram(p);
	glEnableVertexAttribArray(0);

	size_t bytes = (size_t)W * H * 4, alloc = bytes > (4 << 20) ? bytes : (4 << 20);
	unsigned char *pixels = malloc(alloc), *out = malloc(alloc);
	for (size_t i = 0; i < alloc; i++) pixels[i] = (unsigned char)(i * 7);
	GLuint tex = make_tex(W, H, pixels), pbo = 0, fbo = 0, vbo = 0;
	int is_pbo = !strncmp(mode, "pbo", 3), bgra = strstr(mode, "bgra") != NULL;
	if (is_pbo) {
		glGenBuffers(1, &pbo);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, bytes, NULL, GL_STREAM_DRAW);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	if (!strncmp(mode, "rtt", 3) || !strncmp(mode, "clear", 5) || !strncmp(mode, "read", 4) || !strcmp(mode, "pboread") ||
	    !strcmp(mode, "getbuf") || !strcmp(mode, "mapread"))
		fbo = make_fbo(tex);
	if (!strcmp(mode, "getbuf") || !strcmp(mode, "mapread")) {
		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, 4 << 20, pixels, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	}
	if (!strcmp(mode, "pboread")) {
		glGenBuffers(1, &pbo);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
		glBufferData(GL_PIXEL_PACK_BUFFER, bytes, NULL, GL_STREAM_READ);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
	if (!strcmp(mode, "mipmap"))
		glGenerateMipmap(GL_TEXTURE_2D);
	GLenum fmt = bgra ? GL_BGRA : GL_RGBA, type = bgra ? GL_UNSIGNED_INT_8_8_8_8_REV : GL_UNSIGNED_BYTE;
	unsigned sum = 0, bad = 0;

	double *tc = malloc(sizeof(double) * frames), *tt = malloc(sizeof(double) * frames), *tg = malloc(sizeof(double) * frames);
	GLuint qs[2];
	glGenQueries(2, qs);
	for (int f = -10; f < frames; f++) {
		double t0 = now();
		pixels[(f & 1023) * 64] ^= 0xff;
		if (gputime) glQueryCounter(qs[0], GL_TIMESTAMP);
		glBindFramebuffer(GL_FRAMEBUFFER, winfbo);
		glViewport(0, 0, W, H);
		glBindTexture(GL_TEXTURE_2D, tex);
		if (!strncmp(mode, "sub-", 4)) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, fmt, type, pixels);
			draw_quad(1);
		} else if (!strncmp(mode, "pbo-", 4) || !strncmp(mode, "pbomap", 6)) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
			if (!strncmp(mode, "pbomap", 6)) {
				void *m = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, bytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
				memcpy(m, pixels, bytes);
				glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
			} else {
				glBufferData(GL_PIXEL_UNPACK_BUFFER, bytes, NULL, GL_STREAM_DRAW);
				glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, bytes, pixels);
			}
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, fmt, type, 0);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
			draw_quad(1);
		} else if (!strcmp(mode, "subsmall")) {
			for (int i = 0; i < k; i++)
				glTexSubImage2D(GL_TEXTURE_2D, 0, (i * tile) % (W - tile), (i * tile / (W - tile) * tile) % (H - tile), tile, tile, GL_RGBA, GL_UNSIGNED_BYTE, pixels + i * 64 * 4);
			draw_quad(1);
		} else if (!strcmp(mode, "create")) {
			GLuint t = make_tex(W, H, NULL), fb = make_fbo(t);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			glBindTexture(GL_TEXTURE_2D, tex);
			draw_quad(0.5f);
			glBindFramebuffer(GL_FRAMEBUFFER, winfbo);
			glBindTexture(GL_TEXTURE_2D, t);
			draw_quad(1);
			glDeleteFramebuffers(1, &fb);
			glDeleteTextures(1, &t);
		} else if (!strncmp(mode, "rtt", 3) || !strncmp(mode, "clear", 5)) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			if (strcmp(mode, "rtt-noclear")) {
				float g = !strcmp(mode, "clear5") ? 0.5f : 0.0f;
				glClearColor(g, g, g, 1);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			glBindTexture(GL_TEXTURE_2D, wintex);
			draw_quad(0.5f);
			glBindFramebuffer(GL_FRAMEBUFFER, winfbo);
			glBindTexture(GL_TEXTURE_2D, tex);
			if (!strncmp(mode, "rtt", 3))
				draw_quad(1);
		} else if (!strcmp(mode, "mipmap")) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			glGenerateMipmap(GL_TEXTURE_2D);
			draw_quad(1);
		} else if (!strncmp(mode, "read", 4)) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glClearColor(0.2f, 0.4f, 0.6f, 1); glClear(GL_COLOR_BUFFER_BIT);
			glBindTexture(GL_TEXTURE_2D, wintex);   // not the FBO's own texture (feedback loop)
			draw_quad(0.9f);
			int rw = !strcmp(mode, "readsmall") ? 16 : W, rh = !strcmp(mode, "readsmall") ? 16 : H;
			int bgra = !strcmp(mode, "readbgra");
			memset(out, 0, 8);
			glReadPixels(0, 0, rw, rh, bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, out);
			// pixel (0,0) is outside the 0.9 quad: the clear colour (51,102,153,255)
			unsigned char *px = out;
			if (px[bgra ? 2 : 0] != 51 || px[1] != 102 || px[bgra ? 0 : 2] != 153 || px[3] != 255) bad++;
			// the centre pixel of a full read is inside the quad: the window texture's pattern, not the clear colour
			if (rw == W && !memcmp(out + ((size_t)(H / 2) * W + W / 2) * 4, "\x33\x66\x99\xff", 4)) bad++;
			sum += out[0];
		} else if (!strcmp(mode, "pboread")) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			draw_quad(0.9f);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
			glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, 0);
			void *m = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
			memcpy(out, m, bytes);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
			sum += out[0];
		} else if (!strcmp(mode, "getbuf") || !strcmp(mode, "mapread")) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			memset(out, 0, 64);
			if (!strcmp(mode, "getbuf")) {
				glGetBufferSubData(GL_ARRAY_BUFFER, 1 << 20, 64, out);
			} else {
				void *m = glMapBufferRange(GL_ARRAY_BUFFER, 1 << 20, 64, GL_MAP_READ_BIT);
				memcpy(out, m, 64);
				glUnmapBuffer(GL_ARRAY_BUFFER);
			}
			if (memcmp(out, pixels + (1 << 20), 64)) bad++;
			sum += out[0];
		} else { fprintf(stderr, "unknown mode %s\n", mode); return 2; }
		double t1 = now();
		if (gputime) glQueryCounter(qs[1], GL_TIMESTAMP);
		glFinish();
		double t2 = now();
		if (f >= 0) { tc[f] = t1 - t0; tt[f] = t2 - t0; }
		if (gputime) {
			GLuint64 a, b;
			glGetQueryObjectui64v(qs[0], GL_QUERY_RESULT, &a);
			glGetQueryObjectui64v(qs[1], GL_QUERY_RESULT, &b);
			if (f >= 0) tg[f] = (b - a) * 1e-6;
		}
	}
	if (dump) { FILE *fp = fopen(dump, "wb"); if (fp) { fwrite(out, 1, bytes, fp); fclose(fp); } }
	qsort(tc, frames, sizeof(double), cmp);
	qsort(tt, frames, sizeof(double), cmp);
	qsort(tg, frames, sizeof(double), cmp);
	if (quiet)
		printf("%.3f %.3f %.3f %.3f\n", tc[frames / 2], tc[frames * 9 / 10], tt[frames / 2], tt[frames * 9 / 10]);
	else
		printf("texbench %-12s %dx%d: cpu median %.3f ms (p90 %.3f), frame median %.3f ms (p90 %.3f)%s%s%.3f ms, err 0x%x%s\n",
		       mode, W, H, tc[frames / 2], tc[frames * 9 / 10], tt[frames / 2], tt[frames * 9 / 10],
		       gputime ? ", gpu timestamps median " : "", gputime ? "" : ", -", gputime ? tg[frames / 2] : 0.0, glGetError(),
		       bad ? " DATA MISMATCH" : (!strncmp(mode, "read", 4) || !strcmp(mode, "getbuf") || !strcmp(mode, "mapread")) ? " data ok" : "");
	return sum == 0x12345678;
}

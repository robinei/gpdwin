// R4: raw GL/GLES2 upload-path experiments for the DSI underrun trigger (companion to burst.c).
// SDL only creates the window + context; all GL calls are explicit so that the Mesa path is known.
//   cc -O2 -o r4gl r4gl.c $(pkg-config --cflags --libs sdl3) -ldl
//   r4gl -a gl|es2 -i rgba8|rgba|bgra -f bgra|rgba -m sub|pbo|copy|none [-D] [-t s] [-d ms] [-s WxH]
//     -a  context API: desktop compat GL, or GLES2
//     -i  texture internal format at glTexImage2D (rgba8 = GL_RGBA8 sized, rgba = GL_RGBA unsized, bgra = GL_BGRA_EXT)
//     -f  upload format at glTexSubImage2D: GL_BGRA/GL_UNSIGNED_BYTE or GL_RGBA/GL_UNSIGNED_BYTE
//     -m  sub  = glTexSubImage2D from client memory every frame (SDL renderers do this)
//         pbo  = glTexSubImage2D from a GL_PIXEL_UNPACK_BUFFER refilled every frame (GPU reads a WC buffer)
//         pbo2 = glTexSubImage2D from a persistent+coherent GL_PIXEL_UNPACK_BUFFER (glBufferStorage: iris allocates it
//                snooped/cached-coherent on non-LLC, one BO for the whole run; the GPU reads snooped memory, no BO churn)
//         copy = glCopyTexSubImage2D from an FBO texture every frame (GPU->GPU copy, no CPU data)
//         none = no upload (control)
//     -D  draw the texture each frame (after the upload)
//     -B  draw the texture each frame BEFORE the upload, in the same batch: the texture is then referenced by the
//         unflushed batch and iris takes the staging + GPU copy path (what sway does for a busy client texture)
//     -d  ms to sleep before each frame (burst.c -d), -t seconds, -s texture size (default 853x480)
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei;
typedef unsigned int GLbitfield; typedef float GLfloat; typedef unsigned char GLboolean; typedef char GLchar;
typedef ptrdiff_t GLintptr; typedef ptrdiff_t GLsizeiptr; typedef unsigned char GLubyte;
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_STREAM_DRAW 0x88E0
#define GL_MAP_WRITE_BIT 0x0002
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FLOAT 0x1406
#define GL_TRIANGLE_STRIP 0x0005
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_TEXTURE0 0x84C0
#define GL_MAP_PERSISTENT_BIT 0x0040
#define GL_MAP_COHERENT_BIT 0x0080

static void (*p_glGenTextures)(GLsizei, GLuint *);
static void (*p_glBindTexture)(GLenum, GLuint);
static void (*p_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*p_glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
static void (*p_glCopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
static void (*p_glTexParameteri)(GLenum, GLenum, GLint);
static void (*p_glPixelStorei)(GLenum, GLint);
static void (*p_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*p_glClear)(GLbitfield);
static void (*p_glViewport)(GLint, GLint, GLsizei, GLsizei);
static GLenum (*p_glGetError)(void);
static const GLubyte *(*p_glGetString)(GLenum);
static void (*p_glGenBuffers)(GLsizei, GLuint *);
static void (*p_glBindBuffer)(GLenum, GLuint);
static void (*p_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
static void *(*p_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
static GLboolean (*p_glUnmapBuffer)(GLenum);
static void (*p_glBufferStorage)(GLenum, GLsizeiptr, const void *, GLbitfield);
static void (*p_glGenFramebuffers)(GLsizei, GLuint *);
static void (*p_glBindFramebuffer)(GLenum, GLuint);
static void (*p_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static GLenum (*p_glCheckFramebufferStatus)(GLenum);
static GLuint (*p_glCreateShader)(GLenum);
static void (*p_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*p_glCompileShader)(GLuint);
static void (*p_glGetShaderiv)(GLuint, GLenum, GLint *);
static GLuint (*p_glCreateProgram)(void);
static void (*p_glAttachShader)(GLuint, GLuint);
static void (*p_glLinkProgram)(GLuint);
static void (*p_glGetProgramiv)(GLuint, GLenum, GLint *);
static void (*p_glUseProgram)(GLuint);
static GLint (*p_glGetAttribLocation)(GLuint, const GLchar *);
static GLint (*p_glGetUniformLocation)(GLuint, const GLchar *);
static void (*p_glUniform1i)(GLint, GLint);
static void (*p_glEnableVertexAttribArray)(GLuint);
static void (*p_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
static void (*p_glDrawArrays)(GLenum, GLint, GLsizei);
static void (*p_glActiveTexture)(GLenum);

#define LOAD(n) do { *(void **)&p_##n = SDL_GL_GetProcAddress(#n); if (!p_##n) { fprintf(stderr, "missing %s\n", #n); return 1; } } while (0)

static int check(const char *what)
{
	GLenum e = p_glGetError();
	if (e) { fprintf(stderr, "GL error 0x%x after %s\n", e, what); return 1; }
	return 0;
}

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = p_glCreateShader(type);
	p_glShaderSource(s, 1, &src, NULL);
	p_glCompileShader(s);
	GLint ok = 0;
	p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { fprintf(stderr, "shader compile failed:\n%s\n", src); exit(1); }
	return s;
}

int main(int argc, char **argv)
{
	const char *api = "gl", *ifmt = "rgba8", *ufmt = "bgra", *mode = "sub";
	int secs = 120, w = 853, h = 480, draw = 0, before = 0;
	double delay_ms = 10;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-a") && i + 1 < argc) api = argv[++i];
		else if (!strcmp(argv[i], "-i") && i + 1 < argc) ifmt = argv[++i];
		else if (!strcmp(argv[i], "-f") && i + 1 < argc) ufmt = argv[++i];
		else if (!strcmp(argv[i], "-m") && i + 1 < argc) mode = argv[++i];
		else if (!strcmp(argv[i], "-D")) draw = 1;
		else if (!strcmp(argv[i], "-B")) { draw = 1; before = 1; }
		else if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) delay_ms = atof(argv[++i]);
		else if (!strcmp(argv[i], "-s") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
		else { fprintf(stderr, "usage: see the comment at the top\n"); return 1; }
	}
	int es = !strcmp(api, "es2");
	GLint internal = !strcmp(ifmt, "rgba8") ? GL_RGBA8 : !strcmp(ifmt, "rgba") ? GL_RGBA : GL_BGRA;
	GLenum format = !strcmp(ufmt, "bgra") ? GL_BGRA : GL_RGBA;
	int m_sub = !strcmp(mode, "sub"), m_pbo = !strcmp(mode, "pbo"), m_copy = !strcmp(mode, "copy"), m_pbo2 = !strcmp(mode, "pbo2");

	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	if (es) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	} else {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	}
	SDL_Window *win = SDL_CreateWindow("r4gl", 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
	SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : NULL;
	if (!ctx) { fprintf(stderr, "window/context: %s\n", SDL_GetError()); return 1; }
	SDL_GL_SetSwapInterval(1);

	LOAD(glGenTextures); LOAD(glBindTexture); LOAD(glTexImage2D); LOAD(glTexSubImage2D); LOAD(glCopyTexSubImage2D);
	LOAD(glTexParameteri); LOAD(glPixelStorei); LOAD(glClearColor); LOAD(glClear); LOAD(glViewport); LOAD(glGetError);
	LOAD(glGetString); LOAD(glGenBuffers); LOAD(glBindBuffer); LOAD(glBufferData); LOAD(glGenFramebuffers);
	LOAD(glBindFramebuffer); LOAD(glFramebufferTexture2D); LOAD(glCheckFramebufferStatus); LOAD(glCreateShader);
	LOAD(glShaderSource); LOAD(glCompileShader); LOAD(glGetShaderiv); LOAD(glCreateProgram); LOAD(glAttachShader);
	LOAD(glLinkProgram); LOAD(glGetProgramiv); LOAD(glUseProgram); LOAD(glGetAttribLocation); LOAD(glGetUniformLocation);
	LOAD(glUniform1i); LOAD(glEnableVertexAttribArray); LOAD(glVertexAttribPointer); LOAD(glDrawArrays); LOAD(glActiveTexture);
	if (m_pbo || m_pbo2) { LOAD(glMapBufferRange); LOAD(glUnmapBuffer); }
	if (m_pbo2) LOAD(glBufferStorage);

	printf("api %s: %s / %s\n", api, p_glGetString(GL_VERSION), p_glGetString(GL_RENDERER));
	printf("internal %s upload %s mode %s draw %d before %d %dx%d delay %.1f ms %d s\n", ifmt, ufmt, mode, draw, before, w, h, delay_ms, secs);
	fflush(stdout);

	int ww, wh;
	SDL_GetWindowSizeInPixels(win, &ww, &wh);
	p_glViewport(0, 0, ww, wh);

	size_t size = (size_t)w * h * 4;
	unsigned *px = malloc(size);
	for (int i = 0; i < w * h; i++) px[i] = 0x203040 + (i % w) * 0x010101 % 0x808080;

	GLuint tex, tex2 = 0, fbo = 0, pbo = 0;
	p_glGenTextures(1, &tex);
	p_glBindTexture(GL_TEXTURE_2D, tex);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	p_glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, NULL);
	if (check("glTexImage2D")) return 1;
	p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (!es) p_glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
	p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, px);
	if (check("initial glTexSubImage2D")) return 1;

	if (m_copy) {
		p_glGenTextures(1, &tex2);
		p_glBindTexture(GL_TEXTURE_2D, tex2);
		p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		p_glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, px);
		p_glGenFramebuffers(1, &fbo);
		p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex2, 0);
		if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo incomplete\n"); return 1; }
		p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		p_glBindTexture(GL_TEXTURE_2D, tex);
		if (check("fbo setup")) return 1;
	}
	void *pbo2_ptr = NULL;
	if (m_pbo) {
		p_glGenBuffers(1, &pbo);
		if (check("pbo setup")) return 1;
	}
	if (m_pbo2) {
		p_glGenBuffers(1, &pbo);
		p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
		p_glBufferStorage(GL_PIXEL_UNPACK_BUFFER, size, NULL, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
		pbo2_ptr = p_glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
		p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		if (!pbo2_ptr || check("pbo2 setup")) { fprintf(stderr, "pbo2 setup failed\n"); return 1; }
	}

	GLuint prog = 0;
	GLint a_pos = -1;
	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	if (draw) {
		const char *vs = "attribute vec2 p; varying vec2 t; void main() { t = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }";
		const char *fs_es = "precision mediump float; varying vec2 t; uniform sampler2D s; void main() { gl_FragColor = texture2D(s, t); }";
		const char *fs_gl = "varying vec2 t; uniform sampler2D s; void main() { gl_FragColor = texture2D(s, t); }";
		prog = p_glCreateProgram();
		p_glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
		p_glAttachShader(prog, shader(GL_FRAGMENT_SHADER, es ? fs_es : fs_gl));
		p_glLinkProgram(prog);
		GLint ok = 0;
		p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
		if (!ok) { fprintf(stderr, "link failed\n"); return 1; }
		p_glUseProgram(prog);
		a_pos = p_glGetAttribLocation(prog, "p");
		p_glUniform1i(p_glGetUniformLocation(prog, "s"), 0);
		p_glEnableVertexAttribArray(a_pos);
		p_glVertexAttribPointer(a_pos, 2, GL_FLOAT, 0, 0, quad);
		p_glActiveTexture(GL_TEXTURE0);
		if (check("shader setup")) return 1;
	}

	Uint64 end = SDL_GetTicks() + (Uint64)secs * 1000, frames = 0, t0 = SDL_GetTicks(), t1 = t0;
	while (SDL_GetTicks() < end) {
		SDL_Event e;
		while (SDL_PollEvent(&e))
			if (e.type == SDL_EVENT_QUIT) end = 0;
		if (delay_ms > 0)
			SDL_DelayPrecise((Uint64)(delay_ms * 1e6));
		// change a band of rows like a scrolling game view
		int band = (int)(frames % h);
		for (int y = band; y < band + 8 && y < h; y++)
			for (int x = 0; x < w; x++)
				px[y * w + x] ^= 0x00ffffff;
		p_glBindTexture(GL_TEXTURE_2D, tex);
		if (before) {
			p_glClearColor(0, 0, 0, 1);
			p_glClear(GL_COLOR_BUFFER_BIT);
			p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		if (m_sub) {
			p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, px);
		} else if (m_pbo) {
			p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
			p_glBufferData(GL_PIXEL_UNPACK_BUFFER, size, NULL, GL_STREAM_DRAW);
			void *ptr = p_glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
			if (!ptr) { fprintf(stderr, "map failed\n"); return 1; }
			memcpy(ptr, px, size);
			p_glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
			p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, NULL);
			p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		} else if (m_pbo2) {
			memcpy(pbo2_ptr, px, size);
			p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
			p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, NULL);
			p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		} else if (m_copy) {
			p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			p_glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
			p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		p_glClearColor(0, 0, 0, 1);
		p_glClear(GL_COLOR_BUFFER_BIT);
		if (draw && !before)
			p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		SDL_GL_SwapWindow(win);
		frames++;
		if (frames == 2 && check("first frames")) return 1;
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

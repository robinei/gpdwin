// wlroots-like compositor loop (R7): what sway does on the GPD every frame, as its own program so that a
// test Mesa can be used (sway itself runs the installed Mesa). GLES2 on a GBM device (no window):
//  - the output buffer is a GBM BO like wlroots' swapchain (SCANOUT|RENDERING, 720x1280 XRGB8888, two of
//    them alternating), imported as a dmabuf EGLImage into a renderbuffer FBO;
//  - the "game window" is a 1280x720 client buffer: a dmabuf (GBM BO, like an EGL client: SDL opengl,
//    RetroArch) or a wl_shm-style texture updated with glTexSubImage2D(GL_BGRA_EXT) (shelf);
//  - per frame: clear, draw the window rotated 90 degrees to fill the portrait output (wlroots transform
//    matrix, linear filtering, blending off for an opaque window), a bar texture on top (blended), glFlush;
//    then an EGL native fence like wlroots' explicit sync, or glFinish (-f) to time the GPU too.
// Prints median/p90 of the CPU time to submit a frame and (with -f) the frame's total time.
//   cc -O2 -o composite composite.c -lEGL -lGLESv2 -lgbm
//   ./composite [-c dmabuf|shm] [-m linear|x|y] (client buffer tiling for dmabuf) [-o linear|x] (output tiling)
//               [-d N] (shm: damage N rows per frame, default all) [-n frames] [-f] [-b] (no bar)
//               [-u] (shm: upload through a PBO instead) [-q] (quiet) [-C] (no clear)
//               [-w ms] (sleep this long after each frame, e.g. 15 = a 60 Hz compositor) [-p N] (pipelined: wait for the fence of the frame N frames ago (N=1: the previous frame), so
//                    the GPU never idles and the i915 RPS governor raises the clock; also prints the throughput
//                    = whole run wall time / frames, with a glFinish at the end)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#include <libdrm/drm_fourcc.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define OW 720
#define OH 1280
#define CW 1280
#define CH 720
#define BARH 24

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
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_;
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC glEGLImageTargetRenderbufferStorageOES_;
static PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR_;
static PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR_;
static PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID_;
static PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR_;

static uint64_t mod_of(const char *s)
{
	if (!s || !strcmp(s, "any")) return DRM_FORMAT_MOD_INVALID;
	if (!strcmp(s, "linear")) return DRM_FORMAT_MOD_LINEAR;
	if (!strcmp(s, "x")) return I915_FORMAT_MOD_X_TILED;
	if (!strcmp(s, "y")) return I915_FORMAT_MOD_Y_TILED;
	fprintf(stderr, "bad modifier %s\n", s);
	exit(2);
}

static struct gbm_bo *make_bo(struct gbm_device *gbm, int w, int h, uint32_t use, uint64_t mod)
{
	struct gbm_bo *bo;
	if (mod == DRM_FORMAT_MOD_INVALID)
		bo = gbm_bo_create(gbm, w, h, GBM_FORMAT_XRGB8888, use);
	else
		bo = gbm_bo_create_with_modifiers2(gbm, w, h, GBM_FORMAT_XRGB8888, &mod, 1, use);
	if (!bo) { fprintf(stderr, "gbm_bo_create failed (use %x mod %llx)\n", use, (unsigned long long)mod); exit(2); }
	return bo;
}

static EGLImageKHR import_bo(EGLDisplay d, struct gbm_bo *bo)
{
	int fd = gbm_bo_get_fd(bo);
	uint64_t mod = gbm_bo_get_modifier(bo);
	EGLint attrs[32], i = 0;
	attrs[i++] = EGL_WIDTH; attrs[i++] = gbm_bo_get_width(bo);
	attrs[i++] = EGL_HEIGHT; attrs[i++] = gbm_bo_get_height(bo);
	attrs[i++] = EGL_LINUX_DRM_FOURCC_EXT; attrs[i++] = DRM_FORMAT_XRGB8888;
	attrs[i++] = EGL_DMA_BUF_PLANE0_FD_EXT; attrs[i++] = fd;
	attrs[i++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT; attrs[i++] = gbm_bo_get_offset(bo, 0);
	attrs[i++] = EGL_DMA_BUF_PLANE0_PITCH_EXT; attrs[i++] = gbm_bo_get_stride(bo);
	if (mod != DRM_FORMAT_MOD_INVALID) {
		attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT; attrs[i++] = mod & 0xffffffff;
		attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT; attrs[i++] = mod >> 32;
	}
	attrs[i++] = EGL_NONE;
	EGLImageKHR img = eglCreateImageKHR_(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
	close(fd);
	if (img == EGL_NO_IMAGE_KHR) { fprintf(stderr, "eglCreateImage failed 0x%x\n", eglGetError()); exit(2); }
	return img;
}

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); fprintf(stderr, "shader: %s\n", log); exit(2); }
	return s;
}

// wlroots gles2 texture shader (RGBA variant, alpha uniform), 3x3 matrix like wlr_matrix
static const char *vs =
	"uniform mat3 proj; attribute vec2 pos; attribute vec2 texcoord; varying vec2 v_texcoord;\n"
	"void main() { gl_Position = vec4(proj * vec3(pos, 1.0), 1.0); v_texcoord = texcoord; }\n";
static const char *fs =
	"precision mediump float; varying vec2 v_texcoord; uniform sampler2D tex; uniform float alpha;\n"
	"void main() { gl_FragColor = texture2D(tex, v_texcoord) * alpha; }\n";

// wlr_matrix: projection for a 720x1280 output rotated 90 (WL_OUTPUT_TRANSFORM_90): the logical output
// is 1280x720; a box in logical coordinates (x,y,w,h) maps to the physical buffer with the rotation.
static void box_matrix(float *m, float x, float y, float w, float h)
{
	// logical (1280x720) -> physical (720x1280) rotated 90 -> NDC. Vertex pos in [0,1]^2 of the box.
	// phys_x = OW - (ly), phys_y = lx  (one of the two 90-degree rotations; fine for the work done)
	// NDC: X = phys_x / OW * 2 - 1, Y = 1 - phys_y / OH * 2  (y flipped like wlroots' projection)
	float lx0 = x, ly0 = y;
	// pos (u,v) -> logical (lx0 + u*w, ly0 + v*h) -> phys (OW - ly, lx) -> NDC
	// X = (OW - ly0 - v*h) / OW * 2 - 1 = (1 - 2*ly0/OW) - v * 2h/OW
	// Y = 1 - (lx0 + u*w) / OH * 2 = (1 - 2*lx0/OH) - u * 2w/OH
	// column-major (glUniformMatrix3fv transpose=GL_FALSE): m[col*3 + row]; row X = (0, -2h/OW, c), row Y = (-2w/OH, 0, c)
	m[0] = 0;                m[1] = -2 * w / OH;      m[2] = 0;   // column u
	m[3] = -2 * h / OW;      m[4] = 0;                m[5] = 0;   // column v
	m[6] = 1 - 2 * ly0 / OW; m[7] = 1 - 2 * lx0 / OH; m[8] = 1;   // column 1
}

int main(int argc, char **argv)
{
	const char *client = "dmabuf", *cmod = "any", *omod = "any";
	int frames = 300, finish = 0, bar = 1, damage = CH, pbo = 0, quiet = 0, noclear = 0, pipelined = 0, sleepms = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-c") && i + 1 < argc) client = argv[++i];
		else if (!strcmp(argv[i], "-m") && i + 1 < argc) cmod = argv[++i];
		else if (!strcmp(argv[i], "-o") && i + 1 < argc) omod = argv[++i];
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) damage = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-n") && i + 1 < argc) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f")) finish = 1;
		else if (!strcmp(argv[i], "-b")) bar = 0;
		else if (!strcmp(argv[i], "-u")) pbo = 1;
		else if (!strcmp(argv[i], "-q")) quiet = 1;
		else if (!strcmp(argv[i], "-C")) noclear = 1;
		else if (!strcmp(argv[i], "-w") && i + 1 < argc) sleepms = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p") && i + 1 < argc) pipelined = atoi(argv[++i]);
		else { fprintf(stderr, "usage: see source\n"); return 2; }
	}
	int shm = !strcmp(client, "shm");

	int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("renderD128"); return 2; }
	struct gbm_device *gbm = gbm_create_device(fd);
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_GBM_MESA, gbm, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 2; }
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
		fprintf(stderr, "context failed 0x%x\n", eglGetError()); return 2;
	}
	eglCreateImageKHR_ = (void *)eglGetProcAddress("eglCreateImageKHR");
	eglDestroyImageKHR_ = (void *)eglGetProcAddress("eglDestroyImageKHR");
	glEGLImageTargetTexture2DOES_ = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	glEGLImageTargetRenderbufferStorageOES_ = (void *)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
	eglCreateSyncKHR_ = (void *)eglGetProcAddress("eglCreateSyncKHR");
	eglDestroySyncKHR_ = (void *)eglGetProcAddress("eglDestroySyncKHR");
	eglDupNativeFenceFDANDROID_ = (void *)eglGetProcAddress("eglDupNativeFenceFDANDROID");
	eglClientWaitSyncKHR_ = (void *)eglGetProcAddress("eglClientWaitSyncKHR");

	// output swapchain: 2 BOs like wlroots
	GLuint fbo[2], rb[2];
	struct gbm_bo *obo[2];
	for (int i = 0; i < 2; i++) {
		obo[i] = make_bo(gbm, OW, OH, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING, mod_of(omod));
		EGLImageKHR img = import_bo(d, obo[i]);
		glGenRenderbuffers(1, &rb[i]);
		glBindRenderbuffer(GL_RENDERBUFFER, rb[i]);
		glEGLImageTargetRenderbufferStorageOES_(GL_RENDERBUFFER, img);
		glGenFramebuffers(1, &fbo[i]);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[i]);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo incomplete\n"); return 2; }
	}
	// client window texture
	GLuint tex, bartex, pbuf = 0;
	struct gbm_bo *cbo = NULL;
	unsigned char *pixels = malloc(CW * CH * 4);
	for (int i = 0; i < CW * CH * 4; i++) pixels[i] = (unsigned char)(i * 7);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	if (shm) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, CW, CH, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
		if (pbo) {
			glGenBuffers(1, &pbuf);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbuf);
			glBufferData(GL_PIXEL_UNPACK_BUFFER, CW * CH * 4, NULL, GL_STREAM_DRAW);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
	} else {
		cbo = make_bo(gbm, CW, CH, GBM_BO_USE_RENDERING, mod_of(cmod));
		// fill the client buffer with a known colour (0x40 per byte) so the result can be checked
		uint32_t stride; void *mapdata;
		void *m = gbm_bo_map(cbo, 0, 0, CW, CH, GBM_BO_TRANSFER_WRITE, &stride, &mapdata);
		if (m) { for (int y = 0; y < CH; y++) memset((char *)m + y * stride, 0x40, CW * 4); gbm_bo_unmap(cbo, mapdata); }
		glEGLImageTargetTexture2DOES_(GL_TEXTURE_2D, import_bo(d, cbo));
		if (!m) {
			// gbm_bo_map is not available for this BO: fill it through GL instead (render to it once)
			GLuint tf, tr;
			glGenFramebuffers(1, &tf); glBindFramebuffer(GL_FRAMEBUFFER, tf);
			glGenRenderbuffers(1, &tr); glBindRenderbuffer(GL_RENDERBUFFER, tr);
			glEGLImageTargetRenderbufferStorageOES_(GL_RENDERBUFFER, import_bo(d, cbo));
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, tr);
			glClearColor(0.25f, 0.25f, 0.25f, 1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
			glDeleteFramebuffers(1, &tf); glDeleteRenderbuffers(1, &tr);
		}
	}
	// GPU time per frame from EXT_disjoint_timer_query when available
	PFNGLGENQUERIESEXTPROC glGenQueriesEXT_ = (void *)eglGetProcAddress("glGenQueriesEXT");
	PFNGLQUERYCOUNTEREXTPROC glQueryCounterEXT_ = (void *)eglGetProcAddress("glQueryCounterEXT");
	PFNGLGETQUERYOBJECTUI64VEXTPROC glGetQueryObjectui64vEXT_ = (void *)eglGetProcAddress("glGetQueryObjectui64vEXT");
	int timer = strstr((const char *)glGetString(GL_EXTENSIONS), "GL_EXT_disjoint_timer_query") != NULL && glQueryCounterEXT_ && !shm;
	GLuint tq[2] = { 0, 0 };
	if (timer) glGenQueriesEXT_(2, tq);
	double gpu_sum = 0; int gpu_n = 0;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenTextures(1, &bartex);
	glBindTexture(GL_TEXTURE_2D, bartex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, CW, BARH, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	GLuint p = glCreateProgram();
	glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(p, 0, "pos");
	glBindAttribLocation(p, 1, "texcoord");
	glLinkProgram(p);
	glUseProgram(p);
	GLint uproj = glGetUniformLocation(p, "proj"), ualpha = glGetUniformLocation(p, "alpha");
	glUniform1i(glGetUniformLocation(p, "tex"), 0);
	static const float verts[8] = { 1, 0, 0, 0, 1, 1, 0, 1 };
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
	float mwin[9], mbar[9];
	box_matrix(mwin, 0, 0, CW, CH);
	box_matrix(mbar, 0, 0, CW, BARH);

	double *tc = malloc(sizeof(double) * frames), *tt = malloc(sizeof(double) * frames);
	int shmmode = 0;
	EGLSyncKHR prev_sync[16] = { EGL_NO_SYNC_KHR };
	if (pipelined > 16) pipelined = 16;
	double run0 = 0;
	for (int f = -20; f < frames; f++) {
		int i = f & 1;
		double t0 = now();
		if (f == 0) run0 = t0;
		if (shm) {
			// client commit: wlroots updates the texture from the shm buffer (damage rows)
			pixels[(f & 1023) * 16] ^= 0xff;
			glBindTexture(GL_TEXTURE_2D, tex);
			glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, CW);
			if (pbo) {
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbuf);
				glBufferData(GL_PIXEL_UNPACK_BUFFER, CW * damage * 4, NULL, GL_STREAM_DRAW);
				glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, CW * damage * 4, pixels);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CW, damage, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0);
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
			} else {
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CW, damage, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
			}
			shmmode = 1;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
		if (timer && f >= 0 && f % 50 == 0) glQueryCounterEXT_(tq[0], GL_TIMESTAMP_EXT);
		glViewport(0, 0, OW, OH);
		glDisable(GL_SCISSOR_TEST);
		if (!noclear) {
			glClearColor(0.1f, 0.1f, 0.1f, 1);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		glDisable(GL_BLEND);
		glBindTexture(GL_TEXTURE_2D, tex);
		glUniformMatrix3fv(uproj, 1, GL_FALSE, mwin);
		glUniform1f(ualpha, 1);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		if (bar) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glBindTexture(GL_TEXTURE_2D, bartex);
			glUniformMatrix3fv(uproj, 1, GL_FALSE, mbar);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		if (timer && f >= 0 && f % 50 == 0) glQueryCounterEXT_(tq[1], GL_TIMESTAMP_EXT);
		glFlush();
		EGLSyncKHR sync = EGL_NO_SYNC_KHR;
		int sfd = -1;
		if (eglCreateSyncKHR_) {
			sync = eglCreateSyncKHR_(d, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
			if (sync != EGL_NO_SYNC_KHR)
				sfd = eglDupNativeFenceFDANDROID_(d, sync);
		}
		double t1 = now();
		if (sfd >= 0) close(sfd);
		if (pipelined) {
			// wait for the frame before the previous one (2 frames in flight, like a compositor with 2 buffers)
			int slot = (f + 20) % pipelined;
			EGLSyncKHR old = prev_sync[slot];
			if (old != EGL_NO_SYNC_KHR) {
				if (eglClientWaitSyncKHR_(d, old, 0, EGL_FOREVER_KHR) != EGL_CONDITION_SATISFIED_KHR) fprintf(stderr, "wait failed\n");
				eglDestroySyncKHR_(d, old);
			}
			prev_sync[slot] = sync;
		} else if (finish) {
			glFinish();
		} else if (sync != EGL_NO_SYNC_KHR) {
			eglClientWaitSyncKHR_(d, sync, 0, EGL_FOREVER_KHR); // the page flip of the previous frame has completed by now
		}
		double t2 = now();
		if (!pipelined && sync != EGL_NO_SYNC_KHR) eglDestroySyncKHR_(d, sync);
		if (f >= 0) { tc[f] = t1 - t0; tt[f] = t2 - t0; }
		if (sleepms) usleep(sleepms * 1000);
		if (timer && f >= 0 && f % 50 == 0) {
			GLuint64 a = 0, b = 0;
			glGetQueryObjectui64vEXT_(tq[0], GL_QUERY_RESULT_EXT, &a);
			glGetQueryObjectui64vEXT_(tq[1], GL_QUERY_RESULT_EXT, &b);
			gpu_sum += (b - a) * 1e-6; gpu_n++;
		}
	}
	glFinish();
	// result check: the window covers the whole output, so the output must be the client colour (0x40 bytes)
	unsigned check = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, fbo[(frames - 1) & 1]);
	glReadPixels(OW / 2, OH / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &check);
	double run = (now() - run0) / frames;
	qsort(tc, frames, sizeof(double), cmp);
	qsort(tt, frames, sizeof(double), cmp);
	if (pipelined)
		printf("throughput %.3f ms/frame (%d in flight, %s%s%s) | ", run, pipelined, noclear ? "no clear" : "clear", bar ? "+bar" : "", shm ? ", shm" : "");
	if (timer && gpu_n)
		printf("gpu %.3f ms/frame (timestamps, %d samples) | ", gpu_sum / gpu_n, gpu_n);
	printf("centre pixel %08x | ", check);
	if (!quiet)
		printf("composite %s%s%s out=%s: cpu median %.3f ms (p90 %.3f, max %.3f), frame median %.3f ms (p90 %.3f) [%s], err 0x%x\n",
		       client, shmmode ? "" : " mod=", shmmode ? (pbo ? " pbo" : "") : cmod, omod, tc[frames / 2], tc[frames * 9 / 10], tc[frames - 1],
		       tt[frames / 2], tt[frames * 9 / 10], pipelined ? "pipelined" : finish ? "glFinish" : "fence wait", glGetError());
	else
		printf("%.3f %.3f %.3f %.3f\n", tc[frames / 2], tc[frames * 9 / 10], tt[frames / 2], tt[frames * 9 / 10]);
	return 0;
}

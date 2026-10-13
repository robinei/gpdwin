// LD_PRELOAD shim (R7): logs i915 GEM ioctls with their duration and the GEM_CREATE sizes, to see what a GL
// program allocates and which ioctls are slow.  cc -O2 -shared -fPIC -o ioctlshim.so ioctlshim.c -ldl
//   IOCTLSHIM_ALL=1 logs every ioctl; otherwise only GEM_CREATE, SET_DOMAIN, SET_TILING, SET_CACHING, MMAP_OFFSET,
//   MMAP_GTT and anything slower than 1 ms. Output: stderr.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <libdrm/drm.h>
#include <libdrm/i915_drm.h>

static int (*real_ioctl)(int, unsigned long, ...);
static double t0;
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6; }
static const char *name(unsigned long req)
{
	switch (req) {
	case DRM_IOCTL_I915_GEM_CREATE: return "CREATE";
	case DRM_IOCTL_I915_GEM_SET_DOMAIN: return "SET_DOMAIN";
	case DRM_IOCTL_I915_GEM_SET_TILING: return "SET_TILING";
	case DRM_IOCTL_I915_GEM_SET_CACHING: return "SET_CACHING";
	case DRM_IOCTL_I915_GEM_MMAP_OFFSET: return "MMAP_OFFSET";
	case DRM_IOCTL_I915_GEM_MMAP_GTT: return "MMAP_GTT";
	case DRM_IOCTL_I915_GEM_EXECBUFFER2: return "EXECBUF";
	case DRM_IOCTL_I915_GEM_EXECBUFFER2_WR: return "EXECBUF_WR";
	case DRM_IOCTL_I915_GEM_MADVISE: return "MADVISE";
	case DRM_IOCTL_I915_GEM_BUSY: return "BUSY";
	case DRM_IOCTL_I915_GEM_WAIT: return "WAIT";
	case DRM_IOCTL_GEM_CLOSE: return "GEM_CLOSE";
	case DRM_IOCTL_SYNCOBJ_WAIT: return "SYNCOBJ_WAIT";
	case DRM_IOCTL_SYNCOBJ_CREATE: return "SYNCOBJ_CREATE";
	case DRM_IOCTL_SYNCOBJ_DESTROY: return "SYNCOBJ_DESTROY";
	case DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT: return "CTX_CREATE";
	case DRM_IOCTL_I915_GETPARAM: return "GETPARAM";
	default: return NULL;
	}
}
int ioctl(int fd, unsigned long req, ...)
{
	va_list ap; va_start(ap, req); void *arg = va_arg(ap, void *); va_end(ap);
	if (!real_ioctl) { real_ioctl = dlsym(RTLD_NEXT, "ioctl"); t0 = now(); }
	double a = now();
	int r = real_ioctl(fd, req, arg);
	double b = now();
	const char *n = name(req);
	static int all = -1;
	if (all < 0) all = getenv("IOCTLSHIM_ALL") != NULL;
	int interesting = req == DRM_IOCTL_I915_GEM_CREATE || req == DRM_IOCTL_I915_GEM_SET_DOMAIN || req == DRM_IOCTL_I915_GEM_SET_TILING ||
		req == DRM_IOCTL_I915_GEM_SET_CACHING || req == DRM_IOCTL_I915_GEM_MMAP_OFFSET || req == DRM_IOCTL_I915_GEM_MMAP_GTT;
	if (all || interesting || b - a > 1.0) {
		char extra[64] = "";
		if (req == DRM_IOCTL_I915_GEM_CREATE) { struct drm_i915_gem_create *c = arg; snprintf(extra, sizeof extra, " size=%llu handle=%u", (unsigned long long)c->size, c->handle); }
		if (req == DRM_IOCTL_I915_GEM_SET_DOMAIN) { struct drm_i915_gem_set_domain *s = arg; snprintf(extra, sizeof extra, " handle=%u rd=%x wr=%x", s->handle, s->read_domains, s->write_domain); }
		if (req == DRM_IOCTL_I915_GEM_SET_TILING) { struct drm_i915_gem_set_tiling *s = arg; snprintf(extra, sizeof extra, " handle=%u tiling=%u", s->handle, s->tiling_mode); }
		if (req == DRM_IOCTL_I915_GEM_MMAP_OFFSET) { struct drm_i915_gem_mmap_offset *s = arg; snprintf(extra, sizeof extra, " handle=%u flags=%llx", s->handle, (unsigned long long)s->flags); }
		if (req == DRM_IOCTL_GEM_CLOSE) { struct drm_gem_close *s = arg; snprintf(extra, sizeof extra, " handle=%u", s->handle); }
		if (n) fprintf(stderr, "[%8.3f] %-14s %7.3f ms%s\n", a - t0, n, b - a, extra);
		else fprintf(stderr, "[%8.3f] ioctl %lx       %7.3f ms\n", a - t0, req, b - a);
	}
	return r;
}

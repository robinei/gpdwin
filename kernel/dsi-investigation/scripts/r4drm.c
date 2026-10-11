// R4: raw i915 GEM test of the staging-buffer mechanism, no Mesa involved.
// Each "frame": optionally create a fresh source BO (as iris does for a staging buffer: GEM_CREATE,
// SET_DOMAIN(CPU), SET_CACHING(CACHED) = snooped, MMAP_OFFSET), optionally write it from the CPU through the
// mmap, submit a batch on the BLT engine that copies it to a destination BO (XY_SRC_COPY_BLT, gen8 form) or an
// empty batch, then optionally close it.  Runs headless; the display is not touched.
//   cc -O2 -o r4drm r4drm.c
//   r4drm [-s WxH] [-t s] [-p ms] [-S] [-F] [-W] [-R] [-N] [-E rcs|bcs]
//     -S  snooped source (I915_GEM_SET_CACHING CACHED, WB mmap); default uncached (WC mmap)
//     -F  fresh source BO every frame (create ... close); default one persistent BO
//     -W  CPU writes the whole source every frame (memcpy from a cached buffer)
//     -R  the batch reads the source (blit source -> destination); default empty batch
//     -N  do not even list the source in the execbuf (only with no -R)
//     -E  engine for the empty batch (bcs default); the blit always runs on bcs
//     -p  frame period in ms (default 16.6), -t seconds (default 120), -s size (default 853x480)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/i915_drm.h>

static int fd;

static int xioctl(unsigned long req, void *arg)
{
	int r;
	do r = ioctl(fd, req, arg); while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

static uint32_t gem_create(uint64_t size)
{
	struct drm_i915_gem_create c = { .size = size };
	if (xioctl(DRM_IOCTL_I915_GEM_CREATE, &c)) { perror("GEM_CREATE"); exit(1); }
	return c.handle;
}

static void gem_close(uint32_t h)
{
	struct drm_gem_close c = { .handle = h };
	xioctl(DRM_IOCTL_GEM_CLOSE, &c);
}

static void gem_set_domain_cpu(uint32_t h)
{
	struct drm_i915_gem_set_domain sd = { .handle = h, .read_domains = I915_GEM_DOMAIN_CPU, .write_domain = 0 };
	if (xioctl(DRM_IOCTL_I915_GEM_SET_DOMAIN, &sd)) { perror("SET_DOMAIN"); exit(1); }
}

static void gem_set_caching(uint32_t h, int cached)
{
	struct drm_i915_gem_caching c = { .handle = h, .caching = cached ? I915_CACHING_CACHED : I915_CACHING_NONE };
	if (xioctl(DRM_IOCTL_I915_GEM_SET_CACHING, &c)) { perror("SET_CACHING"); exit(1); }
}

static void *gem_mmap(uint32_t h, uint64_t size, int wb)
{
	struct drm_i915_gem_mmap_offset mo = { .handle = h, .flags = wb ? I915_MMAP_OFFSET_WB : I915_MMAP_OFFSET_WC };
	if (xioctl(DRM_IOCTL_I915_GEM_MMAP_OFFSET, &mo)) { perror("MMAP_OFFSET"); exit(1); }
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mo.offset);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	return p;
}

static double mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void sleep_until(double t)
{
	double now = mono();
	if (t > now) {
		struct timespec ts = { .tv_sec = (time_t)(t - now), .tv_nsec = (long)(((t - now) - (time_t)(t - now)) * 1e9) };
		nanosleep(&ts, NULL);
	}
}

int main(int argc, char **argv)
{
	int w = 853, h = 480, secs = 120, snoop = 0, fresh = 0, write_src = 0, read_src = 0, noref = 0;
	double period_ms = 16.6;
	unsigned engine = I915_EXEC_BLT;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-s") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
		else if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p") && i + 1 < argc) period_ms = atof(argv[++i]);
		else if (!strcmp(argv[i], "-S")) snoop = 1;
		else if (!strcmp(argv[i], "-F")) fresh = 1;
		else if (!strcmp(argv[i], "-W")) write_src = 1;
		else if (!strcmp(argv[i], "-R")) read_src = 1;
		else if (!strcmp(argv[i], "-N")) noref = 1;
		else if (!strcmp(argv[i], "-E") && i + 1 < argc) engine = !strcmp(argv[++i], "rcs") ? I915_EXEC_RENDER : I915_EXEC_BLT;
		else { fprintf(stderr, "usage: see the comment at the top\n"); return 1; }
	}
	if (read_src) engine = I915_EXEC_BLT;
	fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open renderD128"); return 1; }
	struct drm_i915_getparam gp = { .param = I915_PARAM_HAS_EXEC_SOFTPIN };
	int val = 0; gp.value = &val;
	if (xioctl(DRM_IOCTL_I915_GETPARAM, &gp) || !val) { fprintf(stderr, "no softpin\n"); return 1; }

	uint64_t pitch = (uint64_t)w * 4, size = (pitch * h + 4095) & ~4095ull;
	const uint64_t SRC_ADDR = 0x10000000, DST_ADDR = 0x20000000, BB_ADDR = 0x30000000;

	uint32_t dst = gem_create(size);
	uint32_t bb = gem_create(4096);
	uint32_t *bbmap = gem_mmap(bb, 4096, 0);
	int n = 0;
	if (read_src) {
		bbmap[n++] = (2u << 29) | (0x53u << 22) | (1u << 21) | (1u << 20) | 8;	// XY_SRC_COPY_BLT, 10 dwords (gen8)
		bbmap[n++] = (3u << 24) | (0xCCu << 16) | (uint32_t)pitch;		// 32 bpp, ROP copy, dst pitch
		bbmap[n++] = 0;								// dst x1, y1
		bbmap[n++] = ((uint32_t)h << 16) | (uint32_t)w;				// dst x2, y2
		bbmap[n++] = (uint32_t)DST_ADDR; bbmap[n++] = (uint32_t)(DST_ADDR >> 32);
		bbmap[n++] = 0;								// src x1, y1
		bbmap[n++] = (uint32_t)pitch;						// src pitch
		bbmap[n++] = (uint32_t)SRC_ADDR; bbmap[n++] = (uint32_t)(SRC_ADDR >> 32);
		bbmap[n++] = (0x26u << 23) | 2; bbmap[n++] = 0; bbmap[n++] = 0; bbmap[n++] = 0;	// MI_FLUSH_DW
	}
	bbmap[n++] = 0xAu << 23;							// MI_BATCH_BUFFER_END
	if (n & 1) bbmap[n++] = 0;
	uint32_t bb_len = n * 4;

	unsigned char *cached = malloc(size);
	for (uint64_t i = 0; i < size; i++) cached[i] = (unsigned char)(i * 7);

	uint32_t src = 0; void *srcmap = NULL;
	if (!fresh) {
		src = gem_create(size);
		gem_set_domain_cpu(src);
		if (snoop) gem_set_caching(src, 1);
		srcmap = gem_mmap(src, size, snoop);
		memcpy(srcmap, cached, size);
	}
	printf("r4drm: %dx%d (%llu KB) snoop %d fresh %d write %d read %d noref %d engine %s period %.1f ms %d s\n",
	       w, h, (unsigned long long)size / 1024, snoop, fresh, write_src, read_src, noref,
	       engine == I915_EXEC_BLT ? "bcs" : "rcs", period_ms, secs);
	fflush(stdout);

	double start = mono(), next = start, t_create = 0, t_exec = 0;
	unsigned long frames = 0;
	while (mono() < start + secs) {
		next += period_ms * 1e-3;
		sleep_until(next);
		double t0 = mono();
		if (fresh) {
			src = gem_create(size);
			gem_set_domain_cpu(src);
			if (snoop) gem_set_caching(src, 1);
			srcmap = gem_mmap(src, size, snoop);
		}
		double t1 = mono();
		if (write_src) {
			cached[(frames * 64) % size] ^= 0xff;
			memcpy(srcmap, cached, size);
		}
		struct drm_i915_gem_exec_object2 objs[3];
		int nobj = 0;
		memset(objs, 0, sizeof(objs));
		if (read_src || !noref) {
			objs[nobj++] = (struct drm_i915_gem_exec_object2) { .handle = src, .offset = SRC_ADDR, .flags = EXEC_OBJECT_PINNED };
		}
		objs[nobj++] = (struct drm_i915_gem_exec_object2) { .handle = dst, .offset = DST_ADDR, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE };
		objs[nobj++] = (struct drm_i915_gem_exec_object2) { .handle = bb, .offset = BB_ADDR, .flags = EXEC_OBJECT_PINNED };
		struct drm_i915_gem_execbuffer2 eb = {
			.buffers_ptr = (uintptr_t)objs, .buffer_count = nobj, .batch_len = bb_len,
			.flags = engine | I915_EXEC_NO_RELOC,
		};
		double t2 = mono();
		if (xioctl(DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb)) { perror("EXECBUFFER2"); return 1; }
		double t3 = mono();
		if (fresh) {
			munmap(srcmap, size);
			gem_close(src);
		}
		t_create += t1 - t0; t_exec += t3 - t2;
		frames++;
		if (frames % 600 == 0) {
			printf("%lu frames, create %.2f ms, exec %.2f ms per frame\n", frames, t_create / frames * 1e3, t_exec / frames * 1e3);
			fflush(stdout);
		}
	}
	// check the copy once (persistent case): read back the destination
	if (!fresh && read_src) {
		struct drm_i915_gem_wait wt = { .bo_handle = dst, .timeout_ns = 1000000000 };
		xioctl(DRM_IOCTL_I915_GEM_WAIT, &wt);
		gem_set_domain_cpu(dst);
		unsigned char *d = gem_mmap(dst, size, 0);
		printf("copy check: %s\n", memcmp(d, cached, pitch * h) ? "MISMATCH" : "ok");
	}
	printf("%lu frames\n", frames);
	return 0;
}

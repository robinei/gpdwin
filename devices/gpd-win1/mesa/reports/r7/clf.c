// clflush cost on this CPU (R7): what crocus pays when it maps a non-coherent BO cached for reading
// (crocus_bo_map_cpu invalidates the whole BO).  cc -O2 -o clf clf.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6; }
int main(void)
{
	for (size_t sz = 64 << 10; sz <= (16 << 20); sz *= 4) {
		char *p = aligned_alloc(4096, sz);
		memset(p, 1, sz);
		double best = 1e9, bestc = 1e9;
		for (int r = 0; r < 5; r++) {
			memset(p, r, sz); // lines cached (dirty)
			double t = now();
			for (char *q = p; q < p + sz; q += 64) __builtin_ia32_clflush(q);
			__builtin_ia32_mfence();
			t = now() - t; if (t < best) best = t;
			t = now(); // second pass: lines not cached
			for (char *q = p; q < p + sz; q += 64) __builtin_ia32_clflush(q);
			__builtin_ia32_mfence();
			t = now() - t; if (t < bestc) bestc = t;
		}
		printf("clflush %6zu KiB: dirty-cached %.3f ms (%.1f ns/line), uncached %.3f ms (%.1f ns/line)\n", sz >> 10, best, best * 1e6 / (sz / 64), bestc, bestc * 1e6 / (sz / 64));
		free(p);
	}
	return 0;
}

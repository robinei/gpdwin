// Lists the KMS primary plane's IN_FORMATS modifiers (what wlroots can allocate scanout buffers with).
// No master needed for property reads.  cc -O2 -o planefmt planefmt.c -ldrm
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>
int main(int argc, char **argv)
{
	int fd = open(argc > 1 ? argv[1] : "/dev/dri/card1", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open"); return 1; }
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmModePlaneResPtr pr = drmModeGetPlaneResources(fd);
	if (!pr) { perror("planes"); return 1; }
	for (unsigned i = 0; i < pr->count_planes; i++) {
		drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, pr->planes[i], DRM_MODE_OBJECT_PLANE);
		if (!props) continue;
		for (unsigned j = 0; j < props->count_props; j++) {
			drmModePropertyPtr p = drmModeGetProperty(fd, props->props[j]);
			if (!p) continue;
			if (!strcmp(p->name, "type")) printf("plane %u type %llu\n", pr->planes[i], (unsigned long long)props->prop_values[j]);
			if (!strcmp(p->name, "IN_FORMATS")) {
				drmModePropertyBlobPtr b = drmModeGetPropertyBlob(fd, props->prop_values[j]);
				if (b) {
					struct drm_format_modifier_blob *fm = b->data;
					uint32_t *fmts = (uint32_t *)((char *)fm + fm->formats_offset);
					struct drm_format_modifier *mods = (struct drm_format_modifier *)((char *)fm + fm->modifiers_offset);
					printf("plane %u IN_FORMATS: %u formats, %u modifiers:", pr->planes[i], fm->count_formats, fm->count_modifiers);
					for (unsigned m = 0; m < fm->count_modifiers; m++) printf(" 0x%llx", (unsigned long long)mods[m].modifier);
					printf(" (XRGB8888 %s)\n", ({ int has = 0; for (unsigned f = 0; f < fm->count_formats; f++) if (fmts[f] == DRM_FORMAT_XRGB8888) has = 1; has ? "listed" : "not listed"; }));
					drmModeFreePropertyBlob(b);
				}
			}
			drmModeFreeProperty(p);
		}
		drmModeFreeObjectProperties(props);
	}
	return 0;
}

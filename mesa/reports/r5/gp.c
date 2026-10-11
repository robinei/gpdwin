#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <drm/i915_drm.h>
static int gp(int fd,int p,int*v){struct drm_i915_getparam g={.param=p,.value=v};return ioctl(fd,DRM_IOCTL_I915_GETPARAM,&g);}
int main(){int fd=open("/dev/dri/renderD128",O_RDWR);if(fd<0){perror("open");return 1;}
#define P(x) {int v=-1;int r=gp(fd,x,&v);printf("%-36s = %d%s\n",#x,v,r?" (err)":"");}
P(I915_PARAM_CHIPSET_ID) P(I915_PARAM_HAS_EXEC_SOFTPIN) P(I915_PARAM_HAS_ALIASING_PPGTT) P(I915_PARAM_HAS_LLC)
P(I915_PARAM_HAS_EXEC_NO_RELOC) P(I915_PARAM_HAS_EXEC_HANDLE_LUT) P(I915_PARAM_HAS_EXEC_ASYNC) P(I915_PARAM_HAS_EXEC_BATCH_FIRST)
P(I915_PARAM_HAS_EXEC_FENCE_ARRAY) P(I915_PARAM_HAS_EXEC_TIMELINE_FENCES) P(I915_PARAM_HAS_EXEC_SUBMIT_FENCE) P(I915_PARAM_MMAP_GTT_VERSION) P(I915_PARAM_CMD_PARSER_VERSION) P(I915_PARAM_HAS_SECURE_BATCHES)
P(I915_PARAM_SUBSLICE_TOTAL) P(I915_PARAM_EU_TOTAL) P(I915_PARAM_HAS_GPU_RESET)
struct drm_i915_gem_context_param cp={.ctx_id=0,.param=I915_CONTEXT_PARAM_GTT_SIZE};
int r=ioctl(fd,DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM,&cp);printf("CONTEXT_PARAM_GTT_SIZE = 0x%llx (%llu MiB)%s\n",(unsigned long long)cp.value,(unsigned long long)cp.value>>20,r?" (err)":"");
struct drm_i915_gem_get_aperture ap={0};ioctl(fd,DRM_IOCTL_I915_GEM_GET_APERTURE,&ap);printf("aperture total=%llu MiB mappable=%llu MiB\n",(unsigned long long)ap.aper_size>>20,(unsigned long long)ap.aper_available_size>>20);
return 0;}

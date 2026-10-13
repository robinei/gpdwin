/* eb.c: raw i915 execbuf microbenchmark for CHV (gen8), no root.
 * Builds a batch of R MI_STORE_DATA_IMM writes into K target BOs + MI_BATCH_BUFFER_END
 * and submits it N times, in one of these modes:
 *   softpin : EXEC_OBJECT_PINNED, userspace-chosen addresses < 4 GiB, I915_EXEC_NO_RELOC, no reloc list
 *   noreloc : relocation list + I915_EXEC_NO_RELOC with correct presumed offsets (Mesa steady state)
 *   check   : relocation list, no NO_RELOC flag, presumed offsets correct (kernel walks relocs, compares)
 *   write   : relocation list, no NO_RELOC flag, presumed_offset=-1 (kernel rewrites every reloc)
 * Verifies the stored values afterwards (so softpin correctness is checked, not just the ioctl result).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <drm/i915_drm.h>

static int fd;
static uint32_t gem_create(uint64_t size){struct drm_i915_gem_create c={.size=size};if(ioctl(fd,DRM_IOCTL_I915_GEM_CREATE,&c)){perror("create");exit(1);}return c.handle;}
static void *gem_map_wc(uint32_t h,uint64_t size){struct drm_i915_gem_mmap_offset mo={.handle=h,.flags=I915_MMAP_OFFSET_WC};if(ioctl(fd,DRM_IOCTL_I915_GEM_MMAP_OFFSET,&mo)){perror("mmap_offset");exit(1);}void*p=mmap(0,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,mo.offset);if(p==MAP_FAILED){perror("mmap");exit(1);}return p;}
static void gem_wait(uint32_t h){struct drm_i915_gem_wait w={.bo_handle=h,.timeout_ns=-1};if(ioctl(fd,DRM_IOCTL_I915_GEM_WAIT,&w))perror("wait");}
static double now(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
static double ru(struct rusage*r,int sys){struct timeval tv=sys?r->ru_stime:r->ru_utime;return tv.tv_sec+tv.tv_usec*1e-6;}
static int cmpd(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}

int main(int argc,char**argv){
  const char*mode=argc>1?argv[1]:"softpin";
  int K=argc>2?atoi(argv[2]):16;      /* target objects */
  int R=argc>3?atoi(argv[3]):64;      /* stores (= relocs) per batch */
  int N=argc>4?atoi(argv[4]):2000;    /* iterations */
  int waitevery=argc>5?atoi(argv[5]):0; /* GEM_WAIT every n submissions (0 = never) */
  int softpin=!strcmp(mode,"softpin"), noreloc=!strcmp(mode,"noreloc"), check=!strcmp(mode,"check"), wr=!strcmp(mode,"write");
  if(!(softpin||noreloc||check||wr)){fprintf(stderr,"mode?\n");return 2;}
  if(R>K*512){fprintf(stderr,"R too large for K pages\n");return 2;}
  fd=open("/dev/dri/renderD128",O_RDWR);if(fd<0){perror("open");return 1;}
  uint64_t tsize=4096, bsize=((uint64_t)R*16+8+4095)&~4095ULL;
  uint32_t *th=calloc(K,4); void**tmap=calloc(K,sizeof(void*));
  for(int i=0;i<K;i++){th[i]=gem_create(tsize);tmap[i]=gem_map_wc(th[i],tsize);memset(tmap[i],0,tsize);}
  uint32_t bh=gem_create(bsize); uint32_t*bmap=gem_map_wc(bh,bsize);
  uint64_t *addr=calloc(K,8); /* known GPU addresses (softpin: chosen; reloc: learned) */
  if(softpin){for(int i=0;i<K;i++)addr[i]=0x40000000ULL+(uint64_t)i*0x10000;}
  uint64_t baddr=0x80000000ULL;
  struct drm_i915_gem_relocation_entry *relocs=calloc(R,sizeof*relocs);
  /* write batch */
  for(int i=0;i<R;i++){int t=i%K;uint32_t delta=(i/K)*8;uint64_t a=addr[t]+delta;
    bmap[i*4+0]=(0x20<<23)|2; bmap[i*4+1]=(uint32_t)a; bmap[i*4+2]=(uint32_t)(a>>32); bmap[i*4+3]=0;
    relocs[i]=(struct drm_i915_gem_relocation_entry){.target_handle=t,.delta=delta,.offset=i*16+4,.presumed_offset=addr[t],.read_domains=I915_GEM_DOMAIN_RENDER,.write_domain=I915_GEM_DOMAIN_RENDER};}
  bmap[R*4]=0x05000000; bmap[R*4+1]=0;
  struct drm_i915_gem_exec_object2 *obj=calloc(K+1,sizeof*obj);
  for(int i=0;i<K;i++){obj[i].handle=th[i];obj[i].flags=EXEC_OBJECT_WRITE|(softpin?EXEC_OBJECT_PINNED:0);obj[i].offset=addr[i];}
  obj[K].handle=bh; obj[K].flags=softpin?EXEC_OBJECT_PINNED:0; obj[K].offset=softpin?baddr:0;
  if(!softpin){obj[K].relocation_count=R;obj[K].relocs_ptr=(uintptr_t)relocs;}
  struct drm_i915_gem_execbuffer2 eb={.buffers_ptr=(uintptr_t)obj,.buffer_count=K+1,.batch_len=R*16+8,.flags=I915_EXEC_RENDER|I915_EXEC_HANDLE_LUT};
  /* first submission: learn offsets in reloc modes (no NO_RELOC) */
  if(!softpin){ if(ioctl(fd,DRM_IOCTL_I915_GEM_EXECBUFFER2,&eb)){perror("execbuf (learn)");return 1;}
    for(int i=0;i<K;i++)addr[i]=obj[i].offset; baddr=obj[K].offset;
    for(int i=0;i<R;i++)relocs[i].presumed_offset=wr?(uint64_t)-1:addr[i%K];
    gem_wait(bh); }
  if(softpin||noreloc)eb.flags|=I915_EXEC_NO_RELOC;
  /* loop */
  double*t=malloc(N*sizeof(double)); struct rusage r0,r1; getrusage(RUSAGE_SELF,&r0); double w0=now(); int err=0;
  for(int n=0;n<N;n++){
    /* userspace work per batch: set the store value (and in write mode re-poison presumed offsets) */
    for(int i=0;i<R;i++)bmap[i*4+3]=n+1;
    if(wr)for(int i=0;i<R;i++)relocs[i].presumed_offset=(uint64_t)-1;
    double a=now(); if(ioctl(fd,DRM_IOCTL_I915_GEM_EXECBUFFER2,&eb)){err=errno;perror("execbuf");break;} t[n]=now()-a;
    if(waitevery&&(n%waitevery)==waitevery-1)gem_wait(bh);
  }
  double w1=now(); getrusage(RUSAGE_SELF,&r1); gem_wait(bh);
  /* verify */
  int bad=0; for(int i=0;i<R;i++){volatile uint32_t*p=(uint32_t*)((char*)tmap[i%K]+(i/K)*8); if(*p!=(uint32_t)N)bad++;}
  /* check offsets unchanged */
  int moved=0; for(int i=0;i<K;i++)if(obj[i].offset!=addr[i])moved++; if(obj[K].offset!=baddr)moved++;
  qsort(t,N,sizeof(double),cmpd); double sum=0;for(int i=0;i<N;i++)sum+=t[i];
  printf("mode=%s K=%d R=%d N=%d waitevery=%d err=%d bad_stores=%d moved=%d | ioctl us: mean %.1f med %.1f p90 %.1f p99 %.1f max %.1f | wall %.1f us/iter, utime %.1f us/iter, stime %.1f us/iter | bo0 @0x%llx batch @0x%llx\n",
    mode,K,R,N,waitevery,err,bad,moved,sum/N*1e6,t[N/2]*1e6,t[N*9/10]*1e6,t[N*99/100]*1e6,t[N-1]*1e6,(w1-w0)/N*1e6,(ru(&r1,0)-ru(&r0,0))/N*1e6,(ru(&r1,1)-ru(&r0,1))/N*1e6,(unsigned long long)addr[0],(unsigned long long)baddr);
  return err||bad;
}

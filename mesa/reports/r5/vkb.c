/* vkb.c: headless hasvk submission microbenchmark (no window, no swapchain).
 * Records one command buffer with R vkCmdCopyBuffer between K distinct buffers (each copy references
 * two BOs -> relocations in relocation mode) and submits it N times with a fence wait every W submits.
 * Prints per-submit wall/utime/stime.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <vulkan/vulkan.h>
#define CK(x) do{VkResult _r=(x);if(_r!=VK_SUCCESS){fprintf(stderr,"%s failed: %d\n",#x,_r);exit(1);}}while(0)
static double now(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
static double ru(struct rusage*r,int s){struct timeval tv=s?r->ru_stime:r->ru_utime;return tv.tv_sec+tv.tv_usec*1e-6;}
int main(int argc,char**argv){
  int K=argc>1?atoi(argv[1]):32, R=argc>2?atoi(argv[2]):64, N=argc>3?atoi(argv[3]):2000, W=argc>4?atoi(argv[4]):8;
  VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="vkb",.apiVersion=VK_API_VERSION_1_2};
  VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
  VkInstance inst;CK(vkCreateInstance(&ici,0,&inst));
  uint32_t n=1;VkPhysicalDevice pd;vkEnumeratePhysicalDevices(inst,&n,&pd);if(!n){fprintf(stderr,"no device\n");return 1;}
  uint32_t qn=0;vkGetPhysicalDeviceQueueFamilyProperties(pd,&qn,0);VkQueueFamilyProperties qp[8];vkGetPhysicalDeviceQueueFamilyProperties(pd,&qn,qp);uint32_t qf=0;for(uint32_t i=0;i<qn;i++)if(qp[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){qf=i;break;}
  float prio=1;VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=qf,.queueCount=1,.pQueuePriorities=&prio};
  VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
  VkDevice dev;CK(vkCreateDevice(pd,&dci,0,&dev));VkQueue q;vkGetDeviceQueue(dev,qf,0,&q);
  VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(pd,&mp);
  VkBuffer*buf=calloc(K,sizeof*buf);VkDeviceMemory*mem=calloc(K,sizeof*mem);
  for(int i=0;i<K;i++){VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=4096,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};CK(vkCreateBuffer(dev,&bci,0,&buf[i]));VkMemoryRequirements mr;vkGetBufferMemoryRequirements(dev,buf[i],&mr);uint32_t mt=0;for(uint32_t t=0;t<mp.memoryTypeCount;t++)if((mr.memoryTypeBits>>t)&1){mt=t;break;}VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=mt};CK(vkAllocateMemory(dev,&mai,0,&mem[i]));CK(vkBindBufferMemory(dev,buf[i],mem[i],0));}
  VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=qf};VkCommandPool cp;CK(vkCreateCommandPool(dev,&cpi,0,&cp));
  VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cb;CK(vkAllocateCommandBuffers(dev,&cai,&cb));
  VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CK(vkBeginCommandBuffer(cb,&bi));
  for(int i=0;i<R;i++){VkBufferCopy c={.srcOffset=0,.dstOffset=0,.size=256};vkCmdCopyBuffer(cb,buf[i%K],buf[(i+1)%K],1,&c);}
  CK(vkEndCommandBuffer(cb));
  VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CK(vkCreateFence(dev,&fci,0,&fence));
  VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
  for(int i=0;i<20;i++){CK(vkQueueSubmit(q,1,&si,VK_NULL_HANDLE));} vkQueueWaitIdle(q);
  struct rusage r0,r1;getrusage(RUSAGE_SELF,&r0);double w0=now();
  for(int i=0;i<N;i++){int last=(i%W)==W-1; CK(vkQueueSubmit(q,1,&si,last?fence:VK_NULL_HANDLE)); if(last){vkWaitForFences(dev,1,&fence,1,~0ull);vkResetFences(dev,1,&fence);}}
  vkQueueWaitIdle(q);double w1=now();getrusage(RUSAGE_SELF,&r1);
  printf("K=%d R=%d N=%d W=%d | per submit: wall %.1f us, utime %.1f us, stime %.1f us\n",K,R,N,W,(w1-w0)/N*1e6,(ru(&r1,0)-ru(&r0,0))/N*1e6,(ru(&r1,1)-ru(&r0,1))/N*1e6);
  return 0;
}

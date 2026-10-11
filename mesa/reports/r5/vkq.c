#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
int main(int argc,char**argv){
 VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName=argc>1?argv[1]:"vkq",.apiVersion=VK_API_VERSION_1_3};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
 VkInstance inst; if(vkCreateInstance(&ici,0,&inst)!=VK_SUCCESS){printf("no instance\n");return 1;}
 uint32_t n=0; vkEnumeratePhysicalDevices(inst,&n,0); VkPhysicalDevice pd[8]; vkEnumeratePhysicalDevices(inst,&n,pd);
 for(uint32_t i=0;i<n;i++){
  VkPhysicalDeviceDriverProperties drv={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceVulkan12Properties p12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,.pNext=&drv};
  VkPhysicalDeviceProperties2 p2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&p12};
  vkGetPhysicalDeviceProperties2(pd[i],&p2);
  printf("device %u: %s api %u.%u.%u driver %s (%s) conformance %u.%u.%u.%u\n",i,p2.properties.deviceName,VK_VERSION_MAJOR(p2.properties.apiVersion),VK_VERSION_MINOR(p2.properties.apiVersion),VK_VERSION_PATCH(p2.properties.apiVersion),drv.driverName,drv.driverInfo,drv.conformanceVersion.major,drv.conformanceVersion.minor,drv.conformanceVersion.subminor,drv.conformanceVersion.patch);
  VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceVulkan12Features f12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&f13};
  VkPhysicalDeviceVulkan11Features f11={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.pNext=&f12};
  VkPhysicalDeviceFeatures2 f2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f11};
  vkGetPhysicalDeviceFeatures2(pd[i],&f2);
#define F(s,x) printf("  %-45s %d\n",#x,(int)s.x)
  printf(" -- core 1.0 (selected)\n");
  F(f2.features,robustBufferAccess);F(f2.features,fullDrawIndexUint32);F(f2.features,imageCubeArray);F(f2.features,independentBlend);F(f2.features,geometryShader);F(f2.features,tessellationShader);F(f2.features,sampleRateShading);F(f2.features,dualSrcBlend);F(f2.features,logicOp);F(f2.features,multiDrawIndirect);F(f2.features,drawIndirectFirstInstance);F(f2.features,depthClamp);F(f2.features,depthBiasClamp);F(f2.features,fillModeNonSolid);F(f2.features,depthBounds);F(f2.features,wideLines);F(f2.features,largePoints);F(f2.features,multiViewport);F(f2.features,samplerAnisotropy);F(f2.features,textureCompressionBC);F(f2.features,occlusionQueryPrecise);F(f2.features,pipelineStatisticsQuery);F(f2.features,vertexPipelineStoresAndAtomics);F(f2.features,fragmentStoresAndAtomics);F(f2.features,shaderImageGatherExtended);F(f2.features,shaderStorageImageExtendedFormats);F(f2.features,shaderStorageImageReadWithoutFormat);F(f2.features,shaderStorageImageWriteWithoutFormat);F(f2.features,shaderClipDistance);F(f2.features,shaderCullDistance);F(f2.features,shaderFloat64);F(f2.features,shaderInt64);F(f2.features,shaderInt16);F(f2.features,sparseBinding);
  printf(" -- 1.1\n");
  F(f11,storageBuffer16BitAccess);F(f11,uniformAndStorageBuffer16BitAccess);F(f11,storagePushConstant16);F(f11,multiview);F(f11,variablePointersStorageBuffer);F(f11,variablePointers);F(f11,protectedMemory);F(f11,samplerYcbcrConversion);F(f11,shaderDrawParameters);
  printf(" -- 1.2\n");
  F(f12,samplerMirrorClampToEdge);F(f12,drawIndirectCount);F(f12,storageBuffer8BitAccess);F(f12,uniformAndStorageBuffer8BitAccess);F(f12,storagePushConstant8);F(f12,shaderBufferInt64Atomics);F(f12,shaderSharedInt64Atomics);F(f12,shaderFloat16);F(f12,shaderInt8);F(f12,descriptorIndexing);F(f12,shaderSampledImageArrayNonUniformIndexing);F(f12,shaderStorageBufferArrayNonUniformIndexing);F(f12,descriptorBindingSampledImageUpdateAfterBind);F(f12,descriptorBindingStorageBufferUpdateAfterBind);F(f12,descriptorBindingUpdateUnusedWhilePending);F(f12,descriptorBindingPartiallyBound);F(f12,descriptorBindingVariableDescriptorCount);F(f12,runtimeDescriptorArray);F(f12,samplerFilterMinmax);F(f12,scalarBlockLayout);F(f12,imagelessFramebuffer);F(f12,uniformBufferStandardLayout);F(f12,shaderSubgroupExtendedTypes);F(f12,separateDepthStencilLayouts);F(f12,hostQueryReset);F(f12,timelineSemaphore);F(f12,bufferDeviceAddress);F(f12,bufferDeviceAddressCaptureReplay);F(f12,bufferDeviceAddressMultiDevice);F(f12,vulkanMemoryModel);F(f12,vulkanMemoryModelDeviceScope);F(f12,shaderOutputViewportIndex);F(f12,shaderOutputLayer);F(f12,subgroupBroadcastDynamicId);
  printf(" -- 1.3\n");
  F(f13,robustImageAccess);F(f13,inlineUniformBlock);F(f13,descriptorBindingInlineUniformBlockUpdateAfterBind);F(f13,pipelineCreationCacheControl);F(f13,privateData);F(f13,shaderDemoteToHelperInvocation);F(f13,shaderTerminateInvocation);F(f13,subgroupSizeControl);F(f13,computeFullSubgroups);F(f13,synchronization2);F(f13,textureCompressionASTC_HDR);F(f13,shaderZeroInitializeWorkgroupMemory);F(f13,dynamicRendering);F(f13,shaderIntegerDotProduct);F(f13,maintenance4);
  uint32_t ne=0; vkEnumerateDeviceExtensionProperties(pd[i],0,&ne,0); VkExtensionProperties*e=malloc(ne*sizeof*e); vkEnumerateDeviceExtensionProperties(pd[i],0,&ne,e);
  printf(" -- %u device extensions:\n",ne); for(uint32_t k=0;k<ne;k++) printf("  %s %u\n",e[k].extensionName,e[k].specVersion);
 }
 return 0;}

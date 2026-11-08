#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <unistd.h>
#include <cassert>
static bool profiling=true;
static FILE* profileOpen(const char*,const char*){FILE* f=tmpfile();assert(f);fputs(profiling?"1":"0",f);rewind(f);return f;}
#define fopen profileOpen
struct Device {VkInstance instance;VkPhysicalDevice physical;};
static std::recursive_mutex stateMutex;
static std::map<VkDevice,Device> devices;
static std::shared_ptr<std::recursive_mutex> queueLock(VkQueue){static auto m=std::make_shared<std::recursive_mutex>();return m;}
static VkResult fenceStatus=VK_NOT_READY,queryStatus=VK_SUCCESS,applicationResult=VK_SUCCESS;
static unsigned queryReads=0,resetCalls=0;
static VkResult resetResult=VK_SUCCESS;
static unsigned idleCalls=0,freedFences=0,freedPools=0;
static VkResult VKAPI_CALL deviceIdle(VkDevice){++idleCalls;return VK_SUCCESS;}
static void VKAPI_CALL freeFence(VkDevice,VkFence,const VkAllocationCallbacks*){assert(idleCalls==1);++freedFences;}
static void VKAPI_CALL freeQueries(VkDevice,VkQueryPool,const VkAllocationCallbacks*){assert(idleCalls==1);++freedPools;}
struct Call {std::vector<VkSubmitInfo> info;VkFence fence;};
static std::vector<Call> calls;
static VkResult VKAPI_CALL resetFences(VkDevice,uint32_t n,const VkFence*){assert(n==1);++resetCalls;return resetResult;}
static VkResult VKAPI_CALL getFence(VkDevice,VkFence){return fenceStatus;}
static VkResult VKAPI_CALL getQueries(VkDevice,VkQueryPool,uint32_t first,uint32_t count,size_t bytes,void* data,VkDeviceSize stride,VkQueryResultFlags flags){
    assert(first==0&&count==2&&bytes==32&&stride==16);
    assert(flags==(VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
    ++queryReads;auto v=static_cast<uint64_t*>(data);v[0]=250;v[1]=1;v[2]=20;v[3]=1;return queryStatus;
}

static unsigned workloadCalls=0;
static VkResult commandResult=VK_SUCCESS;
static VkPipeline nextPipeline=reinterpret_cast<VkPipeline>(3001);
static VkResult VKAPI_CALL graphicsPipeline(VkDevice,VkPipelineCache,uint32_t n,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){assert(n==1&&info->stageCount==1);++workloadCalls;*out=nextPipeline;return VK_SUCCESS;}
static VkResult VKAPI_CALL commandAllocate(VkDevice,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* out){++workloadCalls;for(uint32_t i=0;i<info->commandBufferCount;i++)out[i]=reinterpret_cast<VkCommandBuffer>(uintptr_t(1001+i));return VK_SUCCESS;}
static VkResult VKAPI_CALL commandBegin(VkCommandBuffer,const VkCommandBufferBeginInfo* info){assert(info&&info->flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);++workloadCalls;return commandResult;}
static VkResult VKAPI_CALL commandReset(VkCommandBuffer,VkCommandBufferResetFlags flags){assert(flags==0);++workloadCalls;return commandResult;}
static VkResult VKAPI_CALL poolReset(VkDevice,VkCommandPool,VkCommandPoolResetFlags flags){assert(flags==0);++workloadCalls;return commandResult;}
static void VKAPI_CALL commandBind(VkCommandBuffer,VkPipelineBindPoint bp,VkPipeline){assert(bp==VK_PIPELINE_BIND_POINT_GRAPHICS);++workloadCalls;}
static void VKAPI_CALL commandDraw(VkCommandBuffer,uint32_t a,uint32_t b,uint32_t c,int32_t d,uint32_t e){assert(a==100&&b==2&&c==3&&d==-4&&e==5);++workloadCalls;}
static void VKAPI_CALL commandDispatch(VkCommandBuffer,uint32_t x,uint32_t y,uint32_t z){assert(x==8&&y==4&&z==2);++workloadCalls;}
static void VKAPI_CALL commandExecute(VkCommandBuffer,uint32_t n,const VkCommandBuffer*){assert(n==1);++workloadCalls;}
static void VKAPI_CALL commandFree(VkDevice,VkCommandPool,uint32_t n,const VkCommandBuffer*){assert(n==1);++workloadCalls;}
static void VKAPI_CALL poolDestroy(VkDevice,VkCommandPool,const VkAllocationCallbacks*){++workloadCalls;}

template<class T>static T deviceProc(VkDevice,const char* name){
#define WORK_STUB(vk,fn) if(!strcmp(name,vk))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(fn))
    WORK_STUB("vkCreateGraphicsPipelines",graphicsPipeline);WORK_STUB("vkAllocateCommandBuffers",commandAllocate);WORK_STUB("vkBeginCommandBuffer",commandBegin);WORK_STUB("vkResetCommandBuffer",commandReset);WORK_STUB("vkResetCommandPool",poolReset);WORK_STUB("vkCmdBindPipeline",commandBind);WORK_STUB("vkCmdDrawIndexed",commandDraw);WORK_STUB("vkCmdDispatch",commandDispatch);WORK_STUB("vkCmdExecuteCommands",commandExecute);WORK_STUB("vkFreeCommandBuffers",commandFree);WORK_STUB("vkDestroyCommandPool",poolDestroy);
#undef WORK_STUB

    if(!strcmp(name,"vkDeviceWaitIdle"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(deviceIdle));
    if(!strcmp(name,"vkDestroyFence"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(freeFence));
    if(!strcmp(name,"vkDestroyQueryPool"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(freeQueries));
    if(!strcmp(name,"vkResetFences"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(resetFences));
    if(!strcmp(name,"vkGetFenceStatus"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(getFence));
    if(!strcmp(name,"vkGetQueryPoolResults"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(getQueries));
    return nullptr;
}
template<class T>static T instanceProc(VkInstance,const char*){return nullptr;}
static void vkGetDeviceQueue(VkDevice,uint32_t,uint32_t,VkQueue*){}
static void vkDestroyDevice(VkDevice,const VkAllocationCallbacks*){}
static VkResult vkQueueSubmit(VkQueue,uint32_t n,const VkSubmitInfo* info,VkFence fence){calls.push_back({std::vector<VkSubmitInfo>(info,info+n),fence});return applicationResult;}
static VkResult vkQueueSubmit2(VkQueue,uint32_t,const VkSubmitInfo2*,VkFence){return VK_SUCCESS;}
static VkResult vkQueuePresentKHR(VkQueue,const VkPresentInfoKHR*){return VK_SUCCESS;}
static VkResult vkAcquireNextImageKHR(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t*){return VK_SUCCESS;}
static VkResult vkQueueWaitIdle(VkQueue){return VK_SUCCESS;}
#include "../entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_profile.h"
#undef fopen
int main(int argc,char**) {
    profiling=argc==1;
    if(!profiling){assert(!x64profile::proc("vkQueueSubmit"));assert(calls.empty());puts("PASS: default-disabled instrumentation bypass");return 0;}
    assert(x64profile::proc("vkQueueSubmit"));

    using namespace x64profile::workload;
    auto dev=reinterpret_cast<VkDevice>(2);auto pool=reinterpret_cast<VkCommandPool>(900);
    uint32_t data[]={16,1,0,0,0};VkSpecializationMapEntry entries[5];for(unsigned i=0;i<5;i++)entries[i]={1239+i,i*4,4};
    VkSpecializationInfo specialization{5,entries,sizeof(data),data};VkPipelineShaderStageCreateInfo stageInfo{};stageInfo.stage=VK_SHADER_STAGE_FRAGMENT_BIT;stageInfo.pSpecializationInfo=&specialization;
    assert(virtualBc(stageInfo));data[0]=32;assert(!virtualBc(stageInfo));data[0]=16;data[1]=0;assert(!virtualBc(stageInfo));data[4]=0x80000000;assert(virtualBc(stageInfo));
    entries[4].offset=UINT32_MAX;assert(spec(&specialization,1243)==0);entries[4].offset=16;
    VkGraphicsPipelineCreateInfo graphicsInfo{};graphicsInfo.stageCount=1;graphicsInfo.pStages=&stageInfo;VkPipeline pipeline{};
    assert(createGraphics(dev,VK_NULL_HANDLE,1,&graphicsInfo,nullptr,&pipeline)==VK_SUCCESS);
    VkCommandBufferAllocateInfo allocation{};allocation.commandPool=pool;allocation.commandBufferCount=2;VkCommandBuffer cb[2];assert(allocate(dev,&allocation,cb)==VK_SUCCESS);
    VkCommandBufferBeginInfo beginInfo{};beginInfo.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;assert(begin(cb[0],&beginInfo)==VK_SUCCESS);
    bind(cb[0],VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);indexed(cb[0],100,2,3,-4,5);dispatch(cb[0],8,4,2);
    auto first=snapshot(1,cb);assert(first.draws==1&&first.bcDraws==1&&first.elements==200&&first.dispatches==1&&first.groups==64&&first.unknown==1);
    auto cold=submitted(1,cb);assert(cold.firstUsePipelines==1);assert(submitted(1,cb).firstUsePipelines==0);
    execute(cb[1],1,cb);auto secondary=snapshot(1,cb+1);assert(secondary.draws==1&&secondary.groups==64);
    commandResult=VK_ERROR_DEVICE_LOST;assert(reset(cb[0],0)==VK_ERROR_DEVICE_LOST);assert(snapshot(1,cb).draws==1);
    commandResult=VK_SUCCESS;assert(begin(cb[0],&beginInfo)==VK_SUCCESS);assert(snapshot(1,cb).draws==0);assert(secondary.draws==1); // submission snapshots survive reset
    assert(resetPool(dev,pool,0)==VK_SUCCESS);assert(snapshot(1,cb+1).draws==0);
    free(dev,pool,1,cb);assert(snapshot(1,cb).unknown==1);destroyPool(dev,pool,nullptr);assert(x64profile::workload::commands.empty());assert(workloadCalls==12);
    puts("PASS: pipeline masks and bounds, preserved draw args, secondary snapshots, reset failure, pool/free cleanup");

    auto queue=reinterpret_cast<VkQueue>(1);auto q=std::make_shared<x64profile::Queue>();
    q->device=reinterpret_cast<VkDevice>(2);q->tried=true;q->queries=reinterpret_cast<VkQueryPool>(3);q->bits=8;q->period=1000;
    for(unsigned i=0;i<8;++i)q->markers[i]=reinterpret_cast<VkCommandBuffer>(uintptr_t(100+i));
    q->completion[0]=reinterpret_cast<VkFence>(4);x64profile::registry[queue]=q;
    VkCommandBuffer commands[]={reinterpret_cast<VkCommandBuffer>(10)};
    VkSemaphore semaphore=reinterpret_cast<VkSemaphore>(11);VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo app{};app.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;app.commandBufferCount=1;app.pCommandBuffers=commands;
    app.waitSemaphoreCount=app.signalSemaphoreCount=1;app.pWaitSemaphores=app.pSignalSemaphores=&semaphore;app.pWaitDstStageMask=&stage;
    VkTimelineSemaphoreSubmitInfo timeline{};timeline.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;app.pNext=&timeline;
    const auto fence=reinterpret_cast<VkFence>(12);
    assert(x64profile::submit(queue,1,&app,fence)==VK_SUCCESS);
    assert(calls.size()==1&&calls[0].info.size()==1&&!memcmp(&calls[0].info[0],&app,sizeof app));
    x64profile::FenceSubmission association;assert(x64profile::fenceSnapshot(q->device,fence,association)&&association.serial==1);
    auto otherDevice=reinterpret_cast<VkDevice>(99);assert(!x64profile::fenceSnapshot(otherDevice,fence,association));
    resetResult=VK_ERROR_DEVICE_LOST;assert(x64profile::resetApplicationFences(q->device,1,&fence)==resetResult);assert(x64profile::fenceSnapshot(q->device,fence,association));
    resetResult=VK_SUCCESS;assert(x64profile::resetApplicationFences(q->device,1,&fence)==VK_SUCCESS);assert(!x64profile::fenceSnapshot(q->device,fence,association));resetCalls=0;

    calls.clear();q->submits=31;
    assert(x64profile::submit(queue,1,&app,fence)==VK_SUCCESS);
    assert(calls.size()==2&&calls[0].info.size()==2&&calls[0].fence==fence);
    assert(!memcmp(&calls[0].info[1],&app,sizeof app)); // all app pNext/semaphores preserved
    assert(calls[1].fence==q->completion[0]&&calls[1].info.size()==1&&q->slots[0].pending&&resetCalls==1);
    x64profile::collect(*q);assert(!queryReads&&q->slots[0].pending); // no read while GPU pending
    fenceStatus=VK_SUCCESS;queryStatus=VK_NOT_READY;x64profile::collect(*q);assert(queryReads==1&&q->slots[0].pending);
    queryStatus=VK_SUCCESS;x64profile::collect(*q);assert(queryReads==2&&!q->slots[0].pending);
    calls.clear();q->submits=63;for(auto& s:q->slots)s.pending=true;
    x64profile::submit(queue,1,&app,fence);assert(calls.size()==1&&calls[0].info.size()==1); // full pool skips sampling
    calls.clear();q->submits=95;q->slots[0].pending=false;applicationResult=VK_ERROR_DEVICE_LOST;
    assert(x64profile::submit(queue,1,&app,fence)==VK_ERROR_DEVICE_LOST&&calls.size()==1);
    assert(!x64profile::fenceSnapshot(q->device,fence,association)); // rejected submit removes association
    assert(idleCalls==0); // no idle waits in rendering or collection
    x64profile::destroyDevice(q->device,nullptr);
    assert(idleCalls==1&&freedFences==1&&freedPools==1&&x64profile::registry.empty()&&x64profile::fenceSubmissions.empty());
    puts("PASS: fence association, cross-device isolation, failed reset preservation, successful reset and submit failure cleanup");
    puts("PASS: original submit/fence/pNext preserved, sparse nonblocking samples, full-pool skip, submit failure, safe teardown");
}

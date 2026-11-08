// GNU x64 diagnostic only. Disabled unless the marker exists at process start.
// CPU call latency and GPU queue timestamps are different metrics. The sampled
// GPU interval includes semaphore/backlog waits; it is NOT pure shader time.
// Never wait for diagnostic queries during rendering. Device teardown drains
// our trailing markers before freeing their resources.
#pragma once
#include <array>
#include <set>

namespace x64profile {
using Clock = std::chrono::steady_clock;
static uint64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}
static bool enabled() {
    static const bool value=[] {
        FILE* file=fopen("/data/storage/el2/base/files/steamohos-native/proton-probe/vulkan-frame-profile","rb");
        if(!file)return false;const bool on=fgetc(file)=='1';fclose(file);return on;
    }();
    return value;
}
struct Metric { uint64_t count=0,total=0,maximum=0,timeouts=0; };
enum Phase { Submit,Submit2,QueueLock,PresentCall,AcquireCall,FenceWait,SemaphoreWait,QueueIdle,QueryRead,GraphicsPipeline,ComputePipeline,PhaseCount };
static std::mutex metricsMutex;
static std::array<Metric,PhaseCount> metrics{};
static void record(Phase phase,uint64_t elapsed,VkResult result=VK_SUCCESS) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    auto& m=metrics[phase];++m.count;m.total+=elapsed;m.maximum=std::max(m.maximum,elapsed);
    if(result==VK_TIMEOUT||result==VK_NOT_READY)++m.timeouts;
}

// Optional command metadata accompanies the existing sparse timestamps. It
// describes recorded API calls, not individual draw durations or executed pixels.
namespace workload {
struct Summary {
    uint64_t draws=0,dispatches=0,indirect=0,bcDraws=0,bcDispatches=0,unknown=0;
    uint64_t firstUsePipelines=0,elements=0,groups=0,graphics=0,compute=0;bool mixedGraphics=false,mixedCompute=false;
    void merge(const Summary& b) {
        firstUsePipelines+=b.firstUsePipelines;draws+=b.draws;dispatches+=b.dispatches;indirect+=b.indirect;elements+=b.elements;groups+=b.groups;
        bcDraws+=b.bcDraws;bcDispatches+=b.bcDispatches;unknown+=b.unknown;
        mixedGraphics|=b.mixedGraphics||(graphics&&b.graphics&&graphics!=b.graphics);
        mixedCompute|=b.mixedCompute||(compute&&b.compute&&compute!=b.compute);
        if(b.graphics)graphics=b.graphics;if(b.compute)compute=b.compute;
    }
};
struct Pipeline {VkDevice device=VK_NULL_HANDLE;uint64_t id=0;bool virtualBc=false;};
struct Command {VkDevice device=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;Summary summary;Pipeline graphics,compute;std::set<uint64_t> usedPipelines;};
static std::mutex mutex;
static std::map<VkCommandBuffer,Command> commands;
static std::map<std::pair<VkDevice,VkPipeline>,Pipeline> pipelines;
static uint64_t serial=0;
static std::map<uint64_t,VkDevice> submittedPipelines;
static uint32_t spec(const VkSpecializationInfo* info,uint32_t id) {
    if(!info||!info->pData)return 0;
    for(uint32_t i=0;i<info->mapEntryCount;++i){const auto& e=info->pMapEntries[i];
        if(e.constantID==id&&e.size==4&&e.offset<=info->dataSize&&info->dataSize-e.offset>=4){uint32_t v;memcpy(&v,static_cast<const char*>(info->pData)+e.offset,4);return v;}}
    return 0;
}
static bool virtualBc(const VkPipelineShaderStageCreateInfo& stage) {
    // Paired DXVK ABI: FirstPipelineConstant=1225; stage mask=14,
    // texture masks=15..18. This indicates a bound virtual SRV, not a sample count.
    uint32_t masks=0;for(uint32_t i=0;i<4;++i)masks|=spec(stage.pSpecializationInfo,1240+i);
    return masks&&(spec(stage.pSpecializationInfo,1239)&stage.stage);
}
static VkResult VKAPI_CALL createGraphics(VkDevice d,VkPipelineCache cache,uint32_t n,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks* alloc,VkPipeline* out) {
    const auto start=nowUs();auto rc=deviceProc<PFN_vkCreateGraphicsPipelines>(d,"vkCreateGraphicsPipelines")(d,cache,n,info,alloc,out);auto us=nowUs()-start;record(GraphicsPipeline,us,rc);
    if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<n;++i){bool bc=false;for(uint32_t j=0;j<info[i].stageCount;++j)bc|=virtualBc(info[i].pStages[j]);auto id=++serial;pipelines[{d,out[i]}]={d,id,bc};fprintf(stderr,"GNU_X64_PIPELINE kind=graphics id=%" PRIu64 " virtual_bc_bound=%u batch_create_us=%" PRIu64 " batch_count=%u end_us=%" PRIu64 "\n",id,unsigned(bc),us,n,nowUs());}}
    return rc;
}
static VkResult VKAPI_CALL createCompute(VkDevice d,VkPipelineCache cache,uint32_t n,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks* alloc,VkPipeline* out) {
    const auto start=nowUs();auto rc=deviceProc<PFN_vkCreateComputePipelines>(d,"vkCreateComputePipelines")(d,cache,n,info,alloc,out);auto us=nowUs()-start;record(ComputePipeline,us,rc);
    if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<n;++i){bool bc=virtualBc(info[i].stage);auto id=++serial;pipelines[{d,out[i]}]={d,id,bc};fprintf(stderr,"GNU_X64_PIPELINE kind=compute id=%" PRIu64 " virtual_bc_bound=%u batch_create_us=%" PRIu64 " batch_count=%u end_us=%" PRIu64 "\n",id,unsigned(bc),us,n,nowUs());}}
    return rc;
}
static void VKAPI_CALL destroyPipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks* a){deviceProc<PFN_vkDestroyPipeline>(d,"vkDestroyPipeline")(d,p,a);std::lock_guard<std::mutex> lock(mutex);auto it=pipelines.find({d,p});if(it!=pipelines.end()){submittedPipelines.erase(it->second.id);pipelines.erase(it);}}
static VkResult VKAPI_CALL allocate(VkDevice d,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* out){auto rc=deviceProc<PFN_vkAllocateCommandBuffers>(d,"vkAllocateCommandBuffers")(d,info,out);if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<info->commandBufferCount;++i){Command c;c.device=d;c.pool=info->commandPool;commands[out[i]]=c;}}return rc;}
static VkDevice device(VkCommandBuffer c){std::lock_guard<std::mutex> lock(mutex);return commands.at(c).device;}
static void clear(Command& c){c.summary={};c.graphics={};c.compute={};c.usedPipelines.clear();}
static VkResult VKAPI_CALL begin(VkCommandBuffer c,const VkCommandBufferBeginInfo* info){auto rc=deviceProc<PFN_vkBeginCommandBuffer>(device(c),"vkBeginCommandBuffer")(c,info);if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);clear(commands.at(c));}return rc;}
static VkResult VKAPI_CALL reset(VkCommandBuffer c,VkCommandBufferResetFlags f){auto rc=deviceProc<PFN_vkResetCommandBuffer>(device(c),"vkResetCommandBuffer")(c,f);if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);clear(commands.at(c));}return rc;}
static VkResult VKAPI_CALL resetPool(VkDevice d,VkCommandPool p,VkCommandPoolResetFlags f){auto rc=deviceProc<PFN_vkResetCommandPool>(d,"vkResetCommandPool")(d,p,f);if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);for(auto& row:commands)if(row.second.device==d&&row.second.pool==p)clear(row.second);}return rc;}
static void VKAPI_CALL free(VkDevice d,VkCommandPool p,uint32_t n,const VkCommandBuffer* c){deviceProc<PFN_vkFreeCommandBuffers>(d,"vkFreeCommandBuffers")(d,p,n,c);std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<n;++i)commands.erase(c[i]);}
static void VKAPI_CALL destroyPool(VkDevice d,VkCommandPool p,const VkAllocationCallbacks* a){deviceProc<PFN_vkDestroyCommandPool>(d,"vkDestroyCommandPool")(d,p,a);std::lock_guard<std::mutex> lock(mutex);for(auto i=commands.begin();i!=commands.end();)if(i->second.device==d&&i->second.pool==p)i=commands.erase(i);else ++i;}
static void clearDevice(VkDevice d){std::lock_guard<std::mutex> lock(mutex);for(auto i=commands.begin();i!=commands.end();)if(i->second.device==d)i=commands.erase(i);else ++i;for(auto i=pipelines.begin();i!=pipelines.end();)if(i->second.device==d)i=pipelines.erase(i);else ++i;for(auto i=submittedPipelines.begin();i!=submittedPipelines.end();)if(i->second==d)i=submittedPipelines.erase(i);else ++i;}
static void VKAPI_CALL bind(VkCommandBuffer c,VkPipelineBindPoint bp,VkPipeline p){VkDevice d;{std::lock_guard<std::mutex> lock(mutex);auto& cmd=commands.at(c);d=cmd.device;auto i=pipelines.find({d,p});Pipeline state=i==pipelines.end()?Pipeline{}:i->second;if(bp==VK_PIPELINE_BIND_POINT_COMPUTE)cmd.compute=state;else if(bp==VK_PIPELINE_BIND_POINT_GRAPHICS)cmd.graphics=state;}deviceProc<PFN_vkCmdBindPipeline>(d,"vkCmdBindPipeline")(c,bp,p);}
static VkDevice note(VkCommandBuffer c,bool compute,bool indirect,uint64_t units=0){std::lock_guard<std::mutex> lock(mutex);auto& cmd=commands.at(c);Summary s;auto p=compute?cmd.compute:cmd.graphics;s.indirect=indirect;s.unknown=!p.id;if(compute){s.groups=units;s.dispatches=1;s.bcDispatches=p.virtualBc;s.compute=p.id;}else{s.elements=units;s.draws=1;s.bcDraws=p.virtualBc;s.graphics=p.id;}cmd.summary.merge(s);if(p.id)cmd.usedPipelines.insert(p.id);return cmd.device;}
static Summary snapshot(uint32_t n,const VkCommandBuffer* buffers){Summary s;std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<n;++i){auto it=commands.find(buffers[i]);if(it==commands.end())++s.unknown;else s.merge(it->second.summary);}return s;}
static Summary submitted(uint32_t n,const VkCommandBuffer* buffers){Summary s;std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<n;++i){auto it=commands.find(buffers[i]);if(it==commands.end()){++s.unknown;continue;}s.merge(it->second.summary);for(auto id:it->second.usedPipelines)if(submittedPipelines.emplace(id,it->second.device).second)++s.firstUsePipelines;}return s;}
static void VKAPI_CALL execute(VkCommandBuffer c,uint32_t n,const VkCommandBuffer* buffers){auto s=snapshot(n,buffers);VkDevice d;{std::lock_guard<std::mutex> lock(mutex);auto& cmd=commands.at(c);cmd.summary.merge(s);for(uint32_t i=0;i<n;++i){auto it=commands.find(buffers[i]);if(it!=commands.end())cmd.usedPipelines.insert(it->second.usedPipelines.begin(),it->second.usedPipelines.end());}d=cmd.device;}deviceProc<PFN_vkCmdExecuteCommands>(d,"vkCmdExecuteCommands")(c,n,buffers);}
static void VKAPI_CALL draw(VkCommandBuffer c,uint32_t a,uint32_t b,uint32_t e,uint32_t f){auto d=note(c,false,false,uint64_t(a)*b);deviceProc<PFN_vkCmdDraw>(d,"vkCmdDraw")(c,a,b,e,f);}
static void VKAPI_CALL indexed(VkCommandBuffer c,uint32_t a,uint32_t b,uint32_t e,int32_t f,uint32_t g){auto d=note(c,false,false,uint64_t(a)*b);deviceProc<PFN_vkCmdDrawIndexed>(d,"vkCmdDrawIndexed")(c,a,b,e,f,g);}
static void VKAPI_CALL indirectDraw(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,uint32_t n,uint32_t s){auto d=note(c,false,true);deviceProc<PFN_vkCmdDrawIndirect>(d,"vkCmdDrawIndirect")(c,b,o,n,s);}
static void VKAPI_CALL indirectIndexed(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,uint32_t n,uint32_t s){auto d=note(c,false,true);deviceProc<PFN_vkCmdDrawIndexedIndirect>(d,"vkCmdDrawIndexedIndirect")(c,b,o,n,s);}
static void VKAPI_CALL dispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z){auto d=note(c,true,false,uint64_t(x)*y*z);deviceProc<PFN_vkCmdDispatch>(d,"vkCmdDispatch")(c,x,y,z);}
static void VKAPI_CALL dispatchIndirect(VkCommandBuffer c,VkBuffer b,VkDeviceSize o){auto d=note(c,true,true);deviceProc<PFN_vkCmdDispatchIndirect>(d,"vkCmdDispatchIndirect")(c,b,o);}
static void VKAPI_CALL dispatchBase(VkCommandBuffer c,uint32_t bx,uint32_t by,uint32_t bz,uint32_t x,uint32_t y,uint32_t z){auto d=note(c,true,false,uint64_t(x)*y*z);auto fn=deviceProc<PFN_vkCmdDispatchBase>(d,"vkCmdDispatchBase");if(!fn)fn=deviceProc<PFN_vkCmdDispatchBase>(d,"vkCmdDispatchBaseKHR");fn(c,bx,by,bz,x,y,z);}
static void VKAPI_CALL indirectCount(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,VkBuffer cb,VkDeviceSize co,uint32_t max,uint32_t stride){auto d=note(c,false,true);auto fn=deviceProc<PFN_vkCmdDrawIndirectCount>(d,"vkCmdDrawIndirectCount");if(!fn)fn=deviceProc<PFN_vkCmdDrawIndirectCount>(d,"vkCmdDrawIndirectCountKHR");if(!fn)fn=deviceProc<PFN_vkCmdDrawIndirectCount>(d,"vkCmdDrawIndirectCountAMD");fn(c,b,o,cb,co,max,stride);}
static void VKAPI_CALL indexedCount(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,VkBuffer cb,VkDeviceSize co,uint32_t max,uint32_t stride){auto d=note(c,false,true);auto fn=deviceProc<PFN_vkCmdDrawIndexedIndirectCount>(d,"vkCmdDrawIndexedIndirectCount");if(!fn)fn=deviceProc<PFN_vkCmdDrawIndexedIndirectCount>(d,"vkCmdDrawIndexedIndirectCountKHR");if(!fn)fn=deviceProc<PFN_vkCmdDrawIndexedIndirectCount>(d,"vkCmdDrawIndexedIndirectCountAMD");fn(c,b,o,cb,co,max,stride);}
static void print(const Summary& s){fprintf(stderr," draw_calls=%" PRIu64 " dispatch_calls=%" PRIu64 " indirect_calls=%" PRIu64 " virtual_bc_draw_calls=%" PRIu64 " virtual_bc_dispatch_calls=%" PRIu64 " graphics_last=%" PRIu64 " compute_last=%" PRIu64 " mixed_graphics=%u mixed_compute=%u metadata_unknown=%" PRIu64,s.draws,s.dispatches,s.indirect,s.bcDraws,s.bcDispatches,s.graphics,s.compute,unsigned(s.mixedGraphics),unsigned(s.mixedCompute),s.unknown);fprintf(stderr," direct_vertices_or_indices=%" PRIu64 " direct_workgroups=%" PRIu64,s.elements,s.groups);fprintf(stderr," first_use_pipeline_count=%" PRIu64,s.firstUsePipelines);}
static PFN_vkVoidFunction proc(const char* name){
#define WORK_PROC(vk,fn) if(!strcmp(name,vk))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    WORK_PROC("vkCreateGraphicsPipelines",createGraphics);WORK_PROC("vkCreateComputePipelines",createCompute);WORK_PROC("vkDestroyPipeline",destroyPipeline);
    WORK_PROC("vkAllocateCommandBuffers",allocate);WORK_PROC("vkFreeCommandBuffers",free);WORK_PROC("vkBeginCommandBuffer",begin);WORK_PROC("vkResetCommandBuffer",reset);WORK_PROC("vkResetCommandPool",resetPool);WORK_PROC("vkDestroyCommandPool",destroyPool);
    WORK_PROC("vkCmdBindPipeline",bind);WORK_PROC("vkCmdExecuteCommands",execute);WORK_PROC("vkCmdDraw",draw);WORK_PROC("vkCmdDrawIndexed",indexed);WORK_PROC("vkCmdDrawIndirect",indirectDraw);WORK_PROC("vkCmdDrawIndexedIndirect",indirectIndexed);WORK_PROC("vkCmdDispatch",dispatch);WORK_PROC("vkCmdDispatchIndirect",dispatchIndirect);
    WORK_PROC("vkCmdDispatchBase",dispatchBase);WORK_PROC("vkCmdDispatchBaseKHR",dispatchBase);
    WORK_PROC("vkCmdDrawIndirectCount",indirectCount);WORK_PROC("vkCmdDrawIndirectCountKHR",indirectCount);WORK_PROC("vkCmdDrawIndirectCountAMD",indirectCount);
    WORK_PROC("vkCmdDrawIndexedIndirectCount",indexedCount);WORK_PROC("vkCmdDrawIndexedIndirectCountKHR",indexedCount);WORK_PROC("vkCmdDrawIndexedIndirectCountAMD",indexedCount);
#undef WORK_PROC
    return nullptr;
}
}

// Associate application fences with a copy of submission metadata. A fence
// wait also covers earlier queue work, so it is not a duration of this batch.
struct FenceSubmission {uint64_t serial=0;workload::Summary work;};
static std::mutex fenceMutex;
static std::map<std::pair<VkDevice,VkFence>,FenceSubmission> fenceSubmissions;
static void rememberFence(VkDevice d,VkFence f,uint64_t serial,const workload::Summary& work) {
    if(!f)return;std::lock_guard<std::mutex> lock(fenceMutex);fenceSubmissions[{d,f}]={serial,work};
}
static bool fenceSnapshot(VkDevice d,VkFence f,FenceSubmission& out) {
    std::lock_guard<std::mutex> lock(fenceMutex);auto i=fenceSubmissions.find({d,f});
    if(i==fenceSubmissions.end())return false;out=i->second;return true;
}
static void forgetFences(VkDevice d,uint32_t n,const VkFence* fences) {
    std::lock_guard<std::mutex> lock(fenceMutex);for(uint32_t i=0;i<n;++i)fenceSubmissions.erase({d,fences[i]});
}
static VkResult VKAPI_CALL resetApplicationFences(VkDevice d,uint32_t n,const VkFence* fences) {
    auto rc=deviceProc<PFN_vkResetFences>(d,"vkResetFences")(d,n,fences);
    if(rc==VK_SUCCESS)forgetFences(d,n,fences);return rc;
}
static void VKAPI_CALL destroyApplicationFence(VkDevice d,VkFence fence,const VkAllocationCallbacks* a) {
    deviceProc<PFN_vkDestroyFence>(d,"vkDestroyFence")(d,fence,a);forgetFences(d,1,&fence);
}

struct Slot { bool pending=false;uint64_t serial=0;uint32_t commands=0;workload::Summary work; };
struct Queue {
    VkDevice device=VK_NULL_HANDLE;uint32_t family=0,bits=0;float period=0;
    bool tried=false;VkCommandPool pool=VK_NULL_HANDLE;VkQueryPool queries=VK_NULL_HANDLE;
    std::array<VkCommandBuffer,8> markers{};std::array<Slot,4> slots{};
    std::array<VkFence,4> completion{};
    uint64_t submits=0,lastRead=0;
};
static std::mutex registryMutex;
static std::map<VkQueue,std::shared_ptr<Queue>> registry;
static std::shared_ptr<Queue> find(VkQueue queue) {
    std::lock_guard<std::mutex> lock(registryMutex);
    auto it=registry.find(queue);return it==registry.end()?nullptr:it->second;
}
static void destroy(Queue& q) {
    for(auto& fence:q.completion)if(fence){deviceProc<PFN_vkDestroyFence>(q.device,"vkDestroyFence")(q.device,fence,nullptr);fence=VK_NULL_HANDLE;}
    if(q.pool)deviceProc<PFN_vkDestroyCommandPool>(q.device,"vkDestroyCommandPool")(q.device,q.pool,nullptr);
    if(q.queries)deviceProc<PFN_vkDestroyQueryPool>(q.device,"vkDestroyQueryPool")(q.device,q.queries,nullptr);
    q.pool=VK_NULL_HANDLE;q.queries=VK_NULL_HANDLE;
}
static void initialize(Queue& q) {
    q.tried=true;Device d;
    {std::lock_guard<std::recursive_mutex> lock(stateMutex);d=devices.at(q.device);}
    VkPhysicalDeviceProperties props{};
    instanceProc<PFN_vkGetPhysicalDeviceProperties>(d.instance,"vkGetPhysicalDeviceProperties")(d.physical,&props);
    auto families=instanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(d.instance,"vkGetPhysicalDeviceQueueFamilyProperties");
    uint32_t count=0;families(d.physical,&count,nullptr);std::vector<VkQueueFamilyProperties> info(count);
    families(d.physical,&count,info.data());
    if(q.family>=count||!info[q.family].timestampValidBits||!(props.limits.timestampPeriod>0)||
       !(info[q.family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) {
        fprintf(stderr,"GNU_X64_PROFILE gpu_timestamps=unsupported family=%u\n",q.family);return;
    }
    q.bits=info[q.family].timestampValidBits;q.period=props.limits.timestampPeriod;
    VkCommandPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pool.queueFamilyIndex=q.family;
    VkResult rc=deviceProc<PFN_vkCreateCommandPool>(q.device,"vkCreateCommandPool")(q.device,&pool,nullptr,&q.pool);
    if(rc!=VK_SUCCESS)goto failed;
    {
        VkQueryPoolCreateInfo query{};query.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=8;
        rc=deviceProc<PFN_vkCreateQueryPool>(q.device,"vkCreateQueryPool")(q.device,&query,nullptr,&q.queries);
        if(rc!=VK_SUCCESS)goto failed;
        VkCommandBufferAllocateInfo alloc{};alloc.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool=q.pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=8;
        rc=deviceProc<PFN_vkAllocateCommandBuffers>(q.device,"vkAllocateCommandBuffers")(q.device,&alloc,q.markers.data());
        if(rc!=VK_SUCCESS)goto failed;
        for(auto& fence:q.completion) {
            VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            rc=deviceProc<PFN_vkCreateFence>(q.device,"vkCreateFence")(q.device,&fi,nullptr,&fence);
            if(rc!=VK_SUCCESS)goto failed;
        }
        for(uint32_t i=0;i<8;++i) {
            VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags=VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            rc=deviceProc<PFN_vkBeginCommandBuffer>(q.device,"vkBeginCommandBuffer")(q.markers[i],&begin);
            if(rc!=VK_SUCCESS)goto failed;
            if(!(i&1))deviceProc<PFN_vkCmdResetQueryPool>(q.device,"vkCmdResetQueryPool")(q.markers[i],q.queries,i,2);
            deviceProc<PFN_vkCmdWriteTimestamp>(q.device,"vkCmdWriteTimestamp")(q.markers[i],
                (i&1)?VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,q.queries,i);
            rc=deviceProc<PFN_vkEndCommandBuffer>(q.device,"vkEndCommandBuffer")(q.markers[i]);
            if(rc!=VK_SUCCESS)goto failed;
        }
    }
    fprintf(stderr,"GNU_X64_PROFILE gpu_timestamps=enabled family=%u valid_bits=%u period_ns=%.6f sample_every=32 slots=4 query_wait=0 metric=queue_span_not_shader_time\n",q.family,q.bits,q.period);
    return;
failed:
    fprintf(stderr,"GNU_X64_PROFILE gpu_timestamps=failed result=%d\n",rc);destroy(q);
}
// Called under the existing per-queue lock. Queries have availability bits and
// no WAIT flag. A busy slot is skipped, never recycled while its query is pending.
static void collect(Queue& q) {
    if(!q.queries)return;
    for(uint32_t i=0;i<q.slots.size();++i) {
        auto& slot=q.slots[i];if(!slot.pending)continue;
        // Completion belongs to us, so application fence resets cannot race
        // this read, and results from an earlier use cannot look current.
        if(deviceProc<PFN_vkGetFenceStatus>(q.device,"vkGetFenceStatus")(q.device,q.completion[i])!=VK_SUCCESS)continue;
        struct Result {uint64_t value,available;};Result values[2]{};
        const auto t=nowUs();
        const auto rc=deviceProc<PFN_vkGetQueryPoolResults>(q.device,"vkGetQueryPoolResults")(
            q.device,q.queries,i*2,2,sizeof(values),values,sizeof(Result),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        record(QueryRead,nowUs()-t,rc);
        if(rc!=VK_SUCCESS||!values[0].available||!values[1].available)continue;
        const uint64_t mask=q.bits>=64?UINT64_MAX:((uint64_t(1)<<q.bits)-1);
        const double us=double((values[1].value-values[0].value)&mask)*q.period/1000.0;
        fprintf(stderr,"GNU_X64_PROFILE gpu_queue_span_us=%.3f submit_serial=%" PRIu64 " command_buffers=%u family=%u includes_queue_dependencies=1",us,slot.serial,slot.commands,q.family);workload::print(slot.work);fprintf(stderr,"\n");
        slot.pending=false;
    }
}
static void report() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    static uint64_t previous=nowUs();const auto t=nowUs();
    if(t-previous<2000000)return;
    const char* labels[]={"submit","submit2","queue_lock","present","acquire","fence_wait","semaphore_wait","queue_idle","query_read","graphics_pipeline","compute_pipeline"};
    fprintf(stderr,"GNU_X64_PROFILE cpu_window_us=%" PRIu64 " pid=%d",t-previous,getpid());
    for(unsigned i=0;i<PhaseCount;++i) {
        const auto& m=metrics[i];
        fprintf(stderr," %s_count_sum_max_timeout=%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64,labels[i],m.count,m.total,m.maximum,m.timeouts);
    }
    fprintf(stderr,"\n");metrics={};previous=t;
}
static void VKAPI_CALL getQueue(VkDevice device,uint32_t family,uint32_t index,VkQueue* out) {
    vkGetDeviceQueue(device,family,index,out);
    if(!*out)return;
    std::lock_guard<std::mutex> lock(registryMutex);
    if(!registry.count(*out)){auto q=std::make_shared<Queue>();q->device=device;q->family=family;registry[*out]=q;}
}
static VkResult VKAPI_CALL submit(VkQueue queue,uint32_t count,const VkSubmitInfo* info,VkFence fence) {
    auto q=find(queue);if(!q)return vkQueueSubmit(queue,count,info,fence);
    auto guard=queueLock(queue);const auto lockStart=nowUs();std::lock_guard<std::recursive_mutex> lock(*guard);
    record(QueueLock,nowUs()-lockStart);++q->submits;
    std::vector<VkSubmitInfo> sampled;int slot=-1;uint32_t commands=0;
    if(q->submits%32==0&&count&&count<=64&&info) {
        if(!q->tried)initialize(*q);
        if(q->queries) {
            for(uint32_t j=0;j<count;++j)commands+=info[j].commandBufferCount;
            if(commands)for(unsigned j=0;j<q->slots.size();++j)if(!q->slots[j].pending){slot=j;break;}
            if(slot>=0) {
                sampled.resize(count+1);sampled.front().sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
                sampled.front().commandBufferCount=1;
                sampled.front().pCommandBuffers=&q->markers[slot*2];
                std::copy_n(info,count,sampled.begin()+1);
                if(deviceProc<PFN_vkResetFences>(q->device,"vkResetFences")(q->device,1,&q->completion[slot])!=VK_SUCCESS)slot=-1;
            }
        }
    }
    workload::Summary work;for(uint32_t i=0;info&&i<count;++i)work.merge(workload::submitted(info[i].commandBufferCount,info[i].pCommandBuffers));
    rememberFence(q->device,fence,q->submits,work);
    const auto t=nowUs();
    auto rc=vkQueueSubmit(queue,slot<0?count:count+1,slot<0?info:sampled.data(),fence);
    const auto elapsed=nowUs()-t;record(Submit,elapsed,rc);
    if(rc!=VK_SUCCESS&&fence)forgetFences(q->device,1,&fence);
    if(elapsed>=100000){fprintf(stderr,"GNU_X64_SUBMIT slow_call_us=%" PRIu64 " serial=%" PRIu64 " result=%d end_us=%" PRIu64,elapsed,q->submits,rc,nowUs());workload::print(work);fprintf(stderr,"\n");}
    if(slot>=0&&rc==VK_SUCCESS) {
        VkSubmitInfo end{};end.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;end.commandBufferCount=1;end.pCommandBuffers=&q->markers[slot*2+1];
        auto diagnostic=vkQueueSubmit(queue,1,&end,q->completion[slot]);
        if(diagnostic==VK_SUCCESS)q->slots[slot]={true,q->submits,commands,work};
        else {
            // Do not recycle a slot whose first marker may still be pending.
            q->slots[slot]={true,q->submits,commands,work};
            fprintf(stderr,"GNU_X64_PROFILE marker_submit_failed result=%d slot=%d\n",diagnostic,slot);
        }
    }
    return rc;
}
static VkResult VKAPI_CALL submit2(VkQueue q,uint32_t n,const VkSubmitInfo2* info,VkFence f) {
    const auto t=nowUs();auto rc=vkQueueSubmit2(q,n,info,f);record(Submit2,nowUs()-t,rc);return rc;
}
static VkResult VKAPI_CALL presentCall(VkQueue queue,const VkPresentInfoKHR* info) {
    const auto t=nowUs();auto rc=vkQueuePresentKHR(queue,info);record(PresentCall,nowUs()-t,rc);
    // Poll at most once per second; never per-fence or per-draw.
    const auto now=nowUs();
    auto guard=queueLock(queue);std::lock_guard<std::recursive_mutex> lock(*guard);
    if(auto q=find(queue))if(now-q->lastRead>=1000000){collect(*q);q->lastRead=now;}
    report();return rc;
}
static VkResult VKAPI_CALL acquire(VkDevice d,VkSwapchainKHR s,uint64_t timeout,VkSemaphore sem,VkFence f,uint32_t* index) {
    const auto t=nowUs();auto rc=vkAcquireNextImageKHR(d,s,timeout,sem,f,index);record(AcquireCall,nowUs()-t,rc);return rc;
}
static VkResult VKAPI_CALL waitFences(VkDevice d,uint32_t n,const VkFence* fences,VkBool32 all,uint64_t timeout) {
    const auto t=nowUs();auto rc=deviceProc<PFN_vkWaitForFences>(d,"vkWaitForFences")(d,n,fences,all,timeout);const auto elapsed=nowUs()-t;record(FenceWait,elapsed,rc);
    if(elapsed>=100000){
        workload::Summary work;uint32_t known=0;uint64_t first=0,last=0;
        for(uint32_t i=0;i<n;++i){FenceSubmission f;if(fenceSnapshot(d,fences[i],f)){++known;work.merge(f.work);if(!first||f.serial<first)first=f.serial;last=std::max(last,f.serial);}}
        fprintf(stderr,"GNU_X64_FENCE slow_wait_us=%" PRIu64 " result=%d wait_all=%u fence_count=%u associated_fences=%u first_submit_serial=%" PRIu64 " last_submit_serial=%" PRIu64 " end_us=%" PRIu64 " includes_prior_queue_work=1",elapsed,rc,unsigned(all),n,known,first,last,nowUs());workload::print(work);fprintf(stderr,"\n");
    }
    return rc;
}
static VkResult VKAPI_CALL waitSemaphores(VkDevice d,const VkSemaphoreWaitInfo* info,uint64_t timeout) {
    auto f=deviceProc<PFN_vkWaitSemaphores>(d,"vkWaitSemaphores");if(!f)return VK_ERROR_EXTENSION_NOT_PRESENT;
    const auto t=nowUs();auto rc=f(d,info,timeout);record(SemaphoreWait,nowUs()-t,rc);return rc;
}
static VkResult VKAPI_CALL queueIdle(VkQueue queue) {
    const auto t=nowUs();auto rc=vkQueueWaitIdle(queue);record(QueueIdle,nowUs()-t,rc);return rc;
}
static void VKAPI_CALL destroyDevice(VkDevice device,const VkAllocationCallbacks* alloc) {
    {std::lock_guard<std::mutex> lock(registryMutex);
     bool pending=false;
     for(const auto& entry:registry)if(entry.second->device==device)
         for(const auto& slot:entry.second->slots)pending|=slot.pending;
     // Our final marker is after the application's fence. The application may
     // legally destroy its device once that fence signals, so finish our own
     // trailing work before freeing the query/command pools. This runs only at
     // destruction, never in the measured render loop.
     if(pending)deviceProc<PFN_vkDeviceWaitIdle>(device,"vkDeviceWaitIdle")(device);
     for(auto it=registry.begin();it!=registry.end();)if(it->second->device==device){destroy(*it->second);it=registry.erase(it);}else ++it;}
    {std::lock_guard<std::mutex> lock(fenceMutex);for(auto i=fenceSubmissions.begin();i!=fenceSubmissions.end();)if(i->first.first==device)i=fenceSubmissions.erase(i);else ++i;}
    workload::clearDevice(device);vkDestroyDevice(device,alloc);
}
static PFN_vkVoidFunction proc(const char* name) {
    if(!enabled())return nullptr;
    if(auto work=workload::proc(name))return work;
#define PROFILE_PROC(vk,fn) if(!strcmp(name,vk))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    PROFILE_PROC("vkGetDeviceQueue",getQueue);PROFILE_PROC("vkDestroyDevice",destroyDevice);
    PROFILE_PROC("vkQueueSubmit",submit);PROFILE_PROC("vkQueueSubmit2",submit2);PROFILE_PROC("vkQueueSubmit2KHR",submit2);
    PROFILE_PROC("vkQueuePresentKHR",presentCall);PROFILE_PROC("vkAcquireNextImageKHR",acquire);
    PROFILE_PROC("vkResetFences",resetApplicationFences);PROFILE_PROC("vkDestroyFence",destroyApplicationFence);
    PROFILE_PROC("vkWaitForFences",waitFences);PROFILE_PROC("vkWaitSemaphores",waitSemaphores);PROFILE_PROC("vkWaitSemaphoresKHR",waitSemaphores);
    PROFILE_PROC("vkQueueWaitIdle",queueIdle);
#undef PROFILE_PROC
    return nullptr;
}
}

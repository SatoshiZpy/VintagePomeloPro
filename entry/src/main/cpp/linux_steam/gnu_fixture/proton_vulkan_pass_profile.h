// Included inside x64profile. Opt-in, bounded recording-time diagnostics.
// Adjacent BOTTOM timestamps are completion-frontier progress, NOT isolated
// shader costs. No new barrier, wait, queue submit or idle is introduced here.
namespace passprofile {
static bool enabled() {
    static const bool on=[] {FILE* f=fopen("/data/storage/el2/base/files/steamohos-native/proton-probe/vulkan-pass-profile","rb");if(!f)return false;bool v=fgetc(f)=='1';fclose(f);return v;}();return on;
}
// A multiview timestamp can consume up to 32 consecutive query indices.
// Reserve this stride even for ordinary passes; inspect only each first index.
static constexpr uint32_t MaxPoints=128, QueryStep=32, MaxSlots=8;
struct Attachment {uint32_t colors=0,depth=0,colorFormat=0,depthFormat=0,samples=0,depthLoad=0,depthStore=0;bool known=false;};
struct Point : Attachment {
    const char* label="";uint32_t pass=0,subpass=0,width=0,height=0;
    uint64_t draws=0,dispatches=0,pipeline=0,units=0;uint32_t viewportW=0,viewportH=0,scissorW=0,scissorH=0;bool mixed=false;
};
struct Slot {VkQueryPool queries=VK_NULL_HANDLE;bool used=false;};
struct Pool {uint32_t family=0,bits=0;float period=0;bool usable=false;};
struct Command {
    VkDevice device=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;VkCommandBufferLevel level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    int slot=-1;uint64_t record=0,submit=0;uint32_t submits=0;bool valid=false,truncated=false,firstWork=false;
    Pool config;Point point;VkRenderPass renderPass=VK_NULL_HANDLE;uint32_t viewportW=0,viewportH=0,scissorW=0,scissorH=0;uint32_t group=0;std::vector<Point> points;
};
static std::mutex mutex;
static std::map<std::pair<VkDevice,VkCommandPool>,Pool> pools;
static std::map<VkDevice,std::array<Slot,MaxSlots>> slots;
static std::map<VkCommandBuffer,Command> commands;
static uint64_t recordSerial=0;
static std::map<std::pair<VkDevice,VkRenderPass>,std::vector<Attachment>> renderPasses;
static VkResult VKAPI_CALL createPool(VkDevice d,const VkCommandPoolCreateInfo* info,const VkAllocationCallbacks* a,VkCommandPool* out) {
    auto rc=deviceProc<PFN_vkCreateCommandPool>(d,"vkCreateCommandPool")(d,info,a,out);if(rc!=VK_SUCCESS)return rc;
    Pool p;p.family=info->queueFamilyIndex;
    Device dev;{std::lock_guard<std::recursive_mutex> lock(stateMutex);dev=devices.at(d);}
    auto properties=instanceProc<PFN_vkGetPhysicalDeviceProperties>(dev.instance,"vkGetPhysicalDeviceProperties");
    auto families=instanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(dev.instance,"vkGetPhysicalDeviceQueueFamilyProperties");
    if(properties&&families&&!(info->flags&VK_COMMAND_POOL_CREATE_PROTECTED_BIT)) {
        VkPhysicalDeviceProperties props{};properties(dev.physical,&props);uint32_t n=0;families(dev.physical,&n,nullptr);
        std::vector<VkQueueFamilyProperties> f(n);families(dev.physical,&n,f.data());
        if(p.family<n&&(f[p.family].queueFlags&VK_QUEUE_GRAPHICS_BIT)){p.bits=f[p.family].timestampValidBits;p.period=props.limits.timestampPeriod;p.usable=p.bits&&p.period>0;}
    }
    std::lock_guard<std::mutex> lock(mutex);pools[{d,*out}]=p;return rc;
}
static void allocate(VkDevice d,const VkCommandBufferAllocateInfo* info,const VkCommandBuffer* out) {
    if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);
    for(uint32_t i=0;i<info->commandBufferCount;++i){Command c;c.device=d;c.pool=info->commandPool;c.level=info->level;commands[out[i]]=c;}
}
static void mark(VkCommandBuffer cb,Command& c,const char* label,bool final=false) {
    if(c.slot<0||!c.valid)return;
    if(c.points.size()>=MaxPoints-(final?0:1)){c.truncated=true;return;}
    c.point.label=label;c.points.push_back(c.point);
    auto stage=c.points.size()==1?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deviceProc<PFN_vkCmdWriteTimestamp>(c.device,"vkCmdWriteTimestamp")(cb,stage,slots.at(c.device)[c.slot].queries,uint32_t(c.points.size()-1)*QueryStep);
    c.group=0;c.point.pipeline=0;c.point.mixed=false;c.point.viewportW=c.point.viewportH=c.point.scissorW=c.point.scissorH=0;
}
// Called only when the application legally resets/re-begins/frees a command
// buffer/pool: its previous recording is no longer pending. ONE_TIME only, so
// query values cannot belong to a concurrent or repeated execution. No fence
// polling/waits are added; unavailable results are discarded, never blocked on.
static void retire(Command& c) {
    if(c.slot<0)return;auto& slot=slots.at(c.device)[c.slot];
    if(c.valid&&c.submits==1&&c.points.size()>1) {
        struct Value {uint64_t ticks,available;};const uint32_t n=uint32_t(c.points.size()-1)*QueryStep+1;
        std::vector<Value> values(n);
        const auto rc=deviceProc<PFN_vkGetQueryPoolResults>(c.device,"vkGetQueryPoolResults")(c.device,slot.queries,0,n,values.size()*sizeof(Value),values.data(),sizeof(Value),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        bool ready=rc==VK_SUCCESS||rc==VK_NOT_READY;for(uint32_t i=0;i<c.points.size();++i)ready&=values[i*QueryStep].available!=0;
        if(ready){
            const uint64_t mask=c.config.bits>=64?UINT64_MAX:((uint64_t(1)<<c.config.bits)-1);
            auto us=[&](unsigned a,unsigned b){return double((values[b*QueryStep].ticks-values[a*QueryStep].ticks)&mask)*c.config.period/1000.0;};
            fprintf(stderr,"GNU_X64_PASS sample=%" PRIu64 " submit_serial=%" PRIu64 " family=%u points=%zu truncated=%u command_span_us=%.3f query_wait=0 metric=completion_frontier\n",c.record,c.submit,c.config.family,c.points.size(),unsigned(c.truncated),us(0,unsigned(c.points.size()-1)));
            for(unsigned i=1;i<c.points.size();++i){const auto& p=c.points[i];const auto& prev=c.points[i-1];
                fprintf(stderr,"GNU_X64_GROUP sample=%" PRIu64 " submit_serial=%" PRIu64 " point=%u from=%s to=%s pass=%u subpass=%u width=%u height=%u attachments_known=%u colors=%u depth=%u color_format=%u depth_format=%u samples=%u depth_load=%u depth_store=%u draws=%" PRIu64 " dispatches=%" PRIu64 " vertices_or_indices=%" PRIu64 " viewport=%ux%u scissor=%ux%u last_pipeline=%" PRIu64 " mixed_pipeline=%u completion_us=%.3f elapsed_us=%.3f\n",c.record,c.submit,i,prev.label,p.label,p.pass,p.subpass,p.width,p.height,unsigned(p.known),p.colors,p.depth,p.colorFormat,p.depthFormat,p.samples,p.depthLoad,p.depthStore,p.draws-prev.draws,p.dispatches-prev.dispatches,p.units-prev.units,p.viewportW,p.viewportH,p.scissorW,p.scissorH,p.pipeline,unsigned(p.mixed),us(i-1,i),us(0,i));
            }
        } else fprintf(stderr,"GNU_X64_PASS dropped=unavailable sample=%" PRIu64 " submit_serial=%" PRIu64 " result=%d\n",c.record,c.submit,rc);
    }
    slot.used=false;c.slot=-1;c.points.clear();
}
static void begin(VkCommandBuffer cb,const VkCommandBufferBeginInfo* info) {
    if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);auto it=commands.find(cb);if(it==commands.end())return;
    auto& c=it->second;retire(c);c.point={};c.renderPass=VK_NULL_HANDLE;c.viewportW=c.viewportH=c.scissorW=c.scissorH=0;c.group=0;c.submit=0;c.submits=0;c.valid=false;c.truncated=false;c.record=++recordSerial;
    auto p=pools.find({c.device,c.pool});
    if(c.record%32||c.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY||!(info->flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)||(info->flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)||p==pools.end()||!p->second.usable)return;
    c.config=p->second;auto& deviceSlots=slots[c.device];
    for(unsigned i=0;i<MaxSlots;++i)if(!deviceSlots[i].used){
        auto& s=deviceSlots[i];if(!s.queries){VkQueryPoolCreateInfo q{};q.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;q.queryType=VK_QUERY_TYPE_TIMESTAMP;q.queryCount=MaxPoints*QueryStep;
            auto rc=deviceProc<PFN_vkCreateQueryPool>(c.device,"vkCreateQueryPool")(c.device,&q,nullptr,&s.queries);if(rc!=VK_SUCCESS){fprintf(stderr,"GNU_X64_PASS allocation_failed=%d\n",rc);return;}}
        s.used=true;c.slot=int(i);c.valid=true;
        deviceProc<PFN_vkCmdResetQueryPool>(c.device,"vkCmdResetQueryPool")(cb,s.queries,0,MaxPoints*QueryStep);
        mark(cb,c,"command_begin");mark(cb,c,"command_ready");return;
    }
}
static void reset(VkCommandBuffer cb) {if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);auto i=commands.find(cb);if(i!=commands.end()){retire(i->second);i->second.valid=false;}}
static void resetPool(VkDevice d,VkCommandPool p,bool remove) {if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);for(auto i=commands.begin();i!=commands.end();)if(i->second.device==d&&i->second.pool==p){retire(i->second);i->second.valid=false;if(remove)i=commands.erase(i);else ++i;}else ++i;if(remove)pools.erase({d,p});}
static void free(VkDevice d,uint32_t n,const VkCommandBuffer* cb) {if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<n;++i){auto it=commands.find(cb[i]);if(it!=commands.end()&&it->second.device==d){retire(it->second);commands.erase(it);}}}
static void submitted(uint64_t serial,uint32_t n,const VkSubmitInfo* info) {if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);for(uint32_t i=0;i<n;++i)for(uint32_t j=0;j<info[i].commandBufferCount;++j){auto it=commands.find(info[i].pCommandBuffers[j]);if(it!=commands.end()&&it->second.slot>=0){it->second.submit=serial;++it->second.submits;}}}
static void work(VkCommandBuffer cb,bool compute,uint64_t pipeline,uint64_t units=0) {
    if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);auto i=commands.find(cb);if(i==commands.end()||i->second.slot<0)return;auto& c=i->second;
    const uint32_t groupSize=c.point.width>=4096&&c.point.height>=4096?1:16;
    if(c.firstWork){if(groupSize==1)mark(cb,c,"before_first_draw");c.firstWork=false;}
    if(c.group>=groupSize)mark(cb,c,"draw_group");++c.group;if(compute)++c.point.dispatches;else ++c.point.draws;
    c.point.units+=units;c.point.viewportW=std::max(c.point.viewportW,c.viewportW);c.point.viewportH=std::max(c.point.viewportH,c.viewportH);c.point.scissorW=std::max(c.point.scissorW,c.scissorW);c.point.scissorH=std::max(c.point.scissorH,c.scissorH);
    if(c.point.pipeline&&c.point.pipeline!=pipeline)c.point.mixed=true;c.point.pipeline=pipeline;
}
static void boundary(VkCommandBuffer cb,const char* label,bool final=false) {if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);auto i=commands.find(cb);if(i!=commands.end())mark(cb,i->second,label,final);}
static void pass(VkCommandBuffer cb,uint32_t w,uint32_t h,uint32_t colors,uint32_t depth,VkRenderPass rp=VK_NULL_HANDLE) {std::lock_guard<std::mutex> lock(mutex);auto i=commands.find(cb);if(i==commands.end())return;auto& c=i->second;mark(cb,c,"before_pass");++c.point.pass;c.firstWork=true;c.point.subpass=0;c.point.width=w;c.point.height=h;static_cast<Attachment&>(c.point)={};c.point.colors=colors;c.point.depth=depth;c.renderPass=rp;auto meta=renderPasses.find({c.device,rp});if(meta!=renderPasses.end()&&!meta->second.empty())static_cast<Attachment&>(c.point)=meta->second.front();}
static void passReady(VkCommandBuffer cb){std::lock_guard<std::mutex> lock(mutex);auto i=commands.find(cb);if(i!=commands.end()&&i->second.point.width>=4096&&i->second.point.height>=4096)mark(cb,i->second,"after_pass_begin");}
static void next(VkCommandBuffer cb){std::lock_guard<std::mutex> lock(mutex);auto i=commands.find(cb);if(i==commands.end())return;mark(cb,i->second,"subpass_end");auto& c=i->second;++c.point.subpass;auto meta=renderPasses.find({c.device,c.renderPass});if(meta!=renderPasses.end()&&c.point.subpass<meta->second.size())static_cast<Attachment&>(c.point)=meta->second[c.point.subpass];}
static VkDevice device(VkCommandBuffer cb){std::lock_guard<std::mutex> lock(mutex);return commands.at(cb).device;}
static VkResult VKAPI_CALL end(VkCommandBuffer cb){boundary(cb,"command_end",true);auto rc=deviceProc<PFN_vkEndCommandBuffer>(device(cb),"vkEndCommandBuffer")(cb);if(rc!=VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);commands.at(cb).valid=false;}return rc;}
static void VKAPI_CALL beginRenderPass(VkCommandBuffer cb,const VkRenderPassBeginInfo* info,VkSubpassContents contents){pass(cb,info->renderArea.extent.width,info->renderArea.extent.height,0,0,info->renderPass);deviceProc<PFN_vkCmdBeginRenderPass>(device(cb),"vkCmdBeginRenderPass")(cb,info,contents);passReady(cb);}
static void VKAPI_CALL endRenderPass(VkCommandBuffer cb){boundary(cb,"pass_body_end");deviceProc<PFN_vkCmdEndRenderPass>(device(cb),"vkCmdEndRenderPass")(cb);boundary(cb,"pass_end");}
static void VKAPI_CALL nextSubpass(VkCommandBuffer cb,VkSubpassContents contents){next(cb);deviceProc<PFN_vkCmdNextSubpass>(device(cb),"vkCmdNextSubpass")(cb,contents);boundary(cb,"subpass_begin");}
// Resolve core/KHR aliases without changing the application's argument objects.
template<class T>static T alias(VkDevice d,const char* core,const char* khr){auto f=deviceProc<T>(d,core);return f?f:deviceProc<T>(d,khr);}
static void VKAPI_CALL beginRendering(VkCommandBuffer cb,const VkRenderingInfo* info){pass(cb,info->renderArea.extent.width,info->renderArea.extent.height,info->colorAttachmentCount,info->pDepthAttachment!=nullptr);alias<PFN_vkCmdBeginRendering>(device(cb),"vkCmdBeginRendering","vkCmdBeginRenderingKHR")(cb,info);passReady(cb);}
static void VKAPI_CALL endRendering(VkCommandBuffer cb){boundary(cb,"pass_body_end");alias<PFN_vkCmdEndRendering>(device(cb),"vkCmdEndRendering","vkCmdEndRenderingKHR")(cb);boundary(cb,"pass_end");}
static void VKAPI_CALL beginRenderPass2(VkCommandBuffer cb,const VkRenderPassBeginInfo* info,const VkSubpassBeginInfo* sub){pass(cb,info->renderArea.extent.width,info->renderArea.extent.height,0,0,info->renderPass);alias<PFN_vkCmdBeginRenderPass2>(device(cb),"vkCmdBeginRenderPass2","vkCmdBeginRenderPass2KHR")(cb,info,sub);passReady(cb);}
static void VKAPI_CALL endRenderPass2(VkCommandBuffer cb,const VkSubpassEndInfo* info){boundary(cb,"pass_body_end");alias<PFN_vkCmdEndRenderPass2>(device(cb),"vkCmdEndRenderPass2","vkCmdEndRenderPass2KHR")(cb,info);boundary(cb,"pass_end");}
static void VKAPI_CALL nextSubpass2(VkCommandBuffer cb,const VkSubpassBeginInfo* begin,const VkSubpassEndInfo* end){next(cb);alias<PFN_vkCmdNextSubpass2>(device(cb),"vkCmdNextSubpass2","vkCmdNextSubpass2KHR")(cb,begin,end);boundary(cb,"subpass_begin");}
// Attachment metadata is read-only and follows render-pass handle lifetime.
template<class Info>static std::vector<Attachment> attachmentInfo(const Info* info){std::vector<Attachment> result;for(uint32_t i=0;i<info->subpassCount;++i){const auto& sub=info->pSubpasses[i];Attachment a;a.known=true;a.colors=sub.colorAttachmentCount;
    for(uint32_t j=0;j<sub.colorAttachmentCount;++j){auto idx=sub.pColorAttachments[j].attachment;if(idx!=VK_ATTACHMENT_UNUSED&&idx<info->attachmentCount){a.colorFormat=info->pAttachments[idx].format;a.samples=info->pAttachments[idx].samples;break;}}
    if(sub.pDepthStencilAttachment){auto idx=sub.pDepthStencilAttachment->attachment;if(idx!=VK_ATTACHMENT_UNUSED&&idx<info->attachmentCount){const auto& desc=info->pAttachments[idx];a.depth=1;a.depthFormat=desc.format;a.samples=desc.samples;a.depthLoad=desc.loadOp;a.depthStore=desc.storeOp;}}
    result.push_back(a);}return result;}
static VkResult VKAPI_CALL createRenderPass(VkDevice d,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks* alloc,VkRenderPass* out){auto rc=deviceProc<PFN_vkCreateRenderPass>(d,"vkCreateRenderPass")(d,info,alloc,out);if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);renderPasses[{d,*out}]=attachmentInfo(info);}return rc;}
static VkResult VKAPI_CALL createRenderPass2(VkDevice d,const VkRenderPassCreateInfo2* info,const VkAllocationCallbacks* alloc,VkRenderPass* out){auto rc=alias<PFN_vkCreateRenderPass2>(d,"vkCreateRenderPass2","vkCreateRenderPass2KHR")(d,info,alloc,out);if(rc==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);renderPasses[{d,*out}]=attachmentInfo(info);}return rc;}
static void VKAPI_CALL destroyRenderPass(VkDevice d,VkRenderPass rp,const VkAllocationCallbacks* alloc){deviceProc<PFN_vkDestroyRenderPass>(d,"vkDestroyRenderPass")(d,rp,alloc);std::lock_guard<std::mutex> lock(mutex);renderPasses.erase({d,rp});}
static void VKAPI_CALL viewport(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkViewport* v){auto d=device(cb);deviceProc<PFN_vkCmdSetViewport>(d,"vkCmdSetViewport")(cb,first,count,v);if(!first&&count){std::lock_guard<std::mutex> lock(mutex);auto& c=commands.at(cb);c.viewportW=uint32_t(v[0].width<0?-v[0].width:v[0].width);c.viewportH=uint32_t(v[0].height<0?-v[0].height:v[0].height);}}
static void VKAPI_CALL scissor(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkRect2D* v){auto d=device(cb);deviceProc<PFN_vkCmdSetScissor>(d,"vkCmdSetScissor")(cb,first,count,v);if(!first&&count){std::lock_guard<std::mutex> lock(mutex);auto& c=commands.at(cb);c.scissorW=v[0].extent.width;c.scissorH=v[0].extent.height;}}
static void clearDevice(VkDevice d){if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);for(auto i=commands.begin();i!=commands.end();)if(i->second.device==d){retire(i->second);i=commands.erase(i);}else ++i;for(auto& s:slots[d])if(s.queries)deviceProc<PFN_vkDestroyQueryPool>(d,"vkDestroyQueryPool")(d,s.queries,nullptr);slots.erase(d);for(auto i=renderPasses.begin();i!=renderPasses.end();)if(i->first.first==d)i=renderPasses.erase(i);else ++i;for(auto i=pools.begin();i!=pools.end();)if(i->first.first==d)i=pools.erase(i);else ++i;}
static PFN_vkVoidFunction proc(const char* name){if(!enabled())return nullptr;
#define PASS_PROC(vk,fn) if(!strcmp(name,vk))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    PASS_PROC("vkCreateRenderPass",createRenderPass);PASS_PROC("vkCreateRenderPass2",createRenderPass2);PASS_PROC("vkCreateRenderPass2KHR",createRenderPass2);PASS_PROC("vkDestroyRenderPass",destroyRenderPass);
    PASS_PROC("vkCmdSetViewport",viewport);PASS_PROC("vkCmdSetScissor",scissor);
    PASS_PROC("vkCreateCommandPool",createPool);PASS_PROC("vkEndCommandBuffer",end);
    PASS_PROC("vkCmdBeginRenderPass",beginRenderPass);PASS_PROC("vkCmdEndRenderPass",endRenderPass);PASS_PROC("vkCmdNextSubpass",nextSubpass);
    PASS_PROC("vkCmdBeginRendering",beginRendering);PASS_PROC("vkCmdBeginRenderingKHR",beginRendering);PASS_PROC("vkCmdEndRendering",endRendering);PASS_PROC("vkCmdEndRenderingKHR",endRendering);
    PASS_PROC("vkCmdBeginRenderPass2",beginRenderPass2);PASS_PROC("vkCmdBeginRenderPass2KHR",beginRenderPass2);PASS_PROC("vkCmdEndRenderPass2",endRenderPass2);PASS_PROC("vkCmdEndRenderPass2KHR",endRenderPass2);PASS_PROC("vkCmdNextSubpass2",nextSubpass2);PASS_PROC("vkCmdNextSubpass2KHR",nextSubpass2);
#undef PASS_PROC
    return nullptr;
}
}

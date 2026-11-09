// Reuse the existing adapter mocks; the old test main is not invoked here.
#define main existingProfileTestMain
#include "proton_vulkan_profile_test.cpp"
#undef main
using namespace x64profile;
namespace detailtest {
static unsigned queryCreates=0,queryDestroys=0,reads=0,renderCalls=0,endCalls=0;
static bool available=true;static const void* originalInfo=nullptr;
static std::map<VkQueryPool,std::vector<uint32_t>> writes;
static VkResult VKAPI_CALL createQueries(VkDevice,const VkQueryPoolCreateInfo* info,const VkAllocationCallbacks*,VkQueryPool* out){assert(info->queryCount==passprofile::MaxPoints*32&&info->queryType==VK_QUERY_TYPE_TIMESTAMP);*out=reinterpret_cast<VkQueryPool>(uintptr_t(6000+ ++queryCreates));return VK_SUCCESS;}
static void VKAPI_CALL destroyQueries(VkDevice,VkQueryPool,const VkAllocationCallbacks*){++queryDestroys;}
static void VKAPI_CALL queryReset(VkCommandBuffer,VkQueryPool q,uint32_t first,uint32_t count){assert(first==0&&count==passprofile::MaxPoints*32);writes[q].clear();}
static void VKAPI_CALL timestamp(VkCommandBuffer,VkPipelineStageFlagBits stage,VkQueryPool q,uint32_t index){assert(index==writes[q].size()*32&&index+32<=passprofile::MaxPoints*32);assert(stage==(index?VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));writes[q].push_back(index);}
static VkResult VKAPI_CALL results(VkDevice,VkQueryPool q,uint32_t first,uint32_t count,size_t bytes,void* dst,VkDeviceSize stride,VkQueryResultFlags flags){assert(first==0&&stride==16&&bytes==count*16&&flags==(VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));++reads;auto out=static_cast<uint64_t*>(dst);for(auto i:writes[q]){assert(i<count);out[i*2]=250+i/32*10;out[i*2+1]=available;}return VK_NOT_READY; /* unused multiview slots remain unavailable */}
static VkResult VKAPI_CALL createCommandPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out){*out=reinterpret_cast<VkCommandPool>(900);return VK_SUCCESS;}
static VkResult VKAPI_CALL end(VkCommandBuffer){++endCalls;return VK_SUCCESS;}
static void VKAPI_CALL beginRendering(VkCommandBuffer,const VkRenderingInfo* info){assert(info==originalInfo);++renderCalls;}
static void VKAPI_CALL endRendering(VkCommandBuffer){++renderCalls;}
static void VKAPI_CALL beginRenderPass(VkCommandBuffer,const VkRenderPassBeginInfo* info,VkSubpassContents c){assert(info==originalInfo&&c==VK_SUBPASS_CONTENTS_INLINE);++renderCalls;}
static void VKAPI_CALL endRenderPass(VkCommandBuffer){++renderCalls;}
static void VKAPI_CALL properties(VkPhysicalDevice,VkPhysicalDeviceProperties* p){p->limits.timestampPeriod=1000;}
static void VKAPI_CALL families(VkPhysicalDevice,uint32_t* n,VkQueueFamilyProperties* p){*n=1;if(p){p->queueFlags=VK_QUEUE_GRAPHICS_BIT;p->timestampValidBits=8;}}
static VkResult VKAPI_CALL createRenderPass(VkDevice,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks*,VkRenderPass* out){assert(info==originalInfo);*out=reinterpret_cast<VkRenderPass>(9001);return VK_SUCCESS;}
static void VKAPI_CALL destroyRenderPass(VkDevice,VkRenderPass,const VkAllocationCallbacks*){}
static void VKAPI_CALL viewport(VkCommandBuffer,uint32_t first,uint32_t count,const VkViewport* v){assert(first==0&&count==1&&v==originalInfo);}
static void VKAPI_CALL scissor(VkCommandBuffer,uint32_t first,uint32_t count,const VkRect2D* v){assert(first==0&&count==1&&v==originalInfo);}
static PFN_vkVoidFunction device(const char* name){
#define STUB(vk,fn) if(!strcmp(name,vk))return reinterpret_cast<PFN_vkVoidFunction>(fn)
 STUB("vkCreateRenderPass",createRenderPass);STUB("vkDestroyRenderPass",destroyRenderPass);STUB("vkCmdSetViewport",viewport);STUB("vkCmdSetScissor",scissor);
 STUB("vkCreateQueryPool",createQueries);STUB("vkDestroyQueryPool",destroyQueries);STUB("vkCmdResetQueryPool",queryReset);STUB("vkCmdWriteTimestamp",timestamp);STUB("vkGetQueryPoolResults",results);STUB("vkCreateCommandPool",createCommandPool);STUB("vkEndCommandBuffer",end);STUB("vkCmdBeginRendering",beginRendering);STUB("vkCmdEndRendering",endRendering);STUB("vkCmdBeginRenderPass",beginRenderPass);STUB("vkCmdEndRenderPass",endRenderPass);
#undef STUB
 return nullptr;
}
static PFN_vkVoidFunction instance(const char* name){if(!strcmp(name,"vkGetPhysicalDeviceProperties"))return reinterpret_cast<PFN_vkVoidFunction>(properties);if(!strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties"))return reinterpret_cast<PFN_vkVoidFunction>(families);return nullptr;}
}
int main(){
 using namespace detailtest;profiling=passProfiling=true;extraDeviceProc=device;extraInstanceProc=instance;
 auto d=reinterpret_cast<VkDevice>(2);devices[d]={reinterpret_cast<VkInstance>(3),reinterpret_cast<VkPhysicalDevice>(4)};
 VkCommandPoolCreateInfo pi{};pi.queueFamilyIndex=0;VkCommandPool pool{};assert(passprofile::createPool(d,&pi,nullptr,&pool)==VK_SUCCESS);
 VkCommandBufferAllocateInfo ai{};ai.commandPool=pool;ai.commandBufferCount=10;VkCommandBuffer cb[10];assert(workload::allocate(d,&ai,cb)==VK_SUCCESS);
 VkCommandBufferBeginInfo bi{};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
 passprofile::recordSerial=31;assert(workload::begin(cb[0],&bi)==VK_SUCCESS);assert(queryCreates==1);
 VkRenderingInfo ri{};ri.renderArea.extent={800,600};ri.viewMask=0xffffffff;ri.colorAttachmentCount=1;originalInfo=&ri;
 passprofile::beginRendering(cb[0],&ri);for(unsigned i=0;i<33;++i)passprofile::work(cb[0],false,i<16?5:6);passprofile::endRendering(cb[0]);assert(passprofile::end(cb[0])==VK_SUCCESS);
 auto& c=passprofile::commands.at(cb[0]);assert(c.points.size()==8&&c.points[3].draws==16&&c.points[4].draws==32&&c.points[5].draws==33&&c.points[5].width==800);
 VkSubmitInfo si{};si.commandBufferCount=1;si.pCommandBuffers=cb;passprofile::submitted(123,1,&si);
 assert(reads==0);passprofile::reset(cb[0]);assert(reads==1&&c.slot==-1); // available first slots accepted even though unused slots return NOT_READY
 assert(idleCalls==0&&renderCalls==2&&endCalls==1);
 passprofile::recordSerial=63;assert(workload::begin(cb[0],&bi)==VK_SUCCESS);VkRenderPassBeginInfo legacy{};legacy.renderArea.extent={128,64};originalInfo=&legacy;passprofile::beginRenderPass(cb[0],&legacy,VK_SUBPASS_CONTENTS_INLINE);for(unsigned i=0;i<3000;++i)passprofile::work(cb[0],false,7);passprofile::endRenderPass(cb[0]);passprofile::end(cb[0]);assert(c.points.size()==passprofile::MaxPoints&&c.truncated&&!strcmp(c.points.back().label,"command_end"));passprofile::submitted(124,1,&si);available=false;passprofile::reset(cb[0]);assert(reads==2&&c.slot==-1);available=true;
 // All slots pinned by recordings: skip instead of reusing or waiting.
 for(unsigned i=0;i<9;++i){passprofile::recordSerial=95;passprofile::begin(cb[i],&bi);}assert(queryCreates==8&&passprofile::commands.at(cb[8]).slot==-1);
 passprofile::resetPool(d,pool,false);for(auto& s:passprofile::slots[d])assert(!s.used);
 // Secondary, reusable and simultaneous recordings are never instrumented.
 passprofile::commands.at(cb[0]).level=VK_COMMAND_BUFFER_LEVEL_SECONDARY;passprofile::recordSerial=127;passprofile::begin(cb[0],&bi);assert(c.slot==-1);passprofile::commands.at(cb[0]).level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
 bi.flags=0;passprofile::recordSerial=127;passprofile::begin(cb[0],&bi);assert(c.slot==-1);bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT|VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;passprofile::recordSerial=127;passprofile::begin(cb[0],&bi);assert(c.slot==-1);
 bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;passprofile::recordSerial=127;passprofile::begin(cb[0],&bi);passprofile::end(cb[0]);passprofile::submitted(125,1,&si);passprofile::submitted(126,1,&si);passprofile::reset(cb[0]);assert(reads==2); // repeated execution rejected
 // Large depth-only passes are split per draw; preserve attachment and viewport metadata.
 VkAttachmentDescription ad{};ad.format=VK_FORMAT_D32_SFLOAT;ad.samples=VK_SAMPLE_COUNT_1_BIT;ad.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;ad.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 VkAttachmentReference depth{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pDepthStencilAttachment=&depth;
 VkRenderPassCreateInfo rpInfo{};rpInfo.attachmentCount=1;rpInfo.pAttachments=&ad;rpInfo.subpassCount=1;rpInfo.pSubpasses=&sub;VkRenderPass rp{};originalInfo=&rpInfo;assert(passprofile::createRenderPass(d,&rpInfo,nullptr,&rp)==VK_SUCCESS);
 passprofile::recordSerial=159;passprofile::begin(cb[0],&bi);legacy.renderPass=rp;legacy.renderArea.extent={8192,8192};originalInfo=&legacy;passprofile::beginRenderPass(cb[0],&legacy,VK_SUBPASS_CONTENTS_INLINE);
 VkViewport vp{};vp.width=8192;vp.height=-8192;originalInfo=&vp;passprofile::viewport(cb[0],0,1,&vp);VkRect2D sc{};sc.extent={8192,8192};originalInfo=&sc;passprofile::scissor(cb[0],0,1,&sc);
 passprofile::work(cb[0],false,22,3);passprofile::work(cb[0],false,23,3);passprofile::endRenderPass(cb[0]);passprofile::end(cb[0]);assert(c.points[5].draws==1&&c.points[5].units==3&&c.points[5].pipeline==22&&c.points[5].known&&c.points[5].colors==0&&c.points[5].depthFormat==VK_FORMAT_D32_SFLOAT&&c.points[5].viewportH==8192&&c.points[5].scissorW==8192);assert(c.points[6].draws==2&&c.points[6].pipeline==23);assert(!strcmp(c.points[3].label,"after_pass_begin")&&!strcmp(c.points[4].label,"before_first_draw")&&c.points[3].draws==0&&c.points[4].draws==0);
 passprofile::destroyRenderPass(d,rp,nullptr);assert(passprofile::renderPasses.empty()); // already recorded snapshots remain valid
 passprofile::submitted(127,1,&si);passprofile::reset(cb[0]);assert(reads==3);
 passprofile::clearDevice(d);assert(queryDestroys==8&&passprofile::commands.empty()&&idleCalls==0);
 VkPipelineShaderStageCreateInfo stages[2]{};stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[1].stage=VK_SHADER_STAGE_GEOMETRY_BIT;VkPipelineRasterizationStateCreateInfo rs{};rs.rasterizerDiscardEnable=VK_TRUE;VkDynamicState ds=VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE;VkPipelineDynamicStateCreateInfo dyn{};dyn.dynamicStateCount=1;dyn.pDynamicStates=&ds;VkGraphicsPipelineCreateInfo gp{};gp.stageCount=2;gp.pStages=stages;gp.pRasterizationState=&rs;gp.pDynamicState=&dyn;auto state=workload::pipelineState(gp);assert(state.stages==9&&state.discard==1&&state.discardDynamic==1);assert(gp.pStages==stages&&rs.rasterizerDiscardEnable==VK_TRUE);
 puts("PASS: bounded pass/group queries, multiview stride, wrap, unavailable drop, reset lifecycle, truncation final marker, full-slot skip, no waits, argument preservation, secondary/repeat/simultaneous exclusion");
}

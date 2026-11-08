/* End-to-end BGRA UAV correctness: actual D3D11 compute, clears and GPU readback.
 * No expected output is drawn by the CPU and no feature/result is overridden. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
typedef HRESULT (WINAPI *CreateFn)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
typedef HRESULT (WINAPI *CompileFn)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
static ID3D11Device* device;
static ID3D11DeviceContext* context;
static CompileFn compile;
static FILE* report;
static int pattern_failures;
static void checked(HRESULT hr,const char* name){if(FAILED(hr)){fprintf(report,"BGRA_TEST FAIL stage=%s hr=%08lx\n",name,(unsigned long)hr);fflush(report);exit(1);}}
static ID3D11ComputeShader* shader(const char* code){ID3DBlob *blob=NULL,*error=NULL;ID3D11ComputeShader* cs=NULL;HRESULT hr=compile(code,strlen(code),"bgra-test",NULL,NULL,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);if(error){fprintf(report,"compiler: %s\n",(char*)ID3D10Blob_GetBufferPointer(error));ID3D10Blob_Release(error);}checked(hr,"compile");checked(ID3D11Device_CreateComputeShader(device,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&cs),"create-shader");ID3D10Blob_Release(blob);return cs;}
static ID3D11Texture2D* texture(DXGI_FORMAT format,UINT bind){D3D11_TEXTURE2D_DESC d={0};d.Width=d.Height=d.MipLevels=d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.BindFlags=bind;ID3D11Texture2D* t=NULL;checked(ID3D11Device_CreateTexture2D(device,&d,NULL,&t),"create-texture");return t;}
static ID3D11UnorderedAccessView* uav(ID3D11Texture2D* t,DXGI_FORMAT format){D3D11_UNORDERED_ACCESS_VIEW_DESC d={0};d.Format=format;d.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;ID3D11UnorderedAccessView* v=NULL;checked(ID3D11Device_CreateUnorderedAccessView(device,(ID3D11Resource*)t,&d,&v),"create-uav");return v;}
static void pixel(const char* name,ID3D11Texture2D* src,int b0,int b1,int b2,int b3){D3D11_TEXTURE2D_DESC d;ID3D11Texture2D_GetDesc(src,&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=NULL;checked(ID3D11Device_CreateTexture2D(device,&d,NULL,&staging),"create-readback");ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)staging,(ID3D11Resource*)src);D3D11_MAPPED_SUBRESOURCE m={0};checked(ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&m),"readback");const unsigned char* p=m.pData;int expected[]={b0,b1,b2,b3};int ok=1;for(int i=0;i<4;i++)if(abs((int)p[i]-expected[i])>1)ok=0;fprintf(report,"BGRA_TEST %s stage=%s bytes=%u,%u,%u,%u expected=%d,%d,%d,%d\n",ok?"PASS":"FAIL",name,p[0],p[1],p[2],p[3],b0,b1,b2,b3);fflush(report);ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)staging,0);ID3D11Texture2D_Release(staging);if(!ok)exit(2);}
static void dispatch(ID3D11ComputeShader* cs,ID3D11UnorderedAccessView* a,ID3D11UnorderedAccessView* b){ID3D11UnorderedAccessView* views[]={a,b};ID3D11DeviceContext_CSSetShader(context,cs,NULL,0);ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,2,views,NULL);ID3D11DeviceContext_Dispatch(context,1,1,1);views[0]=views[1]=NULL;ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,2,views,NULL);}

/* Patterned atlas regression: compare GPU Load/point/linear sampling against
 * an independent CPU BC1 reference over 16K coordinates, including atlas seams. */
#define PATTERN_WIDTH 8840u
#define PATTERN_SAMPLES 16384u
static unsigned char* pattern_data;
static uint32_t pattern_hash(uint32_t v){v^=v>>16;v*=0x7feb352d;v^=v>>15;v*=0x846ca68b;return v^(v>>16);}
static void pattern_xy(unsigned i,unsigned* x,unsigned* y){
 unsigned block;
 if(i<128)block=(8192u*(i/16+1)-1+i%2)%(2210u*2210u);
 else block=pattern_hash(i)%(2210u*2210u);
 *x=(block%2210)*4+(i%4);*y=(block/2210)*4+((i/4)%4);
}
static void reference_bc1(unsigned x,unsigned y,float* out){
 x%=PATTERN_WIDTH;y%=PATTERN_WIDTH;
 const unsigned char* b=pattern_data+((y/4)*2210+x/4)*8;
 unsigned a=b[0]|b[1]<<8,c=b[2]|b[3]<<8,indices=(unsigned)b[4]|(unsigned)b[5]<<8|(unsigned)b[6]<<16|(unsigned)b[7]<<24;
 unsigned ci=(indices>>(2*((y%4)*4+x%4)))&3;
 unsigned aa[4]={(a>>11)&31,(a>>5)&63,a&31,255},cc[4]={(c>>11)&31,(c>>5)&63,c&31,255};
 for(unsigned k=0;k<3;k++){unsigned shift=k==1?6:5;aa[k]=(aa[k]<<(8-shift))|(aa[k]>>(2*shift-8));cc[k]=(cc[k]<<(8-shift))|(cc[k]>>(2*shift-8));}
 for(unsigned k=0;k<4;k++)out[k]=ci==0?aa[k]:ci==1?cc[k]:a<=c?(ci==3?0:(aa[k]+cc[k])/2.0f):ci==2?(2*aa[k]+cc[k])/3.0f:(aa[k]+2*cc[k])/3.0f;
}
static void pattern_test(void){
 const unsigned bytes=2210u*2210u*8u;pattern_data=malloc(bytes);if(!pattern_data)exit(6);
 for(unsigned i=0;i<bytes/8;i++){uint32_t a=pattern_hash(i+123),b=pattern_hash(i+739321);memcpy(pattern_data+i*8,&a,4);memcpy(pattern_data+i*8+4,&b,4);}
 D3D11_TEXTURE2D_DESC d={0};d.Width=d.Height=PATTERN_WIDTH;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_BC1_UNORM;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA init={pattern_data,2210*8,bytes};ID3D11Texture2D* src=NULL;checked(ID3D11Device_CreateTexture2D(device,&d,&init,&src),"pattern-create");
 ID3D11ShaderResourceView* srv=NULL;checked(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)src,NULL,&srv),"pattern-srv");
 d.Width=256;d.Height=PATTERN_SAMPLES/256;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET;
 ID3D11Texture2D *dst=NULL,*staging=NULL;checked(ID3D11Device_CreateTexture2D(device,&d,NULL,&dst),"pattern-target");ID3D11UnorderedAccessView* view=uav(dst,d.Format);
 d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;checked(ID3D11Device_CreateTexture2D(device,&d,NULL,&staging),"pattern-staging");
 D3D11_SAMPLER_DESC sd={0};sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
 ID3D11SamplerState* samplers[2]={NULL,NULL};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;checked(ID3D11Device_CreateSamplerState(device,&sd,&samplers[0]),"pattern-point");sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;checked(ID3D11Device_CreateSamplerState(device,&sd,&samplers[1]),"pattern-linear");
 const char* expressions[]={"src.Load(int3(p,0))","src.SampleLevel(smp,(float2(p)+0.5)/8840.0,0)","src.SampleLevel(smp,(float2(p)+float2(0.75,1.25))/8840.0,0)"};
 const char* names[]={"load","point","linear"};
 for(unsigned test=0;test<6;test++){
 unsigned filter=test%3;
 char code[2048];snprintf(code,sizeof(code),"Texture2D<float4> src:register(t0);SamplerState smp:register(s0);RWTexture2D<float4> dst:register(u0);uint hash(uint v){v^=v>>16;v*=0x7feb352d;v^=v>>15;v*=0x846ca68b;return v^(v>>16);}[numthreads(8,8,1)]void main(uint3 tid:SV_DispatchThreadID){uint i=tid.y*256+tid.x;uint b=i<128?(8192*(i/16+1)-1+i%%2)%%(2210*2210):hash(i)%%(2210*2210);uint2 p=uint2((b%%2210)*4+i%%4,(b/2210)*4+(i/4)%%4);dst[tid.xy]=%s;}",expressions[filter]);
 ID3D11ComputeShader* cs=shader(code);ID3D11DeviceContext_CSSetShader(context,cs,NULL,0);ID3D11DeviceContext_CSSetShaderResources(context,0,1,&srv);ID3D11DeviceContext_CSSetSamplers(context,0,1,&samplers[filter==2]);ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,1,&view,NULL);ID3D11DeviceContext_Dispatch(context,32,(PATTERN_SAMPLES/256)/8,1);
 ID3D11UnorderedAccessView* none=NULL;ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,1,&none,NULL);if(test>=3){
 const char* vcode="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);}";
 char pcode[2048];snprintf(pcode,sizeof(pcode),"Texture2D<float4> src:register(t0);SamplerState smp:register(s0);uint hash(uint v){v^=v>>16;v*=0x7feb352d;v^=v>>15;v*=0x846ca68b;return v^(v>>16);}float4 main(float4 pos:SV_Position):SV_Target{uint2 tid=(uint2)pos.xy;uint i=tid.y*256+tid.x;uint b=i<128?(8192*(i/16+1)-1+i%%2)%%(2210*2210):hash(i)%%(2210*2210);uint2 p=uint2((b%%2210)*4+i%%4,(b/2210)*4+(i/4)%%4);return %s;}",expressions[filter]);
 ID3DBlob *vb=NULL,*pb=NULL;checked(compile(vcode,strlen(vcode),"pattern-vs",NULL,NULL,"main","vs_5_0",0,0,&vb,NULL),"pattern-compile-vs");checked(compile(pcode,strlen(pcode),"pattern-ps",NULL,NULL,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pb,NULL),"pattern-compile-ps");
 ID3D11VertexShader* vs=NULL;ID3D11PixelShader* ps=NULL;checked(ID3D11Device_CreateVertexShader(device,ID3D10Blob_GetBufferPointer(vb),ID3D10Blob_GetBufferSize(vb),NULL,&vs),"pattern-create-vs");checked(ID3D11Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(pb),ID3D10Blob_GetBufferSize(pb),NULL,&ps),"pattern-create-ps");
 ID3D11RenderTargetView* target=NULL;checked(ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)dst,NULL,&target),"pattern-rtv");
 D3D11_RASTERIZER_DESC rd={0};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ID3D11RasterizerState* rs=NULL;checked(ID3D11Device_CreateRasterizerState(device,&rd,&rs),"pattern-rasterizer");ID3D11DeviceContext_RSSetState(context,rs);
 D3D11_VIEWPORT vp={0,0,256,PATTERN_SAMPLES/256,0,1};ID3D11DeviceContext_RSSetViewports(context,1,&vp);ID3D11DeviceContext_OMSetRenderTargets(context,1,&target,NULL);ID3D11DeviceContext_IASetPrimitiveTopology(context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11DeviceContext_VSSetShader(context,vs,NULL,0);ID3D11DeviceContext_PSSetShader(context,ps,NULL,0);ID3D11DeviceContext_PSSetShaderResources(context,0,1,&srv);ID3D11DeviceContext_PSSetSamplers(context,0,1,&samplers[filter==2]);
 const FLOAT sentinel[]={1,0,1,0};ID3D11DeviceContext_ClearRenderTargetView(context,target,sentinel);ID3D11DeviceContext_Draw(context,3,0);ID3D11RenderTargetView* noTarget=NULL;ID3D11DeviceContext_OMSetRenderTargets(context,1,&noTarget,NULL);
 ID3D11VertexShader_Release(vs);ID3D11PixelShader_Release(ps);ID3D11RenderTargetView_Release(target);ID3D11RasterizerState_Release(rs);ID3D10Blob_Release(vb);ID3D10Blob_Release(pb);
 }
 ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)staging,(ID3D11Resource*)dst);
 D3D11_MAPPED_SUBRESOURCE map={0};checked(ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&map),"pattern-readback");unsigned bad=0,maxerror=0;
 for(unsigned i=0;i<PATTERN_SAMPLES;i++){
 unsigned x,y;pattern_xy(i,&x,&y);float expected[4];reference_bc1(x,y,expected);
 if(filter==2){float b[4],c[4],e[4];reference_bc1(x+1,y,b);reference_bc1(x,y+1,c);reference_bc1(x+1,y+1,e);for(unsigned k=0;k<4;k++)expected[k]=expected[k]*0.1875f+b[k]*0.0625f+c[k]*0.5625f+e[k]*0.1875f;}
 const unsigned char* actual=(unsigned char*)map.pData+(i/256)*map.RowPitch+(i%256)*4;int mismatch=0;
 for(unsigned k=0;k<4;k++){unsigned error=abs((int)actual[k]-(int)(expected[k]+0.5f));if(error>maxerror)maxerror=error;if(error>3)mismatch=1;}
 if(mismatch){if(bad<8)fprintf(report,"BC_PATTERN mismatch mode=%s xy=%u,%u actual=%u,%u,%u,%u expected=%.1f,%.1f,%.1f,%.1f\n",names[filter],x,y,actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);bad++;}
 }
 ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)staging,0);fprintf(report,"BC_PATTERN %s stage=%s mode=%s samples=%u bad=%u max_error=%u extent=8840x8840\n",bad?"FAIL":"PASS",test<3?"cs":"ps",names[filter],PATTERN_SAMPLES,bad,maxerror);pattern_failures+=bad!=0;ID3D11ComputeShader_Release(cs);
 }
 free(pattern_data);
}

int main(void){CreateDirectoryA("C:\\bgra-shader-dump",NULL);SetEnvironmentVariableA("DXVK_SHADER_DUMP_PATH","C:\\bgra-shader-dump");SetEnvironmentVariableA("DXVK_WINEHUA_DUMP_REMAPPED_SPIRV","1");report=fopen("C:\\bgra-uav-test.log","w");if(!report)return 3;setvbuf(report,NULL,_IONBF,0);fprintf(report,"BGRA_TEST begin pointer_bytes=%u\n",(unsigned)sizeof(void*));HMODULE dxgi=LoadLibraryA("V:\\dxvk\\dxgi.dll");fprintf(report,"BGRA_TEST dll=dxgi loaded=%d error=%lu\n",dxgi!=NULL,GetLastError());HMODULE d3d=LoadLibraryA("V:\\dxvk\\d3d11.dll");fprintf(report,"BGRA_TEST dll=d3d11 loaded=%d error=%lu\n",d3d!=NULL,GetLastError());HMODULE compiler=LoadLibraryA("d3dcompiler_47.dll");fprintf(report,"BGRA_TEST dll=compiler loaded=%d error=%lu\n",compiler!=NULL,GetLastError());if(!dxgi||!d3d||!compiler){fprintf(report,"BGRA_TEST FAIL DLL error=%lu\n",GetLastError());return 4;}union{FARPROC raw;CreateFn fn;}create={.raw=GetProcAddress(d3d,"D3D11CreateDevice")};union{FARPROC raw;CompileFn fn;}c={.raw=GetProcAddress(compiler,"D3DCompile")};compile=c.fn;if(!create.fn||!compile)return 5;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;checked(create.fn(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,&level,1,D3D11_SDK_VERSION,&device,NULL,&context),"device");
ID3D11Texture2D *bgra=texture(DXGI_FORMAT_B8G8R8A8_TYPELESS,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS),*rgba=texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_UNORDERED_ACCESS);
ID3D11UnorderedAccessView *bv=uav(bgra,DXGI_FORMAT_B8G8R8A8_UNORM),*rv=uav(rgba,DXGI_FORMAT_R8G8B8A8_UNORM);
ID3D11ComputeShader* store=shader("RWTexture2D<float4> dst:register(u0);[numthreads(1,1,1)]void main(){dst[int2(0,0)]=float4(0.25,0.5,0.75,1);}");
dispatch(store,bv,NULL);pixel("typed-store-bgra",bgra,191,128,64,255);
dispatch(store,rv,NULL);pixel("same-shader-rebind-rgba",rgba,64,128,191,255);
dispatch(store,bv,NULL);pixel("rebind-back-bgra",bgra,191,128,64,255);
ID3D11ComputeShader* load=shader("RWTexture2D<float4> src:register(u0);RWTexture2D<float4> dst:register(u1);[numthreads(1,1,1)]void main(){dst[int2(0,0)]=src[int2(0,0)];}");
dispatch(load,bv,rv);pixel("typed-load-logical-rgba",rgba,64,128,191,255);
FLOAT f[]={0.125f,0.25f,0.5f,1};ID3D11DeviceContext_ClearUnorderedAccessViewFloat(context,bv,f);pixel("clear-float-bgra",bgra,128,64,32,255);
UINT u[]={0x11,0x22,0x33,0x44};ID3D11DeviceContext_ClearUnorderedAccessViewUint(context,bv,u);pixel("clear-uint-bgra",bgra,0x33,0x22,0x11,0x44);
D3D11_SHADER_RESOURCE_VIEW_DESC sd={0};sd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;ID3D11ShaderResourceView* srv=NULL;checked(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)bgra,&sd,&srv),"create-bgra-srv");
ID3D11ComputeShader* sample=shader("Texture2D<float4> src:register(t0);RWTexture2D<float4> dst:register(u0);[numthreads(1,1,1)]void main(){dst[int2(0,0)]=src.Load(int3(0,0,0));}");
ID3D11DeviceContext_CSSetShaderResources(context,0,1,&srv);dispatch(sample,rv,NULL);pixel("bgra-srv-readback",rgba,0x11,0x22,0x33,0x44);
fprintf(report,"BGRA_TEST PASS all=7\n");
/* A wide BC1 texture must use the atlas path on this 8K device. The same
 * shader is rebound to an ordinary RGBA SRV and back, exercising both
 * specialization values rather than merely translating the shader. */
D3D11_TEXTURE2D_DESC vd={0};vd.Width=8840;vd.Height=4;vd.MipLevels=vd.ArraySize=vd.SampleDesc.Count=1;vd.Format=DXGI_FORMAT_BC1_UNORM;vd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
unsigned char blocks[2210*8]={0};for(unsigned i=0;i<2210;i++){blocks[i*8]=0;blocks[i*8+1]=0xf8;}
D3D11_SUBRESOURCE_DATA init={blocks,sizeof(blocks),sizeof(blocks)};
ID3D11Texture2D* vt=NULL;checked(ID3D11Device_CreateTexture2D(device,&vd,&init,&vt),"virtual-bc1-texture");
ID3D11ShaderResourceView* vs=NULL;checked(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)vt,NULL,&vs),"virtual-bc1-srv");
ID3D11Texture2D* normal=texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE);const unsigned char green[]={0,255,0,255};ID3D11DeviceContext_UpdateSubresource(context,(ID3D11Resource*)normal,0,NULL,green,4,4);
ID3D11ShaderResourceView* ns=NULL;checked(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)normal,NULL,&ns),"normal-rgba-srv");
ID3D11ShaderResourceView* rebind[]={vs,ns,vs};
for(unsigned i=0;i<3;i++){ID3D11DeviceContext_CSSetShaderResources(context,0,1,&rebind[i]);dispatch(sample,rv,NULL);pixel(i==0?"virtual-bc1-red":i==1?"virtual-to-normal-green":"normal-to-virtual-red",rgba,i==1?0:255,i==1?255:0,0,255);}
/* Pixel-stage regression uses the same three SRV rebinds. */
const char* vcode="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);}";
const char* pcode="Texture2D<float4> src:register(t0);float4 main():SV_Target{return src.Load(int3(0,0,0));}";
ID3DBlob *vb=NULL,*pb=NULL;checked(compile(vcode,strlen(vcode),"bc-vs",NULL,NULL,"main","vs_5_0",0,0,&vb,NULL),"compile-vs");checked(compile(pcode,strlen(pcode),"bc-ps",NULL,NULL,"main","ps_5_0",0,0,&pb,NULL),"compile-ps");
ID3D11VertexShader* vshader=NULL;ID3D11PixelShader* pshader=NULL;checked(ID3D11Device_CreateVertexShader(device,ID3D10Blob_GetBufferPointer(vb),ID3D10Blob_GetBufferSize(vb),NULL,&vshader),"create-vs");checked(ID3D11Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(pb),ID3D10Blob_GetBufferSize(pb),NULL,&pshader),"create-ps");
ID3D11Texture2D* target=texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET);ID3D11RenderTargetView* rtv=NULL;checked(ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)target,NULL,&rtv),"create-rtv");
D3D11_RASTERIZER_DESC rd={0};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ID3D11RasterizerState* rs=NULL;checked(ID3D11Device_CreateRasterizerState(device,&rd,&rs),"create-rasterizer");ID3D11DeviceContext_RSSetState(context,rs);
D3D11_VIEWPORT vp={0,0,1,1,0,1};ID3D11DeviceContext_RSSetViewports(context,1,&vp);ID3D11DeviceContext_OMSetRenderTargets(context,1,&rtv,NULL);ID3D11DeviceContext_IASetPrimitiveTopology(context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11DeviceContext_VSSetShader(context,vshader,NULL,0);ID3D11DeviceContext_PSSetShader(context,pshader,NULL,0);
for(unsigned i=0;i<3;i++){ID3D11DeviceContext_PSSetShaderResources(context,0,1,&rebind[i]);ID3D11DeviceContext_Draw(context,3,0);pixel(i==0?"ps-virtual-bc1-red":i==1?"ps-virtual-to-normal-green":"ps-normal-to-virtual-red",target,i==1?0:255,i==1?255:0,0,255);}
fprintf(report,"BC_STAGE_TEST PASS all=6\n");
/* Mixed SRVs exercise all four texture-mask words and normal rebind resets. */
const unsigned slots[]={1,33,65,97,127};
for(unsigned j=0;j<5;j++){
 char code[512];snprintf(code,sizeof(code),"Texture2D<float4> a:register(t0);Texture2D<float4> b:register(t%u);RWTexture2D<float4> dst:register(u0);[numthreads(1,1,1)]void main(){dst[int2(0,0)]=(a.Load(int3(0,0,0))+b.Load(int3(0,0,0)))*0.5;}",slots[j]);
 ID3D11ComputeShader* cs=shader(code);
 for(unsigned mode=0;mode<4;mode++){
 ID3D11ShaderResourceView* a=mode==0||mode==3?vs:ns;ID3D11ShaderResourceView* b=mode==1||mode==3?vs:ns;
 ID3D11DeviceContext_CSSetShaderResources(context,0,1,&a);ID3D11DeviceContext_CSSetShaderResources(context,slots[j],1,&b);dispatch(cs,rv,NULL);
 char name[64];snprintf(name,sizeof(name),"mixed-srv-slot-%u-mode-%u",slots[j],mode);pixel(name,rgba,mode==2?0:mode==3?255:128,mode==2?255:mode==3?0:128,0,255);
 }
 ID3D11ComputeShader_Release(cs);
}
fprintf(report,"BC_MASK_TEST PASS all=20\n");
/* Three mip levels with different colors distinguish real trilinear blending
 * from the single-level and integral-LOD shortcuts. */
D3D11_TEXTURE2D_DESC md=vd;md.Height=8;md.MipLevels=3;
unsigned char *mips[3];D3D11_SUBRESOURCE_DATA mi[3];
for(unsigned l=0;l<3;l++){unsigned bx=((8840u>>l)+3)/4,by=((8u>>l)+3)/4;unsigned short color=l==0?0xf800:l==1?0x07e0:0x001f;mips[l]=calloc(bx*by,8);for(unsigned b=0;b<bx*by;b++){mips[l][b*8]=(unsigned char)color;mips[l][b*8+1]=(unsigned char)(color>>8);}mi[l].pSysMem=mips[l];mi[l].SysMemPitch=bx*8;mi[l].SysMemSlicePitch=bx*by*8;}
ID3D11Texture2D* mt=NULL;ID3D11ShaderResourceView* ms=NULL;checked(ID3D11Device_CreateTexture2D(device,&md,mi,&mt),"mip-create");checked(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)mt,NULL,&ms),"mip-srv");
D3D11_SAMPLER_DESC sm={0};sm.AddressU=sm.AddressV=sm.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sm.MaxLOD=D3D11_FLOAT32_MAX;sm.MaxAnisotropy=1;sm.ComparisonFunc=D3D11_COMPARISON_NEVER;
const float lods[]={0.0f,0.5f,1.25f,2.0f,0.75f,1.75f};const int expected[][4]={{255,0,0,255},{128,128,0,255},{0,191,64,255},{0,0,255,255},{0,255,0,255},{0,0,255,255}};
for(unsigned i=0;i<6;i++){
 sm.Filter=i<4?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT;ID3D11SamplerState* sampler=NULL;checked(ID3D11Device_CreateSamplerState(device,&sm,&sampler),"mip-sampler");
 char code[512];snprintf(code,sizeof(code),"Texture2D<float4> src:register(t0);SamplerState smp:register(s0);RWTexture2D<float4> dst:register(u0);[numthreads(1,1,1)]void main(){dst[int2(0,0)]=src.SampleLevel(smp,float2(0.3,0.6),%.2f);}",lods[i]);
 ID3D11ComputeShader* cs=shader(code);ID3D11DeviceContext_CSSetShaderResources(context,0,1,&ms);ID3D11DeviceContext_CSSetSamplers(context,0,1,&sampler);dispatch(cs,rv,NULL);char name[64];snprintf(name,sizeof(name),"mip-filter-%s-lod-%.2f",i<4?"linear":"point",lods[i]);pixel(name,rgba,expected[i][0],expected[i][1],expected[i][2],expected[i][3]);ID3D11ComputeShader_Release(cs);ID3D11SamplerState_Release(sampler);
}
fprintf(report,"BC_MIP_TEST PASS all=6\n");

pattern_test();fclose(report);return pattern_failures?2:0;}

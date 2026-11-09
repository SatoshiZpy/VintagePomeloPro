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
static void checked(HRESULT hr,const char* name){if(FAILED(hr)){fprintf(report,"BGRA_TEST FAIL stage=%s hr=%08lx\n",name,(unsigned long)hr);fflush(report);exit(1);}}
static ID3D11ComputeShader* shader(const char* code){ID3DBlob *blob=NULL,*error=NULL;ID3D11ComputeShader* cs=NULL;HRESULT hr=compile(code,strlen(code),"bgra-test",NULL,NULL,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);if(error){fprintf(report,"compiler: %s\n",(char*)ID3D10Blob_GetBufferPointer(error));ID3D10Blob_Release(error);}checked(hr,"compile");checked(ID3D11Device_CreateComputeShader(device,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&cs),"create-shader");ID3D10Blob_Release(blob);return cs;}
static ID3D11Texture2D* texture(DXGI_FORMAT format,UINT bind){D3D11_TEXTURE2D_DESC d={0};d.Width=d.Height=d.MipLevels=d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.BindFlags=bind;ID3D11Texture2D* t=NULL;checked(ID3D11Device_CreateTexture2D(device,&d,NULL,&t),"create-texture");return t;}
static ID3D11UnorderedAccessView* uav(ID3D11Texture2D* t,DXGI_FORMAT format){D3D11_UNORDERED_ACCESS_VIEW_DESC d={0};d.Format=format;d.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;ID3D11UnorderedAccessView* v=NULL;checked(ID3D11Device_CreateUnorderedAccessView(device,(ID3D11Resource*)t,&d,&v),"create-uav");return v;}
static void pixel(const char* name,ID3D11Texture2D* src,int b0,int b1,int b2,int b3){D3D11_TEXTURE2D_DESC d;ID3D11Texture2D_GetDesc(src,&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=NULL;checked(ID3D11Device_CreateTexture2D(device,&d,NULL,&staging),"create-readback");ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)staging,(ID3D11Resource*)src);D3D11_MAPPED_SUBRESOURCE m={0};checked(ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&m),"readback");const unsigned char* p=m.pData;int expected[]={b0,b1,b2,b3};int ok=1;for(int i=0;i<4;i++)if(abs((int)p[i]-expected[i])>1)ok=0;fprintf(report,"BGRA_TEST %s stage=%s bytes=%u,%u,%u,%u expected=%d,%d,%d,%d\n",ok?"PASS":"FAIL",name,p[0],p[1],p[2],p[3],b0,b1,b2,b3);fflush(report);ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)staging,0);ID3D11Texture2D_Release(staging);if(!ok)exit(2);}
static void dispatch(ID3D11ComputeShader* cs,ID3D11UnorderedAccessView* a,ID3D11UnorderedAccessView* b){ID3D11UnorderedAccessView* views[]={a,b};ID3D11DeviceContext_CSSetShader(context,cs,NULL,0);ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,2,views,NULL);ID3D11DeviceContext_Dispatch(context,1,1,1);views[0]=views[1]=NULL;ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,2,views,NULL);}
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
fprintf(report,"BC_STAGE_TEST PASS all=6\n");fclose(report);return 0;}

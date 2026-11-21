/**
 * A bring-up aid for the Vulkan renderer (docs/VULKAN.md): in Vulkan mode g_D3DDevice points here. Nothing reaches
 * Vulkan through it; every call does nothing and the first call of each method from each place is logged to nfs.log
 * ("unported Direct3D 9 call"), naming the code that still has to be ported. Methods that hand out objects fail with
 * null results. Generated from the IDirect3DDevice9 declaration in the Windows SDK's d3d9.h.
 */
#include <intrin.h>

#include "game/platform/Platform.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::vk
{
namespace
{
void Report(const char *method, void *caller)
{
    struct Seen
    {
        const char *method;
        void *caller;
    };
    static Seen seen[512];
    static int count;
    for (int i = 0; i < count; ++i)
        if (seen[i].method == method && seen[i].caller == caller)
            return;
    if (count == 512)
        return;
    seen[count++] = {method, caller};
    nfs::Log("Vulkan: unported Direct3D 9 call %s from %08x", method, reinterpret_cast<unsigned int>(caller));
}

class ReportingDevice : public IDirect3DDevice9
{
  public:
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObj) override
    {
        if (ppvObj)
            *ppvObj = nullptr;
        return E_NOINTERFACE;
    }

    ULONG __stdcall AddRef() override
    {
        return 1;
    }

    ULONG __stdcall Release() override
    {
        return 1;
    }

    HRESULT __stdcall TestCooperativeLevel() override
    {
        return D3D_OK;
    }

    UINT __stdcall GetAvailableTextureMem() override
    {
        return 512u << 20;
    }

    HRESULT __stdcall EvictManagedResources() override
    {
        Report("EvictManagedResources", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetDirect3D(IDirect3D9** ppD3D9) override
    {
        Report("GetDirect3D", _ReturnAddress());
        if (ppD3D9)
            *ppD3D9 = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall GetDeviceCaps(D3DCAPS9* pCaps) override
    {
        *pCaps = g_DeviceCaps;
        return D3D_OK;
    }

    HRESULT __stdcall GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override
    {
        Report("GetDisplayMode", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) override
    {
        Report("GetCreationParameters", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override
    {
        Report("SetCursorProperties", _ReturnAddress());
        return D3D_OK;
    }

    void __stdcall SetCursorPosition(int X, int Y, DWORD Flags) override
    {
        Report("SetCursorPosition", _ReturnAddress());
    }

    BOOL __stdcall ShowCursor(BOOL bShow) override
    {
        Report("ShowCursor", _ReturnAddress());
        return 0;
    }

    HRESULT __stdcall CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override
    {
        Report("CreateAdditionalSwapChain", _ReturnAddress());
        if (pSwapChain)
            *pSwapChain = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override
    {
        Report("GetSwapChain", _ReturnAddress());
        if (pSwapChain)
            *pSwapChain = nullptr;
        return D3DERR_INVALIDCALL;
    }

    UINT __stdcall GetNumberOfSwapChains() override
    {
        Report("GetNumberOfSwapChains", _ReturnAddress());
        return 0;
    }

    HRESULT __stdcall Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override
    {
        Report("Reset", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) override
    {
        Report("Present", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override
    {
        Report("GetBackBuffer", _ReturnAddress());
        if (ppBackBuffer)
            *ppBackBuffer = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override
    {
        Report("GetRasterStatus", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetDialogBoxMode(BOOL bEnableDialogs) override
    {
        Report("SetDialogBoxMode", _ReturnAddress());
        return D3D_OK;
    }

    void __stdcall SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp) override
    {
        Report("SetGammaRamp", _ReturnAddress());
    }

    void __stdcall GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override
    {
        Report("GetGammaRamp", _ReturnAddress());
    }

    HRESULT __stdcall CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override
    {
        Report("CreateTexture", _ReturnAddress());
        if (ppTexture)
            *ppTexture = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override
    {
        Report("CreateVolumeTexture", _ReturnAddress());
        if (ppVolumeTexture)
            *ppVolumeTexture = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override
    {
        Report("CreateCubeTexture", _ReturnAddress());
        if (ppCubeTexture)
            *ppCubeTexture = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override
    {
        Report("CreateVertexBuffer", _ReturnAddress());
        if (ppVertexBuffer)
            *ppVertexBuffer = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override
    {
        Report("CreateIndexBuffer", _ReturnAddress());
        if (ppIndexBuffer)
            *ppIndexBuffer = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
        Report("CreateRenderTarget", _ReturnAddress());
        if (ppSurface)
            *ppSurface = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
        Report("CreateDepthStencilSurface", _ReturnAddress());
        if (ppSurface)
            *ppSurface = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint) override
    {
        Report("UpdateSurface", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override
    {
        Report("UpdateTexture", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override
    {
        Report("GetRenderTargetData", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override
    {
        Report("GetFrontBufferData", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) override
    {
        Report("StretchRect", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color) override
    {
        Report("ColorFill", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
        Report("CreateOffscreenPlainSurface", _ReturnAddress());
        if (ppSurface)
            *ppSurface = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override
    {
        Report("SetRenderTarget", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override
    {
        Report("GetRenderTarget", _ReturnAddress());
        if (ppRenderTarget)
            *ppRenderTarget = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override
    {
        Report("SetDepthStencilSurface", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override
    {
        Report("GetDepthStencilSurface", _ReturnAddress());
        if (ppZStencilSurface)
            *ppZStencilSurface = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall BeginScene() override
    {
        Report("BeginScene", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall EndScene() override
    {
        Report("EndScene", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override
    {
        Report("Clear", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) override
    {
        Report("SetTransform", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override
    {
        Report("GetTransform", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall MultiplyTransform(D3DTRANSFORMSTATETYPE a0, CONST D3DMATRIX* a1) override
    {
        Report("MultiplyTransform", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetViewport(CONST D3DVIEWPORT9* pViewport) override
    {
        Report("SetViewport", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetViewport(D3DVIEWPORT9* pViewport) override
    {
        Report("GetViewport", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetMaterial(CONST D3DMATERIAL9* pMaterial) override
    {
        Report("SetMaterial", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetMaterial(D3DMATERIAL9* pMaterial) override
    {
        Report("GetMaterial", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetLight(DWORD Index, CONST D3DLIGHT9* a1) override
    {
        Report("SetLight", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetLight(DWORD Index, D3DLIGHT9* a1) override
    {
        Report("GetLight", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall LightEnable(DWORD Index, BOOL Enable) override
    {
        Report("LightEnable", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetLightEnable(DWORD Index, BOOL* pEnable) override
    {
        Report("GetLightEnable", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetClipPlane(DWORD Index, CONST float* pPlane) override
    {
        Report("SetClipPlane", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetClipPlane(DWORD Index, float* pPlane) override
    {
        Report("GetClipPlane", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override
    {
        Report("SetRenderState", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override
    {
        *pValue = g_renderer ? g_renderer->GetRenderState(State) : 0;
        return D3D_OK;
    }

    HRESULT __stdcall CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override
    {
        Report("CreateStateBlock", _ReturnAddress());
        if (ppSB)
            *ppSB = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall BeginStateBlock() override
    {
        Report("BeginStateBlock", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall EndStateBlock(IDirect3DStateBlock9** ppSB) override
    {
        Report("EndStateBlock", _ReturnAddress());
        if (ppSB)
            *ppSB = nullptr;
        return D3D_OK;
    }

    HRESULT __stdcall SetClipStatus(CONST D3DCLIPSTATUS9* pClipStatus) override
    {
        Report("SetClipStatus", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetClipStatus(D3DCLIPSTATUS9* pClipStatus) override
    {
        Report("GetClipStatus", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override
    {
        Report("GetTexture", _ReturnAddress());
        if (ppTexture)
            *ppTexture = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override
    {
        Report("SetTexture", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override
    {
        Report("GetTextureStageState", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override
    {
        Report("SetTextureStageState", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override
    {
        *pValue = g_renderer ? g_renderer->GetSamplerState(Sampler, Type) : 0;
        return D3D_OK;
    }

    HRESULT __stdcall SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override
    {
        Report("SetSamplerState", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall ValidateDevice(DWORD* pNumPasses) override
    {
        Report("ValidateDevice", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY* pEntries) override
    {
        Report("SetPaletteEntries", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) override
    {
        Report("GetPaletteEntries", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetCurrentTexturePalette(UINT PaletteNumber) override
    {
        Report("SetCurrentTexturePalette", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetCurrentTexturePalette(UINT *PaletteNumber) override
    {
        Report("GetCurrentTexturePalette", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetScissorRect(CONST RECT* pRect) override
    {
        Report("SetScissorRect", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetScissorRect(RECT* pRect) override
    {
        Report("GetScissorRect", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetSoftwareVertexProcessing(BOOL bSoftware) override
    {
        Report("SetSoftwareVertexProcessing", _ReturnAddress());
        return D3D_OK;
    }

    BOOL __stdcall GetSoftwareVertexProcessing() override
    {
        Report("GetSoftwareVertexProcessing", _ReturnAddress());
        return 0;
    }

    HRESULT __stdcall SetNPatchMode(float nSegments) override
    {
        Report("SetNPatchMode", _ReturnAddress());
        return D3D_OK;
    }

    float __stdcall GetNPatchMode() override
    {
        Report("GetNPatchMode", _ReturnAddress());
        return 0.0f;
    }

    HRESULT __stdcall DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override
    {
        Report("DrawPrimitive", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall DrawIndexedPrimitive(D3DPRIMITIVETYPE, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override
    {
        Report("DrawIndexedPrimitive", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override
    {
        Report("DrawPrimitiveUP", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override
    {
        Report("DrawIndexedPrimitiveUP", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override
    {
        Report("ProcessVertices", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) override
    {
        Report("CreateVertexDeclaration", _ReturnAddress());
        if (ppDecl)
            *ppDecl = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override
    {
        Report("SetVertexDeclaration", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override
    {
        Report("GetVertexDeclaration", _ReturnAddress());
        if (ppDecl)
            *ppDecl = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetFVF(DWORD FVF) override
    {
        Report("SetFVF", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetFVF(DWORD* pFVF) override
    {
        Report("GetFVF", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader) override
    {
        Report("CreateVertexShader", _ReturnAddress());
        if (ppShader)
            *ppShader = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetVertexShader(IDirect3DVertexShader9* pShader) override
    {
        Report("SetVertexShader", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetVertexShader(IDirect3DVertexShader9** ppShader) override
    {
        Report("GetVertexShader", _ReturnAddress());
        if (ppShader)
            *ppShader = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override
    {
        Report("SetVertexShaderConstantF", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override
    {
        Report("GetVertexShaderConstantF", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override
    {
        Report("SetVertexShaderConstantI", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override
    {
        Report("GetVertexShaderConstantI", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) override
    {
        Report("SetVertexShaderConstantB", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override
    {
        Report("GetVertexShaderConstantB", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override
    {
        Report("SetStreamSource", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override
    {
        Report("GetStreamSource", _ReturnAddress());
        if (ppStreamData)
            *ppStreamData = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetStreamSourceFreq(UINT StreamNumber, UINT Setting) override
    {
        Report("SetStreamSourceFreq", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) override
    {
        Report("GetStreamSourceFreq", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetIndices(IDirect3DIndexBuffer9* pIndexData) override
    {
        Report("SetIndices", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetIndices(IDirect3DIndexBuffer9** ppIndexData) override
    {
        Report("GetIndices", _ReturnAddress());
        if (ppIndexData)
            *ppIndexData = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader) override
    {
        Report("CreatePixelShader", _ReturnAddress());
        if (ppShader)
            *ppShader = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetPixelShader(IDirect3DPixelShader9* pShader) override
    {
        Report("SetPixelShader", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetPixelShader(IDirect3DPixelShader9** ppShader) override
    {
        Report("GetPixelShader", _ReturnAddress());
        if (ppShader)
            *ppShader = nullptr;
        return D3DERR_INVALIDCALL;
    }

    HRESULT __stdcall SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override
    {
        Report("SetPixelShaderConstantF", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override
    {
        Report("GetPixelShaderConstantF", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override
    {
        Report("SetPixelShaderConstantI", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override
    {
        Report("GetPixelShaderConstantI", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) override
    {
        Report("SetPixelShaderConstantB", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override
    {
        Report("GetPixelShaderConstantB", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo) override
    {
        Report("DrawRectPatch", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo) override
    {
        Report("DrawTriPatch", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall DeletePatch(UINT Handle) override
    {
        Report("DeletePatch", _ReturnAddress());
        return D3D_OK;
    }

    HRESULT __stdcall CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override
    {
        Report("CreateQuery", _ReturnAddress());
        if (ppQuery)
            *ppQuery = nullptr;
        return D3DERR_INVALIDCALL;
    }

};

ReportingDevice g_reportingDevice;
} // namespace

IDirect3DDevice9 *ReportingDevice()
{
    return &g_reportingDevice;
}
} // namespace nfs::vk

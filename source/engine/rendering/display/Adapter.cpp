/**
 * The display adapter as the original code asks Direct3D 9 about it (g_D3D: the adapter's name and PCI ids, its display
 * modes, the device caps, format checks), answered from Windows and the Vulkan renderer's needs instead, so that the game
 * never creates a Direct3D 9 object (docs/VULKAN.md, "Vulkan only"). The game makes its device through the Vulkan
 * renderer (EngineInit.cpp sub_6BFBD0), so CreateDevice is never called; it fails if anything tries.
 *
 * -d3d9probe: forwards every call to a real Direct3D 9 object and logs the call and its answer, to check these answers
 * against what Direct3D 9 gave on this machine.
 */
#include <intrin.h>
#include <stdlib.h>
#include <string.h>

#include "game/platform/Platform.h"
#include "engine/rendering/display/adapter.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"

#pragma comment(lib, "user32.lib")

namespace nfs::adapter
{
namespace
{
/** The display modes the adapter offers at 32 bits a pixel: Windows' list for the primary display, each size and
 * refresh rate once, in Windows' order (Direct3D 9's). */
const int kMaxModes = 512;
D3DDISPLAYMODE g_modes[kMaxModes];
int g_modeCount = -1;

void LoadModes()
{
    if (g_modeCount >= 0)
        return;
    g_modeCount = 0;
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode); ++i)
    {
        if (mode.dmBitsPerPel != 32 || g_modeCount == kMaxModes)
            continue;
        const D3DDISPLAYMODE m = {mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency, D3DFMT_X8R8G8B8};
        bool seen = false;
        for (int k = 0; k < g_modeCount && !seen; ++k)
            seen = g_modes[k].Width == m.Width && g_modes[k].Height == m.Height && g_modes[k].RefreshRate == m.RefreshRate;
        if (!seen)
            g_modes[g_modeCount++] = m;
    }
}

unsigned int HexAfter(const char *text, const char *key)
{
    const char *p = strstr(text, key);
    return p ? strtoul(p + strlen(key), nullptr, 16) : 0;
}

/** The primary display's adapter: its name, and the PCI vendor, device and subsystem ids from its device id
 * ("PCI\VEN_1002&DEV_7550&SUBSYS_...&REV_C0"). */
void Identify(D3DADAPTER_IDENTIFIER9 &id)
{
    memset(&id, 0, sizeof(id));
    DISPLAY_DEVICEA device = {};
    device.cb = sizeof(device);
    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &device, 0); ++i)
    {
        if (!(device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
            continue;
        lstrcpynA(id.Description, device.DeviceString, sizeof(id.Description));
        lstrcpynA(id.DeviceName, device.DeviceName, sizeof(id.DeviceName));
        lstrcpynA(id.Driver, "vulkan-1.dll", sizeof(id.Driver));
        id.VendorId = HexAfter(device.DeviceID, "VEN_");
        id.DeviceId = HexAfter(device.DeviceID, "DEV_");
        id.SubSysId = HexAfter(device.DeviceID, "SUBSYS_");
        id.Revision = HexAfter(device.DeviceID, "REV_");
        id.WHQLLevel = 1;
        break;
    }
}

/** What the Vulkan renderer offers, in Direct3D 9's terms: shader model 3, anisotropic filtering, the texture and
 * blend capabilities the game checks for. */
void Caps(D3DCAPS9 &c)
{
    memset(&c, 0, sizeof(c));
    c.DeviceType = D3DDEVTYPE_HAL;
    c.Caps2 = D3DCAPS2_CANAUTOGENMIPMAP | D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_FULLSCREENGAMMA;
    c.Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
    c.PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE;
    c.DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_PUREDEVICE | D3DDEVCAPS_HWRASTERIZATION;
    c.PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                          D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_BLENDOP | D3DPMISCCAPS_SEPARATEALPHABLEND;
    c.RasterCaps = D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_DEPTHBIAS |
                   D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS | D3DPRASTERCAPS_SCISSORTEST | D3DPRASTERCAPS_MIPMAPLODBIAS;
    c.ZCmpCaps = c.AlphaCmpCaps = 0xff;
    c.SrcBlendCaps = c.DestBlendCaps = 0x3fff;
    c.ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    c.TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_MIPMAP |
                    D3DPTEXTURECAPS_MIPCUBEMAP | D3DPTEXTURECAPS_PROJECTED | D3DPTEXTURECAPS_VOLUMEMAP |
                    D3DPTEXTURECAPS_MIPVOLUMEMAP;
    c.TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
                          D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                          D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    c.CubeTextureFilterCaps = c.VolumeTextureFilterCaps = c.TextureFilterCaps;
    c.TextureAddressCaps = c.VolumeTextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR |
                                                        D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_BORDER |
                                                        D3DPTADDRESSCAPS_INDEPENDENTUV | D3DPTADDRESSCAPS_MIRRORONCE;
    c.StencilCaps = 0xff | D3DSTENCILCAPS_TWOSIDED;
    c.MaxTextureWidth = c.MaxTextureHeight = 16384;
    c.MaxVolumeExtent = 2048;
    c.MaxTextureRepeat = 8192;
    c.MaxTextureAspectRatio = 16384;
    c.MaxAnisotropy = 16;
    c.MaxVertexW = 1e10f;
    c.MaxTextureBlendStages = 8;
    c.MaxSimultaneousTextures = 8;
    c.FVFCaps = 8;
    c.MaxActiveLights = 8;
    c.MaxUserClipPlanes = 6;
    c.MaxVertexBlendMatrices = 4;
    c.MaxVertexBlendMatrixIndex = 255;
    c.MaxPointSize = 256.0f;
    c.MaxPrimitiveCount = 0xffffff;
    c.MaxVertexIndex = 0xffffff;
    c.MaxStreams = 16;
    c.MaxStreamStride = 255;
    c.VertexShaderVersion = D3DVS_VERSION(3, 0);
    c.MaxVertexShaderConst = 256;
    c.PixelShaderVersion = D3DPS_VERSION(3, 0);
    c.PixelShader1xMaxValue = 65504.0f;
    c.NumSimultaneousRTs = 4;
    c.DeclTypes = 0x3ff;
    c.VS20Caps.NumTemps = 32;
    c.VS20Caps.StaticFlowControlDepth = 4;
    c.VS20Caps.DynamicFlowControlDepth = 24;
    c.PS20Caps.NumTemps = 32;
    c.PS20Caps.StaticFlowControlDepth = 4;
    c.PS20Caps.DynamicFlowControlDepth = 24;
    c.PS20Caps.NumInstructionSlots = 512;
    c.MaxVShaderInstructionsExecuted = c.MaxPShaderInstructionsExecuted = 65535;
    c.MaxVertexShader30InstructionSlots = c.MaxPixelShader30InstructionSlots = 32768;
}

void Report(const char *what, unsigned int a, unsigned int b, unsigned int c, long result)
{
    nfs::Log("adapter: %s(%x, %x, %x) = %08x from %08x", what, a, b, c, static_cast<unsigned int>(result),
             reinterpret_cast<unsigned int>(_ReturnAddress()));
}

class Adapter : public IDirect3D9
{
  public:
    /** -d3d9probe: the real object every call goes to as well, and whose answers are the ones returned. */
    IDirect3D9 *real = nullptr;

    HRESULT __stdcall QueryInterface(REFIID, void **object) override
    {
        if (object)
            *object = nullptr;
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
    HRESULT __stdcall RegisterSoftwareDevice(void *) override
    {
        return D3DERR_NOTAVAILABLE;
    }
    UINT __stdcall GetAdapterCount() override
    {
        const UINT n = real ? real->GetAdapterCount() : 1;
        if (real)
            Report("GetAdapterCount", 0, 0, 0, static_cast<long>(n));
        return n;
    }
    HRESULT __stdcall GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *id) override
    {
        if (real)
        {
            const HRESULT hr = real->GetAdapterIdentifier(adapter, flags, id);
            D3DADAPTER_IDENTIFIER9 own;
            Identify(own);
            nfs::Log("adapter: GetAdapterIdentifier %s %04x:%04x sub %08x rev %x (own: %s %04x:%04x sub %08x rev %x)",
                     id->Description, id->VendorId, id->DeviceId, id->SubSysId, id->Revision, own.Description,
                     own.VendorId, own.DeviceId, own.SubSysId, own.Revision);
            return hr;
        }
        if (!id)
            return D3DERR_INVALIDCALL;
        Identify(*id);
        return D3D_OK;
    }
    UINT __stdcall GetAdapterModeCount(UINT adapter, D3DFORMAT format) override
    {
        LoadModes();
        const UINT own = format == D3DFMT_X8R8G8B8 ? static_cast<UINT>(g_modeCount) : 0;
        if (real)
        {
            const UINT n = real->GetAdapterModeCount(adapter, format);
            nfs::Log("adapter: GetAdapterModeCount(format %d) = %u (own %u)", format, n, own);
            return n;
        }
        return own;
    }
    HRESULT __stdcall EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT index, D3DDISPLAYMODE *mode) override
    {
        LoadModes();
        if (real)
        {
            const HRESULT hr = real->EnumAdapterModes(adapter, format, index, mode);
            const D3DDISPLAYMODE &own = index < static_cast<UINT>(g_modeCount) ? g_modes[index] : D3DDISPLAYMODE{};
            if (own.Width != mode->Width || own.Height != mode->Height || own.RefreshRate != mode->RefreshRate)
                nfs::Log("adapter: mode %u: %ux%u %u Hz, own %ux%u %u Hz", index, mode->Width, mode->Height,
                         mode->RefreshRate, own.Width, own.Height, own.RefreshRate);
            return hr;
        }
        if (!mode || format != D3DFMT_X8R8G8B8 || index >= static_cast<UINT>(g_modeCount))
            return D3DERR_INVALIDCALL;
        *mode = g_modes[index];
        return D3D_OK;
    }
    HRESULT __stdcall GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) override
    {
        if (real)
        {
            const HRESULT hr = real->GetAdapterDisplayMode(adapter, mode);
            Report("GetAdapterDisplayMode", mode->Width, mode->Height, mode->RefreshRate, hr);
            return hr;
        }
        DEVMODEA current = {};
        current.dmSize = sizeof(current);
        if (!mode || !EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &current))
            return D3DERR_INVALIDCALL;
        *mode = {current.dmPelsWidth, current.dmPelsHeight, current.dmDisplayFrequency, D3DFMT_X8R8G8B8};
        return D3D_OK;
    }
    HRESULT __stdcall CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT display, D3DFORMAT back,
                                      BOOL windowed) override
    {
        if (real)
        {
            const HRESULT hr = real->CheckDeviceType(adapter, type, display, back, windowed);
            Report("CheckDeviceType", display, back, windowed, hr);
            return hr;
        }
        return D3D_OK;
    }
    HRESULT __stdcall CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT display, DWORD usage,
                                        D3DRESOURCETYPE resource, D3DFORMAT format) override
    {
        if (real)
        {
            const HRESULT hr = real->CheckDeviceFormat(adapter, type, display, usage, resource, format);
            Report("CheckDeviceFormat", usage, resource, format, hr);
            return hr;
        }
        // Every format the game asks for but the vendors' depth texture formats (DF16, DF24, INTZ...): the original
        // chooses its shadow map method by them (sub_6C1510), and X360 Stuff's (depth texture, D24S8) is the one the
        // rebuild keeps on every card (EngineInit.cpp).
        const unsigned int fourcc = static_cast<unsigned int>(format);
        return (usage & D3DUSAGE_DEPTHSTENCIL) && fourcc > 0xff ? D3DERR_NOTAVAILABLE : D3D_OK;
    }
    HRESULT __stdcall CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT format, BOOL windowed,
                                                 D3DMULTISAMPLE_TYPE samples, DWORD *quality) override
    {
        if (real)
        {
            const HRESULT hr = real->CheckDeviceMultiSampleType(adapter, type, format, windowed, samples, quality);
            Report("CheckDeviceMultiSampleType", format, samples, quality ? *quality : 0, hr);
            return hr;
        }
        // The renderer draws without multisampling (its antialiasing is FXAA and supersampling): one quality level, so
        // the original's antialiasing limit (sub_6C1510: levels - 1) is none.
        if (quality)
            *quality = 1;
        return samples <= D3DMULTISAMPLE_NONMASKABLE ? D3D_OK : D3DERR_NOTAVAILABLE;
    }
    HRESULT __stdcall CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT display, D3DFORMAT target,
                                             D3DFORMAT depth) override
    {
        if (real)
        {
            const HRESULT hr = real->CheckDepthStencilMatch(adapter, type, display, target, depth);
            Report("CheckDepthStencilMatch", display, target, depth, hr);
            return hr;
        }
        return D3D_OK;
    }
    HRESULT __stdcall CheckDeviceFormatConversion(UINT adapter, D3DDEVTYPE type, D3DFORMAT from, D3DFORMAT to) override
    {
        if (real)
        {
            const HRESULT hr = real->CheckDeviceFormatConversion(adapter, type, from, to);
            Report("CheckDeviceFormatConversion", from, to, 0, hr);
            return hr;
        }
        return D3D_OK;
    }
    HRESULT __stdcall GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *caps) override
    {
        if (real)
        {
            const HRESULT hr = real->GetDeviceCaps(adapter, type, caps);
            Report("GetDeviceCaps", caps->TextureFilterCaps, caps->PixelShaderVersion, caps->MaxAnisotropy, hr);
            return hr;
        }
        if (!caps)
            return D3DERR_INVALIDCALL;
        Caps(*caps);
        return D3D_OK;
    }
    HMONITOR __stdcall GetAdapterMonitor(UINT) override
    {
        const POINT origin = {0, 0};
        return MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    }
    HRESULT __stdcall CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **device) override
    {
        nfs::Log("adapter: CreateDevice from %08x: there is no Direct3D 9 device", reinterpret_cast<unsigned int>(_ReturnAddress()));
        if (device)
            *device = nullptr;
        return D3DERR_NOTAVAILABLE;
    }
};

Adapter g_adapter;
} // namespace

IDirect3D9 *Create()
{
    if (nfs::vk::HasArgument("-d3d9probe"))
    {
        using CreateFunction = IDirect3D9 *(__stdcall *)(UINT);
        if (HMODULE d3d9 = LoadLibraryA("d3d9.dll"))
            if (CreateFunction create = reinterpret_cast<CreateFunction>(GetProcAddress(d3d9, "Direct3DCreate9")))
                g_adapter.real = create(D3D_SDK_VERSION);
    }
    return &g_adapter;
}
} // namespace nfs::adapter

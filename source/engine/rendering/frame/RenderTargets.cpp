/**
 * Render target setup (original: zPlatform eCreateRenderTargets 0x6bd7c0, eReleaseRenderTargets 0x6bd620 and
 * eServiceScreenCapture 0x6c0910), extended for the render scale (engine/rendering/display/display.h) and the frame rate counter
 * (interface/hud/overlay.h); and the target binding the views use (0x6bd3b0).
 *
 * With a render scale other than 100%, a render target and depth buffer of the scaled size stand in for the back buffer
 * (g_BackBufferSurface, g_DepthStencilSurface), so everything the renderer draws, and everything that reads the screen
 * back (post effects, screenshots), works on the scaled frame. Just before each frame is presented it is copied onto
 * the real back buffer with linear filtering; at 200% that averages every 2x2 block. The frame rate counter is drawn on
 * the real back buffer after that, so it is not in screenshots.
 */
#include "game/platform/Device.h"
#include "game/platform/Platform.h"
#include "engine/rendering/shadows/shadow.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "core/log.h"

namespace nfs
{
void LogCodePatches();
}
#include "interface/hud/overlay.h"
#include "dev/profile.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"

NFS_REPLACE(0x6bd3b0, sub_6BD3B0)
NFS_REPLACE(0x6c86b0, GetShadowWidth)
NFS_REPLACE(0x6c86c0, GetShadowHeight)

/** The shadow map: a colour target and the depth texture the world effects sample (D24S8 or ATI's DF16, by the
 * shadow map method), with their top surfaces. */
extern "C" void *dword_982CF0;
extern "C" void *dword_982CEC;
extern "C" void *dword_982CF8;
extern "C" void *dword_982CF4;
/** Shadow map method from the adapter's formats: 0 none, 1 depth textures (NVIDIA), 2 DF16 (ATI). */
extern "C" int dword_982C08;

/** Car reflections: the cube target the environment map views draw into, its faces and their depth buffers, and the
 * static cube of the front end's garage (EnvMapTextureLoadedCallback); size and formats, and whether they exist. */
extern "C" void *dword_982A30;
extern "C" void *dword_982A34[6];
extern "C" void *dword_982A4C[6];
extern "C" void *dword_982A64;
extern "C" unsigned char byte_8F945B;
extern "C" int EnableEnvMap__009017B8;
extern "C" int dword_901840;
extern "C" int dword_8F8FF4;
extern "C" int dword_8F8FF8;
extern "C" int dword_8F8FFC;
/** The rear-view mirror's surface and depth buffer, and a 320x200 target made with it when dword_982B18 is set. */
extern "C" void *_g_I3DSurface2;
extern "C" void *_g_I3DSurface;
extern "C" int dword_982B18;
extern "C" void *_g_I3DTexture2;
extern "C" void *_g_ID3Surface3;
extern "C" int D3DFORMAT_008f9000;
extern "C" int _g_D3DFORMAT;
extern "C" int _g_D3D_stencil_Width;
extern "C" int _g_D3D_stencil_height;
/** Road reflections (320x240 with their depth buffer, a second target at the highest detail), or a white 1x1 texture
 * when they are off. */
extern "C" void *dword_982A6C;
extern "C" void *dword_982A68;
extern "C" void *dword_982A78;
extern "C" void *dword_982A74;
extern "C" void *dword_982A70;
extern "C" int lpData_009017d4;
extern "C" int dword_90185C;

extern "C" void __cdecl sub_6E2F50();
extern "C" void __cdecl sub_6BD3B0(eRenderTarget *target, int clear);
int GetShadowWidth();
int GetShadowHeight();


namespace nfs::display
{
namespace
{
int Clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

float Clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}
} // namespace

int ShadowMapSize()
{
    static const int size = Clamp(config::Int("Display", "ShadowMapResolution", 2048), 256, 8192);
    return size;
}

namespace
{
/** The reflection cube's face size and the road reflection's scale in percent, read at their first use. */
int s_reflectionSize;
int s_roadReflectionPercent;
} // namespace

int ReflectionMapSize()
{
    if (!s_reflectionSize)
        s_reflectionSize = Clamp(config::Int("Display", "ReflectionResolution", 1024), 64, 4096);
    return s_reflectionSize;
}

void SetReflectionMapSize(int size)
{
    s_reflectionSize = Clamp(size, 64, 4096);
    config::WriteInt("Display", "ReflectionResolution", s_reflectionSize);
}

int RoadReflectionPercent()
{
    if (!s_roadReflectionPercent)
        s_roadReflectionPercent =
            Clamp(static_cast<int>(config::Float("Display", "RoadReflectionScale", 0.5f) * 100.0f + 0.5f), 10, 100);
    return s_roadReflectionPercent;
}

void SetRoadReflectionPercent(int percent)
{
    s_roadReflectionPercent = Clamp(percent, 10, 100);
    config::WriteFloat("Display", "RoadReflectionScale", static_cast<float>(s_roadReflectionPercent) * 0.01f);
}

void RoadReflectionSize(int &width, int &height)
{
    const float scale = static_cast<float>(RoadReflectionPercent()) * 0.01f;
    width = Clamp(static_cast<int>(static_cast<float>(g_WindowWidth) * scale), 64, 8192);
    height = Clamp(static_cast<int>(static_cast<float>(g_WindowHeight) * scale), 48, 8192);
}

void MirrorSize(int &width, int &height)
{
    static const float scale = Clamp(config::Float("Display", "MirrorScale", 1.0f), 0.25f, 2.0f);
    width = Clamp(static_cast<int>(static_cast<float>(g_WindowWidth / 2) * scale), 64, 8192);
    height = Clamp(static_cast<int>(static_cast<float>(g_WindowHeight / 6) * scale), 16, 8192);
}
} // namespace nfs::display

/** The shadow map's size (the original returned 1024 for both). */
int GetShadowWidth()
{
    return nfs::display::ShadowMapSize();
}

int GetShadowHeight()
{
    return nfs::display::ShadowMapSize();
}

namespace
{
/** The size of a target's surface (a texture, cube face or depth buffer), or the fallback when there is none. */
void TargetSize(void *surface, int fallbackWidth, int fallbackHeight, int &width, int &height)
{
    width = fallbackWidth;
    height = fallbackHeight;
    if (!surface)
        return;
    UINT w = 0, h = 0;
    device::SurfaceSize(surface, w, h);
    if (w && h)
    {
        width = static_cast<int>(w);
        height = static_cast<int>(h);
    }
}
} // namespace

/**
 * The table of render targets the views draw into (original: zPlatform 0x6bd0d0): the screen, road reflections, the
 * front end, the mirror, the shadow map and the six reflection cube faces. Each entry takes the size of the surface it
 * holds, so the views' viewports and projections always match what was made, whatever made it (the original used the
 * sizes it expected).
 */
void eInitRenderTargetTable()
{
    static const int kCubeViews[6] = {0x13, 0x15, 0x16, 0x17, 0x12, 0x14};
    eRenderTarget *table = &g_BackBufferTarget;
    for (int i = 0; i < 11; ++i)
    {
        eRenderTarget &t = table[i];
        t.ID = i;
        t.unknown_10 = 1;
        switch (i)
        {
        case 0:
            t.ViewID = 1;
            t.Surface = g_BackBufferSurface;
            t.DepthSurface = g_DepthStencilSurface;
            t.Width = g_WindowWidth;
            t.Height = g_WindowHeight;
            break;
        case 1:
            t.ViewID = 4;
            t.Surface = static_cast<IDirect3DSurface9 *>(dword_982A68);
            t.DepthSurface = static_cast<IDirect3DSurface9 *>(dword_982A78);
            TargetSize(t.Surface, 320, 240, t.Width, t.Height);
            break;
        case 2:
            // The front end: the screen without a depth buffer (the original leaves the entry's depth as it was).
            t.ViewID = 0;
            t.Surface = g_BackBufferSurface;
            t.Width = g_WindowWidth;
            t.Height = g_WindowHeight;
            break;
        case 3:
            t.ViewID = 3;
            t.Surface = static_cast<IDirect3DSurface9 *>(_g_I3DSurface2);
            t.DepthSurface = static_cast<IDirect3DSurface9 *>(_g_I3DSurface);
            TargetSize(t.Surface, _g_D3D_stencil_Width, _g_D3D_stencil_height, t.Width, t.Height);
            break;
        case 4:
            t.ViewID = 0xF;
            t.Surface = static_cast<IDirect3DSurface9 *>(dword_982CEC);
            t.DepthSurface = static_cast<IDirect3DSurface9 *>(dword_982CF4);
            TargetSize(t.Surface, GetShadowWidth(), GetShadowHeight(), t.Width, t.Height);
            break;
        default:
        {
            const int face = i - 5;
            const int size = EnableEnvMap__009017B8 ? dword_8F8FF4 : 1;
            t.ViewID = kCubeViews[face];
            t.Surface = static_cast<IDirect3DSurface9 *>(dword_982A34[face]);
            t.DepthSurface = static_cast<IDirect3DSurface9 *>(dword_982A4C[face]);
            TargetSize(t.Surface, size, size, t.Width, t.Height);
            break;
        }
        }
    }
}

/** Creates the screen's targets after the device is created or reset. (An empty platform hook the original called last,
 * 0x6c3a30, is left out.) */
void eCreateRenderTargets()
{
    // X360 Stuff gives the road reflection and mirror 24-bit depth buffers instead of 16-bit ones (its shadow method
    // change is in eInitEnginePlat).
    _g_D3DFORMAT = D3DFMT_D24S8;
    nfs::x360::ApplyDataChanges();
    g_UnusedSurface = nullptr;
    dword_8F8FF4 = nfs::display::ReflectionMapSize();
    nfs::display::MirrorSize(_g_D3D_stencil_Width, _g_D3D_stencil_height);
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    // The renderer's back buffer is already at the render scale.
    nfs::vk::Texture *back = r->BackBuffer(), *depth = r->DepthBuffer();
    // eReleaseRenderTargets releases the back buffer once and the depth buffer twice (once more through the
    // screen's target entry); the renderer keeps its own reference to both.
    ++back->refs;
    depth->refs += 2;
    g_BackBufferSurface = reinterpret_cast<IDirect3DSurface9 *>(back);
    g_DepthStencilSurface = reinterpret_cast<IDirect3DSurface9 *>(depth);
    if (g_VideoOptions.ShadowDetail > 0)
        eCreateShadowMapTargets();
    eCreatePostProcessTargets();
    eCreateEnvMapTargets();
    eCreateRoadReflectionTargets();
    eCreateMirrorTargets();
    if (nfs::vk::HasArgument("-logpatches"))
        nfs::LogCodePatches();
    eInitRenderTargetTable();
    g_RenderTargetsReset = true;
    nfs::x360::InitResources();
}

void eReleaseRenderTargets()
{
    nfs::x360::ReleaseResources();
    NFS_ORIGINAL(0x6bd620, void(__cdecl *)())();
    nfs::overlay::ReleaseDeviceObjects();
    nfs::profile::ReleaseDeviceObjects();
    nfs::shadow::ReleaseTargets();
}

/** Saves screenshots and movie frames of the finished frame; it runs just before the frame is presented. */
void eServiceScreenCapture()
{
    nfs::profile::EndFrame();
    nfs::overlay::Draw();
}


/**
 * Draws what is queued, then binds a target for a view: its surfaces and a viewport over it. With `clear`, the player
 * views, the mirror and the shadow and reflection maps (targets 1, 3, 5 to 10) clear colour and depth, target 2 only
 * colour, the others everything. The colour is cleared to transparent black (X360 Stuff: alpha carries depth).
 */
extern "C" void __cdecl sub_6BD3B0(eRenderTarget *target, int clear)
{
    sub_6E2F50();
    device::SetTarget(target->Surface, target->DepthSurface, target->Width, target->Height);
    if (clear)
    {
        const int id = target->ID;
        DWORD flags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL;
        if ((id >= 5 && id <= 10) || id == 1 || id == 3)
            flags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
        else if (id == 2)
            flags = D3DCLEAR_TARGET;
        device::Clear(flags, 0, 1.0f, 0);
    }
    g_CurrentRenderTarget = target;
    g_CurrentEffect = nullptr;
}

/** The shadow map's targets (ShadowMapSize() square; the original 1024). Returns an HRESULT. A texture's top surface
 * is the texture itself, with a reference of its own. */
int eCreateShadowMapTargets()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    const uint32_t kSize = static_cast<uint32_t>(GetShadowWidth());
    nfs::vk::Texture *color = r->CreateTexture(kSize, kSize, 1, D3DFMT_A8R8G8B8, true);
    if (!color)
        return E_FAIL;
    dword_982CF0 = color;
    nfs::vk::Texture *depth = nullptr;
    if (dword_982C08 == 1)
        depth = r->CreateTexture(kSize, kSize, 1, D3DFMT_D24S8, true);
    else if (dword_982C08 == 2)
        depth = r->CreateTexture(kSize, kSize, 1, MAKEFOURCC('D', 'F', '1', '6'), true);
    if (dword_982C08 == 1 || dword_982C08 == 2)
    {
        if (!depth)
            return E_FAIL;
        dword_982CF8 = depth;
    }
    ++color->refs;
    dword_982CEC = color;
    depth = static_cast<nfs::vk::Texture *>(dword_982CF8);
    if (!depth)
        return E_FAIL;
    ++depth->refs;
    dword_982CF4 = depth;
    if (!nfs::vk::HasArgument("-shadowdebug"))
        r->SetDebugTexture(depth);
    return S_OK;
}

namespace
{
/** A 2D texture's top surface: the texture itself, with a reference of its own. */
void *TopSurface(nfs::vk::Texture *texture)
{
    if (texture)
        ++texture->refs;
    return texture;
}

void ReleaseObject(void *&object)
{
    if (object)
        nfs::vk::g_renderer->Release(static_cast<nfs::vk::Object *>(object));
    object = nullptr;
}

/** The garage's static cube (dword_982A64) as last made. */
void *s_garageCube;
} // namespace

/** The car reflection cube target with its faces and depth buffers, and the garage's static cube; on failure car
 * reflections are switched off. */
void eCreateEnvMapTargets()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    dword_982A30 = nullptr;
    for (int face = 0; face < 6; ++face)
        dword_982A34[face] = dword_982A4C[face] = nullptr;
    dword_982A64 = nullptr;
    if (!byte_8F945B)
    {
        g_RenderTargetsReset = true;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(EnableEnvMap__009017B8 ? dword_8F8FF4 : 16);
    const uint32_t format = static_cast<uint32_t>(dword_8F8FF8);
    // The garage cube is not one of the targets eReleaseRenderTargets releases: it is kept over a reset while its size
    // stays (making a new one each time leaked the old).
    nfs::vk::Texture *garage = static_cast<nfs::vk::Texture *>(s_garageCube);
    if (garage && (garage->width != static_cast<uint32_t>(dword_8F8FF4) || garage->d3dFormat != format))
    {
        ReleaseObject(s_garageCube);
        garage = nullptr;
    }
    if (!garage)
        s_garageCube = garage = r->CreateTexture(dword_8F8FF4, dword_8F8FF4, 1, format, false, true);
    dword_982A64 = garage;
    bool ok = garage != nullptr;
    nfs::vk::Texture *cube = ok ? r->CreateTexture(size, size, 1, format, true, true) : nullptr;
    dword_982A30 = cube;
    ok = cube != nullptr;
    for (uint32_t face = 0; ok && face < 6; ++face)
    {
        dword_982A34[face] = r->CreateSurface(cube, face);
        dword_982A4C[face] = r->CreateTexture(size, size, 1, static_cast<uint32_t>(dword_8F8FFC), true);
        ok = dword_982A4C[face] != nullptr;
    }
    if (ok && nfs::vk::HasArgument("-vkdumpenv"))
        r->SetDebugTexture(cube);
    if (ok)
    {
        g_RenderTargetsReset = true;
        return;
    }
    byte_8F945B = 0;
    EnableEnvMap__009017B8 = 0;
    dword_901840 = 0;
    ReleaseObject(dword_982A30);
    for (int face = 0; face < 6; ++face)
    {
        ReleaseObject(dword_982A34[face]);
        ReleaseObject(dword_982A4C[face]);
    }
}


/** The road reflection target (RoadReflectionSize(); the original 320x240) with its depth buffer, and a second one at
 * the highest detail; with road reflections off a white 1x1 texture stands in. On failure road reflections are
 * switched off. */
void eCreateRoadReflectionTargets()
{
    int reflectionWidth, reflectionHeight;
    nfs::display::RoadReflectionSize(reflectionWidth, reflectionHeight);
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    const uint32_t format = static_cast<uint32_t>(D3DFORMAT_008f9000);
    uint32_t width = static_cast<uint32_t>(reflectionWidth), height = static_cast<uint32_t>(reflectionHeight);
    nfs::vk::Texture *texture;
    if (lpData_009017d4)
    {
        texture = r->CreateTexture(width, height, 1, format, true);
    }
    else
    {
        width = height = 1;
        texture = r->CreateTexture(1, 1, 1, format, false);
        static const uint32_t kWhite = 0xFFFFFFFF;
        if (texture)
            r->UploadTexture(texture, 0, 0, &kWhite, 4);
    }
    dword_982A6C = texture;
    bool ok = texture != nullptr;
    if (ok)
    {
        dword_982A68 = TopSurface(texture);
        dword_982A78 = r->CreateTexture(width, height, 1, static_cast<uint32_t>(_g_D3DFORMAT), true);
        ok = dword_982A78 != nullptr;
    }
    if (ok && dword_90185C == 3 && lpData_009017d4)
    {
        nfs::vk::Texture *second = r->CreateTexture(width, height, 1, format, true);
        dword_982A74 = second;
        ok = second != nullptr;
        if (ok)
            dword_982A70 = TopSurface(second);
    }
    if (ok)
        return;
    lpData_009017d4 = 0;
    dword_90185C = 0;
    ReleaseObject(dword_982A6C);
    ReleaseObject(dword_982A68);
    ReleaseObject(dword_982A78);
    ReleaseObject(dword_982A74);
    ReleaseObject(dword_982A70);
}

/** The rear-view mirror's target and depth buffer, and the 320x200 target that goes with it when dword_982B18 is set. */
void eCreateMirrorTargets()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    const uint32_t format = static_cast<uint32_t>(D3DFORMAT_008f9000);
    const uint32_t width = static_cast<uint32_t>(_g_D3D_stencil_Width);
    const uint32_t height = static_cast<uint32_t>(_g_D3D_stencil_height);
    nfs::vk::Texture *mirror = r->CreateTexture(width, height, 1, format, true);
    g_RearViewMirrorTexture = reinterpret_cast<IDirect3DTexture9 *>(mirror);
    _g_I3DSurface2 = TopSurface(mirror);
    _g_I3DSurface = r->CreateTexture(width, height, 1, static_cast<uint32_t>(_g_D3DFORMAT), true);
    if (dword_982B18 > 0)
    {
        nfs::vk::Texture *extra = r->CreateTexture(320, 200, 1, format, true);
        _g_I3DTexture2 = extra;
        _g_ID3Surface3 = TopSurface(extra);
    }
}

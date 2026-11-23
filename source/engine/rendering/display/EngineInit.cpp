/**
 * Renderer start-up (original: zPlatform eInitEnginePlat 0x6e69f0 and the helpers it calls that touch the device:
 * device creation 0x6bfbd0, __CreateVertexDeclarations 0x6bf3e0, effect creation 0x6d6000, eEffect::Init 0x6d5c10,
 * eEffect::Load 0x6c6080, the caps defaults 0x6df9c0 and the sun occlusion queries 0x6c00d0; the particle buffers
 * 0x6c0e80 are in Particles.cpp), and the effects' deletion at shutdown 0x6c5d20.
 *
 * With the Direct3D 9 renderer every helper runs the original code. In Vulkan mode (docs/VULKAN.md) the same start-up
 * makes the Vulkan renderer where the original makes the device: the window, the video options and the adapter queries
 * (asked of IDirect3D9, which needs no device) are unchanged, effects are the native runtime behind ID3DXEffect, and
 * vertex declarations become renderer vertex layouts.
 */
#include <float.h>
#include <stdlib.h>

#include "game/gen/api.h"
#include "engine/rendering/display/adapter.h"
#include "core/config.h"
#include "gameplay/progression/preferences.h"
#include "engine/rendering/raindrops.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "game/platform/D3DX.h"
#include "engine/rendering/display/display.h"
#include "core/sdl.h"
#include "legacy/link.h"
#include "engine/rendering/x360/post.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/shader.h"
#include "core/log.h"
#include "gameplay/world/timeofday.h"
#include "dev/coverage.h"
#include "dev/sampler.h"
#include "engine/rendering/display/video.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"

/** The effects' parameter table (source/engine/rendering/frame/EffectParameters.cpp). */
void __fastcall ConstructEffect(eEffect *effect, void *);
void __fastcall BindEffectParameters(eEffect *effect, void *);
void __fastcall SetEffectDefaults(eEffect *effect, void *);
void __cdecl ReleaseEffectTargets();


NFS_REPLACE(0x6e69f0, eInitEnginePlat)
NFS_REPLACE(0x6bfbd0, sub_6BFBD0)
NFS_REPLACE(0x6bf3e0, __CreateVertexDeclarations)
NFS_REPLACE(0x6df9c0, sub_6DF9C0)
NFS_REPLACE(0x6d6000, sub_6D6000)
NFS_REPLACE(0x6d5c10, eEffectInit)
NFS_REPLACE(0x6c6080, eEffectLoad)
NFS_REPLACE(0x6c00d0, SunQueriesInit)
NFS_REPLACE(0x6db0d0, sub_6DB0D0)
NFS_REPLACE(0x6c5d20, eDeleteEffects)

// Copy-protection checks the original start-up interleaves with its work; they are called as it calls them.
extern "C" unsigned int __cdecl sub_6E6230(unsigned int, unsigned int);
extern "C" unsigned int __cdecl sub_6E61C0(unsigned int, unsigned int);
extern "C" unsigned int __cdecl sub_6E6310(unsigned int, unsigned int);
extern "C" unsigned int __cdecl sub_6E62A0(unsigned int, unsigned int);
extern "C" unsigned int __cdecl sub_6E6460(unsigned int, unsigned int);
extern "C" unsigned int __cdecl sub_6E60E0(unsigned int, unsigned int);
extern "C" unsigned int __cdecl sub_6E63F0(unsigned int, unsigned int);
extern "C" unsigned int __cdecl sub_6E6150(int, int);
extern "C" unsigned int __cdecl sub_6E6070(unsigned int, unsigned int);

extern "C" void __cdecl sub_6C1510();
extern "C" float __cdecl sub_6DD380(D3DADAPTER_IDENTIFIER9 *identifier);
extern "C" float __cdecl __getUnkMultByGPU(int vendor, int device);
extern "C" void __cdecl sub_6D7500();
extern "C" void __cdecl sub_6E42B0();
extern "C" void __cdecl sub_6DC510();

extern "C" int __first_time;
extern "C" int PrecullerMode;
extern "C" SlotPool *g_textureHeaderPool;
extern "C" SlotPool *g_vertexBufferHeaderPool;
extern "C" D3DADAPTER_IDENTIFIER9 D3DADAPTER_IDENTIFIER9_0093d470;
extern "C" int GPUVendor;
extern "C" int GPUDevice;
extern "C" int __multisamplequality;
extern "C" int __resolution_index;
extern "C" int dword_901890;
extern "C" ID3DXEffectPool *_g_ID3DXEffectPool;
extern "C" unsigned int name_hash;
/** Max texture filtering the card allows (0 bilinear, 1 trilinear, 2 anisotropic) and the option itself. */
extern "C" int dword_9018A0;
extern "C" int lpData_00901818;
/** Shadow map method from the adapter's formats: 0 none, 1 depth textures (NVIDIA), 2 DF16 (ATI). */
extern "C" int dword_982C08;
/** Highest "shader" annotation allowed (-1: any), and whether old NVIDIA cards are held to shader level 1. */
extern "C" int dword_8FAD24;
extern "C" unsigned char byte_8FAD9C;
extern "C" unsigned char dword_982C7C;
extern "C" int dword_982BFC;
extern "C" unsigned char byte_982C40;
extern "C" IDirect3DVertexDeclaration9 *dword_93CB2C;
extern "C" IDirect3DVertexDeclaration9 *dword_93CC60;
extern "C" SlotPool *dword_982AC0;
extern "C" char AutoClass1_0093caf4[];
extern "C" char dword_93E018[];
extern unsigned int g_AdapterOrdinal;
/** The window procedure and the window title. */
extern "C" void sub_6DB6C0();
extern "C" char lpWindowName_008b0050[];

extern "C" HRESULT __stdcall D3DXCreateEffectPool(ID3DXEffectPool **pool);

/** One entry of the effect table at 0x8f9b60: the vertex format as (type, usage) index pairs ending with type 8, and
 * the resource the effect is loaded from. */
struct EffectRecord
{
    const char *Name;
    int Format[0x20];
    const char *FixedName;
    const char *Resource;
    int unknown_8c;
};

static_assert(sizeof(EffectRecord) == 0x90, "effect table record");

/** The effect table, and the vertex format tables its pairs index: declaration type and size by type index, usage
 * and usage index by usage index. */
extern "C" EffectRecord off_8F9B60[NUM_EFFECTS];
extern "C" unsigned char byte_8FAD28[];
extern "C" unsigned char byte_8FAD48[];
extern "C" unsigned char byte_8FAD70[];
extern "C" int dword_8FAD7C[];

extern "C" void __CreateVertexDeclarations();
extern "C" void sub_6D6000();
extern "C" void sub_6DF9C0();
void __fastcall eEffectInit(eEffect *self, void *, int type);
void __fastcall eEffectLoad(eEffect *self, void *);
void __fastcall SunQueriesInit(void *self, void *);
extern "C" int __cdecl sub_6DB0D0();
extern "C" void __cdecl sub_6D20C0();
extern "C" void __cdecl eInitFEEnvMapPlat();
/** The sun's occlusion queries (the original's device objects; none in Vulkan mode). */
extern "C" void *unk_93E03C[4];
void __fastcall ParticleBuffersInit(void *self, void *, const int *fvf, const int *quads);

namespace
{
const unsigned int kVendorNvidia = 0x10DE;
const unsigned int kVendorIntel = 0x8086;
const unsigned int kVendorSis = 0x1039;
const unsigned int kVendorMatrox = 0x102B;

/** The vtable each of the 31 eEffect objects gets (the subclasses differ only in Start/End). Slots 24, 25 and 28 (the
 * Xbox 360 water and grass effects) are world effects, slot 26 (the car with normal maps) a car effect. */
const unsigned int kEffectVtables[NUM_EFFECTS] = {
    0x8af204, 0x8af234, 0x8af240, 0x8af204, 0x8af24c, 0x8af228, 0x8af204, 0x8af204, 0x8af204, 0x8af210, 0x8af210,
    0x8af264, 0x8af264, 0x8af270, 0x8af204, 0x8af204, 0x8af270, 0x8af210, 0x8af204, 0x8af21c, 0x8af27c, 0x8af288,
    0x8af294, 0x8af258, 0x8af204, 0x8af204, 0x8af24c, 0x8af2a0, 0x8af204, 0x8af2a0, 0x8af204};

/**
 * The Xbox 360 effect each slot loads (rebuild/assets/fx, docs/X360.md); the slots without one keep the PC effect.
 * Slots 24 to 28 held the shadow map depth effects, which the Xbox 360 shadows do not use.
 */
const char *const kX360Effects[NUM_EFFECTS] = {
    "X360_world",         "X360_worldreflect",     "X360_worldbone", "X360_worldnormalmap",
    "X360_car",           "X360_glassreflectshader", "X360_trees",   nullptr,
    "X360_world",         "X360_standard",         "X360_standard",  nullptr,
    nullptr,              nullptr,                 nullptr,          nullptr,
    "X360_visualtreatment", "X360_standard",       "X360_particles", "X360_sky",
    nullptr,              nullptr,                 nullptr,          nullptr,
    "X360_waterseashader", "X360_grass",           "X360_carnormalmap", "X360_debugpoly",
    "X360_grass",         nullptr,                 "X360_worldnormalmap"};

/** The effect table entries the Xbox 360 effects change: the new slots' names, and the debug polygon slot (27), a copy
 * of the front end's (its vertex format included). */
void SetUpX360EffectTable()
{
    static bool done;
    if (done)
        return;
    done = true;
    off_8F9B60[27] = off_8F9B60[9];
    off_8F9B60[24].Name = "WaterSeaShader";
    off_8F9B60[25].Name = "GrassNormalMap";
    off_8F9B60[26].Name = "CarNormalMap";
    off_8F9B60[27].Name = "DebugPoly";
    off_8F9B60[28].Name = "Grass";
}

void ClearSlotPoolFlag(SlotPool *pool)
{
    // Bit 1 of the pool's flags (+0x14) is cleared for the renderer's pools, as the original does.
    *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(pool) + 0x14) &= ~2u;
}

nfs::vk::VertexLayout *Layout(const D3DVERTEXELEMENT9 *elements)
{
    return nfs::vk::g_renderer->CreateVertexLayout(elements);
}

bool CreateRenderer()
{
    // The back buffer is the size the game draws at (render scale included); frames are scaled onto the window.
    nfs::vk::Renderer *renderer = new nfs::vk::Renderer;
    int width = g_WindowWidth, height = g_WindowHeight;
    // -headless: without a window the renderer only draws into its back buffer (-headlesssurface: it presents to the
    // hidden window, to test the surface and swapchain in headless runs).
    HWND window = nfs::display::Headless() && !nfs::vk::HasArgument("-headlesssurface") ? nullptr : g_hWnd;
    if (!renderer->Initialize(window, width > 0 ? width : 640, height > 0 ? height : 480))
    {
        renderer->Shutdown();
        delete renderer;
        return false;
    }
    renderer->SetVSync(g_PresentParameters.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE);
    nfs::vk::g_renderer = renderer;
    return true;
}
} // namespace

/** The device: created on the adapter (or NVPerfHUD's), hardware vertex processing first. The original does not ask
 * for D3DCREATE_FPU_PRESERVE, so Direct3D 9 drops the x87 unit to single precision; Vulkan mode does the same so the
 * simulation computes exactly as it does on Direct3D 9. Returns the HRESULT. */
extern "C" int sub_6BFBD0()
{
    if (!nfs::vk::g_renderer && !CreateRenderer())
    {
        // There is no other renderer to fall back on: say why the game cannot start, and stop.
        nfs::Log("Vulkan: the renderer could not start");
        if (!nfs::display::Headless())
            MessageBoxA(nullptr,
                        "The game needs a graphics card and driver with Vulkan.\n\n"
                        "Update the graphics driver and start the game again. nfs.log next to the game has the details.",
                        "Need for Speed Most Wanted", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    _controlfp(_PC_24, _MCW_PC);
    _controlfp(_RC_NEAR, _MCW_RC);
    _controlfp(_MCW_EM, _MCW_EM);
    g_D3DDevice = nfs::vk::ReportingDevice();
    nfs::vk::g_renderer->BeginFrame();
    nfs::vk::g_renderer->Clear(7, 0, 1.0f, 0);
    nfs::vk::g_renderer->EndFrame();
    return 0;
}

namespace
{
/** Each of the two per-frame allocation buffers (FrameMalloc). The original's 400 KB runs out once the Xbox 360 views
 * (six environment faces, the road reflection, the shadow map) also take from it; a failed allocation silently drops
 * what asked for it, car light flares first among them. */
const unsigned int kFrameMemoryBytes = 8u << 20;
unsigned char *s_originalFrameMemory[2];
} // namespace

extern "C" unsigned char *FrameMemoryBuffer;
extern "C" unsigned char *dword_91512C;

/** The engine shutdown (0x500a80) frees the frame buffers with FastMem::Free: it gets the ones it allocated back. */
extern "C" void __cdecl eShutdownEngine()
{
    if (s_originalFrameMemory[0])
    {
        ::operator delete(FrameMemoryBuffer);
        ::operator delete(dword_91512C);
        FrameMemoryBuffer = s_originalFrameMemory[0];
        dword_91512C = s_originalFrameMemory[1];
        s_originalFrameMemory[0] = s_originalFrameMemory[1] = nullptr;
    }
    NFS_ORIGINAL(0x500a80, void(__cdecl *)())();
}
NFS_REPLACE(0x500a80, eShutdownEngine)

extern "C" int eInitEnginePlat()
{
    nfs::sampler::Start();
    nfs::coverage::Start();
    // eInitEngine's last call: nothing has taken from its frame buffers yet.
    if (!s_originalFrameMemory[0])
    {
        unsigned char *front = static_cast<unsigned char *>(malloc(kFrameMemoryBytes));
        unsigned char *back = static_cast<unsigned char *>(malloc(kFrameMemoryBytes));
        if (front && back)
        {
            s_originalFrameMemory[0] = FrameMemoryBuffer;
            s_originalFrameMemory[1] = dword_91512C;
            FrameMemoryBuffer = front;
            dword_91512C = back;
            FrameMemoryBufferSize = kFrameMemoryBytes;
            CurrentBufferStart = front;
            CurrentBufferPos = front;
            CurrentBufferEnd = front + kFrameMemoryBytes;
        }
        else
        {
            ::operator delete(front);
            ::operator delete(back);
        }
    }
    if (g_SafeMode)
        __first_time = 1;
    struct
    {
        unsigned int a, b;
    } key = {0x8F9280, 0x905E9C};
    PrecullerMode = 1;
    sub_6E61C0(0x87FA2BEC, sub_6E6230(reinterpret_cast<unsigned int>(&key), reinterpret_cast<unsigned int>(&key)));
    // The adapter's name, modes and caps, without Direct3D 9 (source/engine/rendering/display/Adapter.cpp).
    g_D3D = nfs::adapter::Create();
    sub_6E61C0(0xA4BB2EE0, sub_6E6230(0x2B, 0x2B));
    sub_6C1510();
    // X360 Stuff gives every vendor the depth texture shadow map method (Rendering::ForceShadowsOnAllVendors), here
    // where the plugin does it: before the video options are loaded and clamped, so Shadow Detail is not held at off.
    if (!dword_982C08)
        dword_982C08 = 1;
    // The video options are in speed.ini (gameplay/progression/preferences.h): there it is the first start when they are not.
    nfs::config::MoveProfileSections();
    if (!g_SafeMode && nfs::prefs::HaveVideo())
        __first_time = 0;
    if (!__first_time)
        LoadVideoOptions();
    _controlfp(3, 0x8001F);
    g_textureHeaderPool = bNewSlotPool(4, 0x400, "g_textureHeaderPool", 0);
    ClearSlotPoolFlag(g_textureHeaderPool);
    g_vertexBufferHeaderPool = bNewSlotPool(4, 0x400, "g_vertexBufferHeaderPool", 0);
    ClearSlotPoolFlag(g_vertexBufferHeaderPool);

    WNDCLASSEXA wc = {};
    HICON icon = LoadIconA(GetModuleHandleA(nullptr), "IDI_ICON1");
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&sub_6DB6C0);
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hIcon = icon;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = "GameFrame";
    RegisterClassExA(&wc);
    g_BackBufferFormat = D3DFMT_A8R8G8B8;
    if (__first_time)
    {
        g_PerformanceLevel = static_cast<int>(sub_6DD380(&D3DADAPTER_IDENTIFIER9_0093d470));
        if (g_PerformanceLevel > 3)
        {
            __multisamplequality = 1;
            short level = static_cast<short>(static_cast<int>(__getUnkMultByGPU(GPUVendor, GPUDevice)));
            if (sub_6E6070(sub_6E6150(level, level), 0xF4E41267) != 0x75A5125B)
                __resolution_index = 1;
        }
    }
    int width, height;
    GetRacingResolution(&width, &height);
    int refresh = g_RefreshRates[__resolution_index];
    RECT rect = {100, 100, width + 100, height + 100};
    g_WindowWidth = width;
    g_WindowHeight = height;
    struct
    {
        unsigned int a, b;
    } key2 = {0x8F9220, 0x8F8AA8};
    sub_6E62A0(0x899CC300, sub_6E6310(reinterpret_cast<unsigned int>(&key2), reinterpret_cast<unsigned int>(&key2)));
    AdjustWindowRect(&rect, sub_6E6460(0x54052D06, 0x54052D06), FALSE);
    sub_6E62A0(0x8DD6704, sub_6E6310(0xD, 0xD));
    // -headless never shows the window, so it never takes the focus or the cursor from what the user is doing.
    const bool headless = nfs::display::Headless();
    DWORD style = sub_6E6460(0x54052D06, 0x54052D06);
    if (headless)
        style &= ~WS_VISIBLE;
    g_hWnd = CreateWindowExA(0, "GameFrame", lpWindowName_008b0050, style, rect.left, rect.top,
                             rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);
    // SDL places the window, reads the keyboard and mouse and makes the Vulkan surface (core/sdl.h).
    if (!nfs::sdl::AdoptWindow(g_hWnd))
    {
        if (!headless)
            MessageBoxA(nullptr,
                        "SDL3.dll is missing or could not start.\n\n"
                        "It belongs next to the game's exe. nfs.log next to the game has the details.",
                        "Need for Speed Most Wanted", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    UpdateWindow(g_hWnd);
    if (!g_Windowed && !headless)
    {
        SetCursor(nullptr);
        ShowCursor(FALSE);
    }
    // Windowed, the system's cursor hides over the client area only (SDL answers WM_SETCURSOR there), leaving the
    // game's pointer, drawn under it (source/core/input/Cursor.cpp); the title bar and borders keep it.
    if (!headless)
        SDL_HideCursor();
    if (!headless)
    {
        SetFocus(g_hWnd);
        SetForegroundWindow(g_hWnd);
    }
    GetWindowRect(g_hWnd, &g_WindowRect);
    GetClientRect(g_hWnd, &g_ClientRect);
    InitPresentParameters(width, height, refresh);
    if (sub_6BFBD0() && __multisamplequality > 0)
    {
        __multisamplequality = 0;
        dword_901890 = 0;
        __resolution_index = 0;
        InitPresentParameters(width, height, refresh);
        sub_6BFBD0();
    }
    sub_6E60E0(0x54052D06, 0x76470817);
    sub_6E62A0(0x7514A902, sub_6E6310(1, 1));
    if (!headless)
        nfs::display::PlaceWindow();
    // Let the new window handle its first messages before the long loads (X360 Stuff's Rendering::MiniMsgLoop), so it
    // paints and is not reported as hanging.
    MSG message;
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
        Sleep(8);
        if (message.message == WM_NCACTIVATE)
            break;
    }
    __CreateVertexDeclarations();
    sub_6D6000();
    sub_6DF9C0();
    ClampVideoOptions();
    sub_6D7500();
    sub_6E42B0();
    SunQueriesInit(dword_93E018, nullptr);
    eCreateRenderTargets();
    int fvf = 0, quads = 0x400;
    ParticleBuffersInit(AutoClass1_0093caf4, nullptr, &fvf, &quads);
    dword_982AC0 = bNewSlotPool(0x604, 0x20, "eStripSlotPool", 0);
    ClearSlotPoolFlag(dword_982AC0);
    sub_6E63F0(0x6F876682, 0x51E6395B);
    sub_6E61C0(0x50B6B96E, sub_6E6230(4, 4));
    sub_6DC510();
    return 0;
}

/** The two fixed vertex formats: position, normal, colour and one texture coordinate (0x93cb2c), and a transformed
 * screen position with colour and one texture coordinate (0x93cc60). */
extern "C" void __CreateVertexDeclarations()
{
    static const D3DVERTEXELEMENT9 kModel[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, 28, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END(),
    };
    static const D3DVERTEXELEMENT9 kScreen[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0},
        {0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, 20, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END(),
    };
    dword_93CB2C = reinterpret_cast<IDirect3DVertexDeclaration9 *>(Layout(kModel));
    dword_93CC60 = reinterpret_cast<IDirect3DVertexDeclaration9 *>(Layout(kScreen));
}

/** Device caps and first-run video defaults; also switches the fixed-function lights off. */
extern "C" void sub_6DF9C0()
{
    OSVERSIONINFOA version = {sizeof(version)};
#pragma warning(suppress : 4996)
    GetVersionExA(&version);
    byte_982C40 = version.dwMajorVersion != 4;
    if (!dword_982BFC)
    {
        // g_DeviceCaps already holds the adapter's caps (sub_6C1510); there are no fixed-function lights to reset.
        DetectVideoOptionLimits();
        if (__first_time == 1)
        {
            SetDefaultVideoOptions();
            SaveVideoOptions();
        }
        if (nfs::video::TestOptions())
            nfs::video::ApplyTestOptions();
    }
    dword_982BFC = 1;
}

/** Makes the 31 eEffect objects, runs eEffect::Init on each (vertex format, technique choice) and releases the
 * effects again; sub_6D7500 loads them for good. Both renderers: the effect classes are the Xbox 360 layout's. */
extern "C" void sub_6D6000()
{
    SetUpX360EffectTable();
    name_hash = bStringHash("WINDOWREFLECTION");
    g_D3D->GetDeviceCaps(g_AdapterOrdinal, D3DDEVTYPE_HAL, &g_DeviceCaps);
    int filtering = (g_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) ? 1 : 0;
    if (g_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC)
        ++filtering;
    dword_9018A0 = filtering;
    if (lpData_00901818 > filtering)
        lpData_00901818 = filtering;
    for (int i = 0; i < NUM_EFFECTS; ++i)
    {
        eEffect *effect = static_cast<eEffect *>(::operator new(sizeof(eEffect)));
        ConstructEffect(effect, nullptr);
        *reinterpret_cast<unsigned int *>(effect) = kEffectVtables[i];
        g_Effects[i] = effect;
    }
    g_D3D->GetDeviceCaps(g_AdapterOrdinal, D3DDEVTYPE_HAL, &g_DeviceCaps);
    g_D3D->GetAdapterIdentifier(g_AdapterOrdinal, D3DENUM_WHQL_LEVEL, &D3DADAPTER_IDENTIFIER9_0093d470);
    D3DXCreateEffectPool(&_g_ID3DXEffectPool);
    for (int i = 0; i < NUM_EFFECTS; ++i)
        eEffectInit(g_Effects[i], nullptr, i);
    dword_982C7C = 1;
    for (eEffect *effect : g_Effects)
    {
        effect->Effect->OnLostDevice();
        effect->Effect->Release();
        effect->Effect = nullptr;
    }
    if (_g_ID3DXEffectPool)
    {
        reinterpret_cast<IUnknown *>(_g_ID3DXEffectPool)->Release();
        _g_ID3DXEffectPool = nullptr;
    }
}

/** DeInitializeEverything deletes the effects, then releases every texture until it is freed, whatever still holds
 * it: the post effect (and the night sky's) goes with the game's effects, before its vignette texture does. */
extern "C" void eDeleteEffects()
{
    NFS_ORIGINAL(0x6c5d20, void(__cdecl *)())();
    nfs::x360::ReleasePostEffect();
    nfs::timeofday::ReleaseNightSky();
    nfs::x360::ReleaseRainDrops();
}

/** eEffect::Load: (re)creates the effect, the slot's Xbox 360 effect when it has one, else the PC resource, then
 * binds the parameter table and sets the defaults. */
void __fastcall eEffectLoad(eEffect *self, void *)
{
    nfs::x360::LoadPostEffect();
    if (self->Effect)
    {
        self->Effect->OnLostDevice();
        self->Effect->Release();
        self->Effect = nullptr;
    }
    // A new effect can come back at the old one's address; its handles must be looked up again either way.
    nfs::x360::g_shaders[self->Type].effect = nullptr;
    nfs::x360::ResetMaterialCache();
    const char *x360 = kX360Effects[self->Type];
    const char *resource = x360 ? x360 : off_8F9B60[self->Type].Resource;
    self->Effect = nfs::vk::CreateEffect(resource);
    if (!self->Effect)
    {
        nfs::Log("no effect %s", resource);
        return;
    }
    BindEffectParameters(self, nullptr);
    SetEffectDefaults(self, nullptr);
    // The Xbox 360 sky effect: its first level counts as having shaders (the X360 Stuff loader does this).
    if (self == g_Effects[EFFECT_SKY])
        *reinterpret_cast<unsigned char *>(&self->LevelShaders[0]) = 1;
}

/**
 * eEffect::Init(type): the vertex declaration from the effect table, the effect itself, and the technique to use:
 * the best "shader" level per shadow level for the world and car effects, the best one overall for the others, with the
 * original's exceptions for cards that cannot run some of them.
 */
void __fastcall eEffectInit(eEffect *self, void *, int type)
{
    if (self->Effect)
    {
        self->Effect->Release();
        self->Effect = nullptr;
    }
    const int vendor = GPUVendor, device = GPUDevice;
    self->Type = type;
    const EffectRecord &record = off_8F9B60[type];
    D3DVERTEXELEMENT9 elements[17];
    int count = 0, offset = 0;
    for (const int *pair = record.Format; pair[0] != 8 && count < 16; pair += 2, ++count)
    {
        elements[count].Stream = 0;
        elements[count].Offset = static_cast<WORD>(offset);
        elements[count].Type = byte_8FAD28[pair[0] * 4];
        elements[count].Method = 0;
        elements[count].Usage = byte_8FAD48[pair[1] * 4];
        elements[count].UsageIndex = byte_8FAD70[pair[1]];
        offset += dword_8FAD7C[pair[0]];
    }
    elements[count] = D3DDECL_END();
    self->VertexDeclaration = reinterpret_cast<IDirect3DVertexDeclaration9 *>(Layout(elements));
    self->Stride = offset;
    eEffectLoad(self, nullptr);
    if (!self->Effect)
        return;

    struct
    {
        const char *Creator;
        UINT Parameters;
        UINT Techniques;
        UINT Functions;
    } desc;
    self->Technique = nullptr;
    self->Effect->GetDesc(&desc);
    for (UINT t = 0; t < desc.Techniques; ++t)
    {
        D3DXHANDLE technique = self->Effect->GetTechnique(t);
        if (!technique)
            continue;
        int shader = 0, shadowLevel = 0;
        if (D3DXHANDLE a = self->Effect->GetAnnotationByName(technique, "shader"))
            self->Effect->GetInt(a, &shader);
        if (D3DXHANDLE a = self->Effect->GetAnnotationByName(technique, "shadowLevel"))
            self->Effect->GetInt(a, &shadowLevel);
        if (dword_8FAD24 != -1 && dword_8FAD24 < shader)
            continue;
        if (vendor == kVendorNvidia)
        {
            bool older = g_DeviceCaps.PixelShaderVersion < D3DPS_VERSION(3, 0) && (device < 0x330 || device > 0x334);
            if (older && byte_8FAD9C && shader > 1)
                continue;
        }
        else if (vendor == kVendorIntel && (device == 0x2772 || device == 0x2582) && shader > 0)
        {
            continue;
        }
        if (type == 1)
        {
            if ((vendor == kVendorSis || (vendor == kVendorIntel && device == 0x2572)) && t < 3)
                continue;
        }
        else if ((type == 4 && vendor == kVendorMatrox) || (type == 11 && vendor == kVendorSis))
        {
            if (t == 0)
                continue;
        }
        if (self->Effect->ValidateTechnique(technique) != D3D_OK)
            continue;
        bool perShadowLevel = type == 0 || type == 1 || type == 3 || type == 4 || type == 5 || type == 18;
        if (perShadowLevel)
        {
            int level = shadowLevel;
            if (shadowLevel == 0)
            {
                if (self->LevelShaders[0] >= shader)
                    continue;
                self->LevelShaders[0] = shader;
                self->LevelTechniques[0] = static_cast<int>(t);
            }
            else if (dword_982C08 == 1 || dword_982C08 == 2)
            {
                if (dword_982C08 == 1)
                {
                    if (shadowLevel >= 4)
                        continue;
                }
                else if (shadowLevel >= 4)
                {
                    level = shadowLevel - 3;
                }
                else
                {
                    continue;
                }
                if (self->LevelShaders[level] < shader)
                {
                    self->LevelShaders[level] = shader;
                    self->LevelTechniques[level] = static_cast<int>(t);
                }
            }
            else
            {
                continue;
            }
            if (level > self->MaxLevel)
                self->MaxLevel = level;
            self->Technique = technique;
            self->TechniqueIndex = static_cast<int>(t);
            self->HasShaders = true;
        }
        else if (self->LevelShaders[0] == -1 || shader > self->LevelShaders[0])
        {
            self->HasShaders = shader > 0;
            self->LevelShaders[0] = shader;
            self->TechniqueIndex = static_cast<int>(t);
            self->Technique = technique;
            struct
            {
                const char *Name;
                UINT Passes;
                UINT Annotations;
            } techniqueDesc;
            self->Effect->GetTechniqueDesc(technique, &techniqueDesc);
            self->PassCount = static_cast<int>(techniqueDesc.Passes);
        }
    }
}


/**
 * Applies changed video options (original: zPlatform 0x6db0d0, the device reset): everything made for the old options
 * is released, the back buffer takes the new render size, the swapchain the new sync, and the render targets, particle
 * buffers and effect state are made again. Nothing is lost on Vulkan, so this cannot fail. Returns 0.
 */
extern "C" int __cdecl sub_6DB0D0()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    DetectVideoOptionLimits();
    for (eEffect *effect : g_Effects)
        effect->Effect->OnLostDevice();
    eReleaseRenderTargets();
    for (void *&query : unk_93E03C)
    {
        if (query)
            static_cast<IDirect3DQuery9 *>(query)->Release();
        query = nullptr;
    }
    ReleaseEffectTargets();
    sub_6D20C0();
    const uint32_t width = static_cast<uint32_t>(g_WindowWidth), height = static_cast<uint32_t>(g_WindowHeight);
    nfs::vk::Texture *back = r->BackBuffer();
    if (width && height && (!back || back->width != width || back->height != height))
        r->Resize(width, height);
    // The Video page's Sync: the swapchain is made again when the swap interval changes.
    r->SetVSync(g_PresentParameters.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE);
    SunQueriesInit(dword_93E018, nullptr);
    eCreateRenderTargets();
    eInitFEEnvMapPlat();
    const int fvf = 0, quads = 0x400;
    ParticleBuffersInit(AutoClass1_0093caf4, nullptr, &fvf, &quads);
    eResetRenderStates();
    for (eEffect *effect : g_Effects)
    {
        effect->Effect->OnResetDevice();
        BindEffectParameters(effect, nullptr);
    }
    return 0;
}

/** The sun's occlusion queries (for lens flares); in Vulkan mode there are none yet, which the game handles as it does
 * a card without queries. */
void __fastcall SunQueriesInit(void *self, void *)
{
    unsigned int *q = static_cast<unsigned int *>(self);
    q[0] = 0;
    q[1] = 1;
    q[2] = 0x3F000000;
    q[3] = 0x3F7FF972;
    *reinterpret_cast<unsigned char *>(q + 4) = 0;
    q[5] = 0;
    q[6] = 0x3F800000;
    q[7] = 0;
    q[8] = 0;
    for (int i = 0; i < 4; ++i)
        q[9 + i] = 0;
}

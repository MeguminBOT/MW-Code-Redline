/**
 * The views each camera mode uses, with the Xbox 360 mirror (X360 Stuff's patches to MaybeChangeViewMode 0x6cfad0 and
 * sub_595D40 for CubemapRVM, and Rendering::MaybeChangeViewModeEx 0x1002fd80): the mirror is drawn from the rear
 * environment map face, so its own view never gets a target, and it shows in the bumper camera whatever the camera
 * mover does.
 */
#include "game/platform/Device.h"
#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"

NFS_REPLACE(0x6cfad0, MaybeChangeViewMode)
NFS_REPLACE(0x595d40, sub_595D40)

extern "C" int RenderingViewMode;
extern "C" void __cdecl MaybeChangeViewMode();
extern "C" bool __cdecl sub_595D40(int view);

namespace
{
const int kViewPlayer1 = 1, kViewPlayer2 = 2, kViewMirror = 3, kViewRoadReflection = 4, kView5 = 5;

/** The players' list (UTL::Collections::ListableSet<IPlayer>): its size and first entry. */
int PlayerCount()
{
    return *reinterpret_cast<const int *>(0x92D884);
}

void *FirstPlayer()
{
    return **reinterpret_cast<void ***>(0x92D87C);
}

void SetActive(int view, bool active)
{
    eViews[view].Active = active;
}

void SetSize(eRenderTarget *target, unsigned long width, unsigned long height)
{
    target->Width = static_cast<int>(width);
    target->Height = static_cast<int>(height);
}
} // namespace

/** Whether the mirror shows (sub_595D40): with the cube map mirror the camera mover and the camera mode no longer
 * matter, only the race HUD. */
extern "C" bool __cdecl sub_595D40(int)
{
    if (PlayerCount())
    {
        if (void *player = FirstPlayer())
        {
            typedef void *(__thiscall * Get)(void *);
            typedef bool(__thiscall * Query)(void *);
            Get get = (*reinterpret_cast<Get **>(player))[8];
            if (void *simable = get(player))
            {
                Query query = (*reinterpret_cast<Query **>(simable))[6];
                if (!query(simable))
                    return false;
            }
        }
    }
    if (FEManager::ShouldPauseSimulation(true))
        return false;
    if (!FEDatabase)
        return false;
    const char *settings = *reinterpret_cast<char **>(reinterpret_cast<char *>(FEDatabase) + 0x10);
    if (!settings[0x39])
        return false;
    return cFEng::mInstance->IsPackagePushed("HUD_SingleRace.fng");
}

/** The camera mode's views (MaybeChangeViewMode, then Rendering::MaybeChangeViewModeEx). */
extern "C" void __cdecl MaybeChangeViewMode()
{
    NFS_ORIGINAL(0x6cfad0, void(__cdecl *)())();
    using namespace nfs::x360;
    // The original sets the road reflection's target to 320x240 here, whatever was made for it. That shrank the target, and at 4:3
    // the view's projection is narrower than the player's: the wet road reads the target at each pixel's place on the screen, so
    // on a wide screen everything in it, the car first, came out a third wider than it stands.
    eRenderTarget *road = eViews[kViewRoadReflection].RenderTarget;
    UINT roadWidth = 0, roadHeight = 0;
    if (road->Surface)
        device::SurfaceSize(road->Surface, roadWidth, roadHeight);
    if (roadWidth && roadHeight)
        SetSize(road, roadWidth, roadHeight);
    if (RenderingViewMode == 2 && options::kCubemapRVM)
    {
        eViews[kViewMirror].RenderTarget->unknown_10 = 0;
        SetActive(kViewMirror, false);
    }
    eRenderTarget *first = eViews[kViewPlayer1].RenderTarget, *second = eViews[kViewPlayer2].RenderTarget;
    switch (RenderingViewMode)
    {
    case 0:
        for (int v = kViewPlayer1; v <= kView5; ++v)
            SetActive(v, false);
        break;
    case 1:
    case 2:
        SetActive(kViewPlayer1, true);
        SetActive(kViewPlayer2, false);
        SetActive(kViewMirror, RenderingViewMode == 2);
        SetActive(kViewRoadReflection, true);
        SetActive(kView5, false);
        SetSize(first, g_renderWidth, g_renderHeight);
        break;
    case 3:
    case 4:
    {
        SetActive(kViewPlayer1, true);
        SetActive(kViewPlayer2, true);
        SetActive(kViewMirror, false);
        SetActive(kViewRoadReflection, true);
        SetActive(kView5, true);
        const unsigned long width = RenderingViewMode == 4 ? g_renderWidth >> 1 : g_renderWidth;
        const unsigned long height = RenderingViewMode == 3 ? g_renderHeight >> 1 : g_renderHeight;
        SetSize(first, width, height);
        SetSize(second, width, height);
        break;
    }
    }
}

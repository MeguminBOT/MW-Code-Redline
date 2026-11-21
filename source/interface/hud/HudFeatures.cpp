/** Which HUD elements are shown (original: zFe2 FEngHud::DetermineHudFeatures, SetHudFeatures). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x57ca60, DetermineHudFeatures)
NFS_REPLACE(0x57cdb0, SetHudFeatures)
NFS_REPLACE(0x5a0c20, IsHudVisible)

namespace
{
struct Element
{
    int offset;
    unsigned long long features;
};

const Element kElements[] = {
    {0x2c0, 0x8000000},   {0x2c4, 0x2},      {0x2c8, 0x2},       {0x2cc, 0x20000000}, {0x2dc, 0x20000},
    {0x2e0, 0x40},        {0x2e4, 0x800},    {0x2e8, 0x40000},   {0x2d8, 0x4000},     {0x320, 0x10000},
    {0x324, 0x200},       {0x328, 0x400000}, {0x2f8, 0x4000000}, {0x2fc, 0x18},       {0x300, 0x100000},
    {0x304, 0x400000000}, {0x308, 0x800000}, {0x30c, 0x2000000}, {0x2d0, 0x1000},     {0x2d4, 0x1000},
    {0x310, 0x20},        {0x2ec, 0x4},      {0x2f0, 0x1000000}, {0x31c, 0x200000},   {0x318, 0x400},
    {0x32c, 0x200000000},
};

const int kFeatures = 0x18;
const int kHudType = 0x20;
const int kPlayerIndex = 0x2c;
const int kHudShowsTurbo = 0x2bd;
const int kHudProfileTag = 0x330;
const int kRaceActive = 0x1960;
const int kRaceKind = 0x1964;
const int kRaceParameters = 0x1968;
const int kOnlineRace = 2;
const int kViewStride = 0x70;
const int kViewMovers = 0x44;
const int kSimRunning = 3;
const int kFreeRoamTrack = 2000;
void *const kRaceOverMessageHandle = reinterpret_cast<void *>(0x565040);

const int kProfileTag = 0x34;
const int kProfileRaceMap = 0x3c;
const int kProfileFreeRoamMap = 0x3d;
const int kProfilePlayer = 0x80;
const int kProfilePlayerStride = 0x14;
const int kPlayerHud = 0x00;
const int kPlayerDragHud = 0x01;
const int kPlayerRaceInfo = 0x05;
const int kPlayerUnits = 0x10;
const int kMapHidden = 2;
const int kUnitsNone = 4;

template <typename T> T &At(const void *base, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(base) + offset));
}

template <typename R> R Virtual(void *object, int slot)
{
    using Fn = R(__thiscall *)(void *);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[slot / 4])(object);
}

bool OnlineRace()
{
    return NFS_ORIGINAL(0x735620, bool(__thiscall *)(void *))(reinterpret_cast<void *>(0x9b0fa0));
}

bool TollboothRace()
{
    return NFS_ORIGINAL(0x50e1b0, bool(__cdecl *)())();
}

bool GameLoading()
{
    return NFS_ORIGINAL(0x50de50, bool(__thiscall *)(void *))(reinterpret_cast<void *>(0x925e70));
}

bool PursuitRace(const GRaceParameters *parameters)
{
    return parameters && parameters->GetIsPursuitRace();
}

bool CameraFree(IPlayer *player)
{
    const int view = Virtual<int>(player, 0x2c);
    char *movers = reinterpret_cast<char *>(0x9195e0) + view * kViewStride + kViewMovers;
    char *node = *reinterpret_cast<char **>(movers);
    if (node == movers || !node)
        return false;
    void *mover = node - 4;
    if (At<int>(mover, 0xc) != 1)
        return false;
    return Virtual<short>(mover, 0x30) == 0;
}

bool HudAllowed(FEngHud *hud, IPlayer *player)
{
    if (!CameraFree(player))
        return false;
    const char *race = reinterpret_cast<const char *>(GRaceStatus::fObj);
    if (race && At<int>(race, kRaceActive) == 1 && FEngHud::bIsRestartingRace)
        return false;
    if (FEManager::mPauseRequest > 0 || Sim::GetState() != kSimRunning)
        return false;
    cFEng *fe = cFEng::mInstance;
    if (fe->IsPackagePushed("Pause_Main.fng") || fe->IsPackagePushed("MC_Main.fng") ||
        fe->IsPackagePushed("PostRace_Results.fng"))
        return false;
    if (*reinterpret_cast<const bool *>(0x91ca58))
        return false;
    if (!TheHudResourceManager.AreResourcesLoaded(At<ePlayerHudType>(hud, kHudType)))
        return false;
    if (*reinterpret_cast<void *const *>(0x9885c8))
        return false;
    if (At<char>(hud, kHudProfileTag) != At<char>(FEDatabase->CurrentUserProfile, kProfileTag))
        return false;
    if (*reinterpret_cast<const int *>(0x8ee348) > 0)
        return false;
    return !GameLoading();
}
} // namespace

unsigned long long __fastcall DetermineHudFeatures(FEngHud *hud, void *, IPlayer *player)
{
    if (!HudAllowed(hud, player))
        return 0;
    const char *profile = reinterpret_cast<const char *>(FEDatabase->CurrentUserProfile);
    const char *race = reinterpret_cast<const char *>(GRaceStatus::fObj);
    const GRaceParameters *parameters = At<const GRaceParameters *>(race, kRaceParameters);
    unsigned int low = 0;
    unsigned int high = 0;

    const bool freeRoamTrack = At<short>(TrackInfo::LoadedTrackInfo, 0x8a) == kFreeRoamTrack;
    const char map = At<char>(profile, parameters ? kProfileRaceMap : kProfileFreeRoamMap);
    if (map != kMapHidden && freeRoamTrack)
        low = OnlineRace() ? 0x10000 : 0x14000;

    const char *settings = profile + kProfilePlayer + At<int>(hud, kPlayerIndex) * kProfilePlayerStride;
    if (At<char>(settings, GRaceStatus::IsDragRace() ? kPlayerDragHud : kPlayerHud))
        low |= 0x8000000;
    if (At<char>(settings, kPlayerHud))
    {
        low |= 0x2;
        if (At<char>(hud, kHudShowsTurbo))
            low |= 0x20000;
        if (GRaceStatus::IsDragRace())
            low |= 0x40;
        low |= 0x800;
        if (!OnlineRace())
            low |= 0x40000;
    }

    const bool pursuit = PursuitRace(parameters);
    race = reinterpret_cast<const char *>(GRaceStatus::fObj);
    if (At<int>(race, kRaceActive) == 1 && !pursuit)
    {
        settings = profile + kProfilePlayer + At<int>(hud, kPlayerIndex) * kProfilePlayerStride;
        if (At<char>(settings, kPlayerRaceInfo))
            low |= 0x8;
        if (At<char>(settings, kPlayerDragHud))
            low |= 0x4000000;
    }
    else
    {
        low |= 0x100000;
        high |= 0x4;
    }
    settings = profile + kProfilePlayer + At<int>(hud, kPlayerIndex) * kProfilePlayerStride;
    if (At<char>(settings, kPlayerUnits) != kUnitsNone)
        low |= 0x10;
    if (TollboothRace())
        low |= 0x2000000;

    const char *state = Virtual<const char *>(player, 0x14);
    if (state[3])
    {
        race = reinterpret_cast<const char *>(GRaceStatus::fObj);
        if (At<int>(race, kRaceKind) == kOnlineRace || PursuitRace(At<const GRaceParameters *>(race, kRaceParameters)))
            low |= 0x1000;
    }
    if (At<int>(GRaceStatus::fObj, kRaceActive) == 1)
        low |= 0x20;

    void *list = At<void *>(hud, 4);
    void *message = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(list, kRaceOverMessageHandle);
    if (message && Virtual<bool>(message, 0xc))
        low |= 0x4;

    low |= 0x31c80400;
    high |= 0x3;
    if (!OnlineRace())
        low |= 0x200000;
    return static_cast<unsigned long long>(high) << 32 | low;
}

void __fastcall SetHudFeatures(FEngHud *hud, void *, unsigned long long features)
{
    char *p = reinterpret_cast<char *>(hud);
    unsigned long long &current = *reinterpret_cast<unsigned long long *>(p + kFeatures);
    const unsigned long long changed = features ^ current;
    for (const Element &e : kElements)
    {
        HudElement *element = *reinterpret_cast<HudElement **>(p + e.offset);
        if (element && (changed & e.features))
            element->Toggle(features);
    }
    current = features;
}

int __fastcall IsHudVisible(FEngHud *hud)
{
    return *reinterpret_cast<const unsigned long long *>(reinterpret_cast<const char *>(hud) + kFeatures) != 0 ? 1 : 0;
}

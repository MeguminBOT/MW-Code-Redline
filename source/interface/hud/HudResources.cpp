/** The HUD's resources (original: zFe2 HudResourceManager). */
#include "game/gen/api.h"
#include "gameplay/progression/parts.h"
#include "legacy/link.h"

namespace
{
const int kRaceKind = 0x1964;
const int kRaceParameters = 0x1968;
const int kOnlineRace = 2;
const int kProfileCarDB = 0x414;
const int kProfileOnlineCar = 0xa8;
const int kCarRecords = 200;
const int kCarRecordSize = 0x14;
const int kCarRecordCustomization = 0x10;
const int kCustomizationRecords = 0xfa0;
const int kCustomizationRecordSize = 0x198;
const unsigned char kLastCustomization = 0x4b;
const int kPlayerCars = 0x2c;
const int kPlayerStride = 0x18;
const int kSlotHudColour = 0x84;
const int kState = 4;
const int kFile = 8;
const int kStateLoading = 1;
const int kStateLoaded = 2;
const int kStateUnloading = 3;
const int kCustomTextures = 5;
const int kRaceActive = 0x1960;
const int kProfileBin = 0xb0;
const int kAllocate = 0x2000;
const int kAllocateDrag = 0x2047;
const int kResourceType = 3;
const unsigned int kTachNeedle = 0xcdfce1b0;
const int kTachLines = 0x309878bc;

enum Hud
{
    kHudDrag = 2,
    kHudPlayer1 = 3,
    kHudPlayer2 = 4,
    kHudDragPlayer1 = 5,
    kHudDragPlayer2 = 6,
};

template <typename T> T &At(const void *base, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(base) + offset));
}

const unsigned char *PlayerCarRecord(const unsigned char *db)
{
    const unsigned char *race = reinterpret_cast<const unsigned char *>(GRaceStatus::fObj);
    const unsigned char *profile = reinterpret_cast<const unsigned char *>(FEDatabase->CurrentUserProfile);
    if (At<int>(race, kRaceKind) == kOnlineRace)
    {
        const unsigned int handle = At<unsigned int>(profile, kProfileOnlineCar);
        for (int i = 0; i < kCarRecords; ++i)
            if (At<unsigned int>(db, i * kCarRecordSize) == handle)
                return db + i * kCarRecordSize;
        return nullptr;
    }
    const GRaceParameters *parameters = At<const GRaceParameters *>(race, kRaceParameters);
    if (!parameters || parameters->GetIsPursuitRace())
        return nullptr;
    const unsigned int handle = At<unsigned int>(FEDatabase, kPlayerCars + FEDatabase->RaceMode * kPlayerStride);
    return reinterpret_cast<const unsigned char *>(
        reinterpret_cast<FEPlayerCarDB *>(const_cast<unsigned char *>(db))->GetCarRecordByHandle(handle));
}

/** mPackageName is declared const in the generated header; the compiler drops writes to it. */
const char *volatile &PackageName()
{
    return *reinterpret_cast<const char *volatile *>(0x91cf24);
}

const char *HudTexturePack(int hud)
{
    switch (hud)
    {
    case kHudDrag:
        return HudDragTexturePackFilename;
    case kHudPlayer1:
    case kHudPlayer2:
        return HudSplitScreenTexturePackFilename;
    case kHudDragPlayer1:
    case kHudDragPlayer2:
        return HudDragSplitScreenTexturePackFilename;
    default:
        return HudSingleRaceTexturePackFilename;
    }
}
} // namespace

CarPart *HudResourceManager::GetCarPart(ePlayerHudType, CAR_SLOT_ID slot)
{
    const unsigned char *db = reinterpret_cast<const unsigned char *>(FEDatabase->CurrentUserProfile) + kProfileCarDB;
    const unsigned char *record = PlayerCarRecord(db);
    if (!record)
        return nullptr;
    const unsigned char customization = record[kCarRecordCustomization];
    if (customization > kLastCustomization)
        return nullptr;
    reinterpret_cast<FECarRecord *>(const_cast<unsigned char *>(record))->GetType();
    return nfs::parts::Installed(reinterpret_cast<const FECustomizationRecord *>(
                                     db + kCustomizationRecords + customization * kCustomizationRecordSize),
                                 slot);
}

int HudResourceManager::GetCustomHudColour(ePlayerHudType hud, CAR_SLOT_ID slot)
{
    if (hud != 1)
        return 0;
    CarPart *part = GetCarPart(static_cast<ePlayerHudType>(1), slot);
    if (!part)
        return 0;
    const unsigned char red = static_cast<unsigned char>(part->GetAppliedAttributeIParam(bStringHash("RED"), 0));
    const unsigned char green = static_cast<unsigned char>(part->GetAppliedAttributeIParam(bStringHash("GREEN"), 0));
    const unsigned char blue = static_cast<unsigned char>(part->GetAppliedAttributeIParam(bStringHash("BLUE"), 0));
    return static_cast<int>(0xff000000u | red << 16 | green << 8 | blue);
}

bool HudResourceManager::GetCustomHudTexPackFilename(ePlayerHudType hud, char *const name)
{
    mCustIndex = 0;
    if (hud != 1)
    {
        bSPrintf(name, "");
        return false;
    }
    if (CarPart *part = GetCarPart(static_cast<ePlayerHudType>(1), static_cast<CAR_SLOT_ID>(kSlotHudColour)))
        mCustIndex = part->GetAppliedAttributeIParam(FEngHashString("HUDINDEX"), 0);
    bSPrintf(name, "GLOBAL\\HUDS_Custom_%2.2d.bin", mCustIndex);
    return true;
}

const char *HudResourceManager::GetHudFengName(ePlayerHudType hud)
{
    switch (hud)
    {
    case kHudDrag:
        return "HUD_Drag.fng";
    case kHudPlayer1:
        return "HUD_Player1.fng";
    case kHudPlayer2:
        return "HUD_Player2.fng";
    case kHudDragPlayer1:
        return "HUD_Drag_Player1.fng";
    case kHudDragPlayer2:
        return "HUD_Drag_Player2.fng";
    default:
        return "HUD_SingleRace.fng";
    }
}

bool HudResourceManager::AreResourcesLoaded(ePlayerHudType hud)
{
    if (At<int>(this, kState) != kStateLoaded)
        return false;
    const int loading = LoadingResourcesForHudType;
    if (hud == kHudPlayer2)
        return loading == kHudPlayer1;
    if (hud == kHudDragPlayer2)
        return loading == kHudDragPlayer1;
    return loading == hud;
}

void HudResourceManager::LoadingCompleteCallback()
{
    using TextureLoaded = void(__cdecl *)(unsigned int);
    const TextureLoaded loaded = static_cast<TextureLoaded>(&HudResourceManager::LoadingCompleteCallbackBridge);
    const unsigned int self = reinterpret_cast<unsigned int>(this);
    char *packName = reinterpret_cast<char *>(&mCustHudTexPackName);
    const int phase = ++mPhase;
    if (phase == 1)
    {
        char name[0x20];
        char file[0x40];
        bSPrintf(file, "");
        bSPrintf(name, "");
        if (!ChooseMinimapTextureName(LoadingResourcesForHudType, name, sizeof(name), file, sizeof(file)))
        {
            LoadingCompleteCallback();
            return;
        }
        bSNPrintf(reinterpret_cast<char *>(gChoppedMiniMapManager) + 8, 0x40, name);
        pMiniMapTexture = LoadResourceFile(file, static_cast<ResourceFileType>(4), 0, nullptr, nullptr, 0, 0);
        unsigned int hash = bStringHash(name);
        eLoadStreamingTexture(&hash, 1, loaded, self, 0);
    }
    else if (phase == 2)
    {
        float rotation;
        const bool custom = GetCustomHudTexPackFilename(LoadingResourcesForHudType, packName);
        ChooseLoadableTextures(LoadingResourcesForHudType, mTachLinesHash, rotation);
        FEngSetMultiImageRot(reinterpret_cast<FEMultiImage *>(FEngFindObject(PackageName(), kTachNeedle)), rotation);
        if (custom)
        {
            eLoadStreamingTexturePack(packName,
                                      reinterpret_cast<void(__cdecl *)(void *)>(static_cast<TextureLoaded>(
                                          &HudResourceManager::LoadedCustomHudTexturePackCallbackBridge)),
                                      this, 0);
            return;
        }
        FEngSetTextureHash(FEngFindImage(PackageName(), kTachLines), mTachLinesHash);
        unsigned int hash = mTachLinesHash;
        eLoadStreamingTexture(&hash, 1, loaded, self, 0);
    }
    else if (phase == 3)
    {
        At<int>(&TheHudResourceManager, kState) = kStateLoaded;
        cFEng::mInstance->MakeLoadedPackagesDirty();
        SetSoundControlState(false, static_cast<eSNDCTLSTATE>(0xc), "HUDLoaded");
    }
}

void HudResourceManager::UnloadRequiredResources(ePlayerHudType)
{
    unsigned int *const customTextures = reinterpret_cast<unsigned int *>(0x91ccfc);
    char *packName = reinterpret_cast<char *>(&mCustHudTexPackName);
    eWaitForStreamingTexturePackLoading(nullptr);
    At<int>(this, kState) = kStateUnloading;
    eWaitUntilRenderingDone();
    cFEng::mInstance->MakeLoadedPackagesDirty();
    eUnloadAllStreamingTextures(HudDragTexturePackFilename);
    eUnloadAllStreamingTextures(HudSingleRaceTexturePackFilename);
    eUnloadAllStreamingTextures(HudSplitScreenTexturePackFilename);
    eUnloadAllStreamingTextures(HudDragSplitScreenTexturePackFilename);
    if (ResourceFile *file = At<ResourceFile *>(this, kFile))
    {
        UnloadResourceFile(file);
        At<ResourceFile *>(this, kFile) = nullptr;
    }
    if (pMiniMapTexture)
    {
        UnloadResourceFile(pMiniMapTexture);
        pMiniMapTexture = nullptr;
    }
    if (gChoppedMiniMapManager)
        gChoppedMiniMapManager->UncompressMaps(nullptr, 0);
    if (bStrCmp(packName, "") == 0)
    {
        if (mTachLinesHash)
        {
            unsigned int hash = mTachLinesHash;
            eUnloadStreamingTexture(&hash, 1);
            mTachLinesHash = 0;
        }
    }
    else
    {
        eUnloadStreamingTexture(customTextures, kCustomTextures);
        for (int i = 0; i < kCustomTextures; ++i)
            customTextures[i] = 0;
        eUnloadStreamingTexturePack(packName);
        bSPrintf(packName, "");
    }
    eWaitUntilRenderingDone();
    At<ResourceFile *>(this, kFile) = nullptr;
    At<int>(this, kState) = 0;
    PackageName() = nullptr;
}

bool HudResourceManager::ChooseMinimapTextureName(ePlayerHudType hud, char *name, unsigned int nameSize, char *file,
                                                  unsigned int fileSize)
{
    if (hud == kHudDrag)
        return false;
    const unsigned char *race = reinterpret_cast<const unsigned char *>(GRaceStatus::fObj);
    if (race)
    {
        const int size = static_cast<int>(nameSize);
        if (!At<int>(race, kRaceActive))
        {
            const unsigned char bin = At<unsigned char>(FEDatabase->CurrentUserProfile, kProfileBin);
            bSNPrintf(name, size, bin > 12 ? "MINI_MAP_UNLOCK_1" : bin > 8 ? "MINI_MAP_UNLOCK_2" : "MINI_MAP");
        }
        else if (const GRaceParameters *parameters = At<const GRaceParameters *>(race, kRaceParameters))
        {
            if (!parameters->GetIsPursuitRace())
                bSNPrintf(name, size, "MINI_MAP_%s", parameters->GetEventID());
            else if (parameters->GetRegion() == 0)
                bSNPrintf(name, size, "MINI_MAP_UNLOCK_1");
            else if (parameters->GetRegion() == 1)
                bSNPrintf(name, size, "MINI_MAP_UNLOCK_2");
            else
                bSNPrintf(name, size, "MINI_MAP");
        }
        for (int i = 0; i < size; ++i)
            if (name[i] == '.')
                name[i] = '_';
        bToUpper(name);
        bSNPrintf(file, static_cast<int>(fileSize), "TRACKS\\L2RA\\%s.BIN", name);
    }
    return bFileExists(file) != 0;
}

float FEngHud::ChooseMaxRpmTextureNumber(float rpm)
{
    if (rpm < 7000.0f)
        return 7000.0f;
    if (rpm < 8000.0f)
        return 8000.0f;
    if (rpm < 9000.0f)
        return 9000.0f;
    return 10000.0f;
}

void HudResourceManager::LoadingCompleteCallbackBridge(unsigned int manager)
{
    reinterpret_cast<HudResourceManager *>(manager)->LoadingCompleteCallback();
}

void HudResourceManager::LoadedCustomHudTexturesCallbackBridge(unsigned int manager)
{
    reinterpret_cast<HudResourceManager *>(manager)->LoadedCustomHudTexturesCallback();
}

void HudResourceManager::LoadedCustomHudTexturePackCallbackBridge(unsigned int manager)
{
    reinterpret_cast<HudResourceManager *>(manager)->LoadedCustomHudTexturePackCallback();
}

void HudResourceManager::LoadRequiredResources(ePlayerHudType hud, const char *package)
{
    mPhase = 0;
    const char *pack = HudTexturePack(hud);
    eWaitUntilRenderingDone();
    int allocation = kAllocate;
    if (hud == kHudDrag)
    {
        allocation = kAllocateDrag;
        TheTrackStreamer.MakeSpaceInPool(bFileSize(pack), true);
    }
    ResourceFile *file = NFS_ORIGINAL(0x65fd30, ResourceFile * (__cdecl *)(const char *, int, int, int, int))(
        pack, kResourceType, 0, 0, 0);
    At<ResourceFile *>(this, kFile) = file;
    file->SetAllocationParams(allocation, pack);
    file->BeginLoading(reinterpret_cast<void(__cdecl *)(void *)>(
                           static_cast<void(__cdecl *)(int)>(&HudResourceManager::LoadingCompleteCallbackBridge)),
                       this);
    At<int>(this, kState) = kStateLoading;
    LoadingResourcesForHudType = hud;
    PackageName() = package;
}

/**
 * The front end's owner and its pad ports (original: zFe FEManager::Get 0x516e10, GetGarageType 0x516e20,
 * SetGarageType 0x516e30, GetGarageNameFromType 0x516e40, GetGaragePrefixFromType 0x516e90, StopFE 0x516f30,
 * ShouldPauseSimulation 0x525b20, ClearControllerError 0x50ea10, Update 0x5642c0; cFEngJoyInput::JoyDisable 0x514a10, IsJoyPluggedIn
 * 0x514a60, JoyEnable 0x514a70, IsJoyEnabled 0x514ae0, CheckUnplugged 0x559280).
 *
 * FEManager (0x30): +0 suppresses the controller error, +1 allows it outside races, +2 a controller error wanted per
 * port (8), +0x18 the garage type, +0x1c the one before, +0x28 the frames until the chyron. cFEngJoyInput: an action
 * queue per port (one: port 0; port 4 means all of them, which is port 0). A queue's +0x281: its pad is needed.
 * The database (0x91cf90): +0 the mode (2 split screen), +0xc the first player's port, +0xd the second's, +0x12c flags
 * (4 split screen). Ports 4 and -1 for the queue checks read port 0, as the flags do.
 *
 * Each frame: the memory card's tick, the wheels' forces stopped outside racing, the pads read, the controller error
 * shown for the first port that wants it (every device polled first), the engine updated (milliseconds), then the
 * package manager's tick (its error tick while the engine shows an error), the garage's car loaders, the movie, and the
 * chyron when asked for or when its countdown ends.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const int kAllPorts = 4;
const int kNoPort = -1;
const int kRacing = 6;
const int kErrorPorts = 8;
const char kControllerUnplugged[] = "ControllerUnplugged.fng";
FEManager *const *const kManager = reinterpret_cast<FEManager *const *>(0x91cae0);
const int *const kPauseRequests = reinterpret_cast<const int *>(0x91cae4);
const int *const kGameFlow = reinterpret_cast<const int *>(0x925e90);
const int *const kInis = reinterpret_cast<const int *>(0x9885c8);
const int *const kMoviePlayer = reinterpret_cast<const int *>(0x91cb10);
char *const *const kFEDatabase = reinterpret_cast<char *const *>(0x91cf90);

using QueueTest = bool(__thiscall *)(void *);
using QueueCall = void(__thiscall *)(void *);
using QueueEnable = void(__thiscall *)(void *, bool);
const QueueTest kIsEnabled = reinterpret_cast<QueueTest>(0x626dd0);
const QueueTest kIsConnected = reinterpret_cast<QueueTest>(0x626d70);
const QueueEnable kEnable = reinterpret_cast<QueueEnable>(0x644060);
const QueueCall kFlush = reinterpret_cast<QueueCall>(0x642d70);
const float *const kFrameSeconds = reinterpret_cast<const float *>(0x9259bc);
const float *const kWorldTime = reinterpret_cast<const float *>(0x925970);
MemoryCard *const *const kMemoryCard = reinterpret_cast<MemoryCard *const *>(0x91cb20);
const int *const kInScreenConstructor = reinterpret_cast<const int *>(0x91d218);
const int kGarage = 3;
const int kSimRunning = 3;
using SimTest = bool(__cdecl *)();
using SimState = int(__cdecl *)();
const SimTest kSimExists = reinterpret_cast<SimTest>(0x6e8d10);
const SimState kSimState = reinterpret_cast<SimState>(0x6e8d20);
using UserMode = int(__cdecl *)();
const UserMode kGetUserMode = reinterpret_cast<UserMode>(0x6e8df0);

void *Queue(const cFEngJoyInput *j, int port)
{
    return reinterpret_cast<void *const *>(j)[port == kAllPorts ? 0 : port];
}
} // namespace

bool __fastcall CheckUnplugged(cFEngJoyInput *j, void *);
void __fastcall HandleJoy(void *self, void *);
NFS_REPLACE(0x559280, CheckUnplugged)

FEManager *__cdecl FEManager::Get()
{
    return *kManager;
}

eGarageType FEManager::GetGarageType()
{
    return At<eGarageType>(this, 0x18);
}

void FEManager::SetGarageType(eGarageType type)
{
    At<eGarageType>(this, 0x1c) = At<eGarageType>(this, 0x18);
    At<eGarageType>(this, 0x18) = type;
}

char const *FEManager::GetGarageNameFromType()
{
    switch (At<int>(this, 0x18))
    {
    case 1:
        return "FRONTEND\\PLATFORMS\\PLATFORMCRIB.BIN";
    case 2:
        return "FRONTEND\\PLATFORMS\\CAREER_SAFEHOUSE.BIN";
    case 3:
        return "FRONTEND\\PLATFORMS\\CUSTOMIZATION_SHOP.BIN";
    case 4:
        return "FRONTEND\\PLATFORMS\\CUSTOMIZATION_SHOP_BACKROOM.BIN";
    case 5:
        return "FRONTEND\\PLATFORMS\\CAR_LOT.BIN";
    default:
        return "";
    }
}

char const *FEManager::GetGaragePrefixFromType(eGarageType type)
{
    switch (static_cast<int>(type))
    {
    case 1:
        return "QRACE";
    case 2:
        return "CAREER";
    case 3:
    case 4:
        return "CSHOP";
    case 5:
        return "CARLOT";
    default:
        return "";
    }
}

void FEManager::StopFE()
{
    void *queue = Queue(cFEngJoyInput::mInstance, 0);
    kEnable(queue, false);
    kFlush(queue);
    FEPackageManager::Get()->CloseAllPackages(0);
    BootFlowManager::Destroy();
    At<int>(this, 0x28) = 0;
}

bool __cdecl FEManager::ShouldPauseSimulation(bool menus)
{
    const char *m = reinterpret_cast<const char *>(*kManager);
    if (!m[0])
        for (int i = 0; i < kErrorPorts; ++i)
            if (m[2 + i])
            {
                if (menus && !*kInis && !*kMoviePlayer)
                    return true;
                break;
            }
    return *kPauseRequests != 0;
}

void FEManager::ClearControllerError(int port)
{
    if (port == kNoPort)
        return;
    if (port == kAllPorts)
    {
        for (int i = 0; i < kErrorPorts; ++i)
            At<char>(this, 2 + i) = 0;
        return;
    }
    At<char>(this, 2 + port) = 0;
}

void cFEngJoyInput::JoyDisable(JoystickPort port, bool flush)
{
    void *queue = Queue(this, static_cast<int>(port));
    kEnable(queue, false);
    if (flush)
        kFlush(queue);
}

bool cFEngJoyInput::IsJoyPluggedIn(JoystickPort port)
{
    return kIsConnected(reinterpret_cast<void *const *>(this)[static_cast<int>(port)]);
}

void cFEngJoyInput::JoyEnable(JoystickPort port, bool flush)
{
    if (static_cast<int>(port) == kNoPort)
        return;
    void *queue = Queue(this, static_cast<int>(port));
    if (kIsEnabled(queue))
        return;
    kEnable(queue, true);
    if (flush)
        kFlush(queue);
}

bool cFEngJoyInput::IsJoyEnabled(JoystickPort port)
{
    return kIsEnabled(Queue(this, static_cast<int>(port)));
}

bool __fastcall CheckUnplugged(cFEngJoyInput *j, void *)
{
    FEManager *manager = *kManager;
    if (*kGameFlow != kRacing && !At<char>(manager, 1))
    {
        At<unsigned char>(Queue(j, 0), 0x281) = 0;
        return false;
    }
    const char *db = *kFEDatabase;
    const bool splitScreen = (At<unsigned int>(db, 0x12c) & 4) && db[0] == 2;
    const bool twoPlayers = kGetUserMode() == 1 || splitScreen;
    const int first = static_cast<signed char>(db[0xc]);
    if (first == kNoPort)
        return false;
    const int second = twoPlayers ? static_cast<signed char>(db[0xd]) : kNoPort;
    At<unsigned char>(Queue(j, first), 0x281) = 1;
    if (second != kNoPort)
        At<unsigned char>(Queue(j, second), 0x281) = 1;
    cFEng *fe = cFEng::mInstance;
    bool unplugged = false;
    if (!kIsConnected(Queue(j, first)))
    {
        manager->WantControllerError(first);
        unplugged = true;
    }
    else if (!twoPlayers && !fe->IsPackagePushed(kControllerUnplugged))
        manager->ClearControllerError(first);
    if (second != kNoPort)
    {
        if (!kIsConnected(Queue(j, second)))
        {
            manager->WantControllerError(second);
            return true;
        }
        if (!twoPlayers && !fe->IsPackagePushed(kControllerUnplugged))
            manager->ClearControllerError(second);
    }
    return unplugged;
}

void FEManager::Update()
{
    char *self = reinterpret_cast<char *>(this);
    if (MemoryCard *card = *kMemoryCard)
        card->Tick(static_cast<int>(*kFrameSeconds * 1000.0f));
    if (!kSimExists() || kSimState() != kSimRunning || *kInis)
        SteeringWheels_StopAllForces();
    if (cFEngJoyInput::mInstance)
        HandleJoy(cFEngJoyInput::mInstance, nullptr);
    for (int port = 0; port < kErrorPorts; ++port)
    {
        if (!self[2 + port])
            continue;
        if (((!*kInis && !*kMoviePlayer) || self[1]) && !self[0])
        {
            for (int i = 0, n = At<int>(&IOModule::GetIOModule(), 4); i < n; ++i)
                if (void *device = At<void *>(&IOModule::GetIOModule(), 8 + i * 4))
                    reinterpret_cast<void(__thiscall *)(void *)>((*static_cast<void ***>(device))[0x1c / 4])(device);
            FEPackage *shown;
            if (*kInScreenConstructor > 0)
                shown = FEPackageManager::Get()->FindPackage(kControllerUnplugged);
            else
                shown = reinterpret_cast<FEPackage *>(
                    reinterpret_cast<FEList *>(reinterpret_cast<char *>(*reinterpret_cast<FEngine **>(cFEng::mInstance)) +
                                               0xd8)
                        ->FindNode(kControllerUnplugged));
            if (!shown)
                cFEng::mInstance->PushErrorPackage(kControllerUnplugged, port,
                                                   static_cast<unsigned int>(FEngMapJoyportToJoyParam(port)));
        }
        break;
    }
    const unsigned int racing = (*kGameFlow == kRacing && *kWorldTime > 0.0f) ? 1 : 0;
    FEngine *engine = *reinterpret_cast<FEngine **>(cFEng::mInstance);
    engine->Update(static_cast<int>(*kFrameSeconds * 1200.0f), racing);
    engine = *reinterpret_cast<FEngine **>(cFEng::mInstance);
    if (engine && At<char>(engine, 0x524c))
    {
        FEPackageManager::Get()->ErrorTick();
        return;
    }
    FEPackageManager::Get()->Tick();
    if (*kGameFlow == kGarage)
        UpdateGarageCarLoaders();
    if (gMoviePlayer)
        gMoviePlayer->Update();
    if (SummonChyronNow)
    {
        SummonChyron(nullptr, nullptr, nullptr);
        SummonChyronNow = 0;
        return;
    }
    int &countdown = At<int>(self, 0x28);
    if (countdown >= 0 && --countdown == 0)
        SummonChyron(nullptr, nullptr, nullptr);
}

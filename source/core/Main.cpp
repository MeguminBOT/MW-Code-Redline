#include "game/gen/api.h"
#include "gameplay/vehicles/cars.h"
#include "dev/dev.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "core/log.h"
#include "core/runtime.h"
#include "dev/test.h"
#include "dev/proving.h"
#include "dev/testtrack.h"
#include "core/timing.h"
#include "engine/rendering/vulkan/renderer.h"

extern "C" void __cdecl _exit(int code);

NFS_REPLACE(0x666590, main)

static bool HasArgument(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i)
        if (bStrICmp(argv[i], flag) == 0)
            return true;
    return false;
}

static const char *ArgumentValue(int argc, char **argv, const char *flag)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (bStrICmp(argv[i], flag) == 0)
            return argv[i + 1];
    return nullptr;
}

/** SkipFERaceID is a char[16] (SkipFEPlayerPerformance follows it at 0x926078). */
static const int kSkipFERaceIDSize = 16;
/** SkipFEPovType1, the debug race's camera. */
static const unsigned int kSkipFEPovType1 = 0x8f86c4;
static const float kMaxFrameMs = 32000.0f;
static const float kTickScale = 65536.0f;

int main(int argc, char **argv)
{
    nfs::RunStaticInitializers();
    nfs::InstallCrashLog();
    nfs::display::RedirectHeadlessSaves();
    if (HasArgument(argc, argv, "-nfstest"))
        _exit(nfs::test::RunAll());

    if (IsTheGameAlreadyRunning("speed.exe", 1))
        _exit(0);
    nfs::MuteAudioIfAsked();

    // Development switch: boot straight into the debug race instead of the front end.
    if (HasArgument(argc, argv, "-skipfe"))
        SkipFE = 1;
    // -skipfecar NAME: the debug race's car (a pvehicle collection, bmwm3gtr by default).
    if (const char *car = ArgumentValue(argc, argv, "-skipfecar"))
    {
        *const_cast<const char *volatile *>(&SkipFEPlayerCar) = car;
        nfs::Log("debug race car: %s", SkipFEPlayerCar);
    }
    // -skipferace ID: the debug race is that event (its id, 1.2.2; QuickGame::HandleSkipFEOptions 0x6ea040 reads it).
    if (const char *race = ArgumentValue(argc, argv, "-skipferace"))
    {
        char *id = reinterpret_cast<char *>(&SkipFERaceID);
        int i = 0;
        for (; race[i] && i < kSkipFERaceIDSize - 1; ++i)
            id[i] = race[i];
        id[i] = 0;
        nfs::Log("debug race: event %s", race);
    }
    // -skipfepov N: the debug race's camera (POVTypes: 0 bumper, 1 hood, 2 near, 3 far).
    if (const char *pov = ArgumentValue(argc, argv, "-skipfepov"))
    {
        *reinterpret_cast<int *>(kSkipFEPovType1) = *pov - '0';
        nfs::Log("debug race camera: %d", *pov - '0');
    }
    // -skipfeai N, -skipfecops N: opponents in the debug race, and cops at most (0: as the game has it).
    if (const char *racers = ArgumentValue(argc, argv, "-skipfeai"))
        SkipFENumAICars = *racers - '0';
    if (const char *cops = ArgumentValue(argc, argv, "-skipfecops"))
    {
        SkipFEMaxCops = *cops - '0';
        SkipFEDisableCops = 0;
    }
    // -skipfetraffic PERCENT: the debug race's traffic density (none by default).
    if (const char *traffic = ArgumentValue(argc, argv, "-skipfetraffic"))
    {
        int percent = 0;
        for (const char *p = traffic; *p >= '0' && *p <= '9'; ++p)
            percent = percent * 10 + (*p - '0');
        SkipFETrafficDensity = percent;
        nfs::Log("debug race traffic: %d%%", percent);
    }

    nfs::display::Initialize();
    nfs::timing::Initialize();
    nfs::cars::ConfigureLoaderPool();
    InitializeEverything(argc, argv);
    if (SkipFE && (nfs::testtrack::Enabled() || nfs::proving::Enabled()))
    {
        // The test track's race is the car alone.
        SkipFENumAICars = 0;
        SkipFEDisableCops = 1;
        SkipFEDisableTraffic = 1;
    }
    if (SkipFE)
        RaceStarter::StartSkipFERace();
    else
        TheGameFlowManager.LoadFrontend();

    frames_elapsed = 1;
    loop_ticker = bGetTicker();
    Scheduler::fgScheduler->Synchronize(RealTimer);

    while (!ExitTheGameFlag)
    {
        unsigned int now = bGetTicker();
        float elapsed = bGetTickerDifference(loop_ticker, now);
        if (now != loop_ticker && elapsed <= 0.0f)
        {
            loop_ticker = now;
            continue;
        }
        // Not in the original, which only skipped frames under 0.25 ms and left the 60 per second cap to the Scheduler.
        const float min_frame_ms = nfs::timing::MinFrameMs();
        if (elapsed < min_frame_ms)
        {
            nfs::timing::Wait(min_frame_ms - elapsed);
            continue;
        }

        loop_ticker = now;
        if (elapsed > kMaxFrameMs)
            elapsed = kMaxFrameMs;
        MainLoop(static_cast<int>(elapsed * kTickScale));
        if (twkDumpProfileMarks)
            twkDumpProfileMarks = false;
    }

    nfs::dev::Shutdown();
    if (nfs::vk::g_renderer)
        nfs::vk::g_renderer->FlushPipelineCache();
    DeInitializeEverything();
    delete Hermes::System::mObj;
    Hermes::System::mObj = nullptr;
    _exit(0);
    return 0;
}

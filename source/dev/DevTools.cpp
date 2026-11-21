/**
 * Development tools (dev/dev.h, docs/DEV.md), on with -dev. Not in the original game.
 *
 * Path recording (F5): every simulation step the player car's state is read where its behaviours take its controls
 * (PInput::GetControls 0x69cbb0, source/dev/TestTrack.cpp): the rigid body's position, orientation and velocities
 * (IRigidBody), the controls (InputControls), the gear (ITransmission), the engine speed and nitrous (IEngine) and the
 * speed (IVehicle). The records go to paths\ in the dev folder (nfs::dev::Folder), written a block at a time.
 *
 * Playback (Dev.PlayPath): the car goes to the recording's start (the pose warp, dev/pose.h), then every step its rigid
 * body takes the recorded state and its input the recorded controls, so it follows the recording exactly whatever the
 * physics would make of it, and the transmission is shifted to the recorded gear. Dev.PlayRate=N plays it N times as
 * fast (records skipped, velocities scaled): a 200 km/h drive through the world at 400 km/h, for streaming tests. The playback's frame times are
 * logged at its end (average, percentiles, worst) and written with the car's place to FILE.frames.csv.
 *
 * The file: a 128-byte header - "NFSPATH1", the version (1), the record size (88), the simulation step in seconds, the
 * record count (0 when the recording was cut off: readers count by the file's size), the car's name (32 bytes), the
 * clock's hour at the start (-1 with the clock off) and the date and time (20 bytes) - then the records, Record below.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "dev/dev.h"
#include "engine/rendering/display/display.h"
#include "core/log.h"
#include "dev/pose.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace nfs::dev
{
namespace
{
const int kGameFlowRacing = 6;
/** The player car's interfaces, by their _IHandle functions (UTL::COM::Object::_IList::Find). */
const unsigned int kVehicleHandle = 0x4040e0;
const unsigned int kRigidBodyHandle = 0x4039f0;
const unsigned int kInputHandle = 0x403f90;
const unsigned int kEngineHandle = 0x404020;
const unsigned int kTransmissionHandle = 0x404010;
/** Their methods (vtable offsets): IRigidBody (RigidBody), IEngine and ITransmission (EngineRacer), IVehicle
 * (PVehicle). */
const int kBodyPosition = 0x20;
const int kBodyLinearVelocity = 0x24;
const int kBodyAngularVelocity = 0x28;
const int kBodyOrientation = 0x44;
const int kBodySetPosition = 0x5c;
const int kBodySetLinearVelocity = 0x60;
const int kBodySetAngularVelocity = 0x64;
const int kBodySetOrientation = 0x74;
const int kEngineRpm = 0x04;
const int kEngineNitrous = 0x1c;
const int kEngineNitrousOn = 0x20;
const int kTransmissionGear = 0x04;
const int kTransmissionShift = 0x0c;
const int kTransmissionShifting = 0x10;
const int kTransmissionReversing = 0x14;
const int kVehicleName = 0x4c;
const int kVehicleSpeed = 0x90;
/** InputControls (PInput + 8). */
const int kSteering = 0x04;
const int kGas = 0x14;
const int kBrake = 0x18;
const int kHandBrake = 0x1c;
const int kAction = 0x20;
const int kNitrous = 0x21;

const char kMagic[8] = {'N', 'F', 'S', 'P', 'A', 'T', 'H', '1'};
const unsigned int kVersion = 1;
/** The simulation's step (source/core/FrameRate.cpp kStepRate). */
const float kStep = 1.0f / 60.0f;
/** Records are written a second at a time: a crash loses at most that. */
const int kBufferRecords = 60;
const char kFolder[] = "dev";
const char kPaths[] = "paths";

#pragma pack(push, 1)
struct Header
{
    char magic[8];
    unsigned int version;
    unsigned int recordSize;
    float step;
    unsigned int count;
    char car[32];
    float hour;
    char started[20];
    unsigned char reserved[48];
};

struct Record
{
    /** Simulation time, seconds. */
    float time;
    /** Simulation axes (Y up): the renderer's X is z, its Y -x, its Z y. */
    float position[3];
    /** Quaternion x, y, z, w. */
    float orientation[4];
    /** m/s and rad/s. */
    float velocity[3];
    float angular[3];
    /** -1 (left) to 1 (right); 0 to 1. */
    float steering;
    float gas;
    float brake;
    float handbrake;
    float rpm;
    /** m/s. */
    float speed;
    /** Nitrous left, 0 to 1. */
    float nitrous;
    /** 0 reverse, 1 neutral, 2 first... */
    signed char gear;
    /** Bit 0: the nitrous button; bit 1: the action button. */
    unsigned char buttons;
    /** Bit 0: nitrous on; bit 1: shifting; bit 2: reversing. */
    unsigned char flags;
    unsigned char pad;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 128, "path header");
static_assert(sizeof(Record) == 88, "path record");

struct Player
{
    void *simable;
    void *input;
    void *body;
    void *engine;
    void *transmission;
    void *vehicle;
};

struct Recorder
{
    /** F5 has recording on: each drive (between loading screens, or until the car changes) goes to a file of its
     * own. */
    bool armed;
    void *car;
    DWORD retryAt;
    /** Logged once when a drive's file has had no step for a while (the car's input not found). */
    bool warned;
    HANDLE file;
    Header header;
    Record buffer[kBufferRecords];
    int buffered;
    unsigned int count;
    char name[MAX_PATH];
    DWORD startTick;
};

struct Playback
{
    char path[MAX_PATH];
    bool tried;
    bool warped;
    bool playing;
    bool done;
    bool quit;
    Record *records;
    unsigned int count;
    float startTime;
    float lastStep;
    float rate;
    /** The frame times (ms) and the car's place (renderer axes) of each frame. */
    float (*frames)[4];
    int frameCount, frameCapacity;
    LARGE_INTEGER lastFrame;
};

struct State
{
    int enabled;
    bool key;
    Player player;
    float stepTime;
    bool stepDone;
    Recorder recorder;
    Playback play;
};

State g_state = {-1};

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}

/** A file next to the exe. */
void ExePath(char *out, const char *name)
{
    DWORD n = GetModuleFileNameA(nullptr, out, MAX_PATH);
    while (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/')
        --n;
    lstrcpynA(out + n, name, MAX_PATH - n);
}

/** The player car and its interfaces, found anew for every simulation step: the car object stays from one race to the
 * next while its behaviours (input, engine and gearbox, rigid body) are made anew (PVehicle::ReloadBehaviors), so a
 * pointer kept from an earlier step can point at a freed behaviour. */
void FindPlayer()
{
    Player &p = g_state.player;
    void *simable = nfs::pose::PlayerSimable();
    p.simable = simable;
    p.input = simable ? nfs::pose::PlayerInterface(kInputHandle) : nullptr;
    p.body = simable ? nfs::pose::PlayerInterface(kRigidBodyHandle) : nullptr;
    p.engine = simable ? nfs::pose::PlayerInterface(kEngineHandle) : nullptr;
    p.transmission = simable ? nfs::pose::PlayerInterface(kTransmissionHandle) : nullptr;
    p.vehicle = simable ? nfs::pose::PlayerInterface(kVehicleHandle) : nullptr;
}

/** "12.34" (wsprintf has no floats). */
void Hundredths(char *out, float value)
{
    int h = static_cast<int>(value * 100.0f + (value < 0.0f ? -0.5f : 0.5f));
    const char *sign = h < 0 ? "-" : "";
    if (h < 0)
        h = -h;
    wsprintfA(out, "%s%d.%02d", sign, h / 100, h % 100);
}

void Flush()
{
    Recorder &r = g_state.recorder;
    if (r.file && r.buffered)
    {
        DWORD written = 0;
        WriteFile(r.file, r.buffer, static_cast<DWORD>(sizeof(Record) * r.buffered), &written, nullptr);
    }
    r.buffered = 0;
}

/** Starts the drive's file; false (and tried again a second later) while the car is not ready. */
bool StartRecording()
{
    Recorder &r = g_state.recorder;
    if (GetTickCount() < r.retryAt)
        return false;
    FindPlayer();
    const Player &p = g_state.player;
    if (!p.input || !p.body)
    {
        r.retryAt = GetTickCount() + 1000;
        return false;
    }
    char folder[MAX_PATH];
    Folder(folder, kPaths);
    memset(&r.header, 0, sizeof(r.header));
    memcpy(r.header.magic, kMagic, sizeof(kMagic));
    r.header.version = kVersion;
    r.header.recordSize = sizeof(Record);
    r.header.step = kStep;
    const char *car = p.vehicle ? Virtual<const char *>(p.vehicle, kVehicleName) : nullptr;
    lstrcpynA(r.header.car, car ? car : "", sizeof(r.header.car));
    r.header.hour = nfs::timeofday::Enabled() ? nfs::timeofday::Hour() : -1.0f;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wsprintfA(r.header.started, "%04d-%02d-%02d %02d:%02d:%02d", now.wYear, now.wMonth, now.wDay, now.wHour,
              now.wMinute, now.wSecond);
    // path_YYYYMMDD_HHMMSS_car.nfspath, the car's name kept to letters, digits and underscores.
    char safe[32];
    int n = 0;
    for (const char *c = r.header.car; *c && n < 31; ++c)
        safe[n++] = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ? *c : '_';
    safe[n] = 0;
    wsprintfA(r.name, "path_%04d%02d%02d_%02d%02d%02d%s%s.nfspath", now.wYear, now.wMonth, now.wDay, now.wHour,
              now.wMinute, now.wSecond, n ? "_" : "", safe);
    char path[MAX_PATH];
    wsprintfA(path, "%s\\%s", folder, r.name);
    r.file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (r.file == INVALID_HANDLE_VALUE)
    {
        r.file = nullptr;
        r.retryAt = GetTickCount() + 1000;
        nfs::Log("dev: could not create %s", path);
        return false;
    }
    DWORD written = 0;
    WriteFile(r.file, &r.header, sizeof(r.header), &written, nullptr);
    r.buffered = 0;
    r.count = 0;
    r.car = p.simable;
    r.startTick = GetTickCount();
    r.warned = false;
    nfs::Log("dev: recording the path of %s to %s", r.header.car, path);
    return true;
}

void StopRecording()
{
    Recorder &r = g_state.recorder;
    if (!r.file)
        return;
    Flush();
    r.header.count = r.count;
    DWORD written = 0;
    SetFilePointer(r.file, 0, nullptr, FILE_BEGIN);
    WriteFile(r.file, &r.header, sizeof(r.header), &written, nullptr);
    CloseHandle(r.file);
    r.file = nullptr;
    char seconds[16];
    Hundredths(seconds, static_cast<float>(r.count) * kStep);
    nfs::Log("dev: recorded %u steps (%s s) to paths\\%s in the dev folder", r.count, seconds, r.name);
}

/** One simulation step of the player car into the recording. */
void RecordStep(float now, const unsigned char *controls)
{
    Recorder &r = g_state.recorder;
    const Player &p = g_state.player;
    Record &out = r.buffer[r.buffered];
    memset(&out, 0, sizeof(out));
    out.time = now;
    memcpy(out.position, Virtual<const float *>(p.body, kBodyPosition), sizeof(out.position));
    memcpy(out.orientation, Virtual<const float *>(p.body, kBodyOrientation), sizeof(out.orientation));
    memcpy(out.velocity, Virtual<const float *>(p.body, kBodyLinearVelocity), sizeof(out.velocity));
    memcpy(out.angular, Virtual<const float *>(p.body, kBodyAngularVelocity), sizeof(out.angular));
    out.steering = *reinterpret_cast<const float *>(controls + kSteering);
    out.gas = *reinterpret_cast<const float *>(controls + kGas);
    out.brake = *reinterpret_cast<const float *>(controls + kBrake);
    out.handbrake = *reinterpret_cast<const float *>(controls + kHandBrake);
    out.buttons = static_cast<unsigned char>((controls[kNitrous] ? 1 : 0) | (controls[kAction] ? 2 : 0));
    if (p.engine)
    {
        out.rpm = Virtual<float>(p.engine, kEngineRpm);
        out.nitrous = Virtual<float>(p.engine, kEngineNitrous);
        out.flags |= Virtual<bool>(p.engine, kEngineNitrousOn) ? 1 : 0;
    }
    if (p.transmission)
    {
        out.gear = static_cast<signed char>(Virtual<int>(p.transmission, kTransmissionGear));
        out.flags |= Virtual<bool>(p.transmission, kTransmissionShifting) ? 2 : 0;
        out.flags |= Virtual<bool>(p.transmission, kTransmissionReversing) ? 4 : 0;
    }
    if (p.vehicle)
        out.speed = Virtual<float>(p.vehicle, kVehicleSpeed);
    ++r.count;
    if (++r.buffered == kBufferRecords)
        Flush();
}

bool LoadPlayback()
{
    Playback &pb = g_state.play;
    char path[MAX_PATH];
    if (pb.path[1] == ':' || pb.path[0] == '\\')
        lstrcpynA(path, pb.path, MAX_PATH);
    else
    {
        // In the dev folder; "dev\paths\x" (as given when the folder was always dev next to the exe) still works.
        const char *rest = pb.path;
        if ((rest[0] == 'd' || rest[0] == 'D') && (rest[1] == 'e' || rest[1] == 'E') &&
            (rest[2] == 'v' || rest[2] == 'V') && (rest[3] == '\\' || rest[3] == '/'))
            rest += 4;
        Folder(path, "");
        const int n = lstrlenA(path);
        if (n + 1 < MAX_PATH)
        {
            path[n] = '\\';
            lstrcpynA(path + n + 1, rest, MAX_PATH - n - 1);
        }
    }
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        nfs::Log("dev: no recording %s", path);
        return false;
    }
    const DWORD size = GetFileSize(file, nullptr);
    Header header;
    DWORD read = 0;
    ReadFile(file, &header, sizeof(header), &read, nullptr);
    if (read != sizeof(header) || memcmp(header.magic, kMagic, sizeof(kMagic)) || header.recordSize != sizeof(Record))
    {
        CloseHandle(file);
        nfs::Log("dev: %s is not a path recording", path);
        return false;
    }
    pb.count = (size - sizeof(header)) / sizeof(Record);
    pb.records = static_cast<Record *>(::operator new(sizeof(Record) * (pb.count ? pb.count : 1)));
    ReadFile(file, pb.records, sizeof(Record) * pb.count, &read, nullptr);
    CloseHandle(file);
    pb.count = read / sizeof(Record);
    lstrcpynA(pb.path, path, MAX_PATH);
    char seconds[16];
    Hundredths(seconds, static_cast<float>(pb.count) * kStep);
    nfs::Log("dev: playing %s: %s, %u steps (%s s), recorded %s", path, header.car, pb.count, seconds,
             header.started);
    return pb.count > 0;
}

/** The car at a record (simulation axes) as a warp's place: the renderer's axes and the heading in degrees. */
void WarpTo(const Record &r)
{
    // The car's forward axis (simulation +Z) turned by its quaternion.
    const float x = r.orientation[0], y = r.orientation[1], z = r.orientation[2], w = r.orientation[3];
    const float forward[3] = {2.0f * (x * z + w * y), 2.0f * (y * z - w * x), 1.0f - 2.0f * (x * x + y * y)};
    // The warp's heading h faces (-sin h, 0, cos h) in simulation axes.
    const unsigned short angle = bATan(forward[2], -forward[0]);
    const float heading = static_cast<float>(angle) * (360.0f / 65536.0f);
    nfs::pose::RequestWarp(r.position[2], -r.position[0], r.position[1], heading);
}

/** One simulation step of the playback: the car takes the recorded state, its input the recorded controls. */
void PlayStep(float now, unsigned char *controls)
{
    Playback &pb = g_state.play;
    const Player &p = g_state.player;
    if (pb.startTime < 0.0f)
        pb.startTime = now;
    const unsigned int index = static_cast<unsigned int>((now - pb.startTime) / kStep * pb.rate + 0.5f);
    if (index >= pb.count)
    {
        pb.playing = false;
        pb.done = true;
        return;
    }
    const Record &r = pb.records[index];
    *reinterpret_cast<float *>(controls + kSteering) = r.steering;
    *reinterpret_cast<float *>(controls + kGas) = r.gas;
    *reinterpret_cast<float *>(controls + kBrake) = r.brake;
    *reinterpret_cast<float *>(controls + kHandBrake) = r.handbrake;
    controls[kNitrous] = (r.buttons & 1) ? 1 : 0;
    controls[kAction] = (r.buttons & 2) ? 1 : 0;
    if (pb.lastStep == now)
        return;
    pb.lastStep = now;
    Virtual<void>(p.body, kBodySetPosition, r.position);
    Virtual<void>(p.body, kBodySetOrientation, r.orientation);
    const float velocity[3] = {r.velocity[0] * pb.rate, r.velocity[1] * pb.rate, r.velocity[2] * pb.rate};
    const float angular[3] = {r.angular[0] * pb.rate, r.angular[1] * pb.rate, r.angular[2] * pb.rate};
    Virtual<void>(p.body, kBodySetLinearVelocity, velocity);
    Virtual<void>(p.body, kBodySetAngularVelocity, angular);
    if (p.transmission && !Virtual<bool>(p.transmission, kTransmissionShifting) &&
        Virtual<int>(p.transmission, kTransmissionGear) != r.gear)
        Virtual<bool>(p.transmission, kTransmissionShift, static_cast<int>(r.gear));
}

/** The frame times of the playback: logged, and written with the car's place to FILE.frames.csv. */
void ReportPlayback()
{
    Playback &pb = g_state.play;
    const int n = pb.frameCount;
    if (!n)
        return;
    float *sorted = static_cast<float *>(::operator new(sizeof(float) * n));
    float total = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        sorted[i] = pb.frames[i][0];
        total += sorted[i];
    }
    // Shell sort: tens of thousands of frames.
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; ++i)
            for (int j = i; j >= gap && sorted[j - gap] > sorted[j]; j -= gap)
            {
                const float t = sorted[j];
                sorted[j] = sorted[j - gap];
                sorted[j - gap] = t;
            }
    char average[16], median[16], p95[16], p99[16], worst[16], fps[16], seconds[16];
    Hundredths(average, total / n);
    Hundredths(median, sorted[n / 2]);
    Hundredths(p95, sorted[n * 95 / 100]);
    Hundredths(p99, sorted[n * 99 / 100]);
    Hundredths(worst, sorted[n - 1]);
    Hundredths(fps, total > 0.0f ? 1000.0f * n / total : 0.0f);
    Hundredths(seconds, total / 1000.0f);
    nfs::Log("dev: playback done: %d frames in %s s, %s fps; frame ms average %s, median %s, 95%% %s, 99%% %s, worst %s",
             n, seconds, fps, average, median, p95, p99, worst);
    ::operator delete(sorted);
    char path[MAX_PATH + 16];
    wsprintfA(path, "%s.frames.csv", pb.path);
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char line[128];
    DWORD written = 0;
    lstrcpyA(line, "frame,ms,x,y,z\r\n");
    WriteFile(file, line, lstrlenA(line), &written, nullptr);
    for (int i = 0; i < n; ++i)
    {
        char ms[16], x[16], y[16], z[16];
        Hundredths(ms, pb.frames[i][0]);
        Hundredths(x, pb.frames[i][1]);
        Hundredths(y, pb.frames[i][2]);
        Hundredths(z, pb.frames[i][3]);
        const int length = wsprintfA(line, "%d,%s,%s,%s,%s\r\n", i, ms, x, y, z);
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    }
    CloseHandle(file);
    nfs::Log("dev: frame times written to %s", path);
}

void NoteFrame()
{
    Playback &pb = g_state.play;
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const bool first = pb.lastFrame.QuadPart == 0;
    const float ms = first ? 0.0f
                           : static_cast<float>(static_cast<double>(now.QuadPart - pb.lastFrame.QuadPart) * 1000.0 /
                                                static_cast<double>(frequency.QuadPart));
    pb.lastFrame = now;
    if (first)
        return;
    if (pb.frameCount == pb.frameCapacity)
    {
        const int capacity = pb.frameCapacity ? pb.frameCapacity * 2 : 65536;
        float(*grown)[4] = static_cast<float(*)[4]>(::operator new(sizeof(float) * 4 * capacity));
        if (pb.frames)
        {
            memcpy(grown, pb.frames, sizeof(float) * 4 * pb.frameCount);
            ::operator delete(pb.frames);
        }
        pb.frames = grown;
        pb.frameCapacity = capacity;
    }
    float car[4] = {};
    nfs::pose::PlayerCar(car);
    float *f = pb.frames[pb.frameCount++];
    f[0] = ms;
    f[1] = car[0];
    f[2] = car[1];
    f[3] = car[2];
}

void UpdatePlayback(bool racing)
{
    Playback &pb = g_state.play;
    if (!pb.tried)
    {
        pb.tried = true;
        nfs::config::String("Dev", "PlayPath", "", pb.path, sizeof(pb.path));
        pb.quit = nfs::config::Int("Dev", "PlayPathQuit", 0) != 0;
        pb.rate = nfs::config::Float("Dev", "PlayRate", 1.0f);
        if (pb.rate < 0.1f)
            pb.rate = 1.0f;
        pb.startTime = -1.0f;
        pb.lastStep = -1.0f;
        if (pb.path[0] && !LoadPlayback())
            pb.path[0] = 0;
    }
    if (!pb.count || !racing)
        return;
    if (!pb.warped)
    {
        pb.warped = true;
        WarpTo(pb.records[0]);
        return;
    }
    if (!pb.playing && !pb.done && nfs::pose::WarpFinished())
    {
        pb.playing = true;
        nfs::Log("dev: playback started");
    }
    if (pb.playing)
    {
        NoteFrame();
        static DWORD logged;
        if (GetTickCount() - logged > 5000)
        {
            logged = GetTickCount();
            char at[16];
            Hundredths(at, pb.startTime >= 0.0f ? Sim::GetTime() - pb.startTime : -1.0f);
            nfs::Log("dev: playback at %s s of the recording, frame %d, flow %d", at, pb.frameCount,
                     TheGameFlowManager_CurrentGameFlowState);
        }
    }
    if (pb.done && pb.count)
    {
        ReportPlayback();
        pb.count = 0;
        if (pb.quit)
            ExitTheGameFlag = 1;
    }
}
} // namespace

bool Enabled()
{
    if (g_state.enabled < 0)
    {
        g_state.enabled = nfs::vk::HasArgument("-dev") ? 1 : 0;
        // Dev.Record=1: recording on from the start (every drive of the session).
        g_state.recorder.armed = g_state.enabled && nfs::config::Int("Dev", "Record", 0) != 0;
    }
    return g_state.enabled != 0;
}

void Update()
{
    if (!Enabled())
        return;
    const bool racing = TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing;
    Recorder &r = g_state.recorder;
    const bool key = nfs::display::DebugKeyDown(VK_F5);
    if (key && !g_state.key)
    {
        r.armed = !r.armed;
        nfs::Log("dev: path recording %s", r.armed ? "on" : "off");
        if (!r.armed)
            StopRecording();
    }
    g_state.key = key;
    // Out of the world the car and its behaviours go; the next step finds them anew whatever the simulation's clock.
    if (!racing)
        g_state.stepTime = -1.0f;
    // A drive's file ends with it (a loading screen, the car changed); the next drive starts another.
    void *car = racing ? nfs::pose::PlayerSimable() : nullptr;
    if (r.file && car != r.car)
        StopRecording();
    if (r.armed && !r.file && car)
        StartRecording();
    if (r.file && !r.count && !r.warned && GetTickCount() - r.startTick > 3000)
    {
        r.warned = true;
        nfs::Log("dev: nothing recorded yet: the car's input (IInput) was not seen");
    }
    UpdatePlayback(racing);
}

void Controls(void *input, unsigned char *controls)
{
    if (!g_state.recorder.file && !g_state.play.playing)
        return;
    const float now = Sim::GetTime();
    if (now != g_state.stepTime)
    {
        g_state.stepTime = now;
        g_state.stepDone = false;
        FindPlayer();
    }
    const Player &p = g_state.player;
    if (input != p.input || !p.body)
        return;
    if (g_state.play.playing)
    {
        PlayStep(now, controls);
        return;
    }
    if (g_state.stepDone)
        return;
    g_state.stepDone = true;
    RecordStep(now, controls);
}

void Shutdown()
{
    StopRecording();
}

void Folder(char *out, const char *sub)
{
    char given[MAX_PATH];
    nfs::config::String("Dev", "Folder", "", given, sizeof(given));
    if (given[0] && (given[1] == ':' || given[0] == '\\'))
        lstrcpynA(out, given, MAX_PATH);
    else
        ExePath(out, given[0] ? given : kFolder);
    int n = lstrlenA(out);
    while (n > 0 && (out[n - 1] == '\\' || out[n - 1] == '/'))
        out[--n] = 0;
    // Each missing folder on the way (the workspace's dev folder may not exist yet).
    for (int i = 3; i <= n; ++i)
        if (i == n || out[i] == '\\' || out[i] == '/')
        {
            const char kept = out[i];
            out[i] = 0;
            CreateDirectoryA(out, nullptr);
            out[i] = kept;
        }
    if (sub && sub[0] && n + 1 < MAX_PATH)
    {
        out[n] = '\\';
        lstrcpynA(out + n + 1, sub, MAX_PATH - n - 1);
        CreateDirectoryA(out, nullptr);
    }
}

bool Status(char *line, int size)
{
    if (!Enabled())
        return false;
    char text[96];
    if (g_state.recorder.armed && !g_state.recorder.file)
        lstrcpyA(text, "REC  waiting for a drive");
    else if (g_state.recorder.file)
    {
        const DWORD seconds = static_cast<DWORD>(g_state.recorder.count * kStep);
        wsprintfA(text, "REC %u:%02u  %s", seconds / 60, seconds % 60, g_state.recorder.name);
    }
    else if (g_state.play.playing)
    {
        const Playback &pb = g_state.play;
        const DWORD at = static_cast<DWORD>(pb.startTime >= 0.0f ? Sim::GetTime() - pb.startTime : 0.0f);
        const DWORD length = static_cast<DWORD>(pb.count * kStep);
        wsprintfA(text, "PLAY %u:%02u / %u:%02u", at / 60, at % 60, length / 60, length % 60);
    }
    else
        return false;
    lstrcpynA(line, text, size);
    return true;
}
} // namespace nfs::dev

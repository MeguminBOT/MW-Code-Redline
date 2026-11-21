/**
 * Where the player is, for recreating screenshots (dev/pose.h). Not in the original game.
 *
 * The car comes from the first local player (ListableSet<IPlayer>::First 0x425200, as the game's own code finds it):
 * its simulation position (ISimable::GetPosition) and its heading (GetVehicleVectors 0x579840). The simulation's axes
 * are the renderer's turned: render X = sim Z, render Y = -sim X, render Z = sim Y. The warp is GManager::WarpToMarker
 * (0x5ebc80) and GManager::NotifyWorldService (0x5ebb10) without the fades: stream the world in around the spot, place
 * the car and take it out of the simulation; once the streamer has loaded, place it again and put it back.
 */
#include <math.h>
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/pose.h"
#include "engine/rendering/shadows/shadowdebug.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"
#include "gameplay/world/weather.h"
#include "engine/rendering/x360/frame.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

NFS_REPLACE(0x6b6890, PoseInput::IsLookBackButtonPressed)

/** InputPlayer (zPhysicsBehaviors): the human player's input. */
struct PoseInput
{
    bool IsLookBackButtonPressed() const;
};

namespace nfs::pose
{
namespace
{
const int kGameFlowRacing = 6;
const int kPlayerView = 1;
/** ePlayerList's local players. */
const int kLocalPlayers = 1;
/** IPlayer::GetSimable, ISimable::GetPosition, IVehicle::SetVehicleOnGround, and the IVehicle calls WarpToMarker
 * makes after placing the car and NotifyWorldService once the world has loaded. */
const int kGetSimable = 0x04;
const int kGetPosition = 0x60;
const int kSetVehicleOnGround = 0x68;
const int kVehicleWarpStart = 0x84;
const int kVehicleWarpEnd = 0x80;
/** TheTrackStreamer, for TrackStreamer::IsLoadingInProgress (0x7303d0). */
const unsigned int kTrackStreamer = 0x9aa9d0;
/** IVehicle's and IRigidBody's interface handles, for UTL::COM::Object::_IList::Find (0x5d59f0), and
 * IRigidBody::SetLinearVelocity. */
const unsigned int kVehicleHandle = 0x4040e0;
const unsigned int kRigidBodyHandle = 0x4039f0;
const unsigned int kInputHandle = 0x403f90;
/** InputControls (what PInput::GetControls returns): steering, throttle, brake. */
const int kControlSteering = 0x04;
const int kControlGas = 0x14;
const int kControlBrake = 0x18;
const int kBodySetLinearVelocity = 0x60;
const float kMetresPerSecondPerKmh = 1.0f / 3.6f;
/** The warp waits this long into a race, so the start is over and the world near the start has loaded. */
const DWORD kWarpDelayMs = 3000;
/** A tour stop is captured this long after its warp has finished (the car settles), then the next follows. */
const DWORD kTourSettleMs = 3000;
const DWORD kTourCaptureMs = 500;
const int kMaxStops = 256;
const int kMaxTourHours = 12;
const float kDegreesPerAngle = 360.0f / 65536.0f;

struct Spot
{
    float x, y, z;
    float heading;
    float pitch;
};

struct State
{
    bool loaded;
    bool warp_wanted;
    bool warp_done;
    bool camera_held;
    Spot warp;
    Spot camera;
    DWORD racing_since;
    bool key_down;
    /** The car and camera as this frame draws them, for the overlay drawn after it. */
    bool shown;
    Spot car;
    Spot shown_camera;
    /** Debug.Tour: the poses of a debug_poses.txt, visited in turn, each saved as pose_<n>.bmp. */
    Spot tour_cars[kMaxStops];
    Spot tour_cameras[kMaxStops];
    /** The clock's hour at each stop (-1: as it is) and the pose it shows (Debug.TourHours repeats each pose). */
    float tour_hours[kMaxStops];
    int tour_poses[kMaxStops];
    int tour_count;
    int tour_stop;
    DWORD tour_stop_since;
    DWORD tour_captured_at;
    char tour_reflection[48];
    /** Debug.Burst=SECONDS,FRAMES: consecutive frames saved as burst_NN.bmp that many seconds into a race. */
    float burst_at;
    int burst_frames;
    int burst_taken;
    /** Debug.LookBack=SECONDS,LENGTH,PERIOD: from SECONDS into a race, every PERIOD seconds the camera looks back for
     * LENGTH seconds (in front of the car, facing its tail, as the look-back button puts it): cuts for testing. */
    float lookback_at;
    float lookback_length;
    float lookback_period;
    /** Debug.AutoDrive=SECONDS: that many seconds into a race the AI takes over the player's car. */
    float autodrive_at;
    bool autodrive_done;
    /** Debug.WarpSpeed=KMH: a warp leaves the car moving at that speed the way it faces (driving tests without
     * input: does it get through there). */
    float warp_speed;
    /** Debug.WarpGas=FRACTION: after a warp the player's input holds that much throttle, no brake, and the steering at
     * Debug.WarpSteer (-1 full left to 1 full right, 0 by default). */
    float warp_gas;
    float warp_steer;
    /** Debug.WarpLookBack=SECONDS: that long after a warp the look-back button reads as held (-1: never). */
    float warp_look_back;
    /** Debug.LogCar=SECONDS: the car's position logged that often in a race. */
    float log_car_period;
    DWORD log_car_at;
    /** A warp waiting for the world to load around its spot. */
    bool warp_pending;
    Spot warp_spot;
    DWORD warp_finished_at;
};

State g_state;

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}

void *Simable()
{
    void *player = NFS_ORIGINAL(0x425200, void *(__cdecl *)(int))(kLocalPlayers);
    return player ? Virtual<void *>(player, kGetSimable) : nullptr;
}

unsigned short Angle(float degrees)
{
    return static_cast<unsigned short>(static_cast<int>(degrees / kDegreesPerAngle));
}

float Degrees(unsigned short angle)
{
    return static_cast<float>(angle) * kDegreesPerAngle;
}

/** "-12.3" (wsprintf has no floats). */
void Tenths(char *out, float value)
{
    int tenths = static_cast<int>(value * 10.0f + (value < 0.0f ? -0.5f : 0.5f));
    const char *sign = tenths < 0 ? "-" : "";
    if (tenths < 0)
        tenths = -tenths;
    wsprintfA(out, "%s%d.%d", sign, tenths / 10, tenths % 10);
}

/** Up to `count` comma-separated numbers; returns how many were read. */
int ParseNumbers(const char *text, float *out, int count)
{
    int read = 0;
    while (*text && read < count)
    {
        char number[32];
        int n = 0;
        while (*text && *text != ',' && n < 31)
            number[n++] = *text++;
        number[n] = 0;
        out[read++] = bStrToFloat(number);
        if (*text == ',')
            ++text;
    }
    return read;
}

/** The numbers after `key` in `line`, up to the next space. */
int ParseAfter(const char *line, const char *key, float *out, int count)
{
    const char *p = strstr(line, key);
    if (!p)
        return 0;
    p += lstrlenA(key);
    char text[128];
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\r' && p[n] != '\n' && n < 127)
    {
        text[n] = p[n];
        ++n;
    }
    text[n] = 0;
    return ParseNumbers(text, out, count);
}

/** Reads the poses F9 saved (lines of -set Debug.Warp=... -set Debug.Camera=..., and -set World.TimeOfDay=HH:MM when
 * the clock was on) from `name` next to the exe; with Debug.TourHours=H,H,... each pose is visited at each hour. */
void LoadTour(const char *name)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    lstrcpynA(path + n, name, MAX_PATH - n);
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        nfs::Log("pose: no tour file %s", path);
        return;
    }
    static char text[16384];
    DWORD read = 0;
    ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
    CloseHandle(file);
    text[read] = 0;
    char list[128];
    config::String("Debug", "TourHours", "", list, sizeof(list));
    float tourHours[kMaxTourHours];
    int hours = 0;
    for (const char *p = list; *p && hours < kMaxTourHours;)
    {
        const float hour = nfs::timeofday::ParseHour(p);
        if (hour >= 0.0f)
            tourHours[hours++] = hour;
        while (*p && *p != ',')
            ++p;
        if (*p == ',')
            ++p;
    }
    int poses = 0;
    for (char *line = text; *line && g_state.tour_count < kMaxStops;)
    {
        char *end = line;
        while (*end && *end != '\n')
            ++end;
        const char saved = *end;
        *end = 0;
        float car[4], camera[5];
        if (ParseAfter(line, "Debug.Warp=", car, 4) == 4 && ParseAfter(line, "Debug.Camera=", camera, 5) == 5)
        {
            const char *hour = strstr(line, "World.TimeOfDay=");
            for (int h = 0; h < (hours ? hours : 1) && g_state.tour_count < kMaxStops; ++h)
            {
                const int stop = g_state.tour_count++;
                g_state.tour_cars[stop] = {car[0], car[1], car[2], car[3], 0.0f};
                g_state.tour_cameras[stop] = {camera[0], camera[1], camera[2], camera[3], camera[4]};
                g_state.tour_hours[stop] = hours  ? tourHours[h]
                                           : hour ? nfs::timeofday::ParseHour(hour + lstrlenA("World.TimeOfDay="))
                                                  : -1.0f;
                g_state.tour_poses[stop] = poses;
            }
            ++poses;
        }
        *end = saved;
        line = *end ? end + 1 : end;
    }
    g_state.tour_stop = -1;
    nfs::Log("pose: tour of %d stops from %s", g_state.tour_count, path);
}

void Load()
{
    g_state.loaded = true;
    char text[128];
    float values[5] = {};
    config::String("Debug", "Warp", "", text, sizeof(text));
    if (text[0] && ParseNumbers(text, values, 4) == 4)
    {
        g_state.warp = {values[0], values[1], values[2], values[3], 0.0f};
        g_state.warp_wanted = true;
    }
    config::String("Debug", "Camera", "", text, sizeof(text));
    if (text[0] && ParseNumbers(text, values, 5) == 5)
    {
        g_state.camera = {values[0], values[1], values[2], values[3], values[4]};
        g_state.camera_held = true;
    }
    config::String("Debug", "Burst", "", text, sizeof(text));
    if (text[0] && ParseNumbers(text, values, 2) == 2)
    {
        g_state.burst_at = values[0];
        g_state.burst_frames = static_cast<int>(values[1]);
    }
    config::String("Debug", "LookBack", "", text, sizeof(text));
    if (text[0] && ParseNumbers(text, values, 3) == 3)
    {
        g_state.lookback_at = values[0];
        g_state.lookback_length = values[1];
        g_state.lookback_period = values[2] > 0.0f ? values[2] : 1.0f;
    }
    config::String("Debug", "Tour", "", text, sizeof(text));
    if (text[0])
        LoadTour(text);
    config::String("Debug", "AutoDrive", "", text, sizeof(text));
    g_state.autodrive_at = text[0] && ParseNumbers(text, values, 1) == 1 ? values[0] : -1.0f;
    config::String("Debug", "WarpSpeed", "", text, sizeof(text));
    g_state.warp_speed = text[0] && ParseNumbers(text, values, 1) == 1 ? values[0] : 0.0f;
    config::String("Debug", "WarpGas", "", text, sizeof(text));
    g_state.warp_gas = text[0] && ParseNumbers(text, values, 1) == 1 ? values[0] : 0.0f;
    config::String("Debug", "WarpSteer", "", text, sizeof(text));
    g_state.warp_steer = text[0] && ParseNumbers(text, values, 1) == 1 ? values[0] : 0.0f;
    config::String("Debug", "WarpLookBack", "", text, sizeof(text));
    g_state.warp_look_back = text[0] && ParseNumbers(text, values, 1) == 1 ? values[0] : -1.0f;
    config::String("Debug", "LogCar", "", text, sizeof(text));
    g_state.log_car_period = text[0] && ParseNumbers(text, values, 1) == 1 ? values[0] : 0.0f;
}

bool ReadCar(Spot &car)
{
    void *simable = Simable();
    if (!simable)
        return false;
    const float *position = Virtual<const float *>(simable, kGetPosition);
    float flat[2], direction[2] = {1.0f, 0.0f};
    NFS_ORIGINAL(0x579840, void(__cdecl *)(float *, float *, void *))(flat, direction, simable);
    car = {position[2], -position[0], position[1], Degrees(bATan(direction[0], direction[1])), 0.0f};
    return true;
}

bool ReadCamera(Spot &camera)
{
    const Camera *c = eViews[kPlayerView].pCamera;
    if (!c)
        return false;
    const CameraParams &key = c->CurrentKey;
    const unsigned short pitch = bASin(key.Direction.z);
    camera = {key.Position.x, key.Position.y, key.Position.z, Degrees(bATan(key.Direction.x, key.Direction.y)),
              static_cast<float>(static_cast<short>(pitch)) * kDegreesPerAngle};
    return true;
}

void *Vehicle()
{
    void *simable = Simable();
    if (!simable)
        return nullptr;
    void *objects = *reinterpret_cast<void **>(static_cast<char *>(simable) + 4);
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kVehicleHandle);
}

/** Puts the car on the ground at the spot (simulation axes); false without a player car. */
bool Place(const Spot &spot, void *&vehicle)
{
    vehicle = Vehicle();
    if (!vehicle)
        return false;
    const unsigned short heading = Angle(spot.heading);
    const float position[3] = {-spot.y, spot.z, spot.x};
    const float direction[3] = {-bSin(heading), 0.0f, bCos(heading)};
    Virtual<bool>(vehicle, kSetVehicleOnGround, position, direction);
    return true;
}

void Warp(const Spot &spot)
{
    const float position[3] = {-spot.y, spot.z, spot.x};
    NFS_ORIGINAL(0x6f1170, void(__cdecl *)(const float *, bool))(position, false);
    void *vehicle;
    if (!Place(spot, vehicle))
        return;
    Virtual<void>(vehicle, kVehicleWarpStart);
    g_state.warp_pending = true;
    g_state.warp_spot = spot;
    g_state.warp_finished_at = 0;
    char x[16], y[16], z[16], h[16];
    Tenths(x, spot.x);
    Tenths(y, spot.y);
    Tenths(z, spot.z);
    Tenths(h, spot.heading);
    nfs::Log("pose: car warped to %s, %s, %s facing %s", x, y, z, h);
}

/** Ends the warp once the streamer has loaded the world around the spot. */
void FinishWarp(DWORD now)
{
    if (NFS_ORIGINAL(0x7303d0, bool(__thiscall *)(void *))(reinterpret_cast<void *>(kTrackStreamer)))
        return;
    g_state.warp_pending = false;
    g_state.warp_finished_at = now ? now : 1;
    void *vehicle;
    if (Place(g_state.warp_spot, vehicle))
    {
        Virtual<void>(vehicle, kVehicleWarpEnd);
        if (g_state.warp_speed > 0.0f)
        {
            void *simable = Simable();
            void *objects = simable ? *reinterpret_cast<void **>(static_cast<char *>(simable) + 4) : nullptr;
            void *body = objects ? NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(
                                       objects, kRigidBodyHandle)
                                 : nullptr;
            const unsigned short heading = Angle(g_state.warp_spot.heading);
            const float speed = g_state.warp_speed * kMetresPerSecondPerKmh;
            const float velocity[3] = {-bSin(heading) * speed, 0.0f, bCos(heading) * speed};
            if (body)
                Virtual<void>(body, kBodySetLinearVelocity, velocity);
        }
    }
    nfs::Log("pose: warp finished");
}

/** Holds the player camera at the spot: the view matrix as the game makes one (eCreateLookAtMatrix 0x6cf0a0, Z up)
 * and the position, direction and target Camera::SetCameraMatrix would work out from it. */
void HoldCamera(const Spot &spot)
{
    Camera *c = eViews[kPlayerView].pCamera;
    if (!c)
        return;
    CameraParams &key = c->CurrentKey;
    const unsigned short heading = Angle(spot.heading), pitch = Angle(spot.pitch);
    const float flat = bCos(pitch);
    const bVector3 eye = {spot.x, spot.y, spot.z};
    const bVector3 direction = {bCos(heading) * flat, bSin(heading) * flat, bSin(pitch)};
    const float distance = key.TargetDistance > 0.0f ? key.TargetDistance : 10.0f;
    const bVector3 target = {eye.x + direction.x * distance, eye.y + direction.y * distance,
                             eye.z + direction.z * distance};
    const bVector3 up = {0.0f, 0.0f, 1.0f};
    NFS_ORIGINAL(0x6cf0a0, void(__cdecl *)(bMatrix4 *, const bVector3 *, const bVector3 *, const bVector3 *))(
        &key.Matrix, &eye, &target, &up);
    key.Position = eye;
    key.Direction = direction;
    key.Target = target;
    // A still camera: no motion blur.
    c->VelocityKey.Position = {0.0f, 0.0f, 0.0f};
    c->VelocityKey.Direction = {0.0f, 0.0f, 0.0f};
}

/** The tour's next step: warp to a stop and hold its camera, capture it once settled, move on, quit after the last. */
void Tour(DWORD now)
{
    if (g_state.tour_stop >= 0 && (g_state.warp_pending || !g_state.warp_finished_at))
        return;
    if (g_state.tour_stop >= 0 && !g_state.tour_captured_at && now - g_state.warp_finished_at >= kTourSettleMs)
    {
        // pose_NN.bmp, or pose_NN_HHMM.bmp at an hour of the clock.
        const float hour = g_state.tour_hours[g_state.tour_stop];
        const int minutes = hour >= 0.0f ? static_cast<int>(hour * 60.0f + 0.5f) % (24 * 60) : -1;
        char name[32], debug[40];
        if (minutes >= 0)
        {
            wsprintfA(name, "pose_%02d_%02d%02d.bmp", g_state.tour_poses[g_state.tour_stop], minutes / 60, minutes % 60);
            wsprintfA(debug, "pose_%02d_%02d%02d_debug.bmp", g_state.tour_poses[g_state.tour_stop], minutes / 60,
                      minutes % 60);
        }
        else
        {
            wsprintfA(name, "pose_%02d.bmp", g_state.tour_stop);
            wsprintfA(debug, "pose_%02d_debug.bmp", g_state.tour_stop);
        }
        if (nfs::vk::g_renderer)
            nfs::vk::g_renderer->RequestCapture(name, nfs::vk::HasArgument("-shadowdebug") ? debug : nullptr);
        lstrcpyA(g_state.tour_reflection, name);
        lstrcpyA(g_state.tour_reflection + lstrlenA(name) - 4, "_reflect.bmp");
        g_state.tour_captured_at = now;
        return;
    }
    // -reflectdebug: the road reflection's target, after the frame's capture (reading it back ends the frame).
    if (g_state.tour_stop >= 0 && g_state.tour_captured_at && g_state.tour_reflection[0] &&
        now - g_state.tour_captured_at >= kTourCaptureMs / 2)
    {
        if (nfs::vk::HasArgument("-reflectdebug"))
            nfs::x360::RequestReflectionDump(g_state.tour_reflection);
        g_state.tour_reflection[0] = 0;
    }
    if (g_state.tour_stop >= 0 && (!g_state.tour_captured_at || now - g_state.tour_captured_at < kTourCaptureMs))
        return;
    if (++g_state.tour_stop >= g_state.tour_count)
    {
        nfs::Log("pose: tour done");
        g_state.tour_count = 0;
        ExitTheGameFlag = 1;
        return;
    }
    g_state.tour_stop_since = now;
    g_state.tour_captured_at = 0;
    g_state.camera = g_state.tour_cameras[g_state.tour_stop];
    g_state.camera_held = true;
    if (g_state.tour_hours[g_state.tour_stop] >= 0.0f)
        nfs::timeofday::SetHour(g_state.tour_hours[g_state.tour_stop]);
    nfs::Log("pose: tour stop %d", g_state.tour_stop);
    Warp(g_state.tour_cars[g_state.tour_stop]);
}

void AppendLine(const char *line)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    lstrcpyA(path + n, "debug_poses.txt");
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(file, line, static_cast<DWORD>(lstrlenA(line)), &written, nullptr);
    CloseHandle(file);
}

void SavePose()
{
    Spot car, camera;
    if (!ReadCar(car) || !ReadCamera(camera))
        return;
    char v[9][16];
    Tenths(v[0], car.x);
    Tenths(v[1], car.y);
    Tenths(v[2], car.z);
    Tenths(v[3], car.heading);
    Tenths(v[4], camera.x);
    Tenths(v[5], camera.y);
    Tenths(v[6], camera.z);
    Tenths(v[7], camera.heading);
    Tenths(v[8], camera.pitch);
    char hour[48], weather[48];
    nfs::timeofday::PoseOption(hour, sizeof(hour));
    nfs::weather::PoseOption(weather, sizeof(weather));
    char line[320];
    wsprintfA(line, "-set Debug.Warp=%s,%s,%s,%s -set Debug.Camera=%s,%s,%s,%s,%s%s%s\r\n", v[0], v[1], v[2], v[3],
              v[4], v[5], v[6], v[7], v[8], hour, weather);
    AppendLine(line);
    nfs::Log("pose: %s", line);
}
} // namespace

void RequestWarp(float x, float y, float z, float heading)
{
    if (!g_state.loaded)
        Load();
    g_state.warp = {x, y, z, heading, 0.0f};
    g_state.warp_wanted = true;
    g_state.warp_done = false;
}

bool WarpFinished()
{
    return g_state.warp_done && !g_state.warp_pending && g_state.warp_finished_at;
}

bool PlayerCar(float out[4])
{
    Spot car;
    if (!ReadCar(car))
        return false;
    out[0] = car.x;
    out[1] = car.y;
    out[2] = car.z;
    out[3] = car.heading;
    return true;
}

void *PlayerSimable()
{
    return Simable();
}

void *PlayerInterface(unsigned int handle)
{
    void *simable = Simable();
    if (!simable)
        return nullptr;
    void *objects = *reinterpret_cast<void **>(static_cast<char *>(simable) + 4);
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, handle);
}

bool LookBackHeld()
{
    return g_state.warp_look_back >= 0.0f && g_state.warp_finished_at && !g_state.warp_pending &&
           GetTickCount() - g_state.warp_finished_at >= static_cast<DWORD>(g_state.warp_look_back * 1000.0f);
}

/** What the look-back button last read as. */
bool g_lookingBack;

bool LookingBack()
{
    return g_lookingBack;
}

void Controls(void *input, unsigned char *controls)
{
    if (g_state.warp_gas <= 0.0f || !g_state.warp_finished_at || g_state.warp_pending ||
        input != PlayerInterface(kInputHandle))
        return;
    *reinterpret_cast<float *>(controls + kControlGas) = g_state.warp_gas;
    *reinterpret_cast<float *>(controls + kControlBrake) = 0.0f;
    *reinterpret_cast<float *>(controls + kControlSteering) = g_state.warp_steer;
}

void Update()
{
    if (!g_state.loaded)
        Load();
    const bool key_down = display::DebugKeyDown(VK_F9);
    if (key_down && !g_state.key_down)
        SavePose();
    g_state.key_down = key_down;

    if (TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing)
    {
        g_state.racing_since = 0;
        g_state.shown = false;
        return;
    }
    if (!g_state.racing_since)
        g_state.racing_since = GetTickCount();
    if (g_state.warp_wanted && !g_state.warp_done && GetTickCount() - g_state.racing_since >= kWarpDelayMs)
    {
        g_state.warp_done = true;
        Warp(g_state.warp);
    }
    if (g_state.warp_pending)
        FinishWarp(GetTickCount());
    if (g_state.burst_taken < g_state.burst_frames &&
        static_cast<float>(GetTickCount() - g_state.racing_since) >= g_state.burst_at * 1000.0f && nfs::vk::g_renderer)
    {
        // The cascade receivers were held to (Debug.MinCascade) is in the name, for tools/cascadecmp.py.
        char name[32];
        wsprintfA(name, "burst_%02d_c%d.bmp", g_state.burst_taken++, nfs::shadow::debug::ForcedCascade());
        nfs::vk::g_renderer->RequestCapture(name);
    }
    if (g_state.tour_count && GetTickCount() - g_state.racing_since >= kWarpDelayMs)
        Tour(GetTickCount());
    if (g_state.autodrive_at >= 0.0f && !g_state.autodrive_done &&
        static_cast<float>(GetTickCount() - g_state.racing_since) >= g_state.autodrive_at * 1000.0f)
    {
        // The game's own switch (AIVehicleHuman::Update flips the player car's AI control when it is set), the one
        // Extra Options' Auto Drive key sets: driving tests need no key press and no window focus.
        bToggleAiControl = true;
        g_state.autodrive_done = true;
    }
    if (g_state.log_car_period > 0.0f &&
        GetTickCount() - g_state.log_car_at >= static_cast<DWORD>(g_state.log_car_period * 1000.0f))
    {
        g_state.log_car_at = GetTickCount();
        Spot car;
        if (ReadCar(car))
        {
            char x[16], y[16], z[16], h[16];
            Tenths(x, car.x);
            Tenths(y, car.y);
            Tenths(z, car.z);
            Tenths(h, car.heading);
            Spot camera;
            if (ReadCamera(camera))
            {
                char cx[16], cy[16], cz[16], ch[16];
                Tenths(cx, camera.x);
                Tenths(cy, camera.y);
                Tenths(cz, camera.z);
                Tenths(ch, camera.heading);
                nfs::Log("pose: car at %s, %s, %s facing %s; camera at %s, %s, %s facing %s", x, y, z, h, cx, cy, cz,
                         ch);
            }
            else
                nfs::Log("pose: car at %s, %s, %s facing %s", x, y, z, h);
        }
    }
    if (g_state.camera_held)
        HoldCamera(g_state.camera);
    if (g_state.lookback_length > 0.0f)
    {
        const float t = static_cast<float>(GetTickCount() - g_state.racing_since) * 0.001f - g_state.lookback_at;
        Spot car;
        if (t >= 0.0f && t - g_state.lookback_period * floorf(t / g_state.lookback_period) < g_state.lookback_length &&
            ReadCar(car))
        {
            const unsigned short heading = Angle(car.heading);
            Spot back = car;
            back.x += bCos(heading) * 4.0f;
            back.y += bSin(heading) * 4.0f;
            back.z += 1.6f;
            back.heading = car.heading + 180.0f;
            back.pitch = -5.0f;
            HoldCamera(back);
        }
    }
    g_state.shown = ReadCar(g_state.car) && ReadCamera(g_state.shown_camera);
}

int Text(char (*lines)[kLineLength])
{
    if (!g_state.shown)
        return 0;
    const Spot &car = g_state.car, &camera = g_state.shown_camera;
    char x[16], y[16], z[16], h[16], p[16];
    Tenths(x, car.x);
    Tenths(y, car.y);
    Tenths(z, car.z);
    Tenths(h, car.heading);
    wsprintfA(lines[0], "CAR X %s Y %s Z %s H %s", x, y, z, h);
    Tenths(x, camera.x);
    Tenths(y, camera.y);
    Tenths(z, camera.z);
    Tenths(h, camera.heading);
    Tenths(p, camera.pitch);
    wsprintfA(lines[1], "CAM X %s Y %s Z %s H %s P %s", x, y, z, h, p);
    int count = nfs::timeofday::Text(lines[2], kLineLength) ? 3 : 2;
    if (nfs::weather::Text(lines[count], kLineLength))
        ++count;
    return count;
}
} // namespace nfs::pose

/** The look-back button (original: InputPlayer::IsLookBackButtonPressed 0x6b6890, its look-back control above
 * zero), or Debug.WarpLookBack's. */
bool PoseInput::IsLookBackButtonPressed() const
{
    const int kLookBackControl = 0x2dc;
    nfs::pose::g_lookingBack =
        nfs::pose::LookBackHeld() ||
        *reinterpret_cast<const float *>(reinterpret_cast<const char *>(this) + kLookBackControl) > 0.0f;
    return nfs::pose::g_lookingBack;
}

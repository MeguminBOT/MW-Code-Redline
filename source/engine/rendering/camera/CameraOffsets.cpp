/**
 * The player's camera adjustments (engine/rendering/camera/camera.h, docs/CONTROLS.md "Camera Settings"). Not in the original game.
 *
 * Every frame the chase camera (CubicCameraMover::Update) asks its anchor for the view's settings:
 * CameraAnchor::GetPov (0x4713c0, thiscall, wrapped here) picks the car's camerainfo collection for the POV type and
 * copies the values for a single screen ([0] of each array; [1] in split screen) into the anchor:
 *
 *   +0xc8 POV type (0 bumper, 1 hood, 2 close, 3 far, 4-6 the drift, pursuit and other cameras)
 *   +0xca ANGLE (bAngle; positive looks down)    +0xcc LAG (metres along the car, negative behind it), over the
 *   anchor's +0xc4    +0xd0 HEIGHT (metres)      +0xd4 the field at camerainfo +0x10
 *   +0xd8 FOV (bAngle)          +0xdc STIFFNESS    +0xe0 TILTING
 *
 * The anchor's +0x90 is the car's type (a CarTypeInfo hash), whose name keys the car's own adjustments.
 *
 * The wrapper adds the preset's adjustments for the view and the car's to those copies, so the camera eases into them as it does
 * into the game's own values, and the projection, culling and level of detail follow. `-set Debug.CameraLog=1` logs
 * the values a view gets.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "engine/rendering/camera/camera.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"

NFS_REPLACE(0x4713c0, CameraAnchorGetPov)

short *__fastcall CameraAnchorGetPov(char *self, void *, int pov);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

const int kPovType = 0xc8;
const int kFov = 0xd8;
const int kLag = 0xcc;
const int kHeight = 0xd0;
const int kAngle = 0xca;
const int kModel = 0x90;
/** bAngle units in a degree. */
const float kAngleUnits = 65536.0f / 360.0f;

const char *const kViewKeys[nfs::camera::kViews] = {"Bumper", "Hood", "Close", "Far"};
const char *const kAdjustmentKeys[nfs::camera::kAdjustments] = {"Height", "Distance", "Tilt", "Fov"};

struct State
{
    bool read;
    bool logging;
    int preset;
    float values[nfs::camera::kPresets + 1][nfs::camera::kViews][nfs::camera::kAdjustments];
    int currentView;
    int loggedPov;
    /** The car followed (type hash and name) and its own adjustments. */
    unsigned int carHash;
    char car[32];
    float carValues[nfs::camera::kViews][nfs::camera::kAdjustments];
};

State g = {false, false, 0, {}, nfs::camera::kFar, -1, 0, {}, {}};

void CarSection(const char *car, char *out)
{
    wsprintfA(out, "CameraCar %s", car);
}

/** The car's own adjustments, read when the camera first follows it ([Camera] LastCar remembers it for the menus). */
void ReadCar(const char *car)
{
    lstrcpynA(g.car, car, sizeof(g.car));
    CharUpperA(g.car);
    char section[64];
    CarSection(g.car, section);
    for (int v = 0; v < nfs::camera::kViews; ++v)
        for (int a = 0; a < nfs::camera::kAdjustments; ++a)
        {
            char key[32];
            wsprintfA(key, "%s%s", kViewKeys[v], kAdjustmentKeys[a]);
            g.carValues[v][a] = g.car[0] ? nfs::config::Float(section, key, 0.0f) : 0.0f;
        }
}

void Section(int preset, char *out)
{
    wsprintfA(out, "CameraPreset%d", preset);
}

void Key(int view, int adjustment, char *out)
{
    wsprintfA(out, "%s%s", kViewKeys[view], kAdjustmentKeys[adjustment]);
}

void Read()
{
    if (g.read)
        return;
    g.read = true;
    g.logging = nfs::config::Int("Debug", "CameraLog", 0) != 0;
    const int preset = nfs::config::Int("Camera", "Preset", 0);
    g.preset = preset < 0 || preset > nfs::camera::kPresets ? 0 : preset;
    char last[32];
    nfs::config::String("Camera", "LastCar", "", last, sizeof(last));
    ReadCar(last);
    for (int p = 1; p <= nfs::camera::kPresets; ++p)
    {
        char section[32];
        Section(p, section);
        for (int v = 0; v < nfs::camera::kViews; ++v)
            for (int a = 0; a < nfs::camera::kAdjustments; ++a)
            {
                char key[32];
                Key(v, a, key);
                g.values[p][v][a] = nfs::config::Float(section, key, 0.0f);
            }
    }
}

int Angle(float degrees)
{
    return static_cast<int>(degrees * kAngleUnits + (degrees < 0.0f ? -0.5f : 0.5f));
}
} // namespace

/** 0x4713c0 wrapped: the view's settings from the car's camera data, then the preset's adjustments on them. */
short *__fastcall CameraAnchorGetPov(char *self, void *, int pov)
{
    typedef short *(__thiscall * GetPov)(char *, int);
    short *result = NFS_ORIGINAL(0x4713c0, GetPov)(self, pov);
    Read();
    const int type = At<short>(self, kPovType);
    if (type >= 0 && type < nfs::camera::kViews)
        g.currentView = type;
    const unsigned int model = At<unsigned int>(self, kModel);
    if (model && model != g.carHash)
    {
        g.carHash = model;
        const char *name = reinterpret_cast<const char *>(GetCarTypeInfoFromHash(model));
        if (name && lstrcmpiA(name, g.car))
        {
            ReadCar(name);
            nfs::config::WriteString("Camera", "LastCar", g.car);
        }
    }
    if (g.logging && type != g.loggedPov)
    {
        g.loggedPov = type;
        nfs::Log("camera: pov %d fov %d lag %d height %d angle %d (degrees and centimetres x1)", type,
                 static_cast<int>(static_cast<unsigned short>(At<short>(self, kFov)) / kAngleUnits),
                 static_cast<int>(At<float>(self, kLag) * 100.0f), static_cast<int>(At<float>(self, kHeight) * 100.0f),
                 static_cast<int>(At<short>(self, kAngle) / kAngleUnits));
    }
    if (type < 0 || type >= nfs::camera::kViews)
        return result;
    const float *p = g.values[g.preset][type];
    const float *c = g.carValues[type];
    float a[nfs::camera::kAdjustments];
    for (int i = 0; i < nfs::camera::kAdjustments; ++i)
        a[i] = p[i] + c[i];
    if (a[0] == 0.0f && a[1] == 0.0f && a[2] == 0.0f && a[3] == 0.0f)
        return result;
    At<float>(self, kHeight) += a[nfs::camera::kHeight];
    At<float>(self, kLag) -= a[nfs::camera::kDistance];
    int angle = At<short>(self, kAngle) + Angle(a[nfs::camera::kTilt]);
    At<short>(self, kAngle) = static_cast<short>(angle);
    int fov = static_cast<unsigned short>(At<short>(self, kFov)) + Angle(a[nfs::camera::kFov]);
    // Between 20 and 150 degrees.
    fov = fov < 0xe39 ? 0xe39 : fov > 0x6aaa ? 0x6aaa : fov;
    At<short>(self, kFov) = static_cast<short>(fov);
    return result;
}

namespace nfs::camera
{
int Preset()
{
    Read();
    return g.preset;
}

void SetPreset(int preset)
{
    Read();
    g.preset = preset < 0 || preset > kPresets ? 0 : preset;
    nfs::config::WriteInt("Camera", "Preset", g.preset);
}

float Get(int preset, int view, int adjustment)
{
    Read();
    if (preset < 1 || preset > kPresets || view < 0 || view >= kViews || adjustment < 0 || adjustment >= kAdjustments)
        return 0.0f;
    return g.values[preset][view][adjustment];
}

void Set(int preset, int view, int adjustment, float value)
{
    Read();
    if (preset < 1 || preset > kPresets || view < 0 || view >= kViews || adjustment < 0 || adjustment >= kAdjustments)
        return;
    g.values[preset][view][adjustment] = value;
    char section[32], key[32];
    Section(preset, section);
    Key(view, adjustment, key);
    nfs::config::WriteFloat(section, key, value);
}

int CurrentView()
{
    return g.currentView;
}

const char *CurrentCar()
{
    Read();
    return g.car;
}

float GetCar(int view, int adjustment)
{
    Read();
    if (!g.car[0] || view < 0 || view >= kViews || adjustment < 0 || adjustment >= kAdjustments)
        return 0.0f;
    return g.carValues[view][adjustment];
}

void SetCar(int view, int adjustment, float value)
{
    Read();
    if (!g.car[0] || view < 0 || view >= kViews || adjustment < 0 || adjustment >= kAdjustments)
        return;
    g.carValues[view][adjustment] = value;
    char section[64], key[32];
    CarSection(g.car, section);
    Key(view, adjustment, key);
    nfs::config::WriteFloat(section, key, value);
}
} // namespace nfs::camera

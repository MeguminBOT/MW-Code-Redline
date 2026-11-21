/**
 * Frames between simulation steps (core/presentation.h).
 */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "game/platform/Platform.h"
#include "core/presentation.h"
#include "core/timing.h"

static_assert(offsetof(WorldBodyConn, ListNode) == 0x10, "WorldBodyConn layout");
static_assert(offsetof(WorldBodyConn, BodyMatrix) == 0x1C, "WorldBodyConn layout");
static_assert(offsetof(GameFlowManager, CurrentGameFlowState) == 0x20, "GameFlowManager layout");
static_assert(sizeof(eView) == 0x70, "eView layout");
static_assert(offsetof(CameraParams, Position) == 0x40 && offsetof(CameraParams, DummyAngle) == 0xD0,
              "CameraParams layout");

namespace nfs::present
{
namespace
{
const int kMaxBodies = 512;
const int kMaxCameras = 8;
const int kMaxReadings = 16;
const int kMaxReadingValues = 4;
/** eViews holds this many views. */
const int kViewCount = 23;
/** Sim::State while the simulation runs. */
const int kSimRunning = 3;
/** Moving farther than this or turning more than about 60 degrees (cosine of half the angle) between two steps is a
 * teleport or a camera cut, which is shown as is rather than blended. */
const float kSnapDistance = 30.0f;
const float kSnapHalfAngleCos = 0.866f;
/** The most render time one frame may advance. */
const float kMaxRenderStep = 0.1f;

struct Quaternion
{
    float x, y, z, w;
};

struct BodyPose
{
    bMatrix4 *body;
    bMatrix4 previous;
    bMatrix4 current;
    bMatrix4 shown;
    bool has_previous;
    bool applied;
};

struct CameraPose
{
    Camera *camera;
    CameraParams previous;
    CameraParams current;
    CameraParams shown;
    bool has_previous;
    bool applied;
};

/** A HUD reading of the simulation at the last two steps. */
struct Reading
{
    const void *key;
    unsigned int tick;
    float previous[kMaxReadingValues];
    float current[kMaxReadingValues];
};

struct State
{
    Reading readings[kMaxReadings];
    int next_reading;
    BodyPose bodies[kMaxBodies];
    BodyPose last_bodies[kMaxBodies];
    int body_count;
    CameraPose cameras[kMaxCameras];
    CameraPose last_cameras[kMaxCameras];
    int camera_count;
    /** Simulation time the render connections were last updated to. */
    float render_time;
    bool has_render_time;
};

State g_state;

float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float Length(const bVector4 &v)
{
    return static_cast<float>(sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
}

/** The rotation of rows 0-2 of `m` (each divided by its length in `scale`). */
Quaternion RotationOf(const bMatrix4 &m, float scale[3])
{
    float r[3][3];
    for (int i = 0; i < 3; ++i)
    {
        scale[i] = Length(m[i]);
        const float inv = scale[i] > 0.0f ? 1.0f / scale[i] : 0.0f;
        r[i][0] = m[i].x * inv;
        r[i][1] = m[i].y * inv;
        r[i][2] = m[i].z * inv;
    }
    Quaternion q;
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f)
    {
        const float s = static_cast<float>(sqrt(trace + 1.0f)) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r[2][1] - r[1][2]) / s;
        q.y = (r[0][2] - r[2][0]) / s;
        q.z = (r[1][0] - r[0][1]) / s;
    }
    else if (r[0][0] > r[1][1] && r[0][0] > r[2][2])
    {
        const float s = static_cast<float>(sqrt(1.0f + r[0][0] - r[1][1] - r[2][2])) * 2.0f;
        q.w = (r[2][1] - r[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[0][2] + r[2][0]) / s;
    }
    else if (r[1][1] > r[2][2])
    {
        const float s = static_cast<float>(sqrt(1.0f + r[1][1] - r[0][0] - r[2][2])) * 2.0f;
        q.w = (r[0][2] - r[2][0]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (r[1][2] + r[2][1]) / s;
    }
    else
    {
        const float s = static_cast<float>(sqrt(1.0f + r[2][2] - r[0][0] - r[1][1])) * 2.0f;
        q.w = (r[1][0] - r[0][1]) / s;
        q.x = (r[0][2] + r[2][0]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

/** Writes the rotation `q`, rows scaled by `scale`, into rows 0-2 of `m` (their w stays). */
void SetRotation(bMatrix4 &m, const Quaternion &q, const float scale[3])
{
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    const float r[3][3] = {{1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy)},
                           {2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx)},
                           {2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy)}};
    for (int i = 0; i < 3; ++i)
    {
        m[i].x = r[i][0] * scale[i];
        m[i].y = r[i][1] * scale[i];
        m[i].z = r[i][2] * scale[i];
    }
}

/** Normalized linear blend along the shorter arc; false when the turn is too large to be motion. */
bool BlendRotation(const Quaternion &a, Quaternion b, float t, Quaternion &out)
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f)
    {
        b = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    if (dot < kSnapHalfAngleCos)
        return false;
    out = {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)};
    const float length = static_cast<float>(sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w));
    const float inv = length > 0.0f ? 1.0f / length : 0.0f;
    out = {out.x * inv, out.y * inv, out.z * inv, out.w * inv};
    return true;
}

bool Near(const bVector3 &a, const bVector3 &b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz <= kSnapDistance * kSnapDistance;
}

bVector3 Lerp(const bVector3 &a, const bVector3 &b, float t)
{
    return bVector3(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t));
}

/** An object's world matrix (rotation rows, position in row 3) between two steps. */
bool BlendWorldMatrix(const bMatrix4 &a, const bMatrix4 &b, float t, bMatrix4 &out)
{
    const bVector3 pa(a.v3.x, a.v3.y, a.v3.z), pb(b.v3.x, b.v3.y, b.v3.z);
    float scale_a[3], scale_b[3];
    Quaternion q;
    if (!Near(pa, pb) || !BlendRotation(RotationOf(a, scale_a), RotationOf(b, scale_b), t, q))
        return false;
    out = b;
    const float scale[3] = {Lerp(scale_a[0], scale_b[0], t), Lerp(scale_a[1], scale_b[1], t),
                            Lerp(scale_a[2], scale_b[2], t)};
    SetRotation(out, q, scale);
    const bVector3 p = Lerp(pa, pb, t);
    out.v3.x = p.x;
    out.v3.y = p.y;
    out.v3.z = p.z;
    return true;
}

/** Where a camera is, from its world-to-view matrix (row vectors: view = world * R + t, so world = -t * R^T). */
bVector3 CameraPosition(const bMatrix4 &view)
{
    const bVector4 &t = view.v3;
    return bVector3(-(t.x * view.v0.x + t.y * view.v0.y + t.z * view.v0.z),
                    -(t.x * view.v1.x + t.y * view.v1.y + t.z * view.v1.z),
                    -(t.x * view.v2.x + t.y * view.v2.y + t.z * view.v2.z));
}

bool BlendCamera(const CameraParams &a, const CameraParams &b, float t, CameraParams &out)
{
    const bVector3 pa = CameraPosition(a.Matrix), pb = CameraPosition(b.Matrix);
    float scale_a[3], scale_b[3];
    Quaternion q;
    if (!Near(pa, pb) || !BlendRotation(RotationOf(a.Matrix, scale_a), RotationOf(b.Matrix, scale_b), t, q))
        return false;
    out = b;
    SetRotation(out.Matrix, q, scale_b);
    const bVector3 p = Lerp(pa, pb, t);
    const bMatrix4 &m = out.Matrix;
    out.Matrix.v3.x = -(p.x * m.v0.x + p.y * m.v1.x + p.z * m.v2.x);
    out.Matrix.v3.y = -(p.x * m.v0.y + p.y * m.v1.y + p.z * m.v2.y);
    out.Matrix.v3.z = -(p.x * m.v0.z + p.y * m.v1.z + p.z * m.v2.z);
    out.Position = Lerp(a.Position, b.Position, t);
    out.Direction = Lerp(a.Direction, b.Direction, t);
    out.Target = Lerp(a.Target, b.Target, t);
    out.FieldOfView = static_cast<unsigned short>(Lerp(a.FieldOfView, b.FieldOfView, t) + 0.5f);
    return true;
}

float StepFraction()
{
    return Scheduler::fgScheduler ? Scheduler::fgScheduler->mStepFraction : 1.0f;
}

template <typename Fn> void ForEachBody(Fn fn)
{
    bNode *head = &WorldBodyConn::mList.HeadNode;
    for (bNode *node = head->Next; node && node != head; node = node->Next)
    {
        WorldBodyConn *conn =
            reinterpret_cast<WorldBodyConn *>(reinterpret_cast<char *>(node) - offsetof(WorldBodyConn, ListNode));
        if (conn->BodyMatrix)
            fn(conn->BodyMatrix);
    }
}

/** The pose recorded for `body`; `hint` follows the list order, which rarely changes. */
BodyPose *FindBody(BodyPose *poses, int count, const bMatrix4 *body, int &hint)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const int begin = pass == 0 ? hint : 0, end = pass == 0 ? count : hint;
        for (int i = begin; i < end; ++i)
            if (poses[i].body == body)
            {
                hint = i + 1;
                return &poses[i];
            }
    }
    return nullptr;
}

CameraPose *FindCamera(const Camera *camera)
{
    for (int i = 0; i < g_state.camera_count; ++i)
        if (g_state.cameras[i].camera == camera)
            return &g_state.cameras[i];
    return nullptr;
}

void ApplyBodies(float fraction)
{
    int hint = 0;
    ForEachBody([&](bMatrix4 *body) {
        BodyPose *pose = FindBody(g_state.bodies, g_state.body_count, body, hint);
        if (!pose || !pose->has_previous)
            return;
        if (!BlendWorldMatrix(pose->previous, pose->current, fraction, pose->shown))
            pose->shown = pose->current;
        *body = pose->shown;
        pose->applied = true;
    });
}
} // namespace

bool Active()
{
    return timing::AboveStepRate() && !Tweak_FullSpeedMode;
}

void CaptureBodies()
{
    const int last_count = g_state.body_count;
    memcpy(g_state.last_bodies, g_state.bodies, last_count * sizeof(BodyPose));
    int count = 0, hint = 0;
    ForEachBody([&](bMatrix4 *body) {
        if (count == kMaxBodies)
            return;
        BodyPose &pose = g_state.bodies[count++];
        const BodyPose *last = FindBody(g_state.last_bodies, last_count, body, hint);
        pose.body = body;
        pose.has_previous = last != nullptr;
        if (last)
            pose.previous = last->current;
        pose.current = *body;
        pose.applied = false;
    });
    g_state.body_count = count;
}

void CaptureCameras()
{
    CameraPose *last = g_state.last_cameras;
    const int last_count = g_state.camera_count;
    memcpy(last, g_state.cameras, last_count * sizeof(CameraPose));
    g_state.camera_count = 0;
    for (int v = 0; v < kViewCount && g_state.camera_count < kMaxCameras; ++v)
    {
        Camera *camera = eViews[v].pCamera;
        if (!camera || FindCamera(camera))
            continue;
        CameraPose &pose = g_state.cameras[g_state.camera_count++];
        pose.camera = camera;
        pose.has_previous = false;
        for (int i = 0; i < last_count; ++i)
            if (last[i].camera == camera)
            {
                memcpy(&pose.previous, &last[i].current, sizeof(CameraParams));
                pose.has_previous = true;
            }
        memcpy(&pose.current, &camera->CurrentKey, sizeof(CameraParams));
        pose.applied = false;
    }
}

void AnimateRender()
{
    if (!Sim::Exists() || static_cast<int>(Sim::GetState()) != kSimRunning)
        return;
    const float fraction = StepFraction();
    const float step = Sim::GetTimeStep();
    const float time = Sim::GetTime() - (1.0f - fraction) * step;
    float dt = time - g_state.render_time;
    if (!g_state.has_render_time || dt < 0.0f || dt > kMaxRenderStep)
        dt = step;
    g_state.render_time = time;
    g_state.has_render_time = true;
    ApplyBodies(fraction);
    if (dt > 0.0f)
        RenderConn::UpdateServices(dt);
}

void ApplyCameras()
{
    if (!Active())
        return;
    const float fraction = StepFraction();
    for (int v = 0; v < kViewCount; ++v)
    {
        Camera *camera = eViews[v].pCamera;
        CameraPose *pose = camera ? FindCamera(camera) : nullptr;
        if (!pose || !pose->has_previous || pose->applied)
            continue;
        if (!BlendCamera(pose->previous, pose->current, fraction, pose->shown))
            memcpy(&pose->shown, &pose->current, sizeof(CameraParams));
        memcpy(&camera->CurrentKey, &pose->shown, sizeof(CameraParams));
        pose->applied = true;
    }
}

void Interpolate(const void *key, float *values, int count)
{
    if (!Active() || !Sim::Exists() || count > kMaxReadingValues)
        return;
    Reading *reading = nullptr;
    for (int i = 0; i < kMaxReadings && !reading; ++i)
        if (g_state.readings[i].key == key)
            reading = &g_state.readings[i];
    const unsigned int tick = Sim::GetTick();
    if (!reading)
    {
        reading = &g_state.readings[g_state.next_reading];
        g_state.next_reading = (g_state.next_reading + 1) % kMaxReadings;
        reading->key = key;
        reading->tick = tick;
        memcpy(reading->previous, values, count * sizeof(float));
    }
    else if (reading->tick != tick)
    {
        memcpy(reading->previous, reading->current, count * sizeof(float));
        reading->tick = tick;
    }
    memcpy(reading->current, values, count * sizeof(float));
    // A paused simulation stays where it stopped; the fraction would only make the readings swing.
    if (static_cast<int>(Sim::GetState()) != kSimRunning)
        return;
    const float fraction = StepFraction();
    for (int i = 0; i < count; ++i)
        values[i] = Lerp(reading->previous[i], reading->current[i], fraction);
}

void Restore()
{
    for (int v = 0; v < kViewCount; ++v)
    {
        Camera *camera = eViews[v].pCamera;
        CameraPose *pose = camera ? FindCamera(camera) : nullptr;
        if (!pose || !pose->applied)
            continue;
        if (!memcmp(&camera->CurrentKey, &pose->shown, sizeof(CameraParams)))
            memcpy(&camera->CurrentKey, &pose->current, sizeof(CameraParams));
        pose->applied = false;
    }
    int hint = 0;
    ForEachBody([&](bMatrix4 *body) {
        BodyPose *pose = FindBody(g_state.bodies, g_state.body_count, body, hint);
        if (!pose || !pose->applied)
            return;
        if (!memcmp(body, &pose->shown, sizeof(bMatrix4)))
            *body = pose->current;
        pose->applied = false;
    });
}
} // namespace nfs::present

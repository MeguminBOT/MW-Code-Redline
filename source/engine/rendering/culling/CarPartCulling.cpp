/**
 * CarPartCuller::CullParts (original zWorld 0x7422d0): which of a car's cullable parts (the interior and the like, 11
 * of them) the camera can see, from planes in car space. Run for every car each frame.
 *
 * The planes are a table in the exe (0x905048): per part a kind, up to three planes {normal, unused} and their
 * distances. Kind 0: hidden when the eye is in front of every plane; kind 1: hidden as soon as it is in front of one.
 * The first two parts' first plane moves with the steering angle; parts 4 and 5 test the eye turned by it. In the
 * front end (game flow state 3) every part shows.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
struct Plane
{
    float x, y, z, unused;
};

struct CullablePart
{
    int kind;
    int planeCount;
    Plane planes[3];
    float distance[3];
    char unused_44[0x50 - 0x44];
};
static_assert(sizeof(CullablePart) == 0x50, "the cull table's entries");

/** CarPartCuller's entries, one per cullable part. */
struct PartState
{
    float position[3];
    float unused_c;
    int culled;
    int shown;
    bool visible;
    char unused_19[0x20 - 0x19];
};
static_assert(sizeof(PartState) == 0x20, "CarPartCuller's entries");

CullablePart *const kParts = reinterpret_cast<CullablePart *>(0x905048);
const int kPartCount = 11;
/** Steering angle units per unit of distance the first two parts' first plane moves. */
const float &kAngleScale = *reinterpret_cast<const float *>(0x9032f0);
/** Planes whose normals the table holds unnormalized (a debug setting). */
const int &kNormalizePlanes = *reinterpret_cast<const int *>(0x9b0910);
const int &kGameFlowState = *reinterpret_cast<const int *>(0x925e90);
const int kGameFlowInFrontEnd = 3;
} // namespace

void CarPartCuller::CullParts(bVector3 *eye, unsigned short angle)
{
    const float *eyeAt = reinterpret_cast<const float *>(eye);
    float turned[4] = {eyeAt[0], eyeAt[1], eyeAt[2], 0.0f};
    if (angle)
    {
        bMatrix4 turn;
        float *m = reinterpret_cast<float *>(&turn);
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5) ? 0.0f : 1.0f;
        eRotateZ(&turn, &turn, static_cast<unsigned short>(-angle));
        eMulVector(reinterpret_cast<bVector3 *>(turned), &turn, eye);
    }
    const unsigned int steer = angle > 0x8000 ? 0x10000u - angle : angle;
    PartState *parts = reinterpret_cast<PartState *>(this);
    for (int k = 0; k < kPartCount; ++k)
    {
        CullablePart &t = kParts[k];
        PartState &part = parts[k];
        const float saved = t.distance[0];
        bool visible = true;
        if (kGameFlowState != kGameFlowInFrontEnd)
        {
            const float *from = (k == 4 || k == 5) ? turned : eyeAt;
            if (k < 2)
                t.distance[0] += static_cast<float>(steer) / kAngleScale;
            if (t.planeCount > 0)
            {
                const float dx = from[0] - part.position[0], dy = from[1] - part.position[1],
                            dz = from[2] - part.position[2];
                int i = 0;
                for (; i < t.planeCount; ++i)
                {
                    bVector3 n;
                    float *nAt = reinterpret_cast<float *>(&n);
                    nAt[0] = t.planes[i].x;
                    nAt[1] = t.planes[i].y;
                    nAt[2] = t.planes[i].z;
                    if (kNormalizePlanes)
                        bNormalize(&n, &n);
                    const bool front = nAt[0] * dx + nAt[1] * dy + nAt[2] * dz - t.distance[i] >= 0.0f;
                    if (t.kind == 0 && !front)
                        break;
                    if (t.kind == 1 && front)
                    {
                        visible = false;
                        break;
                    }
                }
                if (t.kind == 0 && i == t.planeCount)
                    visible = false;
            }
        }
        part.visible = visible;
        t.distance[0] = saved;
        if (visible)
            ++part.shown;
        else
            ++part.culled;
    }
}

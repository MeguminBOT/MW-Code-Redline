/**
 * The menus' navigation between buttons (original: zFEng FEButtonMap::ComputeButtonLocation 0x5b3df0, GetButtonFrom
 * 0x5b9640). Exactly the original's choice: the button whose place scores lowest from the current one in the pushed
 * direction, scored by the distance plus 200 for each part the direction's agreement (squared, 1 at best) falls short,
 * and nothing under a quarter's agreement. When nothing scores under 1500, the wrapping passes look again from a screen
 * width (640) or height (480) beyond the current button, the ones the package's wrap mode allows (1 sideways, 2 up and
 * down).
 *
 * FEButtonMap: +0 its buttons, +4 their count. A button's place: its position (pData +0x1c), through the game
 * interface's transform of its context (+0x20, vtable +0x2c) when it has one.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include <math.h>

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kDisabled = 0x4000000;
const float kNearest = 0.0001f;
const float kLeastAgreement = 0.25f;
const float kDisagreement = 200.0f;
const float kNone = 1500.0f;
const float kStart = 1e30f;

struct Pass
{
    float x, y;
    unsigned int wrap;
};

const Pass kPasses[5] = {{0, 0, 3}, {640, 0, 1}, {-640, 0, 1}, {0, 480, 2}, {0, -480, 2}};

const float kDirections[8][2] = {{0.0f, -1.0f},       {0.70711f, -0.70711f}, {1.0f, 0.0f},  {0.70711f, 0.70711f},
                                 {0.0f, 1.0f},        {-0.70711f, 0.70711f}, {-1.0f, 0.0f}, {-0.70711f, -0.70711f}};

struct Place
{
    float x, y;
};

Place Locate(FEObject *o, FEGameInterface *gi)
{
    const char *data = reinterpret_cast<const char *>(o->pData);
    const unsigned short context = At<unsigned short>(o, 0x20);
    if (gi && context)
    {
        float matrix[16];
        using Transform = bool(__thiscall *)(FEGameInterface *, unsigned int, float *);
        if (reinterpret_cast<Transform>((*reinterpret_cast<void ***>(gi))[0x2c / 4])(gi, context, matrix))
        {
            float out[3] = {0, 0, 0};
            FEMultMatrix(reinterpret_cast<FEVector3 *>(out), reinterpret_cast<const FEMatrix4 *>(matrix),
                         reinterpret_cast<const FEVector3 *>(data + 0x1c));
            return {out[0], out[1]};
        }
    }
    return {At<float>(data, 0x1c), At<float>(data, 0x20)};
}
} // namespace

void FEButtonMap::ComputeButtonLocation(FEObject *o, FEGameInterface *gi, FEVector2 &out)
{
    const Place p = Locate(o, gi);
    reinterpret_cast<float *>(&out)[0] = p.x;
    reinterpret_cast<float *>(&out)[1] = p.y;
}

FEObject *FEButtonMap::GetButtonFrom(FEObject *from, int direction, FEGameInterface *gi, FEButtonWrapMode wrap)
{
    FEObject **buttons = At<FEObject **>(this, 0);
    const unsigned int count = At<unsigned int>(this, 4);
    const Place at = Locate(from, gi);
    const float dirX = kDirections[direction][0], dirY = kDirections[direction][1];
    float best = kStart;
    unsigned int chosen = 0;
    for (int pass = 0; pass < 5; ++pass)
    {
        if (pass && !(kPasses[pass].wrap & static_cast<unsigned int>(wrap)))
            continue;
        const float startX = at.x + kPasses[pass].x;
        const float startY = at.y + kPasses[pass].y;
        for (unsigned int i = 0; i < count; ++i)
        {
            FEObject *b = buttons[i];
            if ((b->Flags & kDisabled) || b == from)
                continue;
            const Place to = Locate(b, gi);
            const float dx = to.x - startX, dy = to.y - startY;
            const float distance = sqrtf(dx * dx + dy * dy);
            if (distance < kNearest)
                continue;
            const float inverse = 1.0f / distance;
            float agreement = dy * inverse * dirY + dx * inverse * dirX;
            if (agreement >= 0.0f)
                agreement *= agreement;
            const float score = agreement < kLeastAgreement ? kNone : (1.0f - agreement) * kDisagreement + distance;
            if (score < best)
            {
                best = score;
                chosen = i;
            }
        }
    }
    return best < kNone ? buttons[chosen] : nullptr;
}

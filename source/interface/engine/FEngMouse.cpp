/**
 * Whether the mouse is over an object (original: zFe FEngGet2DExtentsForMouse 0x542360, FEngTestForIntersection
 * 0x54e760; FEngine::UpdateMouseState 0x5b0940, the clicks 0x54e890 and 0x54e8e0, the box test 0x5158b0). A rectangle is left, top, right, bottom; a group (type 5) spans its children, each placed from the
 * group's centre; types 1-12 but 5 and 8 are boxes. An FEObjectMouseState: +0 its object, +4/+8 where it was
 * grabbed, +0xc bit 0 over, bit 1 left button, bit 2 right button (as last seen); FEngine +0x16 the buttons down,
 * +0x108 the game interface (+0x48 hit test).
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "interface/engine/mouse.h"

namespace
{
const int kGroup = 5;
const float kFar = 40000.0f;
const unsigned int kHidden = 0x10000000;
const unsigned int kMouseOff = 0x4000000;
const unsigned int kEnter = 0x13f4bd45;
const unsigned int kOver = 0xb30d0683;
const unsigned int kLeave = 0xb30793c1;
const unsigned int kLeftHeld = 0x1e646b2e;
const unsigned int kLeftDown = 0xf459b307;
const unsigned int kLeftUp = 0x7eabca56;
const unsigned int kRightHeld = 0x0da2f4e1;
const unsigned int kRightDown = 0xce59c3da;
const unsigned int kRightUp = 0x98adf589;

const int *const kMouseX = reinterpret_cast<const int *>(0x987f54);
const int *const kMouseY = reinterpret_cast<const int *>(0x987f58);
const bool *const kMousePressed = reinterpret_cast<const bool *>(0x987f71);
const bool *const kMouseReleasedNow = reinterpret_cast<const bool *>(0x987f72);
using GetMousePos = void(__cdecl *)(float *, float *);
const GetMousePos kGetMousePos = reinterpret_cast<GetMousePos>(0x515960);

template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}
} // namespace

bool __cdecl Get2DExtentsForMouse(FEObject *o, float *rect, float dx, float dy);
NFS_REPLACE(0x542360, Get2DExtentsForMouse)
NFS_REPLACE(0x54e890, FEngMousePressedOver)
NFS_REPLACE(0x54e8e0, FEngMouseReleasedOver)

bool __cdecl FEngMousePressedOver(FEObject *o)
{
    return *kMousePressed &&
           FEngTestForIntersection(static_cast<float>(*kMouseX) - 320.0f, static_cast<float>(*kMouseY) - 240.0f, o);
}

bool __cdecl FEngMouseReleasedOver(FEObject *o)
{
    return *kMouseReleasedNow &&
           FEngTestForIntersection(static_cast<float>(*kMouseX) - 320.0f, static_cast<float>(*kMouseY) - 240.0f, o);
}

bool FEngMouseReleased()
{
    return *kMouseReleasedNow;
}

void FEngGetMousePos(float *x, float *y)
{
    kGetMousePos(x, y);
}

bool __cdecl Get2DExtentsForMouse(FEObject *o, float *rect, float dx, float dy)
{
    if (!o)
        return false;
    const int type = static_cast<int>(o->Type);
    if (type == kGroup)
    {
        for (FEObject *child = *reinterpret_cast<FEObject **>(reinterpret_cast<char *>(o) + 0x64); child;
             child = *reinterpret_cast<FEObject **>(reinterpret_cast<char *>(child) + 4))
        {
            float r[4] = {kFar, kFar, -kFar, -kFar};
            float cx, cy, cx2, cy2;
            FEngGetCenter(o, cx, cy);
            FEngGetCenter(o, cx2, cy2);
            if (Get2DExtentsForMouse(child, r, cx2 + dx, cy + dy))
            {
                if (r[0] < rect[0])
                    rect[0] = r[0];
                if (r[2] > rect[2])
                    rect[2] = r[2];
                if (r[1] < rect[1])
                    rect[1] = r[1];
                if (r[3] > rect[3])
                    rect[3] = r[3];
            }
        }
        return true;
    }
    if (type < 1 || type > 12 || type == 8)
        return false;
    FEngGetTopLeft(o, rect[0], rect[1]);
    FEngGetBottomRight(o, rect[2], rect[3]);
    rect[0] = dx + rect[0];
    rect[2] = dx + rect[2];
    rect[1] = dy + rect[1];
    rect[3] = dy + rect[3];
    return true;
}

bool FEngTestForIntersection(float x, float y, FEObject *o)
{
    if (!o)
        return false;
    float r[4] = {kFar, kFar, -kFar, -kFar};
    Get2DExtentsForMouse(o, r, 0.0f, 0.0f);
    if (r[2] < r[0])
    {
        const float t = r[2];
        r[2] = r[0];
        r[0] = t;
    }
    if (r[3] < r[1])
    {
        const float t = r[3];
        r[3] = r[1];
        r[1] = t;
    }
    return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

void FEngine::UpdateMouseState(FEPackage *package, FEObjectMouseState *state, float x, float y)
{
    FEObject *o = At<FEObject *>(state, 0);
    if (o && (o->Flags & kHidden) && (o->Flags & kMouseOff))
        return;
    float cx, cy;
    FEngGetCenter(o, cx, cy);
    const float hx = x - (At<float>(state, 4) - cx);
    const float hy = y - (At<float>(state, 8) - cy);
    void *game = At<void *>(this, 0x108);
    const bool over = reinterpret_cast<bool(__thiscall *)(void *, float, float, FEObject *)>(
        (*reinterpret_cast<void ***>(game))[0x48 / 4])(game, hx, hy, o);
    const unsigned short buttons = At<unsigned short>(this, 0x16);
    const bool left = (buttons & 1) != 0;
    const bool right = (buttons & 2) != 0;
    unsigned int &flags = At<unsigned int>(state, 0xc);
    const bool wasOver = (flags & 1) != 0;
    const bool wasLeft = (flags & 2) != 0;
    const bool wasRight = (flags & 4) != 0;
    const char *name = At<const char *>(package, 0xc);
    cFEng *fe = cFEng::mInstance;
    if (over)
        fe->QueuePackageMessage(wasOver ? kOver : kEnter, name, o);
    else if (wasOver)
        fe->QueuePackageMessage(kLeave, name, o);
    if (left)
    {
        if (wasLeft)
            fe->QueuePackageMessage(kLeftHeld, name, o);
        else if (over)
            fe->QueuePackageMessage(kLeftDown, name, o);
    }
    else if (wasLeft && over)
        fe->QueuePackageMessage(kLeftUp, name, o);
    if (right)
    {
        if (wasRight)
            fe->QueuePackageMessage(kRightHeld, name, o);
        else if (over)
            fe->QueuePackageMessage(kRightDown, name, o);
    }
    else if (wasRight && over)
        fe->QueuePackageMessage(kRightUp, name, o);
    flags = over ? flags | 1 : flags & ~1u;
    flags = left ? flags | 2 : flags & ~2u;
    flags = right ? flags | 4 : flags & ~4u;
}

bool FEngTestForIntersection(float x, float y, const bVector2 &at, const bVector2 &size)
{
    float left = at.x, right = at.x + size.x;
    float top = at.y, bottom = size.y + at.y;
    if (right < left)
    {
        const float t = left;
        left = right;
        right = t;
    }
    if (bottom < top)
    {
        const float t = top;
        top = bottom;
        bottom = t;
    }
    return x >= left && x <= right && y >= top && y <= bottom;
}

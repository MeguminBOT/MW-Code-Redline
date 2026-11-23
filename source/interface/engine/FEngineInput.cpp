/**
 * The front-end engine's pads (original: zFEng FEngine::ProcessPadsForPackage 0x5c20a0).
 *
 * FEngine: +4 the pads (FEJoyPad, 0x88 each: the buttons a frame before, the buttons now, then how long each of the 32
 * buttons has been held), +0x28 the directions repeating fast, +0x2c the directions repeating, +0x30 the buttons whose
 * hold the update starts again, +0x34 a repeat time per button (19), +0x80 the object each button went down on (19),
 * +0xcc the button map's wrap mode, +0xd0 the pad count, +0x108 the game interface.
 * Buttons: 0-3 the directions, 4 accept (6 too, when the package's +0x17 says so), 5-18 the others. Their messages:
 * pressed 0x8f56e8, held 0x8f5718 (7 and 8 only), released 0x8f5740 (accept's 0x936a6a7f when it comes up on the
 * object it went down on); the directions' table 0x8f56c8 (8 entries: a button, a second one or 0xff, and the button
 * map's direction) and their messages 0x8f5670.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const int kPadBytes = 0x88;
const int kButtons = 19;
const int kFirstButton = 4;
const unsigned int kAccept = 0x0c407210;
const unsigned int kPackageAccept = 0x406415e3;
const unsigned int kAcceptUp = 0x936a6a7f;
const unsigned int kUnhighlight = 0x55d1e635;
const unsigned int kHighlight = 0xabc08912;
const unsigned int kNavigationDone = 0x104;
const unsigned int kNoAutoNavigation = 0x80000;
const unsigned int kRepeatSlow = 0x140;
const unsigned int kRepeatFast = 0x78;
FEObject *const kToPackage = reinterpret_cast<FEObject *>(0xfffffffd);
FEObject *const kToGame = reinterpret_cast<FEObject *>(0xfffffffb);

const unsigned int *const kPressedMessages = reinterpret_cast<const unsigned int *>(0x8f56e8);
const unsigned int *const kHeldMessages = reinterpret_cast<const unsigned int *>(0x8f5718);
const unsigned int *const kReleasedMessages = reinterpret_cast<const unsigned int *>(0x8f5740);
const unsigned int *const kDirectionMessages = reinterpret_cast<const unsigned int *>(0x8f5670);
const unsigned char *const kImpulses = reinterpret_cast<const unsigned char *>(0x8f56c8);

FEObject *Navigate(FEngine *e, FEPackage *p, FEObject *from, unsigned int direction)
{
    return reinterpret_cast<FEButtonMap *>(reinterpret_cast<char *>(p) + 0x90)
        ->GetButtonFrom(from, static_cast<int>(direction), At<FEGameInterface *>(e, 0x108),
                        static_cast<FEButtonWrapMode>(At<int>(e, 0xcc)));
}

unsigned int *Pad(FEngine *e, unsigned int index)
{
    return reinterpret_cast<unsigned int *>(At<char *>(e, 4) + index * kPadBytes);
}

unsigned int HeldFor(const unsigned int *pad, unsigned int buttons)
{
    unsigned int least = 0xffffffffu;
    for (int b = 0; b < 32; ++b)
        if ((buttons & (1u << b)) && pad[2 + b] <= least)
            least = pad[2 + b];
    return least;
}

bool HasResponse(const void *responses, unsigned int message)
{
    for (const char *r = static_cast<const char *>(responses); r; r = At<const char *>(r, 4))
        if (At<unsigned int>(r, 0xc) == message)
            return true;
    return false;
}

const char *FindResponse(const void *responses, unsigned int message)
{
    for (const char *r = static_cast<const char *>(responses); r; r = At<const char *>(r, 4))
        if (At<unsigned int>(r, 0xc) == message)
            return r;
    return nullptr;
}

const void *ObjectResponses(FEObject *o)
{
    return o->FirstResponse;
}

const void *PackageResponses(FEPackage *p)
{
    return At<const void *>(p, 0x5c);
}

struct Edges
{
    unsigned int held[kButtons] = {};
    unsigned char pressedPads[kButtons] = {};
    unsigned char releasedPads[kButtons] = {};
    unsigned char heldPads[kButtons] = {};
};

/** A pressed message to the current button when it answers it, else to the package when that does: true if sent. */
bool SendPressed(FEngine *e, FEPackage *p, FEObject *button, unsigned int message, unsigned int pads)
{
    if (button && HasResponse(ObjectResponses(button), message))
    {
        e->QueueMessage(message, nullptr, p, button, pads);
        e->QueueMessage(message, button, p, kToGame, pads);
        return true;
    }
    if (HasResponse(PackageResponses(p), message))
    {
        e->QueueMessage(message, nullptr, p, kToPackage, pads);
        e->QueueMessage(message, nullptr, p, kToGame, pads);
        return true;
    }
    return false;
}
} // namespace

void __fastcall ProcessMessageQueue(FEngine *e, void *);
void __fastcall ProcessPadsForPackage(FEngine *e, void *, FEPackage *p);
NFS_REPLACE(0x5c20a0, ProcessPadsForPackage)

void __fastcall ProcessPadsForPackage(FEngine *e, void *, FEPackage *p)
{
    const unsigned int mask = At<unsigned int>(p, 0x20);
    const unsigned int pads = At<unsigned int>(e, 0xd0);
    if (!mask || !pads)
        return;
    bool any = false;
    for (unsigned int i = 0; i < pads; ++i)
        if (mask & (1u << i))
        {
            const unsigned int *pad = Pad(e, i);
            any |= pad[1] != 0 || pad[0] != 0;
        }
    if (!any)
        return;
    const bool &input = At<bool>(p, 0x28);
    FEObject **downOn = &At<FEObject *>(e, 0x80);
    Edges s;

    for (unsigned int b = kFirstButton; b < kButtons && input; ++b)
    {
        const unsigned int bit = 1u << b;
        const unsigned int padMask = At<unsigned int>(p, 0x20);
        unsigned int pressed = 0, released = 0, held = 0;
        for (unsigned int i = 0; i < pads; ++i)
        {
            if (!(padMask & (1u << i)))
                continue;
            const unsigned int *pad = Pad(e, i);
            const bool now = (pad[1] & bit) == bit, before = (pad[0] & bit) == bit;
            if (now && !before)
            {
                pressed |= bit;
                s.pressedPads[b] |= static_cast<unsigned char>(1u << i);
            }
            if (!now && before)
            {
                released |= bit;
                s.releasedPads[b] |= static_cast<unsigned char>(1u << i);
            }
            if (now && before)
            {
                held |= bit;
                const unsigned int t = HeldFor(pad, bit);
                if (t >= s.held[b])
                    s.held[b] = t;
                s.heldPads[b] |= static_cast<unsigned char>(1u << i);
            }
        }
        if (b == kFirstButton && At<char>(p, 0x17))
            for (unsigned int i = 0; i < pads; ++i)
            {
                if (!(padMask & (1u << i)))
                    continue;
                const unsigned int *pad = Pad(e, i);
                const bool now = (pad[1] & 0x40) != 0, before = (pad[0] & 0x40) != 0;
                if (now && !before)
                {
                    pressed |= 0x10;
                    s.pressedPads[b] |= static_cast<unsigned char>(1u << i);
                }
                if (!now && before)
                {
                    released |= 0x10;
                    s.releasedPads[b] |= static_cast<unsigned char>(1u << i);
                }
                if (now && before)
                {
                    held |= 0x10;
                    const unsigned int t = HeldFor(pad, 0x40);
                    if (t >= s.held[b])
                        s.held[b] = t;
                    s.heldPads[b] |= static_cast<unsigned char>(1u << i);
                }
            }
        if (!held && !released && !pressed)
            continue;
        FEObject *button = At<FEObject *>(p, 0x8c);
        if (b == kFirstButton)
        {
            if (pressed & 0x10)
            {
                downOn[b] = button;
                const unsigned int from = s.pressedPads[b];
                if (button && HasResponse(ObjectResponses(button), kAccept))
                {
                    e->QueueMessage(kAccept, nullptr, p, button, from);
                    e->QueueMessage(kAccept, button, p, kToGame, from);
                }
                else if (HasResponse(PackageResponses(p), kPackageAccept))
                {
                    e->QueueMessage(kPackageAccept, nullptr, p, kToPackage, from);
                    e->QueueMessage(kPackageAccept, nullptr, p, kToGame, from);
                }
            }
        }
        else
        {
            if ((b == 7 || b == 8) && (held & bit))
                SendPressed(e, p, button, kHeldMessages[b], s.pressedPads[b]);
            if (pressed & bit)
            {
                downOn[b] = button;
                SendPressed(e, p, button, kPressedMessages[b], s.pressedPads[b]);
            }
        }
        if (released & bit)
        {
            const unsigned int from = s.releasedPads[b];
            unsigned int message = kReleasedMessages[b];
            if (downOn[b] == button && button)
            {
                downOn[b] = nullptr;
                if (b == kFirstButton)
                    message = kAcceptUp;
                if (HasResponse(ObjectResponses(button), message))
                {
                    e->QueueMessage(message, nullptr, p, button, from);
                    e->QueueMessage(message, button, p, kToGame, from);
                }
            }
            if (HasResponse(PackageResponses(p), message))
            {
                e->QueueMessage(message, nullptr, p, kToPackage, from);
                e->QueueMessage(message, nullptr, p, kToGame, from);
            }
        }
        if (At<int>(e, 0x4114))
            ProcessMessageQueue(e, nullptr);
    }

    // The directions: pressed and held, no releases.
    unsigned int pressedDirections = 0;
    {
        const unsigned int padMask = At<unsigned int>(p, 0x20);
        for (unsigned int b = 0; b < kFirstButton && input; ++b)
        {
            const unsigned int bit = 1u << b;
            for (unsigned int i = 0; i < pads; ++i)
            {
                if (!(padMask & (1u << i)))
                    continue;
                const unsigned int *pad = Pad(e, i);
                const bool now = (pad[1] & bit) == bit, before = (pad[0] & bit) == bit;
                if (now && !before)
                {
                    pressedDirections |= bit;
                    s.pressedPads[b] |= static_cast<unsigned char>(1u << i);
                }
                if (now && before)
                {
                    const unsigned int t = HeldFor(pad, bit);
                    if (t >= s.held[b])
                        s.held[b] = t;
                    s.heldPads[b] |= static_cast<unsigned char>(1u << i);
                }
            }
        }
    }

    for (unsigned int d = 0; d < 8; ++d)
    {
        if (!input)
            return;
        FEObject *button = At<FEObject *>(p, 0x8c);
        const unsigned char *impulse = kImpulses + d * 4;
        const unsigned char a = impulse[0], second = impulse[1];
        unsigned int heldTime, pressedBit;
        unsigned char from;
        if (second == 0xff)
        {
            heldTime = s.held[a];
            pressedBit = pressedDirections >> a;
            from = static_cast<unsigned char>(s.pressedPads[a] | s.heldPads[a]);
        }
        else
        {
            heldTime = s.held[a];
            if (s.held[second] <= heldTime)
                heldTime = s.held[second];
            pressedBit = (pressedDirections >> second) & (pressedDirections >> a);
            from = static_cast<unsigned char>((s.pressedPads[second] & s.pressedPads[a]) |
                                              (s.heldPads[second] & s.heldPads[a]));
        }
        const unsigned int bit = 1u << d;
        const unsigned int repeat = (At<unsigned int>(e, 0x28) & bit) ? kRepeatFast : kRepeatSlow;
        bool fire = false;
        if (repeat <= heldTime)
        {
            if (heldTime)
                At<unsigned int>(e, 0x2c) |= bit;
            fire = true;
        }
        else if (!(pressedBit & 1))
        {
            if (!heldTime)
                At<unsigned int>(e, 0x2c) &= ~bit;
        }
        else if (!heldTime)
            fire = true;
        if (!fire)
        {
            if (At<int>(e, 0x4114))
                ProcessMessageQueue(e, nullptr);
            continue;
        }

        At<unsigned int>(e, 0x34 + a * 4) = repeat;
        if (second != 0xff)
            At<unsigned int>(e, 0x34 + second * 4) = repeat;
        s.held[a] = 0;
        At<unsigned int>(e, 0x30) |= 1u << a;
        if (second != 0xff)
        {
            s.held[second] = 0;
            At<unsigned int>(e, 0x30) |= 1u << second;
        }
        const unsigned int direction = *reinterpret_cast<const unsigned short *>(impulse + 2);
        const unsigned int message = kDirectionMessages[direction];
        if (!button)
        {
            if (HasResponse(PackageResponses(p), message))
            {
                e->QueueMessage(message, nullptr, p, kToPackage, from);
                e->QueueMessage(message, nullptr, p, kToGame, from);
            }
            return;
        }
        FEObject *next = nullptr;
        if (const char *r = FindResponse(ObjectResponses(button), message))
        {
            e->QueueMessage(message, nullptr, p, button, from);
            if (!(button->Flags & kNoAutoNavigation))
            {
                bool handled = false;
                const unsigned int count = At<unsigned int>(r, 0x10);
                const unsigned int *entries = At<const unsigned int *>(r, 0x14);
                for (unsigned int k = 0; k < count; ++k)
                    if (entries[k * 3] == kNavigationDone)
                    {
                        handled = true;
                        break;
                    }
                if (!handled)
                    next = Navigate(e, p, button, direction);
            }
        }
        else
        {
            if (!(button->Flags & kNoAutoNavigation))
                next = Navigate(e, p, button, direction);
            e->QueueMessage(message, nullptr, p, kToPackage, from);
        }
        e->QueueMessage(message, button, p, kToGame, from);
        if (!next)
            return;
        for (unsigned int b = kFirstButton; b < kButtons; ++b)
            if (downOn[b])
            {
                downOn[b] = nullptr;
                const unsigned int up = b == kFirstButton ? kAcceptUp : kReleasedMessages[b];
                if (HasResponse(ObjectResponses(button), up))
                {
                    e->QueueMessage(up, nullptr, p, button, s.releasedPads[b]);
                    e->QueueMessage(up, button, p, kToGame, s.releasedPads[b]);
                }
            }
        if (FEObject *current = At<FEObject *>(p, 0x8c))
        {
            e->QueueMessage(kUnhighlight, nullptr, p, current, 0);
            e->QueueMessage(kUnhighlight, current, p, kToGame, 0);
        }
        e->QueueMessage(kHighlight, nullptr, p, next, 0);
        e->QueueMessage(kHighlight, next, p, kToGame, 0);
        At<FEObject *>(p, 0x8c) = next;
        return;
    }
}

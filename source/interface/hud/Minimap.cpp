/**
 * The in-race minimap (original: zFe2 Minimap::Update and its element updates; SetupMinimap and the constructor still
 * run the original code): world positions become points on the scrolling track map, turned with the player's heading
 * when the map rotates, clamped to its rim, with icons for the player, cops and the helicopter, the other racers and
 * gameplay icons.
 *
 * Minimap: +0x30 the map items (+8 the image, +0xc position, +0x18 hidden), +0x38 the map, +0x3c..+0x48 its four corner
 * images, +0x8c the north indicator, +0x90 the player arrow, +0x94 the second player arrow, +0x98 the TrackInfo (its
 * +0xac, +0xb0 the map's origin, +0xb4 its scale), +0xa8 the zoom, +0xac heading in degrees, +0xb0/+0xb4 the player on
 * the map, +0xb8/+0xbc the map centre on screen, +0xc0 the cop light blink counter, +0xc4 whether the map rotates, +0xc8
 * and +0xcc the helicopter icon and its line of sight, +0xd0 eight cop icons, +0xf0 eight racer icons, +0x114 the
 * highlighted target ring, +0x118 the gameplay icons (8 per type).
 */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x567550, MinimapUpdateTrackMapArt)
NFS_REPLACE(0x5675d0, MinimapUpdateElementArt)
NFS_REPLACE(0x57a1f0, MinimapUpdateIconElement)
NFS_REPLACE(0x57a280, MinimapUpdateMiniMapItems)
NFS_REPLACE(0x594ea0, MinimapUpdatePlayer2Element)
NFS_REPLACE(0x57a110, MinimapUpdateAiRacerElements)
NFS_REPLACE(0x579e00, MinimapUpdateCopElements)
NFS_REPLACE(0x57a300, MinimapUpdateGameplayIcons)
NFS_REPLACE(0x59db50, MinimapUpdate)

void __fastcall MinimapUpdateTrackMapArt(Minimap *self);
void __fastcall MinimapUpdateElementArt(Minimap *self, void *, bVector2 *position, bVector2 *direction, FEObject *object,
                                        bool target);
void __fastcall MinimapUpdateIconElement(Minimap *self, void *, FEImage *image, GIcon *icon);
void __fastcall MinimapUpdateMiniMapItems(Minimap *self);
void __fastcall MinimapUpdatePlayer2Element(Minimap *self);
void __fastcall MinimapUpdateAiRacerElements(Minimap *self);
void __fastcall MinimapUpdateCopElements(Minimap *self, void *, void *vehicle);
void __fastcall MinimapUpdateGameplayIcons(Minimap *self, void *, IPlayer *player);
void __fastcall MinimapUpdate(Minimap *self, void *, IPlayer *player);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

struct Node
{
    Node *next;
    Node *prev;
};

/** A virtual call through the vtable of `object`, slot given as a byte offset. */
template <typename R> R Virtual(void *object, int slot)
{
    return (*reinterpret_cast<R(__thiscall ***)(void *)>(object))[slot / 4](object);
}

const int kVehicleIsActive = 0x88;
const int kVehicleSimable = 4;
const int kVehicleClass = 0x44;
const int kPursuitTargetSeen = 0x8c;
const int kPursuitChaseActive = 0x14;
const int kMaxIcons = 8;
const int kIconTypes = 17;
const float kDegreesPerAngle = 0.005493164f;
const float kRadiansPerDegree = 0.017453292f;
const float kMapRim = 0.06f;
const float kFadeStart = 0.125f;
const float kFadeEnd = 0.23f;

/** IPursuitAI and IVehicle interface handles (the address of their _IHandle functions, used as keys). */
void *const kPursuitAIHandle = reinterpret_cast<void *>(0x4038e0);
void *const kVehicleHandle = reinterpret_cast<void *>(0x4040e0);
const unsigned int kNormalScript = 0x1744b3;

void *FindInterface(void *list, void *handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(list, handle);
}

/** The chopper class key (a VehicleClass, compared by its value). */
int ChopperClass()
{
    return *reinterpret_cast<int *>(0x92c4f4);
}

int VehicleClassOf(void *vehicle)
{
    return *Virtual<int *>(vehicle, kVehicleClass);
}

char *Options()
{
    return At<char *>(*reinterpret_cast<char **>(0x91cf90), 0x10);
}

/** The gameplay icon info table entry for a type: +4 its FE database mask. */
unsigned int IconMask(int type)
{
    return reinterpret_cast<unsigned int *>(0x8f3abc)[type * 5];
}

FEObject *IconObject(Minimap *self, int type, int index)
{
    return At<FEObject *>(self, 0x118 + (index + type * 8) * 4);
}
} // namespace

void __cdecl Minimap::ConvertPos(bVector2 &world, bVector2 &map, TrackInfo *track)
{
    char *t = reinterpret_cast<char *>(track);
    map.x = (world.x - At<float>(t, 0xac)) / At<float>(t, 0xb4);
    map.y = (At<float>(t, 0xb0) - world.y) / At<float>(t, 0xb4) + 1.0f;
}

void Minimap::AdjustForWidescreen(bool widen)
{
    char *self = reinterpret_cast<char *>(this);
    float shift;
    if (widen)
    {
        shift = -120.0f;
        *reinterpret_cast<float *>(0x91cf04) = -120.0f;
        *reinterpret_cast<float *>(0x91cf0c) = -0.9375f;
    }
    else
    {
        shift = 120.0f;
        *reinterpret_cast<float *>(0x91cf04) = 0.0f;
        *reinterpret_cast<float *>(0x91cf0c) = 0.9375f;
    }
    At<float>(self, 0xb8) += shift;
    const int corners[5] = {0x3c, 0x40, 0x44, 0x48, 0x90};
    for (int offset : corners)
        At<float>(At<char *>(At<char *>(self, offset), 0x2c), 0x1c) += shift;
    At<float>(At<char *>(At<char *>(self, 0x90), 0x2c), 0x20) = At<float>(self, 0xbc);
}

void Minimap::RefreshMapItems()
{
    Node *items = &At<Node>(this, 0x30);
    for (Node *n = items->next; n != items; n = n->next)
        FEngSetInvisible(At<FEObject *>(n, 8));
    for (Node *n = items->next; n != items; n = items->next)
    {
        n->prev->next = n->next;
        n->next->prev = n->prev;
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(reinterpret_cast<void *>(0x925b30), n, 0x1c, nullptr);
    }
}

void __fastcall MinimapUpdateTrackMapArt(Minimap *self)
{
    const float heading = At<float>(self, 0xac);
    if (!At<int>(self, 0xc4))
    {
        FEngSetRotationZ(At<FEObject *>(self, 0x90), heading);
        FEngSetRotationZ(At<FEObject *>(self, 0x38), 0.0f);
        FEngSetRotationZ(At<FEObject *>(self, 0x8c), 0.0f);
        return;
    }
    FEngSetRotationZ(At<FEObject *>(self, 0x90), 0.0f);
    FEngSetRotationZ(At<FEObject *>(self, 0x38), -heading);
    FEngSetRotationZ(At<FEObject *>(self, 0x8c), -heading);
}

void __fastcall MinimapUpdateElementArt(Minimap *self, void *, bVector2 *position, bVector2 *direction, FEObject *object,
                                        bool target)
{
    char *track = At<char *>(self, 0x98);
    const float toMap = 1.0f / At<float>(track, 0xb4);
    const float mapY = (At<float>(track, 0xb0) - position->y) * toMap + 1.0f;
    const float dx = ((position->x - At<float>(track, 0xac)) * toMap - At<float>(self, 0xb0)) * At<float>(self, 0xa8);
    const float dy = (mapY - At<float>(self, 0xb4)) * At<float>(self, 0xa8);
    const float angle = At<float>(self, 0xac) * kRadiansPerDegree;
    const float s = bSin(angle);
    const float c = bSin(angle + 1.5707964f);
    float x = c * dx + s * dy;
    float y = c * dy - s * dx;
    const float distance = sqrtf(x * x + y * y);
    float alpha = 1.0f;
    if (distance > 0.0f && distance > kMapRim)
    {
        // Past the rim: pinned to it, fading further out (targets stay solid).
        const float toRim = kMapRim / distance;
        x *= toRim;
        y *= toRim;
        if (distance > kFadeStart)
            alpha = 1.0f - (distance - kFadeStart) * 9.523809f;
        if (distance > kFadeEnd)
            alpha = 0.0f;
        if (target)
            alpha = 1.0f;
    }
    else if (!(distance <= kMapRim))
    {
        FEngSetInvisible(object);
        return;
    }
    const float screenX = x * 1024.0f + At<float>(self, 0xb8);
    const float screenY = y * 1024.0f + At<float>(self, 0xbc);
    FEngSetCenter(object, screenX, screenY);
    FEngSetVisible(object);
    FEngSetRotationZ(object, static_cast<float>(bATan(direction->y, direction->x)) * kDegreesPerAngle - At<float>(self, 0xac));
    FEColor colour = FEngGetObjectColor(object);
    unsigned int packed = NFS_ORIGINAL(0x5b2d80, unsigned int(__thiscall *)(FEColor *))(&colour) & 0xffffffu;
    packed |= static_cast<unsigned int>(static_cast<unsigned char>(static_cast<int>(alpha * 255.0f))) << 24;
    FEngSetColor(object, packed);
    if (target)
    {
        FEngSetVisible(At<FEObject *>(self, 0x114));
        FEngSetCenter(At<FEObject *>(self, 0x114), screenX, screenY);
    }
}

void __fastcall MinimapUpdateIconElement(Minimap *self, void *, FEImage *image, GIcon *iconObject)
{
    char *icon = reinterpret_cast<char *>(iconObject);
    bVector2 position, direction;
    position.x = At<float>(icon, 0x10);
    position.y = At<float>(icon, 0x14);
    direction.x = 1.0f;
    direction.y = 0.0f;
    if (At<unsigned short>(icon, 0) != 0xf && !GPS_IsEngaged() && (At<unsigned char>(icon, 2) & 0x80))
        At<unsigned short>(icon, 2) &= 0xff7f;
    FEObject *object = reinterpret_cast<FEObject *>(image);
    MinimapUpdateElementArt(self, nullptr, &position, &direction, object, (At<unsigned char>(icon, 2) >> 7) != 0);
    FEngSetRotationZ(object, 0.0f);
}

void __fastcall MinimapUpdateMiniMapItems(Minimap *self)
{
    Node *items = &At<Node>(self, 0x30);
    for (Node *n = items->next; n != items; n = n->next)
    {
        FEObject *object = At<FEObject *>(n, 8);
        if (At<char>(n, 0x18))
        {
            FEngSetInvisible(object);
            continue;
        }
        FEngSetVisible(object);
        bVector2 direction;
        direction.x = 0.0f;
        direction.y = 1.0f;
        MinimapUpdateElementArt(self, nullptr, &At<bVector2>(n, 0xc), &direction, object, false);
        FEngSetRotationZ(object, 0.0f);
    }
}

void __fastcall MinimapUpdatePlayer2Element(Minimap *self)
{
    if (static_cast<int>(Sim::GetUserMode()) != 1)
        return;
    const int players = *reinterpret_cast<int *>(0x92d884);
    void *player = players ? (*reinterpret_cast<void ***>(0x92d87c))[players - 1] : nullptr;
    ISimable *simable = Virtual<ISimable *>(player, kVehicleSimable);
    bVector2 position, direction;
    GetVehicleVectors(&position, &direction, simable);
    MinimapUpdateElementArt(self, nullptr, &position, &direction, At<FEObject *>(self, 0x94), false);
}

void __fastcall MinimapUpdateAiRacerElements(Minimap *self)
{
    const bool online = NFS_ORIGINAL(0x735620, bool(__thiscall *)(void *))(reinterpret_cast<void *>(0x9b0fa0));
    char *list = reinterpret_cast<char *>(0x92cd18) + (online ? 7 : 3) * 0x60;
    int shown = 0;
    void **vehicle = At<void **>(list, 4);
    void **end = vehicle + At<int>(list, 0xc);
    for (; vehicle != end; ++vehicle)
    {
        if (!Virtual<bool>(*vehicle, kVehicleIsActive))
            continue;
        bVector2 position, direction;
        GetVehicleVectors(&position, &direction, Virtual<ISimable *>(*vehicle, kVehicleSimable));
        MinimapUpdateElementArt(self, nullptr, &position, &direction, At<FEObject *>(self, 0xf0 + shown * 4), false);
        ++shown;
    }
    for (; shown < kMaxIcons; ++shown)
        FEngSetInvisible(At<FEObject *>(self, 0xf0 + shown * 4));
}

void __fastcall MinimapUpdateCopElements(Minimap *self, void *, void *playerVehicle)
{
    int &blink = At<int>(self, 0xc0);
    const int previousBlink = blink;
    blink = previousBlink + 1;
    if (previousBlink >= 8)
        blink = 0;

    void *hud = Virtual<void *>(playerVehicle, 0xac);
    void *pursuit = Virtual<void *>(hud, 0xa0);

    int cops = 0;
    bool helicopterSeen = false;
    bool full = false;
    if (MinimapShowNonPursuitCops || (pursuit && !Virtual<bool>(pursuit, 0x2c)))
    {
        void **vehicle = *reinterpret_cast<void ***>(0x92ce9c);
        void **end = vehicle + *reinterpret_cast<int *>(0x92cea4);
        for (; vehicle != end; ++vehicle)
        {
            void *cop = *vehicle;
            if (!Virtual<bool>(cop, kVehicleIsActive))
                continue;
            if (cops >= kMaxIcons)
            {
                full = true;
                break;
            }
            bVector2 position, direction;
            GetVehicleVectors(&position, &direction, Virtual<ISimable *>(cop, kVehicleSimable));
            void *ai = FindInterface(At<void *>(cop, 4), kPursuitAIHandle);
            const bool chopper = VehicleClassOf(cop) == ChopperClass();
            FEObject *icon = At<FEObject *>(self, 0xd0 + cops * 4);
            if (chopper)
            {
                if (MinimapShowNonPursuitCops || (ai && Virtual<bool>(ai, kPursuitTargetSeen)))
                {
                    FEObject *sight = At<FEObject *>(self, 0xcc);
                    if (Virtual<int>(ai, 0x24) && Virtual<float>(ai, 0x74) <= 0.25f)
                    {
                        if (!FEngIsScriptSet(sight, FEHashUpper("TRACKING")))
                            FEngSetScript(sight, FEHashUpper("TRACKING"), true);
                    }
                    else if (!FEngIsScriptSet(sight, kNormalScript))
                        FEngSetScript(sight, kNormalScript, true);
                }
                MinimapUpdateElementArt(self, nullptr, &position, &direction, At<FEObject *>(self, 0xc8), false);
                MinimapUpdateElementArt(self, nullptr, &position, &direction, At<FEObject *>(self, 0xcc), false);
                helicopterSeen = true;
            }
            else if (MinimapShowNonPursuitCops || (ai && Virtual<bool>(ai, kPursuitTargetSeen) && MinimapShowPursuitCops))
                MinimapUpdateElementArt(self, nullptr, &position, &direction, icon, false);
            else
                FEngSetInvisible(icon);

            if (VehicleClassOf(cop) != ChopperClass())
            {
                unsigned int colour = 0xffcccccc;
                if (ai && Virtual<bool>(ai, kPursuitChaseActive))
                {
                    if (blink <= 2)
                        colour = 0xff0000ff;
                    else if (blink >= 4 && blink <= 6)
                        colour = 0xffa00000;
                }
                FEngSetColor(icon, colour);
            }
            if (VehicleClassOf(cop) != ChopperClass())
                ++cops;
        }
    }
    if (!full)
        for (int i = cops; i < kMaxIcons; ++i)
            FEngSetInvisible(At<FEObject *>(self, 0xd0 + i * 4));
    if (!helicopterSeen)
    {
        FEngSetInvisible(At<FEObject *>(self, 0xc8));
        FEngSetInvisible(At<FEObject *>(self, 0xcc));
    }
}

void __fastcall MinimapUpdateGameplayIcons(Minimap *self, void *, IPlayer *player)
{
    FEngSetInvisible(At<FEObject *>(self, 0x114));
    int used[kIconTypes] = {};
    GIcon *icons[200];
    GManager *manager = *reinterpret_cast<GManager **>(0x91e00c);
    const int count = manager->GatherVisibleIcons(icons, player);
    for (int i = 0; i < count; ++i)
    {
        const int type = At<unsigned short>(icons[i], 0);
        const unsigned int mask = IconMask(type);
        if (!mask || used[type] >= kMaxIcons || !(At<unsigned int>(Options(), 0x40) & mask))
            continue;
        FEObject *object = IconObject(self, type, used[type]);
        ++used[type];
        if (object)
            MinimapUpdateIconElement(self, nullptr, reinterpret_cast<FEImage *>(object), icons[i]);
    }
    for (int type = 0; type < kIconTypes; ++type)
        for (int j = used[type]; j < kMaxIcons; ++j)
            if (FEObject *object = IconObject(self, type, j))
                FEngSetInvisible(object);
}

/** Minimap::Update (virtual, thiscall). */
void __fastcall MinimapUpdate(Minimap *self, void *, IPlayer *player)
{
    if (!((At<unsigned int>(self, 0x20) & At<unsigned int>(self, 0x18)) | (At<unsigned int>(self, 0x24) & At<unsigned int>(self, 0x1c))))
        return;
    if (!player || !Virtual<void *>(player, 4))
        return;

    At<int>(self, 0xc4) = 1;
    if (static_cast<int>(Sim::GetUserMode()) == 1)
        At<int>(self, 0xc4) = 0;
    else
    {
        char *race = *reinterpret_cast<char **>(0x91e000);
        if (!Options()[At<int>(race, 0x1968) ? 0x3c : 0x3d])
            At<int>(self, 0xc4) = 0;
    }
    self->SetupMinimap(player);
    ISimable *simable = Virtual<ISimable *>(player, 4);
    bVector2 position, direction;
    GetVehicleVectors(&position, &direction, simable);
    void *vehicle = FindInterface(At<void *>(simable, 4), kVehicleHandle);
    float speed = 0.0f;
    if (vehicle)
        speed = fabsf(Virtual<float>(vehicle, 0x90));

    char *track = At<char *>(self, 0x98);
    At<float>(self, 0xac) = static_cast<float>(bATan(direction.y, direction.x)) * kDegreesPerAngle;
    At<float>(self, 0xb0) = (position.x - At<float>(track, 0xac)) / At<float>(track, 0xb4);
    At<float>(self, 0xb4) = (At<float>(track, 0xb0) - position.y) / At<float>(track, 0xb4) + 1.0f;
    if (speed > MinimapMaxSpeed)
        speed = MinimapMaxSpeed;
    else if (speed < 0.0f)
        speed = 0.0f;
    float zoom = 1.0f - (1.0f / MinimapMaxSpeed) * speed;
    At<float>(self, 0xa8) = zoom;
    if (zoom < 1.0f)
        At<float>(self, 0xa8) = 1.0f;
    MinimapUpdateTrackMapArt(self);
    if (!At<int>(self, 0xc4))
        At<float>(self, 0xac) = 0.0f;
    MinimapUpdateCopElements(self, nullptr, vehicle);
    MinimapUpdateAiRacerElements(self);
    MinimapUpdatePlayer2Element(self);
    MinimapUpdateMiniMapItems(self);
    MinimapUpdateGameplayIcons(self, nullptr, player);
}

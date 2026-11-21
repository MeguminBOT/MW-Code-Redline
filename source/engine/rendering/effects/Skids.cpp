/**
 * Skid marks (original: zTrack SkidSegment, SkidSet, SkidMaker, CreateNewSkidSet, InitSkids; zWorld
 * KillSkidsOnRaceRestart): each tyre's SkidMaker lays a SkidSet of up to eight segments, merging a segment into the
 * last while the mark runs straight and short, and starting a new set (continuing from the last point) when the set is
 * full or the surface type changes. Sets live in a list (0x9aa4d0, newest first) and in the clan (world area) they
 * start in; when the pool is full the oldest set goes. Each set draws as one quad per pair of segments, fading by
 * `fade` from each segment's alpha.
 *
 * SkidSet (0x110): next, prev, +0x10 the last direction, +0x20 the last segment's length, +0x24 its clan, +0x28 its node
 * in the clan's list, +0x2c its maker, +0x30 the surface type (index into the skid textures), +0x40 the start point,
 * +0x50/+0x60 its bounding box, +0x70 the segments, +0xf0 their count, +0x100 the box centre.
 * SkidSegment (0x10): the centre, then the half-width vector in 1/64 units (three signed bytes), then the alpha.
 *
 * Changes: when the pool cannot supply a new set (bMalloc fails, or a clan node cannot be allocated), the set or node is
 * skipped instead of writing through null as the original does.
 */
#include <math.h>
#include <new>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x72b780, SkidSetAddSegment)
NFS_REPLACE(0x72b2f0, SkidListAddTail)

int __fastcall SkidSetAddSegment(SkidSet *set, void *, bVector3 *point, bVector3 *halfWidth, bool, float alpha);
void *__fastcall SkidListAddTail(void *list, void *, SkidSet *set);

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

void Unlink(Node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

const int kMaxSegments = 8;
const int kSkidTextureCount = 29;
const float kHalfWidthUnit = 1.0f / 64.0f;

Node *const kSkidSets = reinterpret_cast<Node *>(0x9aa4d0);
/** The objects whose skid makers (+0x5c) are reset on a race restart. */
Node *const kSkidOwners = reinterpret_cast<Node *>(0x9b3520);

SlotPool *&SkidPool()
{
    return *reinterpret_cast<SlotPool **>(0x9a39f4);
}

TextureInfo **SkidTextures()
{
    return reinterpret_cast<TextureInfo **>(0x9942d8);
}

SkidSegment *Segment(SkidSet *set, int i)
{
    return reinterpret_cast<SkidSegment *>(reinterpret_cast<char *>(set) + 0x70 + i * 0x10);
}

int &SegmentCount(SkidSet *set)
{
    return At<int>(set, 0xf0);
}

/** The set a maker draws now (the maker's only field). */
SkidSet *&Current(SkidMaker *maker)
{
    return *reinterpret_cast<SkidSet **>(maker);
}

/** A set's maker lets go of it. */
void Detach(SkidSet *set)
{
    if (SkidMaker *maker = At<SkidMaker *>(set, 0x2c))
    {
        Current(maker) = nullptr;
        At<SkidMaker *>(set, 0x2c) = nullptr;
    }
}

float &Coord(bVector3 *v, int i)
{
    return (&v->x)[i];
}
} // namespace

void SkidSegment::SetPoints(bVector3 *point, bVector3 *halfWidth)
{
    char *self = reinterpret_cast<char *>(this);
    for (int i = 0; i < 3; ++i)
        self[0xc + i] = static_cast<char>(static_cast<int>(Coord(halfWidth, i) * 64.0f));
    At<float>(self, 0) = point->x;
    At<float>(self, 4) = point->y;
    At<float>(self, 8) = point->z;
}

void SkidSegment::GetPoints(bVector3 *point, bVector3 *halfWidth)
{
    char *self = reinterpret_cast<char *>(this);
    point->x = At<float>(self, 0);
    point->y = At<float>(self, 4);
    point->z = At<float>(self, 8);
    if (halfWidth)
        for (int i = 0; i < 3; ++i)
            Coord(halfWidth, i) = static_cast<float>(static_cast<int>(self[0xc + i])) * kHalfWidthUnit;
}

void SkidSegment::GetEndPoints(bVector3 *left, bVector3 *right)
{
    char *self = reinterpret_cast<char *>(this);
    for (int i = 0; i < 3; ++i)
    {
        const float centre = At<float>(self, i * 4);
        const float half = static_cast<float>(static_cast<int>(self[0xc + i])) * kHalfWidthUnit;
        Coord(left, i) = half + centre;
        Coord(right, i) = centre - half;
    }
}

void SkidSet::Render(eView *view, unsigned char fade)
{
    TextureInfo *texture = SkidTextures()[At<int>(this, 0x30)];
    if (!texture)
        return;
    ePoly poly;
    for (int i = 0; i < SegmentCount(this) - 1; ++i)
    {
        bVector3 *v[4];
        for (int k = 0; k < 4; ++k)
            v[k] = reinterpret_cast<bVector3 *>(poly.Vertices[k]);
        Segment(this, i)->GetEndPoints(v[0], v[3]);
        Segment(this, i + 1)->GetEndPoints(v[1], v[2]);
        for (int k = 0; k < 4; ++k)
            poly.Vertices[k][2] += 0.05f;
        const unsigned char alpha0 = reinterpret_cast<unsigned char *>(Segment(this, i))[0xf];
        const unsigned char alpha1 = reinterpret_cast<unsigned char *>(Segment(this, i + 1))[0xf];
        const unsigned char a0 = fade > alpha0 ? 0 : static_cast<unsigned char>(alpha0 - fade);
        const unsigned char a1 = fade > alpha1 ? 0 : static_cast<unsigned char>(alpha1 - fade);
        const unsigned char alphas[4] = {a0, a1, a1, a0};
        for (int k = 0; k < 4; ++k)
        {
            poly.Colours[k][0] = poly.Colours[k][1] = poly.Colours[k][2] = 0x80;
            poly.Colours[k][3] = alphas[k];
        }
        reinterpret_cast<eViewPlatInterface *>(view)->Render(&poly, SkidTextures()[At<int>(this, 0x30)],
                                                             &eMathIdentityMatrix, 0, 0.0f);
    }
}

/** bPList<SkidSet>::AddTail (thiscall on the list): a pointer node for the set. */
void *__fastcall SkidListAddTail(void *listObject, void *, SkidSet *set)
{
    Node *list = static_cast<Node *>(listObject);
    Node *n = static_cast<Node *>(bPNode::Malloc());
    if (!n)
        return nullptr;
    At<SkidSet *>(n, 8) = set;
    Node *tail = list->prev;
    tail->next = n;
    list->prev = n;
    n->next = list;
    n->prev = tail;
    return n;
}

int __fastcall SkidSetAddSegment(SkidSet *set, void *, bVector3 *point, bVector3 *halfWidth, bool, float alpha)
{
    char *self = reinterpret_cast<char *>(set);
    const int count = SegmentCount(set);
    float length = 0.0f;
    bVector3 direction;
    direction.x = direction.y = direction.z = 0.0f;
    if (count >= 1)
    {
        const char *last = reinterpret_cast<char *>(Segment(set, count - 1));
        bVector3 delta;
        delta.x = point->x - At<float>(const_cast<char *>(last), 0);
        delta.y = point->y - At<float>(const_cast<char *>(last), 4);
        delta.z = point->z - At<float>(const_cast<char *>(last), 8);
        length = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        bNormalize(&direction, &delta);
    }

    SkidSegment *segment = nullptr;
    bool merged = false;
    if (count >= 2)
    {
        const float turn = 1.0f - (direction.y * At<float>(self, 0x14) + direction.z * At<float>(self, 0x18) +
                                   direction.x * At<float>(self, 0x10));
        const float total = length + At<float>(self, 0x20);
        if (turn > 0.2f)
        {
            // A sharp turn ends the mark.
            Detach(set);
            return 0;
        }
        if (!(total > 3.0f) && (total < 0.25f || turn < 0.002f))
        {
            segment = Segment(set, count - 1);
            At<float>(self, 0x20) = total;
            merged = true;
        }
    }
    if (!merged)
    {
        if (count == kMaxSegments)
            return 1;
        segment = Segment(set, count);
        SegmentCount(set) = count + 1;
        if (count + 1 >= 2)
        {
            At<float>(self, 0x10) = direction.x;
            At<float>(self, 0x14) = direction.y;
            At<float>(self, 0x18) = direction.z;
        }
        At<float>(self, 0x20) = length;
    }
    segment->SetPoints(point, halfWidth);
    reinterpret_cast<char *>(segment)[0xf] = static_cast<char>(static_cast<int>(alpha * 255.0f));

    bVector3 *boxA = &At<bVector3>(self, 0x60);
    bVector3 *boxB = &At<bVector3>(self, 0x50);
    bExpandBoundingBox(boxA, boxB, point, length);
    char *clan = At<char *>(self, 0x24);
    bExpandBoundingBox(reinterpret_cast<bVector3 *>(clan + 0x30), reinterpret_cast<bVector3 *>(clan + 0x40), boxA, boxB);
    At<float>(self, 0x100) = (boxB->x + boxA->x) * 0.5f;
    At<float>(self, 0x104) = (boxB->y + boxA->y) * 0.5f;
    At<float>(self, 0x108) = (boxB->z + boxA->z) * 0.5f;
    return 0;
}

SkidSet::SkidSet(SkidMaker *maker, bVector3 *point, bVector3 *halfWidth, int type, float alpha)
{
    char *self = reinterpret_cast<char *>(this);
    At<int>(self, 0x30) = type;
    At<SkidMaker *>(self, 0x2c) = maker;
    SegmentCount(this) = 0;
    At<bVector3>(self, 0x40).x = point->x;
    At<bVector3>(self, 0x40).y = point->y;
    At<bVector3>(self, 0x40).z = point->z;
    bInitializeBoundingBox(&At<bVector3>(self, 0x60), &At<bVector3>(self, 0x50), point);
    At<float>(self, 0x100) = point->x;
    At<float>(self, 0x104) = point->y;
    At<float>(self, 0x108) = point->z;
    char *clan = reinterpret_cast<char *>(GetClan(point));
    At<char *>(self, 0x24) = clan;
    At<void *>(self, 0x28) = SkidListAddTail(clan + 8, nullptr, this);
    SkidSetAddSegment(this, nullptr, point, halfWidth, false, alpha);
}

SkidSet *__cdecl CreateNewSkidSet(SkidMaker *maker, bVector3 *point, bVector3 *halfWidth, int type, float alpha)
{
    if (bIsSlotPoolFull(SkidPool()))
    {
        // The oldest set makes room.
        SkidSet *oldest = reinterpret_cast<SkidSet *>(kSkidSets->prev);
        Unlink(reinterpret_cast<Node *>(oldest));
        if (SkidMaker *owner = At<SkidMaker *>(oldest, 0x2c))
            if (SkidSet *current = Current(owner))
                Detach(current);
        Node *clanNode = At<Node *>(oldest, 0x28);
        Unlink(clanNode);
        bPNode::Free(clanNode);
        bFree(SkidPool(), oldest);
    }
    void *memory = bMalloc(SkidPool());
    if (!memory)
        return nullptr;
    SkidSet *set = new (memory) SkidSet(maker, point, halfWidth, type, alpha);
    Node *n = reinterpret_cast<Node *>(set);
    Node *first = kSkidSets->next;
    kSkidSets->next = n;
    first->prev = n;
    n->next = first;
    n->prev = kSkidSets;
    return set;
}

void SkidMaker::MakeNoSkid()
{
    if (SkidSet *set = Current(this))
        Detach(set);
}

void SkidMaker::MakeSkid(Car *car, bVector3 *point, bVector3 *halfWidth, int type, float alpha)
{
    // The original measures from a null position here; its only caller (TireState::DoSkids) passes no car.
    if (car && bDistBetween(nullptr, point) > 4.0f)
        return;
    SkidSet *set = Current(this);
    if (!set)
    {
        Current(this) = CreateNewSkidSet(this, point, halfWidth, type, alpha);
        return;
    }
    if (At<int>(set, 0x30) == type && !SkidSetAddSegment(set, nullptr, point, halfWidth, false, alpha))
        return;

    // A new set, continuing from where this one ends.
    set = Current(this);
    const int count = SegmentCount(set);
    bVector3 lastPoint, lastHalf;
    Segment(set, count - 1)->GetPoints(&lastPoint, &lastHalf);
    const float lastAlpha =
        static_cast<float>(reinterpret_cast<unsigned char *>(Segment(set, count - 1))[0xf]) * 0.003921569f;
    Detach(set);
    SkidSet *next = CreateNewSkidSet(this, &lastPoint, &lastHalf, type, lastAlpha);
    if (next)
        SkidSetAddSegment(next, nullptr, point, halfWidth, false, alpha);
    Current(this) = next;
}

void __cdecl InitSkids(int count)
{
    if (!SkidPool())
    {
        SkidPool() = bNewSlotPool(0x110, count, "SkidSetSlotPool", 0);
        At<unsigned int>(SkidPool(), 0x14) &= ~1u;
    }
    for (int i = 0; i < kSkidTextureCount; ++i)
    {
        SkidTextures()[i] = nullptr;
        SkidTextures()[i] = GetTextureInfo(bStringHash("SKID_ROAD"), 1, 0);
    }
    PlotSkidsInCaffeine = 0;
    PlotSkidPointsInCaffeine = 0;
}

void __cdecl KillSkidsOnRaceRestart()
{
    for (Node *n = kSkidOwners->next; n != kSkidOwners; n = n->next)
        reinterpret_cast<SkidMaker *>(reinterpret_cast<char *>(n) + 0x5c)->MakeNoSkid();
    DeleteAllSkids();
}

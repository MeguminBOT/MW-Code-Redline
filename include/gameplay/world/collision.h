#pragma once

/**
 * The world collision as the original keeps it (zWorld2): each streamed section's collision pack
 * (WCollisionAssets::mCollisionPackList, by section number, null while the section is not loaded) holds collision
 * instances, each a shape (WCollisionArticle) placed in the world. Enough for the vanilla fixes and their debugging.
 *
 * Simulation axes: x = -world y, y up, z = world x.
 */
namespace nfs::collision
{
/**
 * WCollisionInstance: the world to the shape's space, as rows (local = world * rows + translation, row vectors; the
 * middle row is (0, 1, 0) unless the flags have kLeans, then the cross product of the other two:
 * WCollisionInstance::MakeMatrix 0x7632b0), the scenery group that switches it on (0: always) and its shape.
 */
struct Instance
{
    float row0[3];
    float extent0;
    short stamp;
    unsigned short flags;
    float extent1;
    unsigned short group;
    unsigned short unknown1a;
    void *article;
    float row2[3];
    float extent2;
    float translation[3];
    float extent3;
};
static_assert(sizeof(Instance) == 0x40, "WCollisionInstance");

struct Pack
{
    int unknown00;
    unsigned int count;
    Instance *instances;
};

const unsigned int kPackList = 0x9b38a0;
const int kMaxPacks = 0xa8c;
const unsigned short kLeans = 3;
/** A wall: two points in the shape's space (x y z w: the first's y the wall's top or foot, the second's the other,
 * the second's w one over the length). Walls stop a car only from their front. */
const int kWallFloats = 8;
const int kWallPoint = 4;
const int kWallInverseLength = 7;
const int kShapeWalls = 0x10;

/** A section's collision pack, or null while it is not loaded. */
inline Pack *LoadedPack(int section)
{
    Pack **packs = *reinterpret_cast<Pack ***>(kPackList);
    return packs && section >= 0 && section < kMaxPacks ? packs[section] : nullptr;
}

/** A shape's walls (WCollisionArticle: their offset after its 16 byte header at 2, their count at 4). */
inline float *Walls(const Instance &c, int &count)
{
    unsigned char *article = static_cast<unsigned char *>(c.article);
    if (!article)
    {
        count = 0;
        return nullptr;
    }
    const unsigned short *header = reinterpret_cast<const unsigned short *>(article);
    count = header[2];
    return reinterpret_cast<float *>(article + kShapeWalls + header[1]);
}

/** A point of an upright instance's shape in the world (simulation axes): rows . (local - translation). */
inline void WorldPoint(const Instance &c, const float local[3], float out[3])
{
    const float d[3] = {local[0] - c.translation[0], local[1] - c.translation[1], local[2] - c.translation[2]};
    out[0] = c.row0[0] * d[0] + c.row0[1] * d[1] + c.row0[2] * d[2];
    out[1] = d[1];
    out[2] = c.row2[0] * d[0] + c.row2[1] * d[1] + c.row2[2] * d[2];
}

/** The instance's place in the world (simulation axes). */
inline void InstancePosition(const Instance &c, float out[3])
{
    out[0] = -(c.row0[0] * c.translation[0] + c.row0[1] * c.translation[1] + c.row0[2] * c.translation[2]);
    out[1] = -c.translation[1];
    out[2] = -(c.row2[0] * c.translation[0] + c.row2[1] * c.translation[1] + c.row2[2] * c.translation[2]);
}

/** Whether a wall of an upright instance comes within `radius` of world x, y. */
inline bool WallNear(const Instance &c, const float *wall, float x, float y, float radius)
{
    float a[3], b[3];
    WorldPoint(c, wall, a);
    WorldPoint(c, wall + kWallPoint, b);
    const float ax = a[2] - x, ay = -a[0] - y, bx = b[2] - x, by = -b[0] - y;
    const float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
    float t = l > 0.0f ? -(ax * dx + ay * dy) / l : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float ex = ax + t * dx, ey = ay + t * dy;
    return ex * ex + ey * ey <= radius * radius;
}
} // namespace nfs::collision

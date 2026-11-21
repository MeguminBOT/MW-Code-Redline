/**
 * Race barrier ends a car can drive around (a vanilla fix, gameplay/vanillafixes.h). Not in the original game.
 *
 * A race's barriers are scenery groups (BARRIER_SPLINE_n, from the race's barrier list in the gameplay vault): walls of
 * see-through chevrons, XO_TrackBarrierPlayer_1 pieces of about 9 m placed end to end, whose collision instances belong
 * to the same group, so they block only while the race has the group on (EnableBarrierSceneryGroup 0x64b410). Where
 * a barrier's last piece stops short of the rock (or the wall at its foot) it should reach, a car squeezes past its end
 * and leaves the course: the barrier's walls, like the guardrails', stop a car only from their front, and the end of
 * a wall is where a car pressed against it slips by. That piece is turned about its joint with the next one until
 * its far end is in the rock, behind the guardrail: its scenery
 * instance (drawn; its box and the boxes of the section's tree nodes above it grown to take it) and its wall in the
 * barrier's collision (one collision instance for the whole group, whose shape holds a wall for each stretch: two
 * points in the shape's space, the first's y the wall's top or foot and the second's the other, the second's w one
 * over the length), whenever the streaming section that brings them is activated (its data is loaded afresh each
 * time; a piece not where the track has it is turned already, and left).
 *
 * - BARRIER_SPLINE_36, in races 1.2.2, 1.4.1, 1.5.3, 2.4.1, 3.2.2, 4.1.1, 4.2.1.r, 9.2.1 and two tollbooth
 *   challenges: its west end (4465, 1146) stops 0.4 m short of the guardrail's wall at the foot of the rock, in the
 *   corner where the guardrail turns along the barrier. Turned 40 degrees, the piece crosses the guardrail 3 m from
 *   its joint and ends 6 m behind it in the rock, its top below the rock's surface.
 *
 * -barrierdebug logs the barrier's walls before and after, and the collision grid's cells (64 m) that list its
 * collision instance; with -set Debug.WallsNear=X,Y,R it logs every loaded wall within R metres of X, Y whenever a
 * section is activated (for finding the next such gap).
 */
#include <math.h>
#include <windows.h>

#include "game/gen/api.h"
#include "gameplay/world/collision.h"
#include "core/config.h"
#include "core/log.h"
#include "gameplay/vanillafixes.h"
#include "engine/rendering/vulkan/renderer.h"

namespace
{
using namespace nfs::collision;

/** A scenery instance (SceneryInstance) and a section as source/engine/rendering/culling/SceneryCulling.cpp has them. */
struct PlacedModel
{
    float boundsMin[3];
    float boundsMax[3];
    unsigned int flags;
    short precullIndex;
    short lightingContext;
    float position[3];
    short rotation[9];
    short infoIndex;
};
static_assert(sizeof(PlacedModel) == 0x40, "SceneryInstance");

struct Section
{
    Section *next;
    Section *prev;
    int unknown08;
    int number;
    int unknown10[2];
    void *infos;
    int infoCount;
    PlacedModel *instances;
    int instanceCount;
    void *tree;
    int treeNodeCount;
};

/** A node of a section's bounding box tree: children are instances (>= 0) or nodes (< 0, from the tree's start). */
struct TreeNode
{
    float min[3];
    float max[3];
    short count;
    short children[5];
};
static_assert(sizeof(TreeNode) == 0x24, "SceneryTreeNode");

/** WGrid: the collision's grid of cells; each cell's node lists the collision instances in it (pack << 16 | index). */
struct GridNode
{
    void *dynamics;
    unsigned short index;
    unsigned short unknown06;
    unsigned char counts[4];
    unsigned short offsets[4];
};

const unsigned int kLoadedSections = 0x9aa478;
/** WGrid::fgGrid. */
const unsigned int kGrid = 0x9b38a8;
const int kGridRows = 0x18;
const int kGridColumns = 0x1c;
const int kGridNodes = 0x20;
const int kGridNodeData = 0x14;
const int kInstanceList = 0;
const float kRotationOne = 8192.0f;
const float kAnglePerDegree = 65536.0f / 360.0f;
/** A piece or wall end within this (metres) of where the track has it is not turned yet. */
const float kSamePlace = 0.3f;
const int kCentimetres = 100;

struct Turn
{
    const char *group;
    /** The piece's scenery instance and where the track has it (world). */
    short section;
    short instance;
    float position[3];
    /** The joint it turns about and the far end of its wall (world x, y), and how far it turns (degrees,
     * counterclockwise seen from above). */
    float pivot[2];
    float end[2];
    float degrees;
};

const Turn kTurns[] = {
    {"BARRIER_SPLINE_36", 1636, 50, {4469.3f, 1144.5f, 16.7f}, {4473.56f, 1143.58f}, {4464.90f, 1145.54f}, -40.0f},
};

bool Debugging()
{
    static const bool debug = nfs::vk::HasArgument("-barrierdebug");
    return debug;
}

int Cm(float metres)
{
    return static_cast<int>(metres * kCentimetres + (metres < 0.0f ? -0.5f : 0.5f));
}

Section *LoadedSection(int number)
{
    Section *head = reinterpret_cast<Section *>(kLoadedSections);
    for (Section *s = head->next; s && s != head; s = s->next)
        if (s->number == number)
            return s;
    return nullptr;
}

/** The scenery group's number (its collision instances' group), or -1. */
int GroupNumber(const char *name)
{
    const char *group = reinterpret_cast<const char *>(FindSceneryGroup(bStringHash(name)));
    return group ? *reinterpret_cast<const short *>(group + 0xc) : -1;
}

/** Grows the boxes of the tree nodes above an instance to hold its box; whether the instance is below `node`. */
bool GrowTree(TreeNode *tree, TreeNode *node, int instance, const float *min, const float *max, int depth)
{
    if (depth > 64)
        return false;
    bool below = false;
    for (int i = 0; i < node->count; ++i)
    {
        const int child = node->children[i];
        if (child >= 0 ? child == instance : GrowTree(tree, tree - child, instance, min, max, depth + 1))
            below = true;
    }
    if (below)
        for (int a = 0; a < 3; ++a)
        {
            node->min[a] = min[a] < node->min[a] ? min[a] : node->min[a];
            node->max[a] = max[a] > node->max[a] ? max[a] : node->max[a];
        }
    return below;
}

/** The piece, if its section is loaded and it is where the track has it (not turned yet). */
PlacedModel *Unturned(Section &section, const Turn &turn)
{
    if (turn.instance >= section.instanceCount)
        return nullptr;
    PlacedModel &piece = section.instances[turn.instance];
    for (int a = 0; a < 3; ++a)
        if (fabsf(piece.position[a] - turn.position[a]) > kSamePlace)
            return nullptr;
    return &piece;
}

/** Turns the piece's scenery instance about the pivot: its rows, position and box (the old box's corners turned). */
void TurnScenery(Section &section, PlacedModel &piece, const Turn &turn, float c, float s)
{
    for (int row = 0; row < 3; ++row)
    {
        const float x = piece.rotation[row * 3] / kRotationOne, y = piece.rotation[row * 3 + 1] / kRotationOne;
        piece.rotation[row * 3] = static_cast<short>(floorf((x * c - y * s) * kRotationOne + 0.5f));
        piece.rotation[row * 3 + 1] = static_cast<short>(floorf((x * s + y * c) * kRotationOne + 0.5f));
    }
    const float px = piece.position[0] - turn.pivot[0], py = piece.position[1] - turn.pivot[1];
    piece.position[0] = turn.pivot[0] + px * c - py * s;
    piece.position[1] = turn.pivot[1] + px * s + py * c;
    float min[2] = {1e30f, 1e30f}, max[2] = {-1e30f, -1e30f};
    for (int corner = 0; corner < 4; ++corner)
    {
        const float x = ((corner & 1) ? piece.boundsMax[0] : piece.boundsMin[0]) - turn.pivot[0];
        const float y = ((corner & 2) ? piece.boundsMax[1] : piece.boundsMin[1]) - turn.pivot[1];
        const float tx = turn.pivot[0] + x * c - y * s, ty = turn.pivot[1] + x * s + y * c;
        min[0] = tx < min[0] ? tx : min[0];
        max[0] = tx > max[0] ? tx : max[0];
        min[1] = ty < min[1] ? ty : min[1];
        max[1] = ty > max[1] ? ty : max[1];
    }
    piece.boundsMin[0] = min[0];
    piece.boundsMin[1] = min[1];
    piece.boundsMax[0] = max[0];
    piece.boundsMax[1] = max[1];
    if (section.tree)
    {
        TreeNode *tree = static_cast<TreeNode *>(section.tree);
        GrowTree(tree, tree, turn.instance, piece.boundsMin, piece.boundsMax, 0);
    }
}

/** The cells of the collision grid that list a pack's instance. */
void LogGridCells(int pack, int index)
{
    const char *grid = *reinterpret_cast<const char *const *>(kGrid);
    if (!grid)
        return;
    const unsigned int rows = *reinterpret_cast<const unsigned int *>(grid + kGridRows);
    const unsigned int columns = *reinterpret_cast<const unsigned int *>(grid + kGridColumns);
    const GridNode *const *nodes = *reinterpret_cast<const GridNode *const *const *>(grid + kGridNodes);
    const float *origin = reinterpret_cast<const float *>(grid);
    nfs::Log("barriers: grid %u x %u, origin %d %d %d cm, %d %d cm", rows, columns, Cm(origin[0]), Cm(origin[1]),
             Cm(origin[2]), Cm(origin[3]), Cm(origin[4]));
    const unsigned int id = (static_cast<unsigned int>(pack) << 16) | static_cast<unsigned int>(index);
    char text[512];
    int length = 0;
    for (unsigned int n = 0; nodes && n < rows * columns && length < static_cast<int>(sizeof(text)) - 16; ++n)
    {
        const GridNode *node = nodes[n];
        if (!node)
            continue;
        const unsigned int *list = reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(node) +
                                                                          kGridNodeData + node->offsets[kInstanceList]);
        for (int i = 0; i < node->counts[kInstanceList]; ++i)
            if (list[i] == id)
                length += wsprintfA(text + length, " %u (row %u column %u)", n, n / columns, n % columns);
    }
    text[length] = 0;
    nfs::Log("barriers: collision instance %d of pack %d is in cells%s", index, pack, length ? text : " none");
}

/** -barrierdebug: a collision instance's walls in the world. */
void LogWalls(const nfs::collision::Instance &c)
{
    int count = 0;
    const float *walls = Walls(c, count);
    for (int i = 0; i < count; ++i)
    {
        const float *w = walls + i * kWallFloats;
        float a[3], b[3];
        WorldPoint(c, w, a);
        WorldPoint(c, w + kWallPoint, b);
        nfs::Log("barriers: wall %d: %d %d to %d %d cm, height %d to %d cm", i, Cm(a[2]), Cm(-a[0]), Cm(b[2]),
                 Cm(-b[0]), Cm(a[1]), Cm(b[1]));
    }
}

/** A number ("-12.5") at p; p moves past it and a comma after it. */
float Number(const char *&p)
{
    const bool negative = *p == '-';
    if (negative)
        ++p;
    float value = 0.0f, scale = 1.0f;
    bool fraction = false;
    for (; (*p >= '0' && *p <= '9') || *p == '.'; ++p)
    {
        if (*p == '.')
            fraction = true;
        else if (fraction)
            value += (*p - '0') * (scale *= 0.1f);
        else
            value = value * 10.0f + (*p - '0');
    }
    if (*p == ',')
        ++p;
    return negative ? -value : value;
}

/** -barrierdebug with Debug.WallsNear=X,Y,R: every wall of the loaded collision within R metres of the point. */
void LogWallsNear()
{
    static float point[3];
    static int state;
    if (!state)
    {
        char text[64];
        nfs::config::String("Debug", "WallsNear", "", text, sizeof(text));
        state = 2;
        if (text[0])
        {
            int count = 0;
            for (const char *p = text; *p && count < 3;)
                point[count++] = Number(p);
            state = count == 3 ? 1 : 2;
        }
    }
    if (state != 1)
        return;
    for (int k = 0; k < kMaxPacks; ++k)
    {
        Pack *pack = LoadedPack(k);
        if (!pack || !pack->instances)
            continue;
        for (unsigned int i = 0; i < pack->count; ++i)
        {
            const nfs::collision::Instance &c = pack->instances[i];
            if (c.flags & kLeans)
                continue;
            int count = 0;
            const float *walls = Walls(c, count);
            for (int w = 0; w < count; ++w)
            {
                const float *wall = walls + w * kWallFloats;
                if (!WallNear(c, wall, point[0], point[1], point[2]))
                    continue;
                float a[3], b[3];
                WorldPoint(c, wall, a);
                WorldPoint(c, wall + kWallPoint, b);
                nfs::Log("barriers: near: pack %d instance %u (group %u) wall %d: %d %d to %d %d cm, height %d to %d cm, "
                         "flags %x", k, i, c.group, w, Cm(a[2]), Cm(-a[0]), Cm(b[2]), Cm(-b[0]), Cm(a[1]), Cm(b[1]),
                         *reinterpret_cast<const unsigned int *>(wall + 3));
            }
        }
    }
}

bool Near(const float sim[3], const float world[2])
{
    return fabsf(sim[2] - world[0]) < kSamePlace && fabsf(-sim[0] - world[1]) < kSamePlace;
}

/** The piece's wall (its points at the far end and the pivot) in the group's collision instance in the section's
 * pack, and which of its points is the far end; null when the pack is not loaded, or the wall is turned already. */
float *FindWall(const Turn &turn, int group, nfs::collision::Instance *&owner, int &endPoint)
{
    Pack *pack = LoadedPack(turn.section);
    if (!pack || !pack->instances)
        return nullptr;
    for (unsigned int i = 0; i < pack->count; ++i)
    {
        nfs::collision::Instance &candidate = pack->instances[i];
        if (candidate.group != group || (candidate.flags & kLeans))
            continue;
        if (Debugging())
        {
            float p[3];
            InstancePosition(candidate, p);
            nfs::Log("barriers: pack %d instance %u of group %d at %d %d cm, radius %d cm", turn.section, i, group,
                     Cm(p[2]), Cm(-p[0]), Cm(candidate.extent3));
            LogWalls(candidate);
            LogGridCells(turn.section, static_cast<int>(i));
        }
        int count = 0;
        float *walls = Walls(candidate, count);
        for (int w = 0; w < count; ++w)
        {
            float *wall = walls + w * kWallFloats;
            float a[3], b[3];
            WorldPoint(candidate, wall, a);
            WorldPoint(candidate, wall + kWallPoint, b);
            if ((Near(a, turn.end) && Near(b, turn.pivot)) || (Near(b, turn.end) && Near(a, turn.pivot)))
            {
                owner = &candidate;
                endPoint = Near(a, turn.end) ? 0 : kWallPoint;
                return wall;
            }
        }
    }
    return nullptr;
}

/**
 * Turns the wall's far end about the pivot: in the world (simulation axes: x = -world y, y up, z = world x), then back
 * into the shape's space (local = world * rows + translation, x and z; the instance stands upright, so its middle row
 * is y and the end's height stays), and the wall's length with it.
 */
void TurnWall(const nfs::collision::Instance &c, float *wall, int endPoint, const Turn &turn, float cs, float sn)
{
    float *end = wall + endPoint;
    float world[3];
    WorldPoint(c, end, world);
    const float x = world[2] - turn.pivot[0], y = -world[0] - turn.pivot[1];
    const float turned[2] = {-(turn.pivot[1] + x * sn + y * cs), turn.pivot[0] + x * cs - y * sn};
    end[0] = turned[0] * c.row0[0] + turned[1] * c.row2[0] + c.translation[0];
    end[2] = turned[0] * c.row0[2] + turned[1] * c.row2[2] + c.translation[2];
    const float dx = wall[kWallPoint] - wall[0], dz = wall[kWallPoint + 2] - wall[2];
    wall[kWallInverseLength] = 1.0f / sqrtf(dx * dx + dz * dz);
}
} // namespace

namespace nfs::vanillafixes
{
void SectionActivated(int number)
{
    if (Debugging())
        LogWallsNear();
    if (!On())
        return;
    // A streaming section brings several scenery sections and their collision packs (P76 brings 1676 and 1636).
    for (const Turn &turn : kTurns)
    {
        Section *section = LoadedSection(turn.section);
        PlacedModel *piece = section ? Unturned(*section, turn) : nullptr;
        if (!piece)
            continue;
        const int group = GroupNumber(turn.group);
        nfs::collision::Instance *collision = nullptr;
        int endPoint = 0;
        float *wall = group >= 0 ? FindWall(turn, group, collision, endPoint) : nullptr;
        if (!wall)
        {
            if (Debugging())
                nfs::Log("barriers: %s's end piece in section %d (activated with %d): no wall found (group %d)",
                         turn.group, turn.section, number, group);
            continue;
        }
        const unsigned short angle = static_cast<unsigned short>(static_cast<int>(turn.degrees * kAnglePerDegree));
        const float c = bCos(angle), s = bSin(angle);
        TurnScenery(*section, *piece, turn, c, s);
        TurnWall(*collision, wall, endPoint, turn, c, s);
        nfs::Log("barriers: %s's end piece turned %d degrees into the rock (scenery %d of section %d and its wall)",
                 turn.group, static_cast<int>(turn.degrees), turn.instance, turn.section);
        if (Debugging())
            LogWalls(*collision);
    }
}
} // namespace nfs::vanillafixes

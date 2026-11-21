/**
 * The scenery culling (engine/rendering/culling/culling.h, docs/CULLING.md), in place of the original GrandSceneryCullInfo::DoCulling
 * (0x731b80), CullView (0x731b10) and ScenerySectionHeader::DrawAScenery (0x729120).
 *
 * Each frame every view to draw (the player views, the road reflection, the environment map faces, the shadow view)
 * gets a SceneryCullInfo. CullView picks the track sections the view may see and walks each one's bounding box tree;
 * DrawAScenery decides for each instance in a visible tree node whether it is drawn and with which model, and appends
 * it to the view's range of the shared draw table that StuffScenery draws.
 *
 * Changes from the original:
 * - The draw table holds kDrawTableSize instances. The original's 5000 were shared by all views of a frame, and past
 *   them instances were left out without a trace.
 * - Sun shadow cascades (SetShadowCasters) see every loaded section with full detail models, and nothing is left out
 *   for lying behind the camera or being small: the original's behind-the-camera test (along the shadow camera's
 *   direction, the sun's) dropped every caster further towards the sun than its own size. Casters of a section the
 *   streamer has just loaded fade in (dithered by the shadow depth shader, x360_car_11_ps_3_0).
 * - The original shadow view (15) is not culled while the rebuild's cascades replace its pass.
 * - The player view's scenery fades in and out (dithered) where the original popped: sections entering or leaving its
 *   visibility list or getting loaded, the preculler, the size limit. Props keep their full detail model (the original
 *   swapped in low detail ones from 25 m).
 * - Reflections (the car's cube, the wet road's mirror) cull without the preculler.
 * - The wet road's mirror flips what hangs over the road (bridges, walkways) about the road under it, not about its own
 *   origin (mirror namespace below), so the road shows it.
 * - Position marker flares (street lamps) go to the rebuild's larger flare pool (engine/rendering/lights/flares.h) at the fade of their
 *   scenery.
 *
 * StuffScenery (0x723fa0) draws a view's range of the table: each instance's model with render flags made from the
 * instance's flags and the caller's, filtered by the caller's (the shadow and reflection passes leave out what their
 * flags exclude).
 *
 * Debugging: -popdebug logs the scenery that appears in or vanishes from the player view while it is on screen, with
 * why it was not drawn the frame before or after; -dumpscenery writes the loaded sections' instances to
 * scenery_dump.txt next to the exe.
 */
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "core/config.h"
#include "engine/rendering/culling/culling.h"
#include "engine/rendering/lights/flares.h"
#include "engine/rendering/lights/lamps.h"
#include "gameplay/ai/helilights.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/profile.h"
#include "engine/rendering/shadows/shadow.h"
#include "engine/rendering/shadows/shadowdebug.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/render.h"

/** The world detail level (source/engine/rendering/frame/PlatformMisc.cpp). */
int __cdecl WorldDetailLevel();


NFS_REPLACE(0x6cf2b0, VisibleStateSB)

/**
 * eViewPlatInterface::GetVisibleStateSB (0x6cf2b0), the box against frustum test of the scenery culling, props, cars
 * and flares, with SSE: 0 when the box is outside one of the view's six planes, 1 when it crosses one, 2 when it is
 * inside them all; `matrix` first moves the box (TransformBound's box around the moved one, worked out from its centre
 * and half sizes rather than its eight corners). The original was x87 code behind a function call for every box and
 * showed up as one of the frame's costliest functions.
 */
int __fastcall VisibleStateSB(void *view, void *, const bVector3 *min, const bVector3 *max, bMatrix4 *matrix)
{
    const bVector3 &lo = *min, &hi = *max;
    float cx = (lo.x + hi.x) * 0.5f, cy = (lo.y + hi.y) * 0.5f, cz = (lo.z + hi.z) * 0.5f;
    float ex = hi.x - cx, ey = hi.y - cy, ez = hi.z - cz;
    const __m128 noSign = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff)), zero = _mm_setzero_ps();
    if (matrix)
    {
        const float *m = reinterpret_cast<const float *>(matrix);
        const __m128 r0 = _mm_loadu_ps(m), r1 = _mm_loadu_ps(m + 4), r2 = _mm_loadu_ps(m + 8), r3 = _mm_loadu_ps(m + 12);
        const __m128 center = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(cx), r0), _mm_mul_ps(_mm_set1_ps(cy), r1)),
                                         _mm_add_ps(_mm_mul_ps(_mm_set1_ps(cz), r2), r3));
        const __m128 extent = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(ex), _mm_and_ps(r0, noSign)),
                                                    _mm_mul_ps(_mm_set1_ps(ey), _mm_and_ps(r1, noSign))),
                                         _mm_mul_ps(_mm_set1_ps(ez), _mm_and_ps(r2, noSign)));
        alignas(16) float c[4], e[4];
        _mm_store_ps(c, center);
        _mm_store_ps(e, extent);
        cx = c[0], cy = c[1], cz = c[2];
        ex = e[0], ey = e[1], ez = e[2];
    }
    // The planes (a, b, c, d) of the view's PlatInfo, a, b, c and d each for four planes at a time.
    const float *planes = reinterpret_cast<const float *>(*static_cast<char **>(view) + 0x140);
    const __m128 centerX = _mm_set1_ps(cx), centerY = _mm_set1_ps(cy), centerZ = _mm_set1_ps(cz);
    const __m128 extentX = _mm_set1_ps(ex), extentY = _mm_set1_ps(ey), extentZ = _mm_set1_ps(ez);
    __m128 a = _mm_loadu_ps(planes), b = _mm_loadu_ps(planes + 4), c = _mm_loadu_ps(planes + 8),
           d = _mm_loadu_ps(planes + 12);
    _MM_TRANSPOSE4_PS(a, b, c, d);
    __m128 distance = _mm_add_ps(_mm_add_ps(_mm_mul_ps(a, centerX), _mm_mul_ps(b, centerY)),
                                 _mm_add_ps(_mm_mul_ps(c, centerZ), d));
    __m128 reach = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_and_ps(a, noSign), extentX),
                                         _mm_mul_ps(_mm_and_ps(b, noSign), extentY)),
                              _mm_mul_ps(_mm_and_ps(c, noSign), extentZ));
    __m128 outside = _mm_cmplt_ps(_mm_add_ps(distance, reach), zero);
    __m128 crossing = _mm_cmplt_ps(_mm_sub_ps(distance, reach), zero);
    // Planes 4 and 5, twice.
    a = _mm_loadu_ps(planes + 16);
    b = _mm_loadu_ps(planes + 20);
    c = a;
    d = b;
    _MM_TRANSPOSE4_PS(a, b, c, d);
    distance = _mm_add_ps(_mm_add_ps(_mm_mul_ps(a, centerX), _mm_mul_ps(b, centerY)),
                          _mm_add_ps(_mm_mul_ps(c, centerZ), d));
    reach = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_and_ps(a, noSign), extentX),
                                  _mm_mul_ps(_mm_and_ps(b, noSign), extentY)),
                       _mm_mul_ps(_mm_and_ps(c, noSign), extentZ));
    outside = _mm_or_ps(outside, _mm_cmplt_ps(_mm_add_ps(distance, reach), zero));
    crossing = _mm_or_ps(crossing, _mm_cmplt_ps(_mm_sub_ps(distance, reach), zero));
    if (_mm_movemask_ps(outside))
        return 0;
    return _mm_movemask_ps(crossing) ? 1 : 2;
}

extern "C" int PrecullerMode;
extern "C" int DisablePrecullerCount;
extern "C" float dword_9AB254;
extern "C" int lpData_00901830;
extern "C" int lpData_009017f4;
extern "C" float flt_902148;
extern "C" void *ScenerySectionHeaders[];
/** Set while the views draw with the original's 0x800 option (instances with 0x80000 are left out, 0x40000 kept). */
extern "C" int dword_982B9C;
/** The model StuffScenery is at (debugging). */
extern "C" eModel *pDebugModel;

namespace nfs::culling
{
namespace
{
const int kDrawTableSize = 32768;
const int kMaxSections = 256;
const int kPlayerView = 1;
const int kSecondPlayerView = 2;
const int kRoadReflectionView = 4;
const int kShadowView = 15;
const int kTrafficShadowView = 16;
const int kFirstEnvironmentView = 18;
const int kLastEnvironmentView = 23;
/** ScenerySectionHeaderList: a bTList of the loaded sections. */
const unsigned int kLoadedSections = 0x9aa478;

/** The preculler: a 32 x 32 grid of 32 m cells (repeating every 1024 m) where each instance has a bit saying it is
 * hidden from anywhere in the cell; the BooBoo list marks 32 m cells (in a 128 x 128 grid) where it is not to be
 * trusted. */
const int kPrecullCellShift = 5;
const int kPrecullCellMask = 0x1f;
const int kPrecullBytes = 0x80;
const int kBooBooMask = 0x7f;
const int kBooBooRowShift = 7;
const int kPrecullOnAndOff = 2;
const int kPrecullWhenMoving = 3;
const int kPrecullOnAndOffPeriod = 61;
const int kPrecullOnAndOffOffFrames = 15;

/** Instance flags. */
const unsigned int kInstanceBelowShadowDetail2 = 0x100;
const unsigned int kInstanceNoMatrix = 0x200;
const unsigned int kInstanceReflected = 0x40;
const unsigned int kInstanceReflectedToo = 0x8000000;
const unsigned int kInstanceLowReflection = 0x80;
const unsigned int kInstanceFullReflection = 0x1000100;
const unsigned int kInstanceLarger = 0x2000000;
/** Cull flags (FUN_004fae40). */
const unsigned int kCullReflection = 0x1800;
const unsigned int kCullMirror = 0x800;
const unsigned int kCullLowDetail = 0x20;
const unsigned int kCullRaise = 0x100;
const unsigned int kCullRotate = 0x4000;
/** An instance flag in this mask leaves the instance out of views with the same cull flag, except the two inverted
 * bits (0x20, 0x40) and 0x8000000, which leave out instances without them. */
const unsigned int kExcludeMask = 0x80000ff;
const unsigned int kExcludeInvert = 0xffffff60;
/** Size test: instances are measured as if this much larger (metres); larger ones count this many pixels more. */
const float kSizePadding = 6.0f;
const int kLargerPixels = 10;
const int kMinPixels = 2;
const int kMinPixelsPlayer = 18;
const int kMinPixelsSplitScreen = 23;
const int kMinPixelsReflection = 32;
/** What covers the car goes into its reflection cube (Overhead): over the camera within this margin (metres), its top
 * this far above the camera, its bottom at most this far above. */
const float kOverheadMargin = 15.0f;
const float kOverheadClearance = 2.5f;
const float kOverheadReach = 40.0f;
const int kSplitScreenViewMode = 3;
/** Low detail models from this far (metres) when their solid has at least kLodMinCount of something (its strips?)
 * and the instance is under kLodPixelsPerMetre pixels per metre of the solid's radius (at least kLodMinRadius). */
const float kLodMinDistance = 25.0f;
const short kLodMinCount = 0x28;
const float kLodMinRadius = 6.0f;
const float kLodPixelsPerMetre = 8.7f;
const int kHighWorldDetail = 2;
/** Rotating models (solid flag 0x80 at 0xe) turn with time, their phase from their position. */
const float kRotationPhasePerMetre = 60.0f;
const int kDegrees = 360;
const unsigned char kSolidRotates = 0x80;
/** Light flares from a model's position markers: type, and the directional ones. */
const int kMarkerFlareType = 0xe;
const unsigned char kFlareOmni = 2;
const unsigned char kFlareDirectional = 4;
const float kMovedFlare = 999.0f;

struct Instance
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
static_assert(sizeof(Instance) == 0x40, "SceneryInstance");

/** models: full detail, unused here, low detail, reflections. */
struct InstanceInfo
{
    char name[24];
    unsigned int modelHashes[4];
    eModel *models[4];
    float radius;
    unsigned int checksum;
    unsigned int hierarchyHash;
    void *hierarchy;
};
static_assert(sizeof(InstanceInfo) == 0x48, "SceneryInfo");

struct Section
{
    Section *next;
    Section *prev;
    int unknown08;
    int number;
    int unknown10[2];
    InstanceInfo *infos;
    int infoCount;
    Instance *instances;
    int instanceCount;
    void *tree;
    int treeNodeCount;
    unsigned char *precullBits;
};

struct DrawEntry
{
    uintptr_t model;
    bMatrix4 *matrix;
    const Instance *instance;
};

struct ViewCull
{
    unsigned char unknown00[0x80];
    eView *view;
    unsigned int flags;
    DrawEntry *begin;
    DrawEntry *cursor;
    DrawEntry *limit;
    unsigned char unknown94[0xc];
    float cameraPosition[4];
    float cameraDirection[4];
    float H;
    int precullCell;
    unsigned char unknownC8[8];
};
static_assert(sizeof(ViewCull) == 0xd0, "SceneryCullInfo");

struct GrandCull
{
    ViewCull views[12];
    int count;
    DrawEntry *begin;
    DrawEntry *cursor;
    DrawEntry *limit;
};

struct SolidFields
{
    unsigned char unknown00[0xe];
    unsigned char flags;
    unsigned char unknown0f[5];
    short count;
    unsigned char unknown16[0x5a];
    float pivot[3];
    unsigned char unknown7c[0x20];
    float radius;
};
static_assert(offsetof(SolidFields, pivot) == 0x70 && offsetof(SolidFields, radius) == 0x9c, "eSolid");

struct ModelFields
{
    unsigned char unknown00[0xc];
    SolidFields *solid;
};

struct MarkerFields
{
    unsigned char unknown00[4];
    int type;
    unsigned char unknown08[0x38];
    float position[3];
};

struct PoolFlare
{
    unsigned char unknown00[0x10];
    float position[3];
    float reflectionHeight;
    float direction[3];
    unsigned char type;
    unsigned char flags;
};

enum Verdict : unsigned char
{
    kDrawn,
    kPrecull,
    kExcluded,
    kOutside,
    kBehind,
    kSmall,
    kNoModel,
    kNoRoom,
    kVerdicts
};

struct Choice
{
    eModel *model;
    int visibility;
    float distance;
    /** The wet road's mirror: flipped about this height (mirror::Place) rather than about the instance's own origin. */
    bool hangs;
    float plane;
};

DrawEntry g_drawTable[kDrawTableSize];
bool g_shadowCasters;
/** The caster set and cascade of the cascade being culled (SetShadowCasters). */
int g_casterSet;
int g_casterCascade = -1;

Section *SectionHeader(int number)
{
    char *entry = static_cast<char *>(ScenerySectionHeaders[number]);
    return entry ? *reinterpret_cast<Section **>(entry + 4) : nullptr;
}

int LoadedSections(short *out, int capacity)
{
    const Section *head = reinterpret_cast<const Section *>(kLoadedSections);
    int count = 0;
    for (const Section *s = head->next; s && s != head && count < capacity; s = s->next)
        out[count++] = static_cast<short>(s->number);
    return count;
}

/** The preculler cell the view's camera is in, or -1 to cull without it (DoCulling). */
int PrecullCell(const ViewCull &cull)
{
    if (g_shadowCasters)
        return -1;
    // Off by default (Culling.Preculler=1 is the game's mode): its cells were made for the original camera and 4:3,
    // and the wider view and the Xbox 360 camera see past what they hide, so scenery came and went as the player
    // crossed cells.
    static const int configured = nfs::config::Int("Culling", "Preculler", 0);
    const int mode = configured >= 0 ? configured : PrecullerMode;
    bool on = true;
    if (mode == 0)
        on = false;
    else if (mode == kPrecullOnAndOff)
        on = RealTimeFrames % kPrecullOnAndOffPeriod >= kPrecullOnAndOffOffFrames;
    else if (mode == kPrecullWhenMoving)
    {
        const float *velocity = reinterpret_cast<const float *>(reinterpret_cast<const char *>(cull.view->pCamera) + 0x200);
        on = !(sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]) < dword_9AB254);
    }
    if (DisablePrecullerCount > 0 && mode != kPrecullOnAndOff)
        on = false;
    const int x = static_cast<int>(cull.cameraPosition[0]), y = static_cast<int>(cull.cameraPosition[1]);
    const int booBoo = ((y >> kPrecullCellShift) & kBooBooMask) << kBooBooRowShift | ((x >> kPrecullCellShift) & kBooBooMask);
    if (reinterpret_cast<const unsigned char *>(&gPrecullerBooBooManager)[booBoo >> 3] & (1 << (booBoo & 7)))
        on = false;
    if (cull.view->ID == kShadowView || cull.view->ID == kTrafficShadowView)
        on = false;
    // Reflections look where the player camera never does (the car's reflection cube all round, the wet road's mirror
    // up), and the preculler's cells hide what a forward camera cannot see from them: scenery came and went in the
    // reflections as the car crossed cells (Culling.ReflectionPreculler=1 keeps it).
    static const bool reflectionPreculler = nfs::config::Int("Culling", "ReflectionPreculler", 0) != 0;
    const int id = cull.view->ID;
    if (!reflectionPreculler && (id == kRoadReflectionView || (id >= kFirstEnvironmentView && id <= kLastEnvironmentView)))
        on = false;
    return on ? ((y >> kPrecullCellShift) & kPrecullCellMask) << kPrecullCellShift | ((x >> kPrecullCellShift) & kPrecullCellMask)
              : -1;
}

/** Pixel size of a sphere seen from the view's camera (sub_729060); 0 when behind the camera (unless `behindToo`). */
int PixelSize(const ViewCull &cull, const float *position, float radius, bool behindToo, float &distance)
{
    const float dx = position[0] - cull.cameraPosition[0], dy = position[1] - cull.cameraPosition[1],
                dz = position[2] - cull.cameraPosition[2];
    if (!behindToo &&
        dz * cull.cameraDirection[2] + dy * cull.cameraDirection[1] + dx * cull.cameraDirection[0] < -radius)
        return 0;
    distance = sqrtf(dy * dy + dz * dz + dx * dx);
    const float beyond = distance - radius;
    return beyond > radius ? static_cast<int>(radius / beyond * cull.H) : static_cast<int>(cull.H);
}

int VisibleState(const ViewCull &cull, const Instance &instance)
{
    bVector3 min(instance.boundsMin[0], instance.boundsMin[1], instance.boundsMin[2]);
    bVector3 max(instance.boundsMax[0], instance.boundsMax[1], instance.boundsMax[2]);
    return static_cast<int>(cull.view->GetVisibleState(&min, &max, nullptr));
}

/**
 * The wet road's mirror (kCullMirror) is drawn from the player's own camera with each instance flipped about its own
 * origin, which does for what stands on the road: it comes out under the road, where the road's reflection looks for it.
 * A bridge's origin is in the air, so it came out at about its own height, in a part of the target no road pixel reads.
 * What hangs over the road (its origin, or the bottom of a section's merged model, kHanging above the ground under it)
 * is flipped about that ground instead, whether marked for reflections or not; some overpasses are not. The ground comes
 * from the collision, once per instance, a few queries a frame.
 */
namespace mirror
{
const float kHanging = 2.0f;
/** Smaller instances are not asked about: lamps, signs, props, all on the ground or too small to show. */
const float kMinRadius = 6.0f;
/** A tree node's reflection is looked for down to this far (metres) below the camera's ground mirrored. */
const float kNodeSlack = 30.0f;
const int kQueriesAFrame = 16;
const unsigned int kRetryFrames = 30;
const int kSlots = 8192;

struct Ground
{
    const Instance *instance;
    float x;
    float height;
    unsigned int asked;
    bool found;
};

Ground g_ground[kSlots];
unsigned int g_frame;
int g_queries;
float g_cameraGround;
int g_hanging;

bool HeightUnder(float x, float y, float z, float &height)
{
    // The simulation's axes (Y up); the query keeps nothing in the manager.
    const float point[3] = {-y, z, x};
    char manager[8] = {};
    return reinterpret_cast<WCollisionMgr *>(manager)->GetWorldHeightAtPoint(
        *reinterpret_cast<const UMath::Vector3 *>(point), height, nullptr);
}

void BeginFrame(const ViewCull &cull)
{
    if (g_frame == eFrameCounter)
        return;
    g_frame = eFrameCounter;
    g_queries = 0;
    static const bool debug = nfs::vk::HasArgument("-reflectdebug");
    if (debug && !(eFrameCounter % 120))
        nfs::Log("road reflection: %d instances hang over the road", g_hanging);
    g_hanging = 0;
    const float *eye = cull.cameraPosition;
    if (!HeightUnder(eye[0], eye[1], eye[2], g_cameraGround))
        g_cameraGround = eye[2] - 1.5f;
}

/** Whether the instance hangs over the ground under it (c.hangs and c.plane set then). */
bool Place(const Instance &instance, const InstanceInfo &info, Choice &c)
{
    // The panoramas are far off and the cheap reflection shaders have no haze; shadow meshes are no scenery.
    const char *name = info.name;
    if (info.radius < kMinRadius || !_strnicmp(name, "PAN_", 4) || !_strnicmp(name, "SHADOW_", 7) ||
        !_strnicmp(name, "SHD_", 4) || !_strnicmp(name, "SKY", 3))
        return false;
    const float low = (instance.flags & kInstanceNoMatrix) ? instance.boundsMin[2] : instance.position[2];
    Ground &g = g_ground[(reinterpret_cast<uintptr_t>(&instance) >> 6) & (kSlots - 1)];
    const bool known = g.instance == &instance && g.x == instance.boundsMin[0];
    if ((!known || (!g.found && eFrameCounter - g.asked >= kRetryFrames)) && g_queries < kQueriesAFrame)
    {
        ++g_queries;
        g.instance = &instance;
        g.x = instance.boundsMin[0];
        g.asked = eFrameCounter;
        g.found = HeightUnder((instance.boundsMin[0] + instance.boundsMax[0]) * 0.5f,
                              (instance.boundsMin[1] + instance.boundsMax[1]) * 0.5f, low - 0.2f, g.height);
        static const bool debug = nfs::vk::HasArgument("-reflectdebug");
        if (debug && g.found && low - g.height > kHanging)
        {
            char name[25];
            lstrcpynA(name, info.name, sizeof(name));
            nfs::Log("road reflection: %s hangs: low %d ground %d radius %d flags %x eye %d", name, static_cast<int>(low),
                     static_cast<int>(g.height), static_cast<int>(info.radius), instance.flags,
                     static_cast<int>(g_cameraGround));
        }
    }
    else if (!known)
    {
        return false;
    }
    if (!g.found || low - g.height <= kHanging)
        return false;
    c.hangs = true;
    c.plane = g.height;
    ++g_hanging;
    return true;
}

/** The view test of an instance with its flip: the box from its reflection's bottom to its own top. */
int VisibleState(const ViewCull &cull, const Instance &instance, const Choice &c)
{
    const float plane = c.hangs ? c.plane : instance.position[2];
    bVector3 min(instance.boundsMin[0], instance.boundsMin[1], 2.0f * plane - instance.boundsMax[2]);
    bVector3 max(instance.boundsMax[0], instance.boundsMax[1], instance.boundsMax[2]);
    return static_cast<int>(cull.view->GetVisibleState(&min, &max, nullptr));
}
} // namespace mirror

/** Whether an instance not marked for reflections still goes into the car's reflection cube: what covers the car.
 * Overpass decks, roofs and canopies are not marked, so the cube saw open sky under them and the car shone in their
 * shade as in the sun. An instance whose box reaches over the camera (within kOverheadMargin round it), higher than
 * kOverheadClearance above it and starting at most kOverheadReach above it, is drawn in the environment views
 * (its full model: the low detail ones lack the undersides). Only a missing reflection mark is overridden: an
 * instance hidden from every view (a switched-off scenery group's 0x10, the race barriers in free roam) stays out. */
bool Overhead(const ViewCull &cull, const Instance &instance, const InstanceInfo &info)
{
    const int id = cull.view->ID;
    if (id < kFirstEnvironmentView || id > kLastEnvironmentView)
        return false;
    // The panoramas and sky domes cover everything and are not overhead.
    if ((info.name[0] == 'P' && info.name[1] == 'A' && info.name[2] == 'N' && info.name[3] == '_') ||
        (info.name[0] == 'S' && info.name[1] == 'K' && info.name[2] == 'Y'))
        return false;
    const float *eye = cull.cameraPosition;
    return instance.boundsMax[2] > eye[2] + kOverheadClearance && instance.boundsMin[2] < eye[2] + kOverheadReach &&
           instance.boundsMin[0] - kOverheadMargin < eye[0] && instance.boundsMax[0] + kOverheadMargin > eye[0] &&
           instance.boundsMin[1] - kOverheadMargin < eye[1] && instance.boundsMax[1] + kOverheadMargin > eye[1];
}

/** Whether the instance is drawn in the view and with which model (the original's tests). An instance left out by the
 * preculler or for its size still gets its model, for fading out. */
Verdict Judge(const Section &section, const ViewCull &cull, const Instance &instance, Choice &c)
{
    const bool preculled =
        cull.precullCell >= 0 &&
        (section.precullBits[instance.precullIndex * kPrecullBytes + (cull.precullCell >> 3)] & (1 << (cull.precullCell & 7)));
    unsigned int flags = instance.flags;
    if ((flags & kInstanceBelowShadowDetail2) && lpData_00901830 >= 2)
        return kExcluded;
    if (flags & (kInstanceReflected | kInstanceReflectedToo))
        flags |= kInstanceReflected | kInstanceReflectedToo;
    const InstanceInfo &info = section.infos[instance.infoIndex];
    const bool mirrored = (cull.flags & kCullMirror) != 0;
    unsigned int excluded = (flags ^ kExcludeInvert) & cull.flags & kExcludeMask;
    if (mirrored && mirror::Place(instance, info, c))
        excluded &= ~(kInstanceReflected | kInstanceReflectedToo);
    if (excluded)
    {
        if ((excluded & ~kExcludeInvert) || !Overhead(cull, instance, info))
            return kExcluded;
        if (c.visibility == 1)
        {
            c.visibility = VisibleState(cull, instance);
            if (c.visibility == 0)
                return kOutside;
        }
        PixelSize(cull, instance.position, info.radius + kSizePadding, true, c.distance);
        c.model = info.models[0] ? info.models[0] : info.models[2];
        return c.model ? kDrawn : kNoModel;
    }
    if (c.visibility == 1)
    {
        c.visibility = mirrored && (c.hangs || !(instance.flags & kInstanceNoMatrix)) ? mirror::VisibleState(cull, instance, c)
                                                                                     : VisibleState(cull, instance);
        if (c.visibility == 0)
            return kOutside;
    }
    if (g_shadowCasters)
    {
        // Panoramas (PAN_*: the mountains, hills and skylines around the city, kilometres across and not where what
        // they stand for is) cast no shadow; theirs covered whole streets and vanished or appeared at once as the
        // streamer swapped panoramas while driving.
        if (info.name[0] == 'P' && info.name[1] == 'A' && info.name[2] == 'N' && info.name[3] == '_')
            return kExcluded;
        // The sky domes (section 2600) lie kilometres beyond every cascade's depth range: drawing them cost draws and
        // left no depth.
        if (info.name[0] == 'S' && info.name[1] == 'K' && info.name[2] == 'Y')
            return kExcluded;
        PixelSize(cull, instance.position, info.radius + kSizePadding, true, c.distance);
        c.model = info.models[0] ? info.models[0] : info.models[2];
        return c.model ? kDrawn : kNoModel;
    }
    // Behind the camera by the instance's own box: its position and its model's radius do not hold every model (a
    // panorama's mountains lie far from its origin), so the mountains round the college vanished and came back while
    // on screen (2371 pixels at 1.9 km) as the camera turned.
    {
        float centre = 0.0f, half = 0.0f;
        for (int a = 0; a < 3; ++a)
        {
            const float mid = (instance.boundsMin[a] + instance.boundsMax[a]) * 0.5f, extent = instance.boundsMax[a] - mid;
            centre += (mid - cull.cameraPosition[a]) * cull.cameraDirection[a];
            half += extent * extent;
        }
        if (centre < -sqrtf(half))
            return kBehind;
    }
    int size = PixelSize(cull, instance.position, info.radius + kSizePadding, true, c.distance);
    bool tooSmall = size < kMinPixels;
    if (instance.flags & kInstanceLarger)
        size += kLargerPixels;
    if (cull.flags & kCullReflection)
    {
        tooSmall |= size < (c.hangs ? kMinPixels : kMinPixelsReflection);
        c.model = (instance.flags & kInstanceLowReflection)    ? info.models[2]
                  : (instance.flags & kInstanceFullReflection) ? info.models[0]
                                                               : info.models[3];
        // The reflection models are flat, for what stands on the road and is seen from above in it.
        if (c.hangs)
            c.model = info.models[2] ? info.models[2] : info.models[0];
    }
    else if (cull.flags & kCullLowDetail)
    {
        tooSmall |= size < kMinPixelsReflection;
        c.model = info.models[2];
    }
    else if (static_cast<int>(eGetCurrentViewMode()) >= kSplitScreenViewMode)
    {
        tooSmall |= size < kMinPixelsSplitScreen;
        c.model = info.models[2];
    }
    else
    {
        tooSmall |= size < kMinPixelsPlayer;
        // The original swaps in the low detail model from 25 m (Culling.PropLod=1); full detail throughout costs only
        // triangles and does not make props change shape as the player drives by.
        static const bool lodSwitch = nfs::config::Int("Culling", "PropLod", 0) != 0;
        bool low = false;
        const ModelFields *high = reinterpret_cast<const ModelFields *>(info.models[0]);
        if (lodSwitch && high && high->solid && high->solid->count >= kLodMinCount)
        {
            const float radius = high->solid->radius < kLodMinRadius ? kLodMinRadius : high->solid->radius;
            low = c.distance >= kLodMinDistance && static_cast<float>(size) / radius < kLodPixelsPerMetre;
        }
        const bool full = !low && WorldDetailLevel() == kHighWorldDetail;
        c.model = full ? info.models[0] : info.models[2];
    }
    if (!c.model)
        return kNoModel;
    return preculled ? kPrecull : tooSmall ? kSmall : kDrawn;
}

/** The light flares at the model's position markers (street lamps and the like), for the player views, drawn at the
 * instance's fade. */
void AddMarkerFlares(const ViewCull &cull, eModel *model, const bMatrix4 *matrix, float fade)
{
    auto next = NFS_ORIGINAL(0x501680, ePositionMarker *(__thiscall *)(eModel *, ePositionMarker *));
    const SolidFields *solid = reinterpret_cast<const ModelFields *>(model)->solid;
    for (ePositionMarker *m = next(model, nullptr); m; m = next(model, m))
    {
        if (!solid)
            continue;
        PoolFlare *flare =
            reinterpret_cast<PoolFlare *>(nfs::flares::Allocate(cull.view == &eViews[kPlayerView] ? 1u : 2u, fade));
        if (!flare)
            continue;
        const MarkerFields &marker = *reinterpret_cast<const MarkerFields *>(m);
        bVector4 p(marker.position[0] - solid->pivot[0], marker.position[1] - solid->pivot[1],
                   marker.position[2] - solid->pivot[2], 1.0f);
        NFS_ORIGINAL(0x6bd920, void(__cdecl *)(bVector4 *, const bMatrix4 *, const bVector4 *))(&p, matrix, &p);
        const char *anchor = *reinterpret_cast<char *const *>(reinterpret_cast<const char *>(cull.view) + 0x68);
        if (anchor && *reinterpret_cast<const float *>(anchor + 0x36a0) > 0.0f &&
            (flare->position[0] != p.x || flare->position[1] != p.y || flare->position[2] != p.z))
            flare->reflectionHeight = kMovedFlare;
        flare->position[0] = p.x;
        flare->position[1] = p.y;
        flare->position[2] = p.z;
        flare->type = static_cast<unsigned char>(marker.type + kMarkerFlareType);
        if (marker.type == 3 || marker.type == 4 || marker.type == 5)
        {
            flare->flags = kFlareDirectional;
            float d[2] = {p.x - matrix->v3.x, p.y - matrix->v3.y};
            NFS_ORIGINAL(0x45f3c0, void *(__cdecl *)(float *, const float *))(d, d);
            flare->direction[0] = d[0];
            flare->direction[1] = d[1];
            flare->direction[2] = 0.0f;
        }
        else
        {
            flare->flags = kFlareOmni;
        }
    }
}

/**
 * Fading: scenery that the culling adds to the player view or takes away from it for any reason but the camera
 * turning - its section entering or leaving the view's visibility list or getting loaded, the preculler, its size -
 * fades in or out over kSeconds (dithered by the rebuild's own shaders, FadeDither) instead of popping. An instance
 * fading out is still drawn until it is gone; a section leaving the list is still culled while its instances fade.
 */
namespace fade
{
const float kSeconds = 0.6f;
/** Marks the frame memory just before a fading draw's matrix as holding its fade (FadeHeader). */
const unsigned int kFadeMagic = 0x45444146;
const int kSectionNumbers = 4096;
const int kMaxLeaving = 64;
/** Everything shows at once after a cut: the camera jumping (metres) or turning (cosine of the angle) between two
 * frames - looking back, another camera - or a pause (seconds). */
const float kJumpDistance = 50.0f;
const float kCutCosine = 0.85f;
const float kPause = 0.5f;

struct Entry
{
    unsigned int frame;
    float value;
};

/** The fades of a section's instances, in the section's order: the culling walks a section's instances in order, so
 * their fades are read in order too (a hash table by instance address cost a cache miss for nearly every instance). */
struct SectionFades
{
    const Instance *instances;
    int count;
    Entry *entries;
};

/** A fading draw's matrix is allocated from frame memory right after one of these (the size of a matrix, so the
 * matrix keeps its alignment): eSetTransform finds a draw's fade with a range check and one read, where a hash table
 * lookup per draw cost a cache miss or two for every draw of every view. */
struct FadeHeader
{
    unsigned int magic;
    unsigned int frame;
    float fade;
    unsigned int check;
    float unused[12];
};
static_assert(sizeof(FadeHeader) == sizeof(bMatrix4), "FadeHeader");

/** -popdebug: fading draws this frame, the most in a frame, and draws that found their fade (g_drawFadesLost). */
int g_drawFades;
unsigned int g_drawFadesFrame;
int g_drawFadesPeak;
int g_drawFadesLost;

struct Leaving
{
    short section;
    float left;
};

SectionFades g_sectionFades[kSectionNumbers];
/** Starts at 1: entries stamped with frame 0 are empty. */
unsigned int g_frame = 1;
float g_time;
float g_step;
bool g_snap;
float g_lastCamera[3];
float g_lastDirection[3];
LARGE_INTEGER g_lastCounter;
/** Per section number: the last frame it was in the player view's list, the frame it entered it, and the frame it is
 * culled in while leaving. */
unsigned int g_listed[kSectionNumbers];
unsigned int g_entered[kSectionNumbers];
unsigned int g_leaving[kSectionNumbers];
Leaving g_leavers[kMaxLeaving];
int g_leaverCount;
/** Per section number: when it was first seen loaded, and the last frame it was seen loaded (shadow casters). */
float g_loadedAt[kSectionNumbers];
unsigned int g_loadedSeen[kSectionNumbers];
/** Per section number: the track streamer is about to unload it (nfs::culling::SectionUnloading), since when. */
bool g_unloading[kSectionNumbers];
float g_unloadingAt[kSectionNumbers];

/** A matrix for a fading draw, in frame memory after its FadeHeader; null when frame memory runs out. */
bMatrix4 *FadingMatrix(float fade)
{
    bMatrix4 *pair = eFrameMallocMatrix(2);
    if (!pair)
        return nullptr;
    FadeHeader &header = *reinterpret_cast<FadeHeader *>(pair);
    header.magic = kFadeMagic;
    header.frame = g_frame;
    header.fade = fade;
    header.check = kFadeMagic ^ g_frame;
    if (g_drawFadesFrame != g_frame)
    {
        g_drawFadesFrame = g_frame;
        g_drawFades = 0;
    }
    if (++g_drawFades > g_drawFadesPeak)
        g_drawFadesPeak = g_drawFades;
    return pair + 1;
}

float DrawFadeOf(const bMatrix4 *matrix)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(matrix);
    if (p < CurrentBufferStart + sizeof(FadeHeader) || p >= CurrentBufferEnd)
        return 1.0f;
    const FadeHeader &header = reinterpret_cast<const FadeHeader *>(matrix)[-1];
    if (header.magic != kFadeMagic || header.frame != g_frame || header.check != (kFadeMagic ^ g_frame))
        return 1.0f;
    ++g_drawFadesLost;
    return header.fade;
}

Entry &Find(const Section &section, int number, const Instance *instance)
{
    SectionFades &s = g_sectionFades[number];
    if (s.instances != section.instances || s.count != section.instanceCount)
    {
        // The section's first look, or another section (or a reload) at that number.
        ::operator delete(s.entries);
        s.instances = section.instances;
        s.count = section.instanceCount > 0 ? section.instanceCount : 0;
        s.entries = static_cast<Entry *>(::operator new(sizeof(Entry) * (s.count + 1)));
        memset(s.entries, 0, sizeof(Entry) * (s.count + 1));
    }
    const int index = static_cast<int>(instance - section.instances);
    // Past the end: the spare entry.
    return s.entries[index >= 0 && index < s.count ? index : s.count];
}

/** Start of the player view's culling: the time step, and whether to show everything at once. */
void BeginFrame(const ViewCull &player)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float elapsed = g_lastCounter.QuadPart
                              ? static_cast<float>(static_cast<double>(now.QuadPart - g_lastCounter.QuadPart) /
                                                   static_cast<double>(frequency.QuadPart))
                              : kPause * 2.0f;
    g_lastCounter = now;
    float moved = 0.0f, turned = 0.0f, length = 0.0f;
    for (int a = 0; a < 3; ++a)
    {
        const float d = player.cameraPosition[a] - g_lastCamera[a];
        moved += d * d;
        g_lastCamera[a] = player.cameraPosition[a];
        length += player.cameraDirection[a] * player.cameraDirection[a];
    }
    const float inverse = length > 0.0f ? 1.0f / sqrtf(length) : 0.0f;
    for (int a = 0; a < 3; ++a)
    {
        const float d = player.cameraDirection[a] * inverse;
        turned += d * g_lastDirection[a];
        g_lastDirection[a] = d;
    }
    g_snap = elapsed > kPause || moved > kJumpDistance * kJumpDistance || turned < kCutCosine;
    g_time += elapsed;
    g_step = elapsed / kSeconds;
    ++g_frame;
}

/** The player view's sections this frame: notes those entering the list, and adds those that left it a moment ago,
 * culled once more while their instances fade out; returns the new count. */
int Sections(short *sections, int count, int capacity)
{
    for (int i = 0; i < count; ++i)
    {
        const int s = sections[i];
        if (s < 0 || s >= kSectionNumbers)
            continue;
        if (g_listed[s] != g_frame - 1)
        {
            g_entered[s] = g_frame;
            if (!g_snap)
                nfs::shadow::debug::NoteList(s, true);
        }
        g_listed[s] = g_frame;
    }
    for (int s = 0; s < kSectionNumbers; ++s)
        if (g_listed[s] == g_frame - 1 && g_leaverCount < kMaxLeaving && !g_snap)
        {
            g_leavers[g_leaverCount++] = {static_cast<short>(s), g_time};
            nfs::shadow::debug::NoteList(s, false);
        }
    int kept = 0;
    for (int i = 0; i < g_leaverCount; ++i)
    {
        const Leaving l = g_leavers[i];
        if (g_listed[l.section] == g_frame || g_time - l.left > kSeconds || !SectionHeader(l.section))
            continue;
        g_leavers[kept++] = l;
        g_leaving[l.section] = g_frame;
        if (count < capacity)
            sections[count++] = l.section;
    }
    g_leaverCount = kept;
    return count;
}

/** Notes the loaded sections (from the cascades' culling, before the player view's): those that were not loaded last
 * frame start fading in. */
void NoteLoaded(const short *sections, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const int s = sections[i];
        if (s < 0 || s >= kSectionNumbers)
            continue;
        if (g_loadedSeen[s] != g_frame && g_loadedSeen[s] + 1 != g_frame)
            g_loadedAt[s] = g_frame <= 3 || g_snap ? -kSeconds : g_time;
        g_loadedSeen[s] = g_frame;
    }
}

/** What is left of a section the streamer is about to unload (1 for any other). */
float UnloadFade(int number)
{
    if (!g_unloading[number])
        return 1.0f;
    const float t = 1.0f - (g_time - g_unloadingAt[number]) / kSeconds;
    return t >= 1.0f ? 1.0f : t > 0.0f ? t : 0.0f;
}

/** How far the shadows of a section have faded in since it was loaded (1 once it has been loaded a moment), and out
 * once the streamer is about to unload it. */
float SectionFade(int number)
{
    if (number < 0 || number >= kSectionNumbers)
        return 1.0f;
    const float t = (g_time - g_loadedAt[number]) / kSeconds;
    const float in = t >= 1.0f ? 1.0f : t > 0.0f ? t : 0.0f;
    const float out = UnloadFade(number);
    return in < out ? in : out;
}

/** How much of a section's shadow casters to draw: as much as its scenery has faded in. A section numbered
 * ScenerySectionLODOffset above another (IsLODScenerySectionNumber) casts like any other: the game's "LOD" sections
 * hold the scenery seen from far, mostly models their section does not have (85 % of their model names over 90
 * pairs; the stadium's deck in I42 over I2's car park). They cast only while their section was not loaded, so the
 * sun shone under the stadium. */
float CasterFade(int number)
{
    return SectionFade(number);
}

/** Updates the instance's fade for this frame's verdict; true when it is to be drawn, with `fade`. */
bool Update(const Section &section, const Instance &instance, Verdict verdict, float &fade)
{
    const int number = section.number >= 0 && section.number < kSectionNumbers ? section.number : 0;
    Entry &e = Find(section, number, &instance);
    if (verdict != kDrawn && verdict != kPrecull && verdict != kSmall)
    {
        // Out of the view, or hidden by the game (a smashed prop): nothing fades. When it is back it shows as it is,
        // so turning the camera never fades anything in.
        e.frame = 0;
        return false;
    }
    const bool wanted = verdict == kDrawn && g_leaving[number] != g_frame && !g_unloading[number];
    if (g_snap)
        e.value = wanted ? 1.0f : 0.0f;
    else if (e.frame != g_frame - 1)
        // Not looked at last frame: it was out of the view (shown as it is), or its section has just entered the list
        // or been loaded (faded in).
        e.value = !wanted ? 0.0f : g_entered[number] == g_frame ? g_step : 1.0f;
    else if (wanted)
        e.value = e.value + g_step < 1.0f ? e.value + g_step : 1.0f;
    else
        e.value = e.value - g_step > 0.0f ? e.value - g_step : 0.0f;
    e.frame = g_frame;
    fade = e.value;
    return e.value > 0.0f;
}
} // namespace fade

/**
 * The far cascades' static layers (engine/rendering/shadows/shadow.h): which sections each holds in full, and whether the game changed
 * scenery since it was drawn.
 */
namespace statics
{
/** Per cascade and section number: its static layer holds the section's casters in full. */
unsigned char g_baked[nfs::shadow::kCascades][fade::kSectionNumbers];
bool g_dirty[nfs::shadow::kCascades];
/** The layer being drawn a slice at a time (kStaticSlice): the sections it holds so far, and the slice now drawn. */
unsigned char g_building[nfs::shadow::kCascades][fade::kSectionNumbers];
int g_slice;
int g_slices = 1;

bool Full(int number)
{
    return fade::CasterFade(number) == 1.0f;
}

/** Keeps the sections the pass draws: in a static pass those in full (which the layer now holds), in a dynamic pass
 * those it does not hold in full. */
int Filter(short *sections, int count)
{
    const int c = g_casterCascade;
    if (c < 0 || c >= nfs::shadow::kCascades)
        return count;
    int kept = 0;
    if (g_casterSet == nfs::culling::kStaticSlice)
    {
        for (int i = 0; i < count; ++i)
        {
            const int s = sections[i];
            if (s < 0 || s >= fade::kSectionNumbers || !Full(s) || s % g_slices != g_slice)
                continue;
            g_building[c][s] = 1;
            sections[kept++] = sections[i];
        }
    }
    else if (g_casterSet == nfs::culling::kStaticCasters)
    {
        memset(g_baked[c], 0, sizeof(g_baked[c]));
        g_dirty[c] = false;
        for (int i = 0; i < count; ++i)
        {
            const int s = sections[i];
            if (s < 0 || s >= fade::kSectionNumbers || !Full(s))
                continue;
            g_baked[c][s] = 1;
            sections[kept++] = sections[i];
        }
    }
    else
    {
        for (int i = 0; i < count; ++i)
        {
            const int s = sections[i];
            if (s >= 0 && s < fade::kSectionNumbers && g_baked[c][s] && Full(s))
                continue;
            sections[kept++] = sections[i];
        }
    }
    return kept;
}
} // namespace statics

/** An instance's placement (SceneryInstance::GetMatrix 0x6beeb0 and GetPosition 0x6bee80): its rotation, stored in
 * 1/8192 steps, and its position. */
void InstanceMatrix(const Instance &instance, bMatrix4 &matrix)
{
    const float kRotationStep = 1.0f / 8192.0f;
    float *m = reinterpret_cast<float *>(&matrix);
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
            m[row * 4 + column] = static_cast<float>(instance.rotation[row * 3 + column]) * kRotationStep;
        m[row * 4 + 3] = 0.0f;
    }
    m[12] = instance.position[0];
    m[13] = instance.position[1];
    m[14] = instance.position[2];
    m[15] = 1.0f;
}

/** Appends the instance to the view's draw table range; false when out of room or frame memory. A fading instance
 * (fade below 1) gets a matrix of its own, by which its draws find their fade (DrawFade). */
bool Append(ViewCull &cull, const Instance &instance, const Choice &c, float fade)
{
    const uintptr_t model = reinterpret_cast<uintptr_t>(c.model) + static_cast<uintptr_t>(c.visibility);
    if (instance.flags & kInstanceNoMatrix)
    {
        if (cull.cursor >= cull.limit)
            return false;
        bMatrix4 *matrix = nullptr;
        if (fade < 1.0f)
        {
            matrix = fade::FadingMatrix(fade);
            if (!matrix)
                return false;
            *matrix = eMathIdentityMatrix;
        }
        else if (c.hangs)
        {
            matrix = eFrameMallocMatrix(1);
            if (!matrix)
                return false;
            *matrix = eMathIdentityMatrix;
            matrix->v2.z = -1.0f;
            matrix->v3.z = 2.0f * c.plane;
        }
        *cull.cursor++ = {model, matrix, &instance};
        return true;
    }
    bMatrix4 *matrix = fade < 1.0f ? fade::FadingMatrix(fade) : eFrameMallocMatrix(1);
    if (!matrix)
        return false;
    InstanceMatrix(instance, *matrix);
    if (instance.flags & cull.flags & kCullRaise)
        matrix->v3.z += flt_902148;
    if (c.hangs)
    {
        // A whole mirror: it may lean, as a ramp's bridge does.
        matrix->v0.z = -matrix->v0.z;
        matrix->v1.z = -matrix->v1.z;
        matrix->v2.z = -matrix->v2.z;
        matrix->v3.z = 2.0f * c.plane - matrix->v3.z;
    }
    else if (cull.flags & kCullMirror)
    {
        matrix->v2.z = -matrix->v2.z;
    }
    if (cull.cursor >= cull.limit)
        return false;
    DrawEntry &entry = *cull.cursor++;
    entry.model = model;
    const SolidFields *solid = reinterpret_cast<const ModelFields *>(c.model)->solid;
    if ((cull.flags & kCullRotate) && solid && (solid->flags & kSolidRotates))
    {
        bMatrix4 rotation;
        const int phase = static_cast<int>(matrix->v3.x * kRotationPhasePerMetre) % kDegrees;
        NFS_ORIGINAL(0x73cad0, void(__cdecl *)(eView *, bMatrix4 *, int, bMatrix4 *))(cull.view, &rotation, phase, matrix);
        bMulMatrix(matrix, matrix, &rotation);
    }
    entry.matrix = matrix;
    entry.instance = &instance;
    if (cull.view == &eViews[kPlayerView] || cull.view == &eViews[kSecondPlayerView])
        AddMarkerFlares(cull, c.model, matrix, fade);
    return true;
}

/**
 * -popdebug: the player view's scenery frame by frame. An instance drawn in one frame and not the other is a pop when
 * it is big enough on screen; the cause is why it was not drawn (the culling's verdict, or its section missing from
 * the view's list or not loaded), or a model change between the frames (detail level). Instances entering or leaving
 * the view's frustum are left out.
 */
namespace pops
{
enum Cause
{
    kCauseVerdict,
    kCauseSection = kVerdicts,
    kCauseStreaming,
    kCauseTree,
    kCauseDetail,
    kCauses
};
const char *const kCauseNames[kCauses] = {"drawn",   "preculler", "excluded",  "outside",    "behind", "small",
                                          "nomodel", "noroom",    "section",   "streaming",  "tree",   "detail"};
const int kSlots = 1 << 16;
const int kMinPopPixels = 8;
const DWORD kReportPeriod = 5000;

struct Record
{
    const Instance *instance;
    unsigned int frame[2];
    const eModel *model[2];
    unsigned short pixels[2];
    unsigned char verdict[2];
    unsigned char fade[2];
    short section;
};

struct Example
{
    int pixels;
    int distance;
    int section;
    char name[24];
    int camera[3];
};

/** A section whose instances popped in for streaming or the visibility list, with the nearest one. */
struct SectionPop
{
    short section;
    short cause;
    int instances;
    int nearest;
};

struct Stats
{
    int count[2][kCauses];
    Example biggest[2][kCauses];
    SectionPop sections[24];
    int sectionCount;
};

Record *g_records;
int g_used;
unsigned int g_frame;
short g_sections[2][kMaxSections];
int g_sectionCount[2];
short g_loaded[2][kMaxSections];
int g_loadedCount[2];
Stats g_stats;
DWORD g_reported;
int g_tablePeak;
int g_noRoom;
/** Frames the player view kept its last drivable section's list (the camera over no drivable section). */
int g_listHeld;
/** Player view draws of fading instances, and the frames with any. */
int g_fadingDraws;
int g_fadingFrames;
unsigned int g_fadingFrame;
/** Fading draws by why (fading in, out for leaving the list, out for size) and the nearest of each (metres). */
int g_fadingWhy[3];
int g_fadingNearest[3];
/** Near fading draws named in the log this report (a few a report, so a race's start does not use them all up). */
int g_named;
int g_casterPopsIn;
int g_casterPopsOut;
int g_casterPopsStreamed;
int g_casterPopsStreamedIn;
int g_casterPopsStreamedInNear;
int g_casterPopsStreamedNear;
/** Calls of DoCulling in the last game frame, and the views of each. */
unsigned int g_callFrame;
int g_calls;
char g_callViews[256];
int g_callViewsLength;
bool g_enabled;
bool g_tracking;

/** The view tracked (Culling.PopView; the player's by default). */
int View()
{
    static const int view = nfs::config::Int("Culling", "PopView", kPlayerView);
    return view;
}

Record &Find(const Instance *instance)
{
    if (g_used > kSlots * 3 / 4)
    {
        memset(g_records, 0, sizeof(Record) * kSlots);
        g_used = 0;
    }
    unsigned int slot = (static_cast<unsigned int>(reinterpret_cast<uintptr_t>(instance)) >> 6) * 2654435761u >> 16;
    for (;; slot = (slot + 1) & (kSlots - 1))
    {
        Record &r = g_records[slot & (kSlots - 1)];
        if (r.instance == instance)
            return r;
        if (!r.instance)
        {
            r.instance = instance;
            ++g_used;
            return r;
        }
    }
}

void Note(const Section &section, const ViewCull &cull, const Instance &instance, Verdict verdict,
          const Choice &c, float fade)
{
    Record &r = Find(&instance);
    const int k = g_frame & 1;
    r.frame[k] = g_frame;
    r.verdict[k] = verdict;
    r.fade[k] = static_cast<unsigned char>(fade * 255.0f + 0.5f);
    r.model[k] = c.model;
    r.section = static_cast<short>(section.number);
    float center[3], radius = 0.0f;
    for (int a = 0; a < 3; ++a)
    {
        center[a] = (instance.boundsMin[a] + instance.boundsMax[a]) * 0.5f;
        const float half = (instance.boundsMax[a] - instance.boundsMin[a]) * 0.5f;
        radius += half * half;
    }
    radius = sqrtf(radius);
    const float dx = center[0] - cull.cameraPosition[0], dy = center[1] - cull.cameraPosition[1],
                dz = center[2] - cull.cameraPosition[2];
    const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    const float pixels = distance > radius ? radius * cull.H / distance : cull.H;
    r.pixels[k] = static_cast<unsigned short>(pixels > 65535.0f ? 65535.0f : pixels);
}

bool Contains(const short *list, int count, int number)
{
    for (int i = 0; i < count; ++i)
        if (list[i] == number)
            return true;
    return false;
}

int SectionCause(int section, int k)
{
    if (!Contains(g_loaded[k], g_loadedCount[k], section))
        return kCauseStreaming;
    if (!Contains(g_sections[k], g_sectionCount[k], section))
        return kCauseSection;
    return kCauseTree;
}

void Count(int direction, int cause, const Record &r, int k)
{
    const int pixels = r.pixels[k];
    if (pixels < kMinPopPixels || cause == kOutside || cause == kCauseTree)
        return;
    ++g_stats.count[direction][cause];
    if (direction == 0 && (cause == kCauseStreaming || cause == kCauseSection))
    {
        float nearest = 0.0f;
        const bVector3 &eye = eViews[kPlayerView].pCamera->CurrentKey.Position;
        const float point[3] = {eye.x, eye.y, eye.z};
        for (int a = 0; a < 3; ++a)
        {
            const float d = point[a] < r.instance->boundsMin[a]   ? r.instance->boundsMin[a] - point[a]
                            : point[a] > r.instance->boundsMax[a] ? point[a] - r.instance->boundsMax[a]
                                                                  : 0.0f;
            nearest += d * d;
        }
        const int metres = static_cast<int>(sqrtf(nearest));
        int i = 0;
        while (i < g_stats.sectionCount && g_stats.sections[i].section != r.section)
            ++i;
        if (i < 24)
        {
            SectionPop &sp = g_stats.sections[i];
            if (i == g_stats.sectionCount)
            {
                ++g_stats.sectionCount;
                sp = {r.section, static_cast<short>(cause), 0, metres};
            }
            ++sp.instances;
            sp.nearest = metres < sp.nearest ? metres : sp.nearest;
        }
    }
    Example &e = g_stats.biggest[direction][cause];
    if (pixels <= e.pixels)
        return;
    const Section *section = SectionHeader(r.section);
    const Instance &instance = *r.instance;
    e.pixels = pixels;
    e.section = r.section;
    e.name[0] = 0;
    if (section && section->instances <= &instance && &instance < section->instances + section->instanceCount)
        lstrcpynA(e.name, section->infos[instance.infoIndex].name, sizeof(e.name));
    const bVector3 &eye = eViews[kPlayerView].pCamera->CurrentKey.Position;
    const float dx = (instance.boundsMin[0] + instance.boundsMax[0]) * 0.5f - eye.x,
                dy = (instance.boundsMin[1] + instance.boundsMax[1]) * 0.5f - eye.y,
                dz = (instance.boundsMin[2] + instance.boundsMax[2]) * 0.5f - eye.z;
    e.distance = static_cast<int>(sqrtf(dx * dx + dy * dy + dz * dz));
    e.camera[0] = static_cast<int>(eye.x);
    e.camera[1] = static_cast<int>(eye.y);
    e.camera[2] = static_cast<int>(eye.z);
}

void Report()
{
    char line[1536];
    int length = wsprintfA(line, "pops (5 s, %d px or more, in/out):", kMinPopPixels);
    for (int cause = kPrecull; cause < kCauses; ++cause)
        if (g_stats.count[0][cause] || g_stats.count[1][cause])
            length += wsprintfA(line + length, " %s %d/%d", kCauseNames[cause], g_stats.count[0][cause],
                                g_stats.count[1][cause]);
    nfs::Log("%s; draw table peak %d of %d, %d left out for room, frame memory failed %d, fading draws peak %d (%d "
             "draws found their fade); culling calls %d:%s",
             line, g_tablePeak, kDrawTableSize, g_noRoom, FrameMallocFailed, fade::g_drawFadesPeak,
             fade::g_drawFadesLost, g_calls, g_callViews);
    fade::g_drawFadesPeak = fade::g_drawFadesLost = 0;
    nfs::Log("pops: shadow casters appearing inside their cascade %d (and %d with their section just loaded, %d of "
             "them within %d m), vanishing inside it %d (and %d with their section unloaded, %d of them within %d m)",
             g_casterPopsIn, g_casterPopsStreamedIn, g_casterPopsStreamedInNear, 250, g_casterPopsOut - g_casterPopsStreamed,
             g_casterPopsStreamed, g_casterPopsStreamedNear, 250);
    g_casterPopsIn = g_casterPopsOut = g_casterPopsStreamed = g_casterPopsStreamedIn = g_casterPopsStreamedInNear = 0;
    g_casterPopsStreamedNear = 0;
    if (g_listHeld)
        nfs::Log("pops: the camera was over no drivable section for %d frames (the last list kept)", g_listHeld);
    nfs::Log("pops: %d draws of fading scenery in %d frames: in %d (nearest %d m), leaving the list %d (%d m), small "
             "%d (%d m)", g_fadingDraws, g_fadingFrames, g_fadingWhy[0], g_fadingNearest[0], g_fadingWhy[1],
             g_fadingNearest[1], g_fadingWhy[2], g_fadingNearest[2]);
    g_listHeld = g_fadingDraws = g_fadingFrames = g_named = 0;
    for (int i = 0; i < 3; ++i)
    {
        g_fadingWhy[i] = 0;
        g_fadingNearest[i] = 100000;
    }
    for (int direction = 0; direction < 2; ++direction)
        for (int cause = kPrecull; cause < kCauses; ++cause)
        {
            const Example &e = g_stats.biggest[direction][cause];
            if (e.pixels)
                nfs::Log("pops:   biggest %s %s: section %d %s, %d px at %d m, camera %d %d %d",
                         direction ? "out" : "in", kCauseNames[cause], e.section, e.name, e.pixels, e.distance,
                         e.camera[0], e.camera[1], e.camera[2]);
        }
    if (g_stats.sectionCount)
    {
        int n = wsprintfA(line, "pops:   sections popping in (section cause instances nearest m):");
        for (int i = 0; i < g_stats.sectionCount && n < 1400; ++i)
            n += wsprintfA(line + n, " %d %s %d %d;", g_stats.sections[i].section,
                           g_stats.sections[i].cause == kCauseStreaming ? "stream" : "list",
                           g_stats.sections[i].instances, g_stats.sections[i].nearest);
        nfs::Log("%s", line);
    }
    memset(&g_stats, 0, sizeof(g_stats));
    g_tablePeak = 0;
    g_noRoom = 0;
}

/** After the frame's culling: compares this frame's verdicts with the last frame's. */
void Analyze()
{
    const unsigned int now = g_frame, before = g_frame - 1;
    const int k = now & 1, p = before & 1;
    for (int i = 0; i < kSlots; ++i)
    {
        const Record &r = g_records[i];
        if (!r.instance)
            continue;
        const bool seenNow = r.frame[k] == now, seenBefore = r.frame[p] == before;
        const bool drawnNow = seenNow && r.fade[k], drawnBefore = seenBefore && r.fade[p];
        if (drawnNow && drawnBefore)
        {
            if (r.model[k] != r.model[p])
                Count(0, kCauseDetail, r, k);
        }
        else if (drawnNow && r.fade[k] == 255)
            Count(0, seenBefore ? r.verdict[p] : SectionCause(r.section, p), r, k);
        else if (drawnBefore && r.fade[p] == 255)
            Count(1, seenNow ? r.verdict[k] : SectionCause(r.section, k), r, p);
    }
    if (GetTickCount() - g_reported > kReportPeriod)
    {
        g_reported = GetTickCount();
        Report();
    }
}
} // namespace pops

/**
 * -popdebug, shadow casters: the scenery each cascade draws, from one drawing of the cascade to its next. A caster drawn
 * one time and not the other while its bounds lie inside the cascade both times is a pop; its cause is its section
 * (loaded or unloaded in between) or the culling's verdict.
 */
namespace casters
{
const int kMaxCasters = 1 << 15;

struct Drawn
{
    const Instance *instance;
    int section;
    float shown;
    /** The instance's box, kept for when its section is gone. */
    float boundsMin[3];
    float boundsMax[3];
};

struct Cascade
{
    Drawn drawn[2][kMaxCasters];
    int count[2];
    bVector4 planes[2][6];
    /** Loaded sections at each drawing, by number. */
    unsigned char loaded[2][4096];
    int current;
    bool primed;
};

Cascade *g_cascades[nfs::shadow::kCascades];
int g_current = -1;
int g_popsIn;
int g_popsOut;
int g_popsStreamed;
int g_popsStreamedIn;
int g_popsStreamedInNear;
int g_popsStreamedNear;

/** Distance (squared) from the player camera to a box. */
float DistanceSquared(const float *boundsMin, const float *boundsMax)
{
    const bVector3 &eye = eViews[kPlayerView].pCamera->CurrentKey.Position;
    const float e[3] = {eye.x, eye.y, eye.z};
    float d2 = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        const float d = e[k] < boundsMin[k] ? boundsMin[k] - e[k] : e[k] > boundsMax[k] ? e[k] - boundsMax[k] : 0.0f;
        d2 += d * d;
    }
    return d2;
}
/** Streamed-in casters this close to the camera (metres) count as near. */
const float kNear = 250.0f;

bool Inside(const bVector4 *planes, const Instance &instance)
{
    for (int p = 0; p < 6; ++p)
    {
        const bVector4 &n = planes[p];
        const float x = n.x >= 0.0f ? instance.boundsMax[0] : instance.boundsMin[0];
        const float y = n.y >= 0.0f ? instance.boundsMax[1] : instance.boundsMin[1];
        const float z = n.z >= 0.0f ? instance.boundsMax[2] : instance.boundsMin[2];
        if (n.x * x + n.y * y + n.z * z + n.w < 0.0f)
            return false;
    }
    return true;
}

int ByInstance(const void *a, const void *b)
{
    const uintptr_t x = reinterpret_cast<uintptr_t>(static_cast<const Drawn *>(a)->instance);
    const uintptr_t y = reinterpret_cast<uintptr_t>(static_cast<const Drawn *>(b)->instance);
    return x < y ? -1 : x > y ? 1 : 0;
}

/** A caster that appeared or vanished, to the shadow debugger's log (named while its section is loaded). */
void Report(int cascade, bool appeared, const Drawn &d, const char *cause)
{
    if (!nfs::shadow::debug::Enabled())
        return;
    const Section *section = SectionHeader(d.section);
    const bool loaded = section && section->instances <= d.instance && d.instance < section->instances + section->instanceCount;
    char name[25] = "?";
    if (loaded)
        lstrcpynA(name, section->infos[d.instance->infoIndex].name, sizeof(name));
    const float centre[3] = {(d.boundsMin[0] + d.boundsMax[0]) * 0.5f, (d.boundsMin[1] + d.boundsMax[1]) * 0.5f,
                             (d.boundsMin[2] + d.boundsMax[2]) * 0.5f};
    nfs::shadow::debug::NoteCasterEvent(cascade, appeared, name, d.section, centre,
                                        static_cast<int>(sqrtf(DistanceSquared(d.boundsMin, d.boundsMax))), cause);
}

/** Start of a cascade's culling. */
void Begin(int cascade, const eView &view)
{
    if (!g_cascades[cascade])
    {
        g_cascades[cascade] = static_cast<Cascade *>(::operator new(sizeof(Cascade)));
        memset(g_cascades[cascade], 0, sizeof(Cascade));
    }
    Cascade &c = *g_cascades[cascade];
    c.current ^= 1;
    c.count[c.current] = 0;
    memcpy(c.planes[c.current], view.PlatInfo->FrustumPlanes, sizeof(c.planes[0]));
    memset(c.loaded[c.current], 0, sizeof(c.loaded[0]));
    const Section *head = reinterpret_cast<const Section *>(kLoadedSections);
    for (const Section *s = head->next; s && s != head; s = s->next)
        if (s->number >= 0 && s->number < 4096)
            c.loaded[c.current][s->number] = 1;
    g_current = cascade;
}

/** A scenery instance the cascade draws, and how much of it (fading in). */
void Note(const Section &section, const Instance &instance, bool drawn, float shown)
{
    if (g_current < 0 || !drawn)
        return;
    Cascade &c = *g_cascades[g_current];
    if (c.count[c.current] < kMaxCasters)
    {
        Drawn &d = c.drawn[c.current][c.count[c.current]++];
        d.instance = &instance;
        d.section = section.number;
        d.shown = shown;
        memcpy(d.boundsMin, instance.boundsMin, sizeof(d.boundsMin));
        memcpy(d.boundsMax, instance.boundsMax, sizeof(d.boundsMax));
    }
}

/** End of a cascade's culling: compares its casters with those of its last drawing. */
void End()
{
    if (g_current < 0)
        return;
    Cascade &c = *g_cascades[g_current];
    const int cascade = g_current;
    g_current = -1;
    const int now = c.current, before = now ^ 1;
    const int startIn = g_popsIn, startOut = g_popsOut, startStreamedIn = g_popsStreamedIn,
              startStreamed = g_popsStreamed;
    qsort(c.drawn[now], static_cast<size_t>(c.count[now]), sizeof(Drawn), ByInstance);
    if (!c.primed)
    {
        c.primed = true;
        return;
    }
    const Drawn *n = c.drawn[now], *p = c.drawn[before];
    int i = 0, j = 0;
    while (i < c.count[now] || j < c.count[before])
    {
        const uintptr_t a = i < c.count[now] ? reinterpret_cast<uintptr_t>(n[i].instance) : ~uintptr_t(0);
        const uintptr_t b = j < c.count[before] ? reinterpret_cast<uintptr_t>(p[j].instance) : ~uintptr_t(0);
        if (a == b)
        {
            ++i;
            ++j;
        }
        else if (a < b)
        {
            const Instance &inst = *n[i].instance;
            const int number = n[i].section;
            if (n[i].shown < 0.99f)
                ;
            else if (number >= 0 && number < 4096 && !c.loaded[before][number])
            {
                ++g_popsStreamedIn;
                const bVector3 &eye = eViews[kPlayerView].pCamera->CurrentKey.Position;
                const float e[3] = {eye.x, eye.y, eye.z};
                float d2 = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    const float d = e[k] < inst.boundsMin[k] ? inst.boundsMin[k] - e[k]
                                    : e[k] > inst.boundsMax[k] ? e[k] - inst.boundsMax[k] : 0.0f;
                    d2 += d * d;
                }
                g_popsStreamedInNear += d2 < kNear * kNear;
                if (d2 < kNear * kNear)
                    Report(cascade, true, n[i], "with its section just loaded");
            }
            else if (Inside(c.planes[before], inst))
            {
                ++g_popsIn;
                Report(cascade, true, n[i], "inside the cascade");
            }
            ++i;
        }
        else
        {
            const Section *section = SectionHeader(p[j].section);
            if (p[j].shown < 0.99f)
                // It was fading out (its section about to be unloaded, nfs::culling::SectionUnloading).
                ;
            else if (!section || p[j].instance < section->instances ||
                     p[j].instance >= section->instances + section->instanceCount)
            {
                ++g_popsStreamed;
                const bool nearby = DistanceSquared(p[j].boundsMin, p[j].boundsMax) < kNear * kNear;
                g_popsStreamedNear += nearby;
                if (nearby)
                    Report(cascade, false, p[j], "with its section unloaded");
            }
            else if (Inside(c.planes[now], *p[j].instance))
            {
                ++g_popsOut;
                Report(cascade, false, p[j], "inside the cascade");
            }
            ++j;
        }
    }
    nfs::shadow::debug::NoteCasterChanges(cascade, g_popsIn - startIn, g_popsOut - startOut,
                                          g_popsStreamedIn - startStreamedIn, g_popsStreamed - startStreamed);
}
} // namespace casters

void DumpScenery()
{
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (length > 0 && path[length - 1] != '\\' && path[length - 1] != '/')
        --length;
    lstrcpyA(path + length, "scenery_dump.txt");
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const char header[] = "section\tname\tx\ty\tz\tsize x\tsize y\tsize z\tflags\tradius\tlow model\r\n";
    DWORD written;
    WriteFile(file, header, sizeof(header) - 1, &written, nullptr);
    const Section *head = reinterpret_cast<const Section *>(kLoadedSections);
    int total = 0;
    for (const Section *s = head->next; s && s != head; s = s->next)
        for (int i = 0; s->instances && s->infos && i < s->instanceCount; ++i)
        {
            const Instance &inst = s->instances[i];
            const InstanceInfo &info = s->infos[inst.infoIndex];
            char name[25];
            lstrcpynA(name, info.name, sizeof(name));
            char line[256];
            const int n = wsprintfA(
                line, "%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%08x\t%d\t%d\r\n", s->number, name,
                static_cast<int>((inst.boundsMin[0] + inst.boundsMax[0]) * 0.5f),
                static_cast<int>((inst.boundsMin[1] + inst.boundsMax[1]) * 0.5f),
                static_cast<int>((inst.boundsMin[2] + inst.boundsMax[2]) * 0.5f),
                static_cast<int>(inst.boundsMax[0] - inst.boundsMin[0]), static_cast<int>(inst.boundsMax[1] - inst.boundsMin[1]),
                static_cast<int>(inst.boundsMax[2] - inst.boundsMin[2]), inst.flags, static_cast<int>(info.radius),
                info.models[2] && info.models[2] != info.models[0] ? 1 : 0);
            WriteFile(file, line, static_cast<DWORD>(n), &written, nullptr);
            ++total;
        }
    CloseHandle(file);
    nfs::Log("culling: %d scenery instances dumped to %s", total, path);
}

/**
 * The list ahead: a drivable section's list holds what can be seen from inside it, so the sections that only the next
 * one sees entered the list as the camera crossed into it and faded in in plain view (a golf course's ground shadows at
 * 59 m, at motorway speed). The loaded sections that the drivable sections ahead of the camera see (along its velocity,
 * kListLeadSeconds of travel, at least kListLeadMin and at most kListLeadMax metres) are in the list as well, so they
 * come in further away; one stays kListKeepSeconds after it was last ahead, so a bend does not take it in and out.
 * A section numbered ScenerySectionLODOffset above another (I42 over I2) is listed like any other: the game's "LOD"
 * sections hold the scenery that is seen from far (the stadium over I2, the walls round it), mostly models their
 * section does not have, and the visibility lists hold both.
 */
const float kListLeadSeconds = 1.2f;
const float kListLeadMin = 40.0f;
const float kListLeadMax = 200.0f;
const float kListStep = 25.0f;
const float kListKeepSeconds = 1.0f;
const int kMaxListAhead = 8;
/** Room left in the list for the sections fading out of it (fade::Sections). */
const int kListRoom = kMaxSections - 32;

struct ListAhead
{
    bVector2 point;
    short number;
    float seen;
};

ListAhead g_listAhead[kMaxListAhead];
int g_listAheadCount;
float g_listEye[3];
LARGE_INTEGER g_listCounter;
float g_listVelocity[2];
float g_listTime;

int ListIndex(const short *sections, int count, int number)
{
    for (int i = 0; i < count; ++i)
        if (sections[i] == number)
            return i;
    return -1;
}

int AddAhead(short *sections, int count, const bVector3 &eye)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float dt = g_listCounter.QuadPart ? static_cast<float>(static_cast<double>(now.QuadPart - g_listCounter.QuadPart) /
                                                                 static_cast<double>(frequency.QuadPart))
                                            : 0.0f;
    g_listCounter = now;
    const float mx = eye.x - g_listEye[0], my = eye.y - g_listEye[1];
    g_listEye[0] = eye.x;
    g_listEye[1] = eye.y;
    if (dt > 0.0f && dt < 0.25f && mx * mx + my * my < 400.0f)
    {
        const float k = dt / (dt + 0.1f);
        g_listVelocity[0] += (mx / dt - g_listVelocity[0]) * k;
        g_listVelocity[1] += (my / dt - g_listVelocity[1]) * k;
        g_listTime += dt;
    }
    else
    {
        g_listVelocity[0] = g_listVelocity[1] = 0.0f;
        g_listAheadCount = 0;
    }
    const float speed = sqrtf(g_listVelocity[0] * g_listVelocity[0] + g_listVelocity[1] * g_listVelocity[1]);
    if (speed > 2.0f)
    {
        float lead = speed * kListLeadSeconds;
        lead = lead < kListLeadMin ? kListLeadMin : lead > kListLeadMax ? kListLeadMax : lead;
        const float ux = g_listVelocity[0] / speed, uy = g_listVelocity[1] / speed;
        for (float d = kListStep; d < lead + 0.5f * kListStep; d += kListStep)
        {
            const bVector2 point(eye.x + ux * d, eye.y + uy * d);
            const char *drivable = reinterpret_cast<const char *>(TheVisibleSectionManager.FindDrivableSection(&point));
            if (!drivable)
                continue;
            const short number = *reinterpret_cast<const short *>(drivable + 0x0c);
            int i = 0;
            while (i < g_listAheadCount && g_listAhead[i].number != number)
                ++i;
            if (i == g_listAheadCount)
            {
                if (g_listAheadCount == kMaxListAhead)
                    continue;
                ++g_listAheadCount;
            }
            g_listAhead[i] = {point, number, g_listTime};
        }
    }
    int kept = 0;
    for (int a = 0; a < g_listAheadCount; ++a)
    {
        const ListAhead &ahead = g_listAhead[a];
        if (g_listTime - ahead.seen > kListKeepSeconds)
            continue;
        g_listAhead[kept++] = ahead;
        const char *drivable = reinterpret_cast<const char *>(TheVisibleSectionManager.FindDrivableSection(&ahead.point));
        if (!drivable || *reinterpret_cast<const short *>(drivable + 0x0c) != ahead.number)
            continue;
        const short visible = *reinterpret_cast<const short *>(drivable + 0x10);
        const short *list = reinterpret_cast<const short *>(drivable + 0x12);
        for (int v = 0; v < visible && v < 72 && count < kListRoom; ++v)
        {
            const int number = list[v];
            if (number < 0 || !SectionHeader(number) || ListIndex(sections, count, number) >= 0)
                continue;
            sections[count++] = static_cast<short>(number);
        }
    }
    g_listAheadCount = kept;
    return count;
}

/** The player view's sections: those the drivable section under its camera can see (WhatSectionsShouldWeDraw), and
 * those the drivable sections ahead of it see (AddAhead). When the camera is over no drivable section (above a
 * pavement, a wall, between road pieces) the original drew every loaded full-detail section instead, so the list jumped
 * back and forth as the camera crossed such spots and scenery came and went; the last drivable section's list stays
 * then. */
int PlayerSections(GrandSceneryCullInfo *grand, const ViewCull &cull, short *sections, SceneryCullInfo *info)
{
    static short s_last[kMaxSections];
    static int s_lastCount;
    static const bool ahead = nfs::config::Int("Culling", "ListAhead", 1) != 0;
    const bVector3 &eye = cull.view->pCamera->CurrentKey.Position;
    const bVector2 position(eye.x, eye.y);
    if (!TheVisibleSectionManager.FindDrivableSection(&position) && s_lastCount > 0)
    {
        ++pops::g_listHeld;
        memcpy(sections, s_last, sizeof(short) * s_lastCount);
        return s_lastCount;
    }
    int count = grand->WhatSectionsShouldWeDraw(sections, 0x80, info);
    if (ahead)
        count = AddAhead(sections, count, eye);
    memcpy(s_last, sections, sizeof(short) * count);
    s_lastCount = count;
    return count;
}

/** Whether the main culling should leave out a view: the original shadow view while the cascades replace it. */
bool Skipped(const ViewCull &cull)
{
    return !g_shadowCasters && cull.view->ID == kShadowView && nfs::shadow::Active();
}
} // namespace

void SetShadowCasters(bool on, int cascade, CasterSet set)
{
    static const bool popArgument = nfs::vk::HasArgument("-popdebug");
    const bool popDebug = popArgument || nfs::shadow::debug::Enabled();
    if (popDebug && !on)
    {
        casters::End();
        pops::g_casterPopsIn += casters::g_popsIn;
        pops::g_casterPopsOut += casters::g_popsOut + casters::g_popsStreamed;
        pops::g_casterPopsStreamed += casters::g_popsStreamed;
        pops::g_casterPopsStreamedIn += casters::g_popsStreamedIn;
        pops::g_casterPopsStreamedInNear += casters::g_popsStreamedInNear;
        pops::g_casterPopsStreamedNear += casters::g_popsStreamedNear;
        casters::g_popsIn = casters::g_popsOut = casters::g_popsStreamed = 0;
        casters::g_popsStreamedIn = casters::g_popsStreamedInNear = casters::g_popsStreamedNear = 0;
    }
    g_shadowCasters = on;
    g_casterSet = on ? set : kAllCasters;
    g_casterCascade = on ? cascade : -1;
    // The pop tracker compares whole passes; a far cascade's static and dynamic passes each hold only part.
    if (popDebug && on && set == kAllCasters && cascade >= 0 && cascade < nfs::shadow::kCascades)
        casters::Begin(cascade, eViews[kShadowView]);
}

void BeginStaticBuild(int cascade)
{
    if (cascade < 0 || cascade >= nfs::shadow::kCascades)
        return;
    memset(statics::g_building[cascade], 0, sizeof(statics::g_building[cascade]));
    statics::g_dirty[cascade] = false;
}

void SetStaticSlice(int slice, int slices)
{
    statics::g_slice = slice;
    statics::g_slices = slices > 0 ? slices : 1;
}

void FinishStaticBuild(int cascade)
{
    if (cascade < 0 || cascade >= nfs::shadow::kCascades)
        return;
    memcpy(statics::g_baked[cascade], statics::g_building[cascade], sizeof(statics::g_baked[cascade]));
}

bool StaticCastersChanged(int cascade)
{
    if (cascade < 0 || cascade >= nfs::shadow::kCascades)
        return false;
    if (statics::g_dirty[cascade])
        return true;
    const unsigned char *baked = statics::g_baked[cascade];
    for (int s = 0; s < fade::kSectionNumbers; ++s)
        if (baked[s] && (!SectionHeader(s) || !statics::Full(s)))
            return true;
    return false;
}

int UnbakedSections(int cascade)
{
    if (cascade < 0 || cascade >= nfs::shadow::kCascades)
        return 0;
    short sections[kMaxSections];
    const int count = LoadedSections(sections, kMaxSections);
    int unbaked = 0;
    for (int i = 0; i < count; ++i)
    {
        const int s = sections[i];
        unbaked += s >= 0 && s < fade::kSectionNumbers && !statics::g_baked[cascade][s] && statics::Full(s);
    }
    return unbaked;
}

void InvalidateStaticCasters()
{
    for (bool &dirty : statics::g_dirty)
        dirty = true;
}

float DrawFade(const bMatrix4 *world)
{
    return world ? fade::DrawFadeOf(world) : 1.0f;
}

void SectionUnloading(int section, bool unloading)
{
    using namespace fade;
    if (section < 0 || section >= kSectionNumbers || g_unloading[section] == unloading)
        return;
    if (unloading)
        g_unloadingAt[section] = g_time;
    else
        // Kept after all: back in from where the fade out had got to.
        g_loadedAt[section] = g_time - UnloadFade(section) * kSeconds;
    g_unloading[section] = unloading;
}

bool SectionFadedOut(int section)
{
    using namespace fade;
    return section < 0 || section >= kSectionNumbers || !g_unloading[section] || g_snap ||
           g_time - g_unloadingAt[section] >= kSeconds;
}
} // namespace nfs::culling

void GrandSceneryCullInfo::DoCulling()
{
    using namespace nfs::culling;
    GrandCull &grand = *reinterpret_cast<GrandCull *>(this);
    grand.begin = grand.cursor = g_drawTable;
    grand.limit = g_drawTable + kDrawTableSize;
    static const bool popDebug = nfs::vk::HasArgument("-popdebug");
    static const bool dump = nfs::vk::HasArgument("-dumpscenery");
    pops::g_tracking = false;
    if (popDebug)
    {
        if (pops::g_callFrame != eFrameCounter)
        {
            pops::g_callFrame = eFrameCounter;
            pops::g_calls = 0;
            pops::g_callViewsLength = 0;
        }
        ++pops::g_calls;
        for (int i = 0; i < grand.count && pops::g_callViewsLength < 200; ++i)
            pops::g_callViewsLength += wsprintfA(pops::g_callViews + pops::g_callViewsLength, "%s%d%s", i ? "," : " [",
                                                 grand.views[i].view->ID, i + 1 == grand.count ? "]" : "");
        if (g_shadowCasters && pops::g_callViewsLength < 200)
            pops::g_callViewsLength += wsprintfA(pops::g_callViews + pops::g_callViewsLength, "s");
    }
    for (int i = 0; i < grand.count; ++i)
    {
        ViewCull &cull = grand.views[i];
        const CameraParams &key = cull.view->pCamera->CurrentKey;
        cull.cameraPosition[0] = key.Position.x;
        cull.cameraPosition[1] = key.Position.y;
        cull.cameraPosition[2] = key.Position.z;
        cull.cameraDirection[0] = key.Direction.x;
        cull.cameraDirection[1] = key.Direction.y;
        cull.cameraDirection[2] = key.Direction.z;
        cull.H = cull.view->H;
        cull.precullCell = PrecullCell(cull);
        if (cull.flags & kCullMirror)
            mirror::BeginFrame(cull);
        pops::g_tracking |= popDebug && !g_shadowCasters && cull.view->ID == pops::View();
        if (!g_shadowCasters && cull.view->ID == kPlayerView)
            fade::BeginFrame(cull);
    }
    if (pops::g_tracking)
    {
        if (!pops::g_records)
            pops::g_records = static_cast<pops::Record *>(::operator new(sizeof(pops::Record) * pops::kSlots));
        if (!pops::g_enabled)
        {
            memset(pops::g_records, 0, sizeof(pops::Record) * pops::kSlots);
            pops::g_enabled = true;
        }
        ++pops::g_frame;
        const int k = pops::g_frame & 1;
        pops::g_loadedCount[k] = LoadedSections(pops::g_loaded[k], kMaxSections);
    }
    for (int i = 0; i < grand.count; ++i)
    {
        ViewCull &cull = grand.views[i];
        cull.begin = cull.cursor = grand.cursor;
        cull.limit = grand.limit;
        if (!Skipped(cull))
            CullView(reinterpret_cast<SceneryCullInfo *>(&cull));
        grand.cursor = cull.cursor;
        // The lamps' glows, with the player view's light flares (engine/rendering/lights/lamps.h).
        if (!g_shadowCasters && cull.view->ID == kPlayerView)
        {
            nfs::lamps::AddGlows(cull.cameraPosition);
            nfs::helilights::AddFlares();
        }
    }
    if (pops::g_tracking)
    {
        const int used = static_cast<int>(grand.cursor - grand.begin);
        pops::g_tablePeak = used > pops::g_tablePeak ? used : pops::g_tablePeak;
        pops::Analyze();
    }
    static DWORD dumpAt;
    if (dump && !g_shadowCasters && dumpAt != 1)
    {
        if (!dumpAt)
            dumpAt = GetTickCount() + 15000;
        else if (GetTickCount() > dumpAt)
        {
            dumpAt = 1;
            DumpScenery();
        }
    }
}

/**
 * The scenery culling of one view: the track sections the view may see (WhatSectionsShouldWeDraw: those the drivable
 * section under its camera can see; every loaded one for shadow casters), each culled by its bounding box tree.
 */
void GrandSceneryCullInfo::CullView(SceneryCullInfo *info)
{
    using namespace nfs::culling;
    const ViewCull &cull = *reinterpret_cast<const ViewCull *>(info);
    short sections[kMaxSections];
    int count;
    if (g_shadowCasters)
    {
        count = LoadedSections(sections, kMaxSections);
        fade::NoteLoaded(sections, count);
        if (g_casterSet != kAllCasters)
            count = statics::Filter(sections, count);
    }
    else if (cull.view->ID == kPlayerView)
    {
        count = PlayerSections(this, cull, sections, info);
        count = fade::Sections(sections, count, kMaxSections);
    }
    else
    {
        count = WhatSectionsShouldWeDraw(sections, 0x80, info);
    }
    if (pops::g_tracking && cull.view->ID == pops::View())
    {
        const int k = pops::g_frame & 1;
        memcpy(pops::g_sections[k], sections, sizeof(short) * count);
        pops::g_sectionCount[k] = count;
    }
    for (int i = 0; i < count; ++i)
    {
        if (sections[i] < 0)
            continue;
        Section *section = SectionHeader(sections[i]);
        if (section && section->tree)
            reinterpret_cast<ScenerySectionHeader *>(section)->TreeCull(info);
    }
}

namespace
{
/** A node of a section's bounding box tree: its box and up to five children, instances (>= 0) or nodes (< 0, counted
 * from the tree's start). */
struct TreeNode
{
    float min[3];
    float max[3];
    short count;
    short children[5];
};
static_assert(sizeof(TreeNode) == 0x24, "SceneryTreeNode");

/** Nodes waiting in TreeCull (the original kept 64). */
const int kTreeStack = 256;
const int kVisiblePartly = 1;
} // namespace

/**
 * Walks a section's bounding box tree for a view (original: zTrack 0x729640): a node partly in view tests its box,
 * one wholly in view passes that on to everything below it, and each instance reached goes to DrawAScenery with the
 * state of its node, in the original's order.
 */
void ScenerySectionHeader::TreeCull(SceneryCullInfo *info)
{
    using namespace nfs::culling;
    const TreeNode *tree = static_cast<const TreeNode *>(reinterpret_cast<const Section *>(this)->tree);
    void *view = reinterpret_cast<const ViewCull *>(info)->view;
    const bool mirrored = (reinterpret_cast<const ViewCull *>(info)->flags & kCullMirror) != 0;
    const TreeNode *nodes[kTreeStack];
    unsigned char states[kTreeStack];
    nodes[0] = tree;
    states[0] = kVisiblePartly;
    int depth = 1;
    while (depth)
    {
        --depth;
        const TreeNode *node = nodes[depth];
        int state = states[depth];
        if (state == kVisiblePartly && mirrored)
        {
            // What the node holds may show only by its reflection, under the road.
            const float reflected = 2.0f * mirror::g_cameraGround - node->max[2] - mirror::kNodeSlack;
            bVector3 lowest(node->min[0], node->min[1], reflected < node->min[2] ? reflected : node->min[2]);
            state = VisibleStateSB(view, nullptr, &lowest, reinterpret_cast<const bVector3 *>(node->max), nullptr);
        }
        else if (state == kVisiblePartly)
            state = VisibleStateSB(view, nullptr, reinterpret_cast<const bVector3 *>(node->min),
                                   reinterpret_cast<const bVector3 *>(node->max), nullptr);
        if (!state)
            continue;
        for (int i = 0; i < node->count; ++i)
        {
            const int child = node->children[i];
            if (child >= 0)
            {
                DrawAScenery(child, info, state);
            }
            else if (depth < kTreeStack)
            {
                nodes[depth] = tree - child;
                states[depth++] = static_cast<unsigned char>(state);
            }
        }
    }
}

/** One instance in a visible tree node of a view: drawn or not, with which model (see Judge). */
void ScenerySectionHeader::DrawAScenery(int index, SceneryCullInfo *info, int visibility)
{
    using namespace nfs::culling;
    const Section &section = *reinterpret_cast<const Section *>(this);
    ViewCull &cull = *reinterpret_cast<ViewCull *>(info);
    const Instance &instance = section.instances[index];
    Choice c = {nullptr, visibility, 0.0f, false, 0.0f};
    Verdict verdict = Judge(section, cull, instance, c);
    const bool player = !g_shadowCasters && cull.view->ID == kPlayerView;
    float shown = 1.0f;
    bool draw = player ? fade::Update(section, instance, verdict, shown) : verdict == kDrawn;
    if (g_shadowCasters && draw)
    {
        // The shadows of a section the streamer has just loaded fade in with its scenery (out before it is unloaded).
        shown = fade::CasterFade(section.number);
        draw = shown != 0.0f;
    }
    if (draw && !Append(cull, instance, c, shown))
    {
        verdict = kNoRoom;
        shown = 0.0f;
    }
    if (g_shadowCasters)
    {
        casters::Note(section, instance, draw && verdict != kNoRoom, shown);
        if (draw && verdict != kNoRoom)
            nfs::shadow::debug::NoteCaster();
    }
    if (player && draw && shown < 1.0f)
        nfs::shadow::debug::NoteFade(verdict == kDrawn && fade::g_leaving[section.number & (fade::kSectionNumbers - 1)] != fade::g_frame);
    if (pops::g_tracking && player && draw && shown < 1.0f)
    {
        const int why = verdict == kSmall ? 2 : fade::g_leaving[section.number & (fade::kSectionNumbers - 1)] == fade::g_frame ? 1 : 0;
        ++pops::g_fadingWhy[why];
        float d2 = 0.0f;
        for (int a = 0; a < 3; ++a)
        {
            const float e = cull.cameraPosition[a];
            const float d = e < instance.boundsMin[a] ? instance.boundsMin[a] - e : e > instance.boundsMax[a] ? e - instance.boundsMax[a] : 0.0f;
            d2 += d * d;
        }
        const int metres = static_cast<int>(sqrtf(d2));
        pops::g_fadingNearest[why] = metres < pops::g_fadingNearest[why] ? metres : pops::g_fadingNearest[why];
        static unsigned int s_namedFrame;
        if (metres < 60 && pops::g_named < 12 && s_namedFrame != pops::g_frame)
        {
            s_namedFrame = pops::g_frame;
            ++pops::g_named;
            char name[25];
            lstrcpynA(name, section.infos[instance.infoIndex].name, sizeof(name));
            nfs::Log("pops: fading %s near: section %d %s, %d m, fade %d%%, entered %d frames ago, loaded %d ms ago", why == 0 ? "in" : why == 1 ? "out (leaving)" : "out (small)",
                     section.number, name, metres, static_cast<int>(shown * 100), static_cast<int>(fade::g_frame - fade::g_entered[section.number & (fade::kSectionNumbers - 1)]),
                     static_cast<int>((fade::g_time - fade::g_loadedAt[section.number & (fade::kSectionNumbers - 1)]) * 1000));
        }
        ++pops::g_fadingDraws;
        if (pops::g_fadingFrame != pops::g_frame)
        {
            pops::g_fadingFrame = pops::g_frame;
            ++pops::g_fadingFrames;
        }
    }
    if (pops::g_tracking && cull.view->ID == pops::View())
    {
        pops::g_noRoom += verdict == kNoRoom;
        pops::Note(section, cull, instance, verdict, c, draw ? shown : 0.0f);
    }
}

namespace
{
/** StuffScenery's flags (the caller's choice of what to draw and how). */
const int kStuffExtraFlags = 0x1;
const int kStuffOverrideFlag = 0x80;
const int kStuffExclusive2000 = 0x10;
const int kStuffExclude2000 = 0x8;
const int kStuffShadowMesh = 0x100;
const int kStuffShadow = 0x200;
const int kStuffExclusive100000 = 0x400;
const int kStuffExclude400000 = 0x800;
const int kStuffExclusive1000000 = 0x1000;
const int kStuffShadowCasters = 0x2000;
/** Instance flags. */
const unsigned int kInstanceOverride = 0x80;
const unsigned int kInstanceFlag100 = 0x100;
const unsigned int kInstanceFlag20000 = 0x20000;
const unsigned int kInstanceHiddenWithOption = 0x40000;
const unsigned int kInstanceShownWithOption = 0x80000;
const unsigned int kInstanceNoShadow1 = 0x100000;
const unsigned int kInstanceNoShadow2 = 0x200000;
const unsigned int kInstanceFlag400000 = 0x400000;
const unsigned int kInstanceFlag1000000 = 0x1000000;
const unsigned int kInstanceShadowMapMesh = 0x4000000;
const unsigned int kInstanceNoShadow3 = 0x40000000;
/** The render flag for models that cast no shadow in the pass, and the one that skips the strips' view test. */
const unsigned int kRenderNoShadow = 0x10000;
const unsigned int kRenderNoCull = 4;
const unsigned int kVisibleFull = 2;
} // namespace

/**
 * Draws the instances the culling put in the view's range of the draw table (original: zTrack 0x723fa0), with the Xbox
 * 360 shadow pass's changes first (nfs::x360::PrepareShadowScenery).
 */
void GrandSceneryCullInfo::StuffScenery(eView *view, int flags)
{
    using namespace nfs::culling;
    nfs::profile::TimerScope profile(nfs::profile::kTimerStuffScenery);
    nfs::x360::PrepareShadowScenery(this, view);
    unsigned int extra = 0, exclusive = 0, exclude = 0;
    if (flags & kStuffExtraFlags)
        extra = 0x1000;
    if (flags & kStuffExclusive100000)
        exclusive = 0x100000;
    if (flags & kStuffOverrideFlag)
        extra |= 0x100;
    if (flags & kStuffExclusive2000)
        exclusive = 0x2000;
    else if (flags & kStuffExclude2000)
        exclude = 0x2000;
    if (flags & (kStuffShadowMesh | kStuffShadow))
        exclusive = kRenderNoShadow;
    if (flags & kStuffExclude400000)
        exclude |= 0x400000;
    if (flags & kStuffExclusive1000000)
        exclusive = 0x1000000;

    GrandCull &grand = *reinterpret_cast<GrandCull *>(this);
    ViewCull *cull = grand.views;
    ViewCull *const last = grand.views + grand.count;
    while (cull < last && cull->view != view)
        ++cull;
    if (cull == last)
        return;
    const bool option = dword_982B9C != 0;
    for (const DrawEntry *entry = cull->begin; entry < cull->cursor; ++entry)
    {
        const unsigned int instance = entry->instance->flags;
        if ((instance & kInstanceShownWithOption) && option)
            continue;
        if ((instance & kInstanceHiddenWithOption) && !option && !(instance & kInstanceFlag100))
            continue;
        eModel *model = reinterpret_cast<eModel *>(entry->model & ~3u);
        pDebugModel = model;
        unsigned int render = extra;
        if (instance & kInstanceOverride)
            render |= 0x2000;
        if (instance & kInstanceFlag1000000)
            render |= 0x100000;
        if (instance & kInstanceFlag100)
            render |= 0x1020000;
        if (instance & kInstanceFlag400000)
            render |= 0x40000;
        if (instance & kInstanceFlag20000)
            render |= 0x800000;
        if (instance & kInstanceShownWithOption)
            render |= 0x400000;
        if (flags & kStuffShadow)
        {
            const bool shadowMesh = (instance & kInstanceShadowMapMesh) != 0;
            if ((instance & (kInstanceNoShadow1 | kInstanceNoShadow2)) && !shadowMesh)
                render |= kRenderNoShadow;
            if (instance & kInstanceNoShadow3)
                render |= kRenderNoShadow;
        }
        if ((flags & kStuffShadowCasters) && (instance & kInstanceNoShadow3))
            render |= kRenderNoShadow;
        if ((flags & kStuffShadowMesh) && (instance & (kInstanceNoShadow1 | kInstanceNoShadow2 | kInstanceNoShadow3)))
            render |= kRenderNoShadow;
        if ((entry->model & 3) == kVisibleFull)
            render |= kRenderNoCull;
        const bool draw = (!exclusive && !exclude) || (exclusive & render) || (exclude && !(exclude & render));
        if (!draw)
            continue;
        reinterpret_cast<eViewPlatInterface *>(view)->Render(model, entry->matrix ? entry->matrix : &eMathIdentityMatrix, nullptr, render, nullptr);
        pDebugModel = nullptr;
    }
}

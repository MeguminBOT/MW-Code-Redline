/**
 * Drawing a world model and its hierarchy of parts (original: zWorld WorldModel::Render 0x7536b0, RenderNode 0x749be0,
 * RenderModel 0x73b9c0).
 *
 * ModelHeirarchy: nodes of 0x10 bytes from +8: +8 the node's eModel, +0xc bit 0 hidden, +0xe its child count, +0xf its
 * first child's index. WorldModel: +0x14 the node whose children +0x18 picks (a bit per child).
 */
#include "game/gen/api.h"
#include "legacy/link.h"

#include <math.h>

namespace
{
const int kNodes = 8;
const int kNodeBytes = 0x10;
const int kNodeModel = 8;
const int kNodeHidden = 0xc;
const int kNodeChildren = 0xe;
const int kNodeFirstChild = 0xf;
const int kPickedNode = 0x14;
const int kPickedChildren = 0x18;
} // namespace

void WorldModel::RenderNode(const ModelHeirarchy *hierarchy, unsigned int index, eView *view, int flags, bMatrix4 *blend,
                            const bMatrix4 *matrix)
{
    const unsigned char *base = reinterpret_cast<const unsigned char *>(hierarchy);
    const unsigned char *node = base + kNodes + index * kNodeBytes;
    char *self = reinterpret_cast<char *>(this);
    eModel *model = *reinterpret_cast<eModel *const *>(node + kNodeModel);
    if (model && *reinterpret_cast<void *const *>(reinterpret_cast<const char *>(model) + 0xc))
        RenderModel(model, view, flags, blend, matrix);
    for (unsigned int child = 0; child < node[kNodeChildren]; ++child)
    {
        if (*reinterpret_cast<unsigned int *>(self + kPickedNode) == index &&
            !(*reinterpret_cast<unsigned int *>(self + kPickedChildren) & (1u << (child & 31))))
            continue;
        const unsigned int next = node[kNodeFirstChild] + child;
        if (base[kNodes + next * kNodeBytes + kNodeHidden] & 1)
            continue;
        RenderNode(hierarchy, next, view, flags, blend, matrix);
    }
}

namespace
{
/** WorldModel: +8 its model, +0xc its model for the 0x800 pass, +0x10 its hierarchy (or none), +0x1c the frame it was
 * last drawn, +0x20/+0x24 the frame and the nearest distance to a car of the view's list this frame, +0x2d drawn in
 * the 0x200000 pass, +0x30 its SpaceNode (+0x20 its matrix, +0x60 that matrix again, +0xf4 dirty, +0xf8 a blend whose
 * matrix is at +0x40), or else +0x40 its own matrix. eView: +0x4 its kind, +0x24 the least pixel size drawn, +0x44
 * the list of cars near it (a node 4 bytes into its car; +0x1c the car's matrix). */
const int kModel = 8;
const int kPassModel = 0xc;
const int kHierarchy = 0x10;
const int kDrawnFrame = 0x1c;
const int kNearFrame = 0x20;
const int kNearest = 0x24;
const int kInShadowPass = 0x2d;
const int kSpace = 0x30;
const int kOwnMatrix = 0x40;
const unsigned int kPassAlternate = 0x800;
const unsigned int kPassShadow = 0x200000;
const unsigned int kPassShadowCasters = 0x2000;
const float kFar = 1000000.0f;
const int *const kBlendWorldModels = reinterpret_cast<const int *>(0x9017d0);

template <typename T> T &Field(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}
} // namespace

void WorldModel::Render(eView *view, int flags)
{
    char *self = reinterpret_cast<char *>(this);
    if (Field<unsigned int>(self, kNearFrame) != eFrameCounter)
    {
        Field<float>(self, kNearest) = kFar;
        Field<unsigned int>(self, kNearFrame) = eFrameCounter;
    }
    char *list = reinterpret_cast<char *>(view) + 0x44;
    char *node = Field<char *>(list, 0);
    const int kind = Field<int>(view, 4);
    if (node != list && node && node - 4 && (kind == 1 || kind == 2 || kind == 3))
    {
        char *space = Field<char *>(self, kSpace);
        const float *own = space ? &Field<float>(space, 0x60 + 0x30) : &Field<float>(self, kOwnMatrix + 0x30);
        const float *car = &Field<float>(Field<char *>(node - 4, 0x1c), 0x40);
        const float dx = car[0] - own[0], dy = car[1] - own[1], dz = car[2] - own[2];
        const float distance = sqrtf(dz * dz + dy * dy + dx * dx);
        const float nearest = Field<float>(self, kNearest);
        Field<float>(self, kNearest) = nearest < distance ? nearest : distance;
    }
    eModel *model = Field<eModel *>(self, kModel);
    if (!model && Field<char *>(self, kHierarchy))
        model = Field<eModel *>(Field<char *>(self, kHierarchy), (Field<int>(self, 0x14) + 1) * 0x10);
    if (flags & kPassAlternate)
    {
        model = Field<eModel *>(self, kPassModel);
        if (!model)
            return;
    }
    if (!model || !Field<void *>(model, 0xc))
        return;
    if (flags & kPassShadow)
    {
        if (!Field<char>(self, kInShadowPass))
            return;
        const char caster = Field<char>(Field<char *>(model, 0xc), 0x18);
        if ((flags & kPassShadowCasters) ? caster != 0 : caster == 0)
            return;
    }
    bMatrix4 *blend = nullptr;
    const bMatrix4 *matrix;
    alignas(16) float local[16];
    if (char *space = Field<char *>(self, kSpace))
    {
        if (Field<int>(space, 0xf4))
            NFS_ORIGINAL(0x7477f0, void(__thiscall *)(void *))(space);
        blend = Field<bMatrix4 *>(Field<char *>(self, kSpace), 0xf8);
        matrix = reinterpret_cast<const bMatrix4 *>(space + 0x20);
        if (!*kBlendWorldModels && blend)
            return;
        if (*kBlendWorldModels && blend)
            NFS_ORIGINAL(0x45ecf0, void(__cdecl *)(void *, const void *, const void *))(
                local, matrix, reinterpret_cast<char *>(blend) + 0x40);
        else
            NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(local, matrix);
    }
    else
    {
        matrix = reinterpret_cast<const bMatrix4 *>(self + kOwnMatrix);
        NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(local, matrix);
    }
    if (view->GetPixelSize(reinterpret_cast<const bVector3 *>(&local[12]), 35.0f) < Field<int>(view, 0x24))
        return;
    if (!NFS_ORIGINAL(0x502860, int(__thiscall *)(eView *, eModel *, const void *))(view, model, local))
        return;
    if (char *hierarchy = Field<char *>(self, kHierarchy))
        RenderNode(reinterpret_cast<const ModelHeirarchy *>(hierarchy), Field<unsigned int>(self, 0x14), view, flags, blend,
                   matrix);
    else
        RenderModel(model, view, flags, blend, matrix);
    Field<unsigned int>(self, kDrawnFrame) = eFrameCounter;
}

namespace
{
/** A model's bounding box (the solid's +0x20 and +0x30) grown by a blend's offset (+0x70) while it draws. */
struct BoundsAdjust
{
    void *min;
    void *max;
    void *offset;

    void Adjust(float sign)
    {
        reinterpret_cast<AABBAdjustor *>(this)->Adjust(sign);
    }
};

const int kLit = 0x2b;
const int kReplacedLightMaterial = 0x80;
const int kReplacedLightKey = 0x84;
const int kViewCamera = 0x40;
const int kBlendOffset = 0x70;
const unsigned int kLightsBytes = 0x130;
eShaperLightRig *const kBlendRig = reinterpret_cast<eShaperLightRig *>(0x8f2f10);
eShaperLightRig *const kWorldModelRig = reinterpret_cast<eShaperLightRig *>(0x8f2e70);
int *const kQuickLightUsed = reinterpret_cast<int *>(0x8f2fa0);
} // namespace

void WorldModel::RenderModel(eModel *model, eView *view, int flags, bMatrix4 *blend, const bMatrix4 *matrix)
{
    char *self = reinterpret_cast<char *>(this);
    BoundsAdjust bounds = {nullptr, nullptr, nullptr};
    if (blend)
    {
        char *solid = Field<char *>(model, 0xc);
        bounds = {solid + 0x20, solid + 0x30, reinterpret_cast<char *>(blend) + kBlendOffset};
        bounds.Adjust(1.0f);
    }
    bMatrix4 *drawn = eFrameMallocMatrix(1);
    if (drawn)
    {
        NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(drawn, matrix);
        if (flags & kPassAlternate)
            Field<float>(drawn, 0x28) = -Field<float>(drawn, 0x28);
        void *lights = nullptr;
        bool draw = true;
        if (Field<char>(self, kLit))
        {
            lights = NFS_ORIGINAL(0x405e80, void *(__cdecl *)(unsigned int))(kLightsBytes);
            if (!lights)
                draw = false;
            else
            {
                char *camera = Field<char *>(view, kViewCamera);
                alignas(16) float eye[4] = {Field<float>(camera, 0x40), Field<float>(camera, 0x44), Field<float>(camera, 0x48),
                                            1.0f};
                if (blend)
                {
                    alignas(16) float offset[4];
                    for (int i = 0; i < 4; ++i)
                        offset[i] = Field<float>(blend, kBlendOffset + i * 4);
                    NFS_ORIGINAL(0x6bd920, void(__cdecl *)(float *, const void *, const float *))(offset, drawn, offset);
                    alignas(16) float placed[16];
                    NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(placed, drawn);
                    for (int i = 0; i < 4; ++i)
                        placed[12 + i] = offset[i];
                    AdjustQuickDynamicLight(kBlendRig, reinterpret_cast<bVector3 *>(eye));
                    elSetupLightContext(static_cast<eDynamicLightContext *>(lights), kBlendRig,
                                        reinterpret_cast<bMatrix4 *>(placed), reinterpret_cast<bMatrix4 *>(camera),
                                        reinterpret_cast<bVector4 *>(eye), view);
                    *kQuickLightUsed = 0;
                }
                else
                    elSetupLightContext(static_cast<eDynamicLightContext *>(lights), kWorldModelRig, drawn,
                                        reinterpret_cast<bMatrix4 *>(camera), reinterpret_cast<bVector4 *>(eye), view);
            }
        }
        if (draw)
        {
            if (eLightMaterial *material = Field<eLightMaterial *>(self, kReplacedLightMaterial))
                model->ReplaceLightMaterial(Field<unsigned int>(self, kReplacedLightKey), material);
            reinterpret_cast<eViewPlatInterface *>(view)->Render(model, drawn, static_cast<eLightContext *>(lights), 0, blend);
        }
    }
    if (bounds.offset)
        bounds.Adjust(-1.0f);
}

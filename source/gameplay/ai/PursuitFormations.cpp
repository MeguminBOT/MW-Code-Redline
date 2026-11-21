/** Pursuit formations: the slots around the target cops are sent to, per formation (original: zAI PursuitFormation,
 * PitFormation, HerdFormation, FollowFormation, StaggerFollowFormation, BoxInFormation, RollingBlockFormation
 * constructors; tables read by
 * tools/porting/formation_tables.py). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x43f120, PursuitFormationConstruct)
NFS_REPLACE(0x43fe90, PitFormationConstruct)
NFS_REPLACE(0x440050, HerdFormationConstruct)
NFS_REPLACE(0x43f770, FollowFormationConstruct)
NFS_REPLACE(0x43fb00, StaggerFollowFormationConstruct)
NFS_REPLACE(0x43f1a0, BoxInFormationConstruct)
NFS_REPLACE(0x43f520, RollingBlockFormationConstruct)
NFS_REPLACE(0x42f630, BoxInFormationUpdate)
NFS_REPLACE(0x42f730, RollingBlockFormationUpdate)
NFS_REPLACE(0x42b130, HerdFormationUpdate)

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

/** PursuitFormation::TargetOffset: where the cop goes relative to the target, where it starts from, how many cops take
 * the slot, and the goal they get there. */
struct TargetOffset
{
    float offset[3];
    float start[3];
    int cops;
    unsigned int goal;
};

const unsigned int *const kNoGoal = reinterpret_cast<const unsigned int *>(0x91ddbc);

void AddOffset(char *formation, const TargetOffset &offset)
{
    NFS_ORIGINAL(0x4393f0, void(__thiscall *)(void *, const TargetOffset *))(formation + 0x10, &offset);
}
/** The tightness and spacing a pursuit's levels give a box-in or rolling block: two attributes of the chased perp's
 * pursuit levels, or 0.5 and 2 without them. */
void FormationSpacing(char *self, void *pursuit, unsigned int tightKey, unsigned int spaceKey)
{
    using Virtual = void *(__thiscall *)(void *);
    char *target = static_cast<char *>((*reinterpret_cast<Virtual **>(pursuit))[2](pursuit));
    void *chased = At<void *>(target, 0x1c);
    void *perp = chased ? NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(
                              At<void *>(chased, 4), reinterpret_cast<void *>(0x4037e0))
                        : nullptr;
    void *levels = perp ? (*reinterpret_cast<Virtual **>(perp))[4](perp) : nullptr;
    if (!levels)
    {
        At<float>(self, 0x20) = 0.5f;
        At<float>(self, 0x24) = 2.0f;
        return;
    }
    using Get = const void *(__thiscall *)(void *, unsigned int, unsigned int);
    const void *tight = NFS_ORIGINAL(0x454810, Get)(levels, tightKey, 0);
    if (!tight)
        tight = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
    At<unsigned int>(self, 0x20) = *static_cast<const unsigned int *>(tight);
    const void *space = NFS_ORIGINAL(0x454810, Get)(levels, spaceKey, 0);
    if (!space)
        space = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
    At<unsigned int>(self, 0x24) = *static_cast<const unsigned int *>(space);
}

const float *const kRollingBlockSlots = reinterpret_cast<const float *>(0x890e58);
const int *const kRollingBlockCops = reinterpret_cast<const int *>(0x892f20);
const int kRollingBlockSlotCount = 5;
} // namespace

void *__fastcall PursuitFormationConstruct(char *self)
{
    At<unsigned int>(self, 0) = 0x892b68;
    At<int>(self, 4) = 0;
    At<int>(self, 8) = 1;
    At<unsigned char>(self, 0xc) = 0;
    At<void *>(self, 0x14) = nullptr;
    At<void *>(self, 0x18) = nullptr;
    At<void *>(self, 0x1c) = nullptr;
    return self;
}

void *__fastcall PitFormationConstruct(char *self, void *, int)
{
    PursuitFormationConstruct(self);
    At<unsigned int>(self, 0) = 0x892f70;
    const unsigned int pit = stringhash32(reinterpret_cast<const char *>(0x892f84));
    AddOffset(self, {{4.0f, 0.0f, -2.7f}, {-10.0f, 0.0f, -2.7f}, 1, pit});
    const unsigned int pitAgain = stringhash32(reinterpret_cast<const char *>(0x892f84));
    AddOffset(self, {{-4.0f, 0.0f, -2.7f}, {10.0f, 0.0f, -2.7f}, 1, pitAgain});
    At<int>(self, 4) = 1;
    At<unsigned char>(self, 0xc) = 1;
    return self;
}

void *__fastcall HerdFormationConstruct(char *self, void *, int)
{
    PursuitFormationConstruct(self);
    At<unsigned int>(self, 0) = 0x892f90;
    AddOffset(self, {{-3.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 1, *kNoGoal});
    AddOffset(self, {{-3.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, 2, *kNoGoal});
    AddOffset(self, {{-3.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 0.0f}, 3, *kNoGoal});
    At<int>(self, 4) = 3;
    At<unsigned char>(self, 0xc) = 0;
    return self;
}

void *__fastcall FollowFormationConstruct(char *self, void *, int)
{
    PursuitFormationConstruct(self);
    At<unsigned int>(self, 0) = 0x892f48;
    AddOffset(self, {{0.0f, 0.0f, -13.0f}, {0.0f, 0.0f, 0.0f}, 1, *kNoGoal});
    AddOffset(self, {{3.5f, 0.0f, -13.0f}, {0.0f, 0.0f, 0.0f}, 2, *kNoGoal});
    AddOffset(self, {{-3.5f, 0.0f, -13.0f}, {0.0f, 0.0f, 0.0f}, 2, *kNoGoal});
    AddOffset(self, {{0.0f, 0.0f, -17.0f}, {0.0f, 0.0f, 0.0f}, 3, *kNoGoal});
    AddOffset(self, {{3.5f, 0.0f, -17.0f}, {0.0f, 0.0f, 0.0f}, 4, *kNoGoal});
    AddOffset(self, {{-3.5f, 0.0f, -17.0f}, {0.0f, 0.0f, 0.0f}, 4, *kNoGoal});
    At<int>(self, 4) = 6;
    At<unsigned char>(self, 0xc) = 0;
    return self;
}

void *__fastcall StaggerFollowFormationConstruct(char *self, void *, int)
{
    PursuitFormationConstruct(self);
    At<unsigned int>(self, 0) = 0x892f5c;
    AddOffset(self, {{0.0f, 0.0f, -13.0f}, {0.0f, 0.0f, 0.0f}, 1, *kNoGoal});
    AddOffset(self, {{0.0f, 0.0f, 13.0f}, {0.0f, 0.0f, 0.0f}, 1, *kNoGoal});
    AddOffset(self, {{3.5f, 0.0f, -13.0f}, {0.0f, 0.0f, 0.0f}, 2, *kNoGoal});
    AddOffset(self, {{-3.5f, 0.0f, 13.0f}, {0.0f, 0.0f, 0.0f}, 2, *kNoGoal});
    AddOffset(self, {{-3.5f, 0.0f, -13.0f}, {0.0f, 0.0f, 0.0f}, 3, *kNoGoal});
    AddOffset(self, {{3.5f, 0.0f, 13.0f}, {0.0f, 0.0f, 0.0f}, 3, *kNoGoal});
    At<int>(self, 4) = 6;
    At<unsigned char>(self, 0xc) = 0;
    return self;
}

void *__fastcall BoxInFormationConstruct(char *self, void *, int, void *pursuit)
{
    PursuitFormationConstruct(self);
    At<unsigned int>(self, 0) = 0x892f0c;
    FormationSpacing(self, pursuit, 0x67a15750, 0x858b1097);
    const float tight = 0.7f - At<float>(self, 0x20) * 0.5f;
    const float level = tight * 0.0f;
    const float back = 2.0f - At<float>(self, 0x20) * 5.0f;
    const char *goal = reinterpret_cast<const char *>(0x892f00);
    AddOffset(self, {{0.0f, 0.0f, 14.0f}, {level, level, back}, 1, stringhash32(goal)});
    AddOffset(self, {{-3.5f, 0.0f, 0.0f}, {tight * -3.5f, level, level}, 2, stringhash32(goal)});
    AddOffset(self, {{3.5f, 0.0f, 0.0f}, {tight * 3.5f, level, level}, 2, stringhash32(goal)});
    AddOffset(self, {{0.0f, 0.0f, -7.5f}, {level, level, tight * -7.5f}, 4, stringhash32(goal)});
    At<int>(self, 4) = 4;
    At<int>(self, 8) = 2;
    At<unsigned char>(self, 0xc) = 1;
    return self;
}

void *__fastcall RollingBlockFormationConstruct(char *self, void *, int, void *pursuit)
{
    PursuitFormationConstruct(self);
    At<unsigned int>(self, 0) = 0x892f34;
    FormationSpacing(self, pursuit, 0xc146fc03, 0x5c9f5f55);
    const float tight = 1.0f - At<float>(self, 0x20) * 0.8f;
    const float back = 2.0f - At<float>(self, 0x20) * 5.0f;
    for (int i = 0; i < kRollingBlockSlotCount; ++i)
    {
        const float *slot = kRollingBlockSlots + i * 3;
        const unsigned int goal = stringhash32(reinterpret_cast<const char *>(0x892f00));
        AddOffset(self, {{slot[0], slot[1], slot[2]}, {tight * slot[0], tight * slot[1], back}, kRollingBlockCops[i], goal});
    }
    At<int>(self, 4) = 4;
    At<int>(self, 8) = 2;
    At<unsigned char>(self, 0xc) = 1;
    return self;
}

namespace
{
/** How far the formation has closed in: the pursuit's finisher time over the formation's own, weighted by tightness. */
float Closing(char *self, void *pursuit, float weight)
{
    using Number = float(__thiscall *)(void *);
    const float elapsed = (*reinterpret_cast<Number **>(pursuit))[0x5c / 4](pursuit);
    const float limit = (*reinterpret_cast<Number **>(self))[0x10 / 4](self);
    const float ratio = elapsed / limit;
    return ratio * weight + (1.0f - weight);
}

void SetOffset(char *self, int slot, float x, float y, float z)
{
    float *offset = reinterpret_cast<float *>(At<char *>(self, 0x14) + slot * sizeof(TargetOffset));
    offset[0] = x;
    offset[1] = y;
    offset[2] = z;
}
} // namespace

void __fastcall BoxInFormationUpdate(char *self, void *, float, void *pursuit)
{
    const float s = Closing(self, pursuit, (At<float>(self, 0x20) + 1.0f) * 0.2f);
    const float level = 0.0f * s;
    SetOffset(self, 0, level, level, s * 14.0f);
    SetOffset(self, 1, s * -3.5f, level, level);
    SetOffset(self, 2, s * 3.5f, level, level);
    SetOffset(self, 3, level, level, s * -7.5f);
}

void __fastcall RollingBlockFormationUpdate(char *self, void *, float, void *pursuit)
{
    const float s = Closing(self, pursuit, At<float>(self, 0x20) * 0.4f);
    const float level = 0.0f * s;
    const float ahead = s * 14.0f;
    SetOffset(self, 0, level, level, ahead);
    SetOffset(self, 1, s * -2.5f, level, ahead);
    SetOffset(self, 2, s * 2.5f, level, ahead);
    SetOffset(self, 3, s * -5.0f, level, ahead);
    SetOffset(self, 4, s * 5.0f, level, ahead);
}

void __fastcall HerdFormationUpdate(char *self, void *, float, void *pursuit)
{
    using Get = void *(__thiscall *)(void *);
    char *target = static_cast<char *>((*reinterpret_cast<Get **>(pursuit))[2](pursuit));
    if (!target || !At<void *>(target, 0x1c))
        return;
    void *ai = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(At<char *>(target, 0x1c), 4),
                                                                           reinterpret_cast<void *>(0x403700));
    if (!ai || !(*reinterpret_cast<Get **>(ai))[0x48 / 4](ai))
        return;
    alignas(16) unsigned char nav[0x300];
    NFS_ORIGINAL(0x77b9e0, void *(__thiscall *)(void *))(nav);
    const float *chased = reinterpret_cast<const float *>(target + 0x10);
    const float *heading = reinterpret_cast<const float *>(target + 0x20);
    const float direction[3] = {heading[0], heading[1], heading[2]};
    NFS_ORIGINAL(0x7862d0, void(__thiscall *)(void *, const float *, const float *, int, int))(nav, chased, direction, 1,
                                                                                         0);
    const float *road = reinterpret_cast<const float *>(nav + 0x98);
    const float on[3] = {road[0], road[1], road[2]};
    const float *along = reinterpret_cast<const float *>(nav + 0xbc);
    float forward[3] = {along[0], along[1], along[2]};
    NFS_ORIGINAL(0x410220, float(__cdecl *)(float *))(forward);
    const float dx = chased[0] - on[0];
    const float dy = chased[1] - on[1];
    const float dz = chased[2] - on[2];
    const float lateral = -forward[0] * dz + forward[1] * dy + dx * forward[2];
    void *network = *reinterpret_cast<void **>(0x9b38b4);
    const char *segments = *reinterpret_cast<const char *const *>(0x9b38c0);
    const short segment = *reinterpret_cast<const short *>(nav + 0x8e);
    const unsigned char *profile = static_cast<const unsigned char *>(
        NFS_ORIGINAL(0x765410, const void *(__thiscall *)(void *, const void *, int))(
            network, segments + segment * 0x16, *reinterpret_cast<const signed char *>(nav + 0x8c)));
    if (profile && profile[0])
    {
        float segmentForward[3];
        NFS_ORIGINAL(0x773fb0, void(__thiscall *)(void *, int, float *))(network, segment, segmentForward);
        const bool against = direction[2] * segmentForward[2] + direction[1] * segmentForward[1] +
                                 direction[0] * segmentForward[0] <
                             0.0f;
        const int lanes = NFS_ORIGINAL(0x4042a0, int(__thiscall *)(const void *, int, bool))(profile, 1, against);
        float outermost = 0.0f;
        for (int lane = 0; lane < lanes; ++lane)
        {
            const int index = against ? profile[0] - lane - 1 : lane;
            if ((reinterpret_cast<const unsigned int *>(profile + 4)[index] & 0xf) != 1)
                continue;
            const float offset = NFS_ORIGINAL(0x410670, float(__thiscall *)(const void *, int, bool))(profile, lane, against);
            if (!(outermost > offset))
                outermost = offset;
        }
        const float wanted = lateral - outermost + 2.0f;
        float push;
        if (3.0f < wanted)
            push = 3.0f;
        else
            push = 1.0f > wanted ? 1.0f : wanted;
        for (char *slot = At<char *>(self, 0x14); slot != At<char *>(self, 0x18); slot += sizeof(TargetOffset))
            *reinterpret_cast<float *>(slot) = -push;
    }
    NFS_ORIGINAL(0x76e220, void(__thiscall *)(void *))(nav);
}

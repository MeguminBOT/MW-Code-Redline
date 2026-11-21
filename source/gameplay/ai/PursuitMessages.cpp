/** The messages cops and pursuits send, and pursuit attribute instances and table lengths (original: zAI
 * MForcePursuitStart, MReqRoadBlock, MPerpBusted, MNotifyPursuitLength, MSetCopsEnabled, Attrib::Gen::pursuitlevels
 * constructors, ChangeWithDefault, Num_CopFormations, pursuitsupport Num_*SupportOptions). */
#include "legacy/link.h"

NFS_REPLACE(0x405870, MForcePursuitStartConstruct)
NFS_REPLACE(0x405a40, MReqRoadBlockConstruct)
NFS_REPLACE(0x408150, MPerpBustedConstruct)
NFS_REPLACE(0x40a7c0, MNotifyPursuitLengthConstruct)
NFS_REPLACE(0x604610, MSetCopsEnabledConstruct)
NFS_REPLACE(0x402db0, PursuitLevelsCopy)
NFS_REPLACE(0x402e10, PursuitLevelsFromSpec)
NFS_REPLACE(0x410320, PursuitLevelsFromKey)
NFS_REPLACE(0x6fe980, PursuitLevelsFromCollection)
NFS_REPLACE(0x704030, PursuitLevelsChangeWithDefault)
NFS_REPLACE(0x402fb0, PursuitLevelsNumCopFormations)
NFS_REPLACE(0x403580, PursuitSupportNumAirSupportOptions)
NFS_REPLACE(0x403600, PursuitSupportNumHeavySupportOptions)
NFS_REPLACE(0x403680, PursuitSupportNumLeaderSupportOptions)

unsigned int *__cdecl MForcePursuitStartGetKind(unsigned int *kind);
unsigned int *__cdecl MReqRoadBlockGetKind(unsigned int *kind);
unsigned int *__cdecl MPerpBustedGetKind(unsigned int *kind);
unsigned int *__cdecl MNotifyPursuitLengthGetKind(unsigned int *kind);
unsigned int *__cdecl MSetCopsEnabledGetKind(unsigned int *kind);

namespace
{
/** Hermes::Message: its kind, a word kept zero, its size, another zero word; the payload follows. */
void Header(unsigned int *message, unsigned int kind, unsigned int size)
{
    message[0] = kind;
    message[1] = 0;
    message[3] = 0;
    message[2] = size;
}

const unsigned int kPursuitLevelsClass = 0x551e22b3;
const unsigned int kPursuitLevelsLayoutSize = 0xc8;

unsigned int Length(void *instance, unsigned int key)
{
    alignas(8) unsigned char attribute[32];
    NFS_ORIGINAL(0x4546c0, void *(__thiscall *)(void *, void *, unsigned int))(instance, attribute, key);
    const unsigned int length = NFS_ORIGINAL(0x452d40, unsigned int(__thiscall *)(void *))(attribute);
    NFS_ORIGINAL(0x452bd0, void(__thiscall *)(void *))(attribute);
    return length;
}

void DefaultLayout(unsigned int *instance)
{
    if (!instance[2])
        instance[2] = reinterpret_cast<unsigned int>(
            NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(kPursuitLevelsLayoutSize));
}
} // namespace

void *__fastcall MForcePursuitStartConstruct(unsigned int *self, void *, int heat)
{
    unsigned int kind;
    Header(self, *MForcePursuitStartGetKind(&kind), 0x14);
    self[4] = static_cast<unsigned int>(heat);
    return self;
}

void *__fastcall MReqRoadBlockConstruct(unsigned int *self, void *, int cops)
{
    unsigned int kind;
    Header(self, *MReqRoadBlockGetKind(&kind), 0x14);
    self[4] = static_cast<unsigned int>(cops);
    return self;
}

void *__fastcall MPerpBustedConstruct(unsigned int *self, void *, void *simable)
{
    unsigned int kind;
    Header(self, *MPerpBustedGetKind(&kind), 0x14);
    self[4] = reinterpret_cast<unsigned int>(simable);
    return self;
}

void *__fastcall MNotifyPursuitLengthConstruct(unsigned int *self, void *, void *simable, float length)
{
    unsigned int kind;
    Header(self, *MNotifyPursuitLengthGetKind(&kind), 0x18);
    self[4] = reinterpret_cast<unsigned int>(simable);
    *reinterpret_cast<float *>(self + 5) = length;
    return self;
}

void *__fastcall MSetCopsEnabledConstruct(unsigned int *self, void *, bool enabled)
{
    unsigned int kind;
    Header(self, *MSetCopsEnabledGetKind(&kind), 0x14);
    *reinterpret_cast<unsigned char *>(self + 4) = enabled;
    return self;
}

void *__fastcall PursuitLevelsCopy(unsigned int *self, void *, const void *other)
{
    NFS_ORIGINAL(0x4523c0, void *(__thiscall *)(void *, const void *))(self, other);
    DefaultLayout(self);
    return self;
}

void *__fastcall PursuitLevelsFromSpec(unsigned int *self, void *, const void *spec, unsigned int index, void *owner)
{
    NFS_ORIGINAL(0x456cb0, void *(__thiscall *)(void *, const void *, unsigned int, void *))(self, spec, index, owner);
    DefaultLayout(self);
    return self;
}

void *__fastcall PursuitLevelsFromKey(unsigned int *self, void *, unsigned int key, unsigned int index, void *owner)
{
    const void *collection = NFS_ORIGINAL(0x455fd0, const void *(__cdecl *)(unsigned int, unsigned int))(
        kPursuitLevelsClass, key);
    NFS_ORIGINAL(0x452380, void *(__thiscall *)(void *, const void *, unsigned int, void *))(self, collection, index,
                                                                                           owner);
    DefaultLayout(self);
    return self;
}

void *__fastcall PursuitLevelsFromCollection(unsigned int *self, void *, const void *collection, unsigned int index,
                                             void *owner)
{
    NFS_ORIGINAL(0x452380, void *(__thiscall *)(void *, const void *, unsigned int, void *))(self, collection, index,
                                                                                           owner);
    DefaultLayout(self);
    return self;
}

void __fastcall PursuitLevelsChangeWithDefault(void *self, void *, unsigned int key)
{
    const void *collection = NFS_ORIGINAL(0x456050, const void *(__cdecl *)(unsigned int, unsigned int))(
        kPursuitLevelsClass, key);
    NFS_ORIGINAL(0x45a760, void(__thiscall *)(void *, const void *))(self, collection);
}

unsigned int __fastcall PursuitLevelsNumCopFormations(void *self)
{
    return Length(self, 0x5c2a7972);
}

unsigned int __fastcall PursuitSupportNumAirSupportOptions(void *self)
{
    return Length(self, 0x3c6dbcb3);
}

unsigned int __fastcall PursuitSupportNumHeavySupportOptions(void *self)
{
    return Length(self, 0xa73c3512);
}

unsigned int __fastcall PursuitSupportNumLeaderSupportOptions(void *self)
{
    return Length(self, 0xebd53935);
}

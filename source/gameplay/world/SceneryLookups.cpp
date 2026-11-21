/** Scenery lookups by hash and section number (original: zTrack FindSceneryGroup, FindSceneryInfo, FindSceneryInstance,
 * FindSceneryHeirarchyByName, BuildSceneryOverrideHashTable; zSpeech IsLODScenerySectionNumber, IsTextureSection). */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
struct Node
{
    Node *next;
    Node *prev;
};

Node *const kGroups = reinterpret_cast<Node *>(0x9ab24c);
Node *const kPacks = reinterpret_cast<Node *>(0x9aa478);
const int kPackInfos = 0x18;
const int kPackInfoCount = 0x1c;
const int kPackInstances = 0x20;
const int kPackInstanceCount = 0x24;
const int kInfoSize = 0x48;
const int kInfoModel = 0x28;
const int kInstanceSize = 0x40;
const int kInstanceInfo = 0x3e;
const int kNameHash = 8;

void *const kHeirarchies = reinterpret_cast<void *>(0x9aa4c4);

unsigned short *const kOverrideStart = reinterpret_cast<unsigned short *>(0x9a3740);
const int kOverrideKeys = 0x100;
const int kOverrideSize = 6;

const int kSectionsPerLetter = 100;

template <typename T> T &At(const void *base, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(base) + offset));
}

bool InfoNamed(const char *info, unsigned int hash)
{
    const char *model = At<const char *>(info, kInfoModel);
    return model && At<unsigned int>(model, kNameHash) == hash;
}
} // namespace

SceneryGroup *FindSceneryGroup(unsigned int hash)
{
    for (Node *n = kGroups->next; n != kGroups; n = n->next)
        if (At<unsigned int>(n, kNameHash) == hash)
            return reinterpret_cast<SceneryGroup *>(n);
    return nullptr;
}

SceneryInfo *FindSceneryInfo(unsigned int hash)
{
    for (Node *pack = kPacks->next; pack != kPacks; pack = pack->next)
    {
        const char *infos = At<const char *>(pack, kPackInfos);
        const int count = At<int>(pack, kPackInfoCount);
        for (int i = 0; i < count; ++i)
            if (InfoNamed(infos + i * kInfoSize, hash))
                return reinterpret_cast<SceneryInfo *>(const_cast<char *>(infos + i * kInfoSize));
    }
    return nullptr;
}

SceneryInstance *FindSceneryInstance(unsigned int hash)
{
    for (Node *pack = kPacks->next; pack != kPacks; pack = pack->next)
    {
        const char *infos = At<const char *>(pack, kPackInfos);
        char *instances = At<char *>(pack, kPackInstances);
        const int count = At<int>(pack, kPackInstanceCount);
        for (int i = 0; i < count; ++i)
        {
            char *instance = instances + i * kInstanceSize;
            if (InfoNamed(infos + At<short>(instance, kInstanceInfo) * kInfoSize, hash))
                return reinterpret_cast<SceneryInstance *>(instance);
        }
    }
    return nullptr;
}

ModelHeirarchy *FindSceneryHeirarchyByName(unsigned int hash)
{
    void *found;
    NFS_ORIGINAL(0x72e480, void *(__thiscall *)(void *, void **, const unsigned int *))(kHeirarchies, &found, &hash);
    if (found == At<void *>(kHeirarchies, 4))
        return nullptr;
    return At<ModelHeirarchy *>(found, 0x10);
}

void BuildSceneryOverrideHashTable()
{
    const unsigned char *entries = *reinterpret_cast<const unsigned char *const *>(0x9a3a64);
    const int count = *reinterpret_cast<const int *>(0x9a3a68);
    int index = 0;
    for (int key = 0; key < kOverrideKeys; ++key)
    {
        kOverrideStart[key] = static_cast<unsigned short>(index);
        while (index < count && entries[index * kOverrideSize] == key)
            ++index;
    }
    kOverrideStart[kOverrideKeys] = static_cast<unsigned short>(index);
}

bool IsLODScenerySectionNumber(int section)
{
    const int number = section % kSectionsPerLetter;
    const int lodStart = *reinterpret_cast<const int *>(0x903164);
    return number >= lodStart && number < lodStart * 2;
}

bool IsTextureSection(int section)
{
    const char letter = static_cast<char>(section / kSectionsPerLetter + 0x40);
    return letter == 'Y' || letter == 'W';
}

const UMath::Matrix4 &Attrib::Gen::visuallook::BlackBloomCurve() const
{
    return *reinterpret_cast<const UMath::Matrix4 *>(At<const char *>(this, 8) + 0x40);
}

const float &Attrib::Gen::visuallook::BlackBloomIntensity() const
{
    return *reinterpret_cast<const float *>(At<const char *>(this, 8) + 0xdc);
}

const UMath::Matrix4 &Attrib::Gen::visuallook::ColourBloomCurve() const
{
    return *reinterpret_cast<const UMath::Matrix4 *>(At<const char *>(this, 8) + 0x80);
}

const float &Attrib::Gen::visuallook::ColourBloomIntensity() const
{
    return *reinterpret_cast<const float *>(At<const char *>(this, 8) + 0xd0);
}

const UMath::Vector4 &Attrib::Gen::visuallook::ColourBloomTint() const
{
    return *reinterpret_cast<const UMath::Vector4 *>(At<const char *>(this, 8) + 0xc0);
}

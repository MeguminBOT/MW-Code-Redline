/** DrawCar/DrawVehicle visibility, child models and effect lists; wheel skid getters (original: zPhysicsBehaviors). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x693af0, DrawCarIsHidden)
NFS_REPLACE(0x693b20, DrawCarHideModel)
NFS_REPLACE(0x693b40, DrawCarReleaseModel)
NFS_REPLACE(0x693b70, DrawCarOnServiceSmall)
NFS_REPLACE(0x6b6f80, DrawCarIsPartVisible)
NFS_REPLACE(0x6b4560, DrawVehicleStopEffects)
NFS_REPLACE(0x6b6f20, DrawVehicleStopEffect)
NFS_REPLACE(0x6a9530, SuspensionRacerGetWheelSkid)
NFS_REPLACE(0x6ae270, SuspensionSplineGetWheelSkid)
NFS_REPLACE(0x6b0480, DrawVehicleGetChildModel)
NFS_REPLACE(0x6b03e0, DrawVehicleEnumerateChildren)
NFS_REPLACE(0x6bbba0, DrawCarInView)
NFS_REPLACE(0x6bbbb0, DrawCarIsRenderable)
NFS_REPLACE(0x6bbbd0, DrawCarDistanceToView)
NFS_REPLACE(0x6bbbe0, DrawCarReset)
NFS_REPLACE(0x6bbbf0, DrawCarOnTaskSimulate)
NFS_REPLACE(0x6bbc00, DrawCarInViewThunk)
NFS_REPLACE(0x6bbc10, DrawCarDistanceToViewThunk)
NFS_REPLACE(0x6bb540, DrawCarShowPart)
NFS_REPLACE(0x6bb9f0, DrawCarHidePart)
NFS_REPLACE(0x6ba250, DrawCarReleaseChildModels)
NFS_REPLACE(0x6ba010, DrawVehicleReleaseChildModels)
NFS_REPLACE(0x6bb7f0, DrawVehiclePlayEffect)
NFS_REPLACE(0x6a3c70, DrawCarOnBehaviorChange)
NFS_REPLACE(0x6a39a0, DrawVehiclePartCreateTrigger)

namespace
{
void *const kModelHandle = reinterpret_cast<void *>(0x68b7e0);
void *const kEffectAllocator = reinterpret_cast<void *>(0x925b30);
const int kNodeSize = 0xc;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

struct Node
{
    Node *next;
    Node *prev;
    void *item;
};

void FreeNode(Node *node, unsigned int size = kNodeSize)
{
    NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(kEffectAllocator, node,
                                                                                           size, nullptr);
}

void DeleteEffect(void *effect)
{
    Virtual<void *, unsigned int>(effect, 0, 1u);
}

void *FindModel(void *object, void *handle = kModelHandle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4), handle);
}

void *Children(void *self)
{
    return Virtual<void *>(static_cast<char *>(self) + 0xc, 0x18);
}

unsigned int NullName()
{
    return *reinterpret_cast<const unsigned int *>(0x91ddbc);
}
} // namespace

int __fastcall DrawCarIsHidden(char *self)
{
    if (self[0x64])
        return 1;
    if (!Virtual<bool>(At<void *>(self, -0xc), 0x88))
        return 1;
    return self[-0x27] ? 1 : 0;
}

void __fastcall DrawCarHideModel(char *self)
{
    Virtual<void>(At<void *>(self, -0xc), 0x84);
    self[0x64] = 1;
}

void __fastcall DrawCarReleaseModel(char *self)
{
    Virtual<void>(self, 0x64);
    if (void *service = At<void *>(self, 0x30))
    {
        NFS_ORIGINAL(0x6eea10, void(__thiscall *)(void *, void *))(self - 0x54, service);
        At<void *>(self, 0x30) = nullptr;
    }
}

bool __fastcall DrawCarOnServiceSmall(char *self, void *, void *service, void *packet)
{
    if (service != At<void *>(self, 0x84))
        return false;
    if (!Virtual<bool>(self + 0x54, 0x24))
    {
        Virtual<void, void *>(self, 0x2c, packet);
        return true;
    }
    self[0xb0] = 0;
    return false;
}

bool __fastcall DrawCarIsPartVisible(char *self, void *, const UCrc32 &part)
{
    void *found;
    NFS_ORIGINAL(0x6b4e60, void *(__thiscall *)(void *, void **, const UCrc32 &))(self + 0x50, &found, part);
    return found == At<void *>(self, 0x54);
}

void __fastcall DrawVehicleStopEffects(char *self)
{
    Node *head = At<Node *>(self, 0x20);
    for (Node *n = head->next; n != At<Node *>(self, 0x20); n = n->next)
        if (n->item)
            DeleteEffect(n->item);
    head = At<Node *>(self, 0x20);
    Node *n = head->next;
    head->next = head;
    head->prev = head;
    At<int>(self, 0x24) = 0;
    while (n != At<Node *>(self, 0x20))
    {
        Node *next = n->next;
        FreeNode(n);
        n = next;
    }
}

void __fastcall DrawVehicleStopEffect(char *self, void *, unsigned int name)
{
    Node *head = At<Node *>(self, 0x20);
    Node *n = head->next;
    while (n != head && At<unsigned int>(n->item, 0x58) != name)
        n = n->next;
    if (n == head)
        return;
    DeleteEffect(n->item);
    if (n == At<Node *>(self, 0x20))
        return;
    n->prev->next = n->next;
    n->next->prev = n->prev;
    FreeNode(n);
    --At<int>(self, 0x24);
}

float __fastcall SuspensionRacerGetWheelSkid(char *self, void *, unsigned int wheel)
{
    return At<float>(At<void *>(self, 0x168 + wheel * 4), 0x130);
}

float __fastcall SuspensionSplineGetWheelSkid(char *self, void *, unsigned int wheel)
{
    return At<float>(At<void *>(self, 0x134 + wheel * 4), 0xe0);
}

void *__fastcall DrawVehicleGetChildModel(char *self, void *, unsigned int name)
{
    if (name == NullName())
        return nullptr;
    void *children = Children(self);
    if (!children)
        return nullptr;
    Node *head = At<Node *>(children, 4);
    for (Node *n = head->next; n != At<Node *>(children, 4); n = n->next)
    {
        void *model = FindModel(n->item);
        if (!model)
            continue;
        unsigned int part;
        if (*Virtual<unsigned int *, unsigned int *>(model, 8, &part) == name)
            return model;
    }
    return nullptr;
}

void *__fastcall DrawVehicleEnumerateChildren(char *self, void *, void *enumerator)
{
    void *children = Children(self);
    if (!children)
        return enumerator;
    char *root = self - 0x54 ? self : nullptr;
    for (Node *n = At<Node *>(children, 4)->next; n != At<Node *>(children, 4); n = n->next)
    {
        void *model = FindModel(n->item);
        if (!model)
            continue;
        char *parent = Virtual<char *>(model, 0x58);
        const bool mine = parent ? root && At<void *>(parent, 4) == At<void *>(root, 4) : !root;
        if (!mine)
            continue;
        if (!Virtual<bool, void *>(enumerator, 0, model))
            return enumerator;
    }
    return enumerator;
}

char __fastcall DrawCarInView(char *self)
{
    return self[0x64];
}

int __fastcall DrawCarIsRenderable(char *self)
{
    return NFS_ORIGINAL(0x6ee9f0, int(__thiscall *)(void *, void *))(self - 0x4c, At<void *>(self, 0x38)) == 1 ? 1 : 0;
}

float __fastcall DrawCarDistanceToView(char *self)
{
    return At<float>(self, 0x68);
}

void __fastcall DrawCarReset(void *)
{
}

void __fastcall DrawCarOnTaskSimulate(void *, void *, float)
{
}

char __fastcall DrawCarInViewThunk(char *self)
{
    return DrawCarInView(self - 8);
}

float __fastcall DrawCarDistanceToViewThunk(char *self)
{
    return DrawCarDistanceToView(self - 8);
}

namespace
{
struct PartNode
{
    void *left;
    void *parent;
    void *right;
    unsigned int key;
    int hidden;
};

PartNode *FindPart(char *self, const UCrc32 &part)
{
    PartNode *found;
    NFS_ORIGINAL(0x6b4df0, void *(__thiscall *)(void *, PartNode **, const UCrc32 &))(self + 0x50, &found, part);
    return found;
}
} // namespace

void __fastcall DrawCarShowPart(char *self, void *, const UCrc32 &part)
{
    PartNode *node = FindPart(self, part);
    if (node == At<PartNode *>(self, 0x54))
        return;
    if (--node->hidden > 0)
        return;
    void *next;
    NFS_ORIGINAL(0x6bb050, void *(__thiscall *)(void *, void **, PartNode *))(self + 0x50, &next, node);
}

void __fastcall DrawCarHidePart(char *self, void *, const UCrc32 &part)
{
    PartNode *node = FindPart(self, part);
    if (node != At<PartNode *>(self, 0x54))
    {
        ++node->hidden;
        return;
    }
    *NFS_ORIGINAL(0x6bb8f0, int *(__thiscall *)(void *, const UCrc32 &))(self + 0x50, part) = 1;
}

void __fastcall DrawVehicleReleaseChildModels(char *self)
{
    if (void *old = At<void *>(self, 0x18))
        Virtual<void *, unsigned int>(old, 0, 1u);
    char *attachments = static_cast<char *>(NFS_ORIGINAL(
        0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(kEffectAllocator, 0x14, nullptr));
    if (!attachments)
    {
        At<void *>(self, 0x18) = nullptr;
        return;
    }
    At<unsigned int>(attachments, 0) = 0x8952f0;
    At<void *>(attachments, 8) = NFS_ORIGINAL(0x478ec0, void *(__thiscall *)(void *))(attachments + 4);
    At<int>(attachments, 0xc) = 0;
    At<char *>(attachments, 0x10) = self - 0x54 ? self + 0xc : nullptr;
    At<char *>(self, 0x18) = attachments;
}

void __fastcall DrawCarReleaseChildModels(char *self)
{
    DrawVehicleReleaseChildModels(self);
    char *map = self + 0x50;
    char *head = At<char *>(map, 4);
    NFS_ORIGINAL(0x6a6070, void(__thiscall *)(void *, void *))(map, At<void *>(head, 4));
    At<char *>(At<char *>(map, 4), 4) = At<char *>(map, 4);
    At<int>(map, 8) = 0;
    At<char *>(At<char *>(map, 4), 0) = At<char *>(map, 4);
    At<char *>(At<char *>(map, 4), 8) = At<char *>(map, 4);
}
void __fastcall DrawVehiclePlayEffect(char *self, void *, unsigned int name, const void *attributes,
                                      const void *position, const void *magnitude, bool tracking)
{
    if (name == NullName())
        return;
    Node *head = At<Node *>(self, 0x20);
    void *effect = nullptr;
    for (Node *n = head->next; n != head; n = n->next)
    {
        if (At<unsigned int>(n->item, 0x58) == name)
        {
            effect = n->item;
            break;
        }
    }
    if (!effect)
    {
        effect = NFS_ORIGINAL(0x652ac0, void *(__cdecl *)(unsigned int))(0x5c);
        if (effect)
        {
            void *owner = Virtual<void *>(self, 0xc);
            const unsigned int world = At<unsigned int>(Virtual<void *>(self, 0x44), 4);
            NFS_ORIGINAL(0x6eea40, void *(__thiscall *)(void *, void *, unsigned int))(effect, owner, world);
            At<unsigned int>(effect, 0) = 0x8ab9b4;
            At<unsigned int>(effect, 0x58) = name;
        }
        char *list = self + 0x1c;
        Node *end = At<Node *>(list, 4);
        Node *node = NFS_ORIGINAL(0x6a62e0, Node * (__thiscall *)(void *, Node *, Node *, void **))(list, end,
                                                                                                    end->prev, &effect);
        NFS_ORIGINAL(0x6ba290, void(__thiscall *)(void *, unsigned int))(list, 1);
        end->prev = node;
        node->prev->next = node;
    }
    NFS_ORIGINAL(0x6f44e0, void(__thiscall *)(void *, const void *, const void *, const void *, int, bool, int))(
        effect, attributes, position, magnitude, 0, tracking, 0);
}

namespace
{
/** A mechanic that changed (the BEHAVIOR_MECHANIC_* hashes) and the interfaces DrawCar keeps for it, by their handles.
 */
struct Interfaces
{
    unsigned int mechanic;
    int offsets[3];
    unsigned int handles[3];
};
const Interfaces kBehaviorInterfaces[] = {
    {0x92c658, {0x88, 0x9c, -1}, {0x404040, 0x445650, 0}},
    {0x92c5e0, {0x98, -1, -1}, {0x4039e0, 0, 0}},
    {0x92c618, {0x8c, 0x94, 0xa0}, {0x405000, 0x620fd0, 0x68cc70}},
};
} // namespace

void __fastcall DrawCarOnBehaviorChange(char *self, void *, const unsigned int *mechanic)
{
    for (const Interfaces &b : kBehaviorInterfaces)
    {
        if (*mechanic != *reinterpret_cast<const unsigned int *>(b.mechanic))
            continue;
        for (int i = 0; i < 3 && b.offsets[i] >= 0; ++i)
            At<void *>(self, b.offsets[i]) = FindModel(At<void *>(self, 0x34), reinterpret_cast<void *>(b.handles[i]));
    }
}

void __fastcall DrawVehiclePartCreateTrigger(char *part, void *, const void *matrix)
{
    const short *bounds = Virtual<const short *>(part + 0x34, 0x40);
    const float half[3] = {static_cast<float>(bounds[8]) * 0.001f, static_cast<float>(bounds[9]) * 0.001f,
                           static_cast<float>(bounds[10]) * 0.001f};
    void *&trigger = At<void *>(part, 0xa0);
    if (!(half[2] * half[2] + half[1] * half[1] + half[0] * half[0] > 0.0f))
    {
        if (trigger)
        {
            NFS_ORIGINAL(0x66fb40, void(__thiscall *)(void *))(trigger);
            FreeNode(static_cast<Node *>(trigger), 8);
            trigger = nullptr;
        }
        return;
    }
    if (!trigger)
    {
        void *memory = NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
            kEffectAllocator, 8, nullptr);
        trigger = memory ? NFS_ORIGINAL(0x667e70,
                                        void *(__thiscall *)(void *, void *, int, const void *, const float *, int))(
                               memory, At<void *>(part, 0x3c), 0, matrix, half, 0)
                         : nullptr;
        return;
    }
    NFS_ORIGINAL(0x668070, void(__thiscall *)(void *, const void *, const float *, int))(trigger, matrix, half, 0);
    NFS_ORIGINAL(0x667fc0, void(__thiscall *)(void *))(trigger);
}

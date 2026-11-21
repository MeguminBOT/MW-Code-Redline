/** DrawVehicle's IModel side: forwards to the vehicle's simable and its attachments (original: zPhysicsBehaviors
 * DrawVehicle getters, Chassis::GetRenderMotion). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6b8f20, DrawVehicleGetModel)
NFS_REPLACE(0x6b8f30, DrawVehicleGetModelConst)
NFS_REPLACE(0x6b8f40, DrawVehicleOnProcessFrame)
NFS_REPLACE(0x6b8f50, DrawVehicleGetLinearVelocity)
NFS_REPLACE(0x6b8f60, DrawVehicleGetAngularVelocity)
NFS_REPLACE(0x6b8f70, DrawVehicleGetTransform)
NFS_REPLACE(0x6b8f80, DrawVehicleGetPartName)
NFS_REPLACE(0x6b8f90, DrawVehicleGetWorldID)
NFS_REPLACE(0x6b8fa0, DrawVehicleGetCollisionGeometry)
NFS_REPLACE(0x6b8fb0, DrawVehicleGetAttributes)
NFS_REPLACE(0x6b8fc0, DrawVehicleGetSimable)
NFS_REPLACE(0x6b8fd0, DrawVehicleGetRootModel)
NFS_REPLACE(0x6b8fe0, DrawVehicleGetParentModel)
NFS_REPLACE(0x6b8ff0, DrawVehicleIsRootModel)
NFS_REPLACE(0x6b9000, DrawVehicleGetEventSequencer)
NFS_REPLACE(0x6b9010, DrawVehicleGetCausalityTime)
NFS_REPLACE(0x6b9020, DrawVehicleAttach)
NFS_REPLACE(0x6b9030, DrawVehicleDetach)
NFS_REPLACE(0x6b9040, DrawVehicleIsAttached)
NFS_REPLACE(0x6b9050, DrawVehicleOnAttached)
NFS_REPLACE(0x6b9060, DrawVehicleOnDetached)
NFS_REPLACE(0x6b9070, DrawVehicleGetAttachments)
NFS_REPLACE(0x6b90c0, DrawVehicleGetModelHandle)
NFS_REPLACE(0x693900, DrawVehicleSetCausality)
NFS_REPLACE(0x693920, DrawVehicleGetCausality)
NFS_REPLACE(0x693940, DrawVehicleGetName)
NFS_REPLACE(0x68d310, ChassisGetRenderMotion)

namespace
{
const int kSimable = -0x20;
const int kModelBase = 0x4c;
const int kRootBase = 0x54;
const int kModel = 8;
const int kAttachments = 0xc;
const int kModelHandle = 0x10;
const int kCollision = 0x14;
const int kCause = 0x28;
const int kCauseTime = 0x2c;
const int kOwner = 0x48;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

void *Simable(void *self)
{
    return At<void *>(self, kSimable);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

void *Attachments(void *self)
{
    return At<void *>(self, kAttachments);
}
} // namespace

void *__fastcall DrawVehicleGetModel(char *self)
{
    return self - kModelBase ? self + kModel : nullptr;
}

void *__fastcall DrawVehicleGetModelConst(char *self)
{
    return self - kModelBase ? self + kModel : nullptr;
}

void __fastcall DrawVehicleOnProcessFrame(void *, void *, float)
{
}

void __fastcall DrawVehicleGetLinearVelocity(void *self, void *, UMath::Vector3 &velocity)
{
    Virtual<void, UMath::Vector3 &>(Simable(self), 0x68, velocity);
}

void __fastcall DrawVehicleGetAngularVelocity(void *self, void *, UMath::Vector3 &velocity)
{
    Virtual<void, UMath::Vector3 &>(Simable(self), 0x6c, velocity);
}

void __fastcall DrawVehicleGetTransform(void *self, void *, UMath::Matrix4 &transform)
{
    Virtual<void, UMath::Matrix4 &>(Simable(self), 0x64, transform);
}

UCrc32 *__fastcall DrawVehicleGetPartName(void *, void *, UCrc32 *name)
{
    *reinterpret_cast<unsigned int *>(name) = *reinterpret_cast<const unsigned int *>(0x91ddbc);
    return name;
}

unsigned int __fastcall DrawVehicleGetWorldID(void *self)
{
    return Virtual<unsigned int>(Simable(self), 0x70);
}

void *__fastcall DrawVehicleGetCollisionGeometry(void *self)
{
    return At<void *>(self, kCollision);
}

void *__fastcall DrawVehicleGetAttributes(void *self)
{
    return Virtual<void *>(Simable(self), 0x44);
}

void *__fastcall DrawVehicleGetSimable(void *self)
{
    return Simable(self);
}

void *__fastcall DrawVehicleGetRootModel(char *self)
{
    return self - kRootBase ? self : nullptr;
}

void *__fastcall DrawVehicleGetParentModel(void *)
{
    return nullptr;
}

bool __fastcall DrawVehicleIsRootModel(void *)
{
    return true;
}

void *__fastcall DrawVehicleGetEventSequencer(void *self)
{
    return Virtual<void *>(Simable(self), 0x74);
}

float __fastcall DrawVehicleGetCausalityTime(void *self)
{
    return At<float>(self, kCauseTime);
}

bool __fastcall DrawVehicleAttach(void *self, void *, void *object)
{
    return NFS_ORIGINAL(0x6fbf80, bool(__thiscall *)(void *, void *))(Attachments(self), object);
}

bool __fastcall DrawVehicleDetach(void *self, void *, void *object)
{
    return NFS_ORIGINAL(0x6fc060, bool(__thiscall *)(void *, void *))(Attachments(self), object);
}

bool __fastcall DrawVehicleIsAttached(void *self, void *, const void *object)
{
    return NFS_ORIGINAL(0x6f25e0, bool(__thiscall *)(void *, const void *))(Attachments(self), object);
}

void __fastcall DrawVehicleOnAttached(void *, void *, void *)
{
}

void __fastcall DrawVehicleOnDetached(void *, void *, void *)
{
}

void *__fastcall DrawVehicleGetAttachments(void *self)
{
    return static_cast<char *>(Attachments(self)) + 4;
}

void *__fastcall DrawVehicleGetModelHandle(void *self)
{
    return At<void *>(self, kModelHandle);
}

void __fastcall DrawVehicleSetCausality(void *self, void *, void *cause, float time)
{
    At<void *>(self, kCause) = cause;
    At<float>(self, kCauseTime) = time;
}

void *__fastcall DrawVehicleGetCausality(void *self)
{
    return At<void *>(self, kCause);
}

UCrc32 *__fastcall DrawVehicleGetName(void *self, void *, UCrc32 *name)
{
    char *attributes = Virtual<char *>(At<void *>(self, kOwner), 0x48);
    *reinterpret_cast<unsigned int *>(name) = At<unsigned int>(At<void *>(attributes, 8), 0x18);
    return name;
}

float __fastcall ChassisGetRenderMotion(void *self)
{
    return At<float>(At<void *>(self, 0x2c), 0x60);
}

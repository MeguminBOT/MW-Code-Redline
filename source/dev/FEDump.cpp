/**
 * A front-end package's objects, logged (interface/engine/feng.h Dump): for working out a layout the package files do not have.
 * `-set Debug.FEDump=1` logs the Video page's package when the page is set up.
 */
#include "game/gen/api.h"
#include "interface/engine/feng.h"
#include "core/log.h"

namespace
{
/** FEObject::Type of a group, whose children hang off it (the list's head at +0x64). */
const int kGroupType = 5;
const int kGroupChildren = 0x64;
const int kMaxDepth = 12;

int Tenths(float value)
{
    return static_cast<int>(value * 10.0f + (value < 0.0f ? -0.5f : 0.5f));
}

void DumpList(FEObject *object, int depth)
{
    static const char kIndent[] = "                        ";
    for (; object; object = object->Next)
    {
        const FEObjData *d = object->pData;
        nfs::Log("fe:%s guid %x hash %08x type %d flags %x pos %d %d size %d %d colour %d %d %d %d scripts %d",
                 kIndent + sizeof(kIndent) - 1 - (depth < kMaxDepth ? depth * 2 : kMaxDepth * 2), object->GUID,
                 object->NameHash, object->Type, object->Flags, d ? Tenths(d->Pos[0]) : 0, d ? Tenths(d->Pos[1]) : 0,
                 d ? Tenths(d->Size[0]) : 0, d ? Tenths(d->Size[1]) : 0, d ? d->Col[0] : 0, d ? d->Col[1] : 0,
                 d ? d->Col[2] : 0, d ? d->Col[3] : 0, static_cast<int>(object->ScriptCount));
        if (object->Type == kGroupType && depth < kMaxDepth)
            DumpList(*reinterpret_cast<FEObject **>(reinterpret_cast<char *>(object) + kGroupChildren), depth + 1);
    }
}
} // namespace

namespace nfs::feng
{
void Dump(const char *package)
{
    FEPackage *loaded = package ? cFEng::mInstance->FindPackage(package) : nullptr;
    if (!loaded)
    {
        nfs::Log("fe: no package %s", package ? package : "(none)");
        return;
    }
    nfs::Log("fe: package %s (positions and sizes in tenths of a front-end unit)", package);
    DumpList(loaded->GetFirstObject(), 0);
}
} // namespace nfs::feng

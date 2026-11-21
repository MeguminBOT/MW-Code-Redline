/** Small render-side helpers (original: zWorld/zWorld2/zFe/unassigned: GarageMainScreen car rendering switches,
 * CloseWorldModels, GetCarEffectMarkerHashes, Rain::SetRainIntensity, CarRenderConn::GetRenderMatrix,
 * VehicleRenderConn::OnStatusCheck/OnEvent, Pkt_Car_Service::Size, SolveEffectQuadratic,
 * HandleWorldEffectEmitterGroupDelete, bSList<CarEmitterPosition>, WorldModel::AttachReplacementTextureTable,
 * cFEngGameInterface::GenerateRenderContext). */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x7a1fd0, GarageDisableCarRendering)
NFS_REPLACE(0x7a1ff0, GarageIsCarRendering)
NFS_REPLACE(0x739400, CarRenderConnGetRenderMatrix)
NFS_REPLACE(0x759290, VehicleRenderConnOnStatusCheck)
NFS_REPLACE(0x7592a0, VehicleRenderConnOnEvent)
NFS_REPLACE(0x742060, PktCarServiceSize)
NFS_REPLACE(0x73eec0, CarEmitterListInit)
NFS_REPLACE(0x74c6f0, CarEmitterListDestroy)
NFS_REPLACE(0x736b30, RenderConnRestoreServices)
NFS_REPLACE(0x516590, GameInterfaceGenerateRenderContext)

namespace
{
const int kGarageCar = 0x34;
const int kCarRendering = 0x574;
const unsigned int *const kEffectMarkers = reinterpret_cast<const unsigned int *>(0x8b2c30);
const int kMarkersPerPosition = 5;

const int kRainIntensity = 0x28c;
const int kRainHidden = 0x294;
const int kRainShown = 0x33e8;
const int kRainFade = 0x33ec;
const int kRainFadeLeft = 0x33f0;
const int kRainDrops = 350;

struct ListNode
{
    ListNode *next;
    ListNode *last;
};

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

SlotPool *WorldModelPool()
{
    return *reinterpret_cast<SlotPool **>(0x9b0a2c);
}
} // namespace

void GarageMainScreen::EnableCarRendering()
{
    if (void *car = At<void *>(this, kGarageCar))
        At<int>(car, kCarRendering) = 1;
}

void __fastcall GarageDisableCarRendering(void *screen)
{
    if (void *car = At<void *>(screen, kGarageCar))
        At<int>(car, kCarRendering) = 0;
}

bool __fastcall GarageIsCarRendering(void *screen)
{
    void *car = At<void *>(screen, kGarageCar);
    return car && At<int>(car, kCarRendering) != 0;
}

void CloseWorldModels()
{
    ListNode *const head = reinterpret_cast<ListNode *>(0x9b0f90);
    while (head->next != head)
    {
        ListNode *model = head->next;
        if (model)
        {
            NFS_ORIGINAL(0x753650, void(__thiscall *)(void *))(model);
            bFree(WorldModelPool(), model);
        }
    }
    bDeleteSlotPool(WorldModelPool());
    *reinterpret_cast<SlotPool **>(0x9b0a2c) = nullptr;
}

const unsigned int *GetCarEffectMarkerHashes(CarEffectPosition position)
{
    return kEffectMarkers + static_cast<int>(position) * kMarkersPerPosition;
}

void Rain::SetRainIntensity(float intensity)
{
    float &current = At<float>(this, kRainIntensity);
    current = *reinterpret_cast<const int *>(0x9b0a30) ? *reinterpret_cast<const float *>(0x904a14) : intensity;
    const int shown = static_cast<int>((1.0 - static_cast<double>(current)) * 350.0);
    At<int>(this, kRainShown) = shown;
    At<int>(this, kRainHidden) = kRainDrops - shown;
    At<float>(this, kRainFadeLeft) = 1.0f - At<float>(this, kRainFade);
}

void __fastcall CarRenderConnGetRenderMatrix(char *conn, void *, bMatrix4 *out)
{
    memcpy(out, conn + 0x330, sizeof(bMatrix4));
}

int __fastcall VehicleRenderConnOnStatusCheck(char *conn)
{
    return At<int>(conn, 0x28) >= 2 ? 1 : 0;
}

void __fastcall VehicleRenderConnOnEvent(void *, void *, int)
{
}

unsigned int __fastcall PktCarServiceSize(void *)
{
    return 0xa0;
}

float SolveEffectQuadratic(float t, const UMath::Vector4 &k)
{
    const float *c = reinterpret_cast<const float *>(&k);
    const double x = t;
    const float v = static_cast<float>(((x * c[3] + c[2]) * x + c[1]) * x + c[0]);
    if (!(v < 1.0f))
        return 1.0f;
    return v < 0.0f ? 0.0f : v;
}

void HandleWorldEffectEmitterGroupDelete(void *connection, EmitterGroup *)
{
    At<void *>(connection, 0x3c) = nullptr;
}

void *__fastcall CarEmitterListInit(void *memory)
{
    ListNode *list = static_cast<ListNode *>(memory);
    list->next = list;
    list->last = list;
    return list;
}

void __fastcall CarEmitterListDestroy(void *memory)
{
    ListNode *list = static_cast<ListNode *>(memory);
    while (list->next != list)
    {
        ListNode *node = list->next;
        list->next = node->next;
        if (list->last == node)
            list->last = list;
        bFree(*reinterpret_cast<SlotPool **>(0x9b0950), node);
    }
}

void WorldModel::AttachReplacementTextureTable(eReplacementTextureTable *table, int count)
{
    if (eModel *model = At<eModel *>(this, 8))
        model->AttachReplacementTextureTable(table, count, 0);
    if (eModel *model = At<eModel *>(this, 0xc))
        model->AttachReplacementTextureTable(table, count, 0);
}

void __fastcall RenderConnRestoreServices(void *)
{
}

void __fastcall GameInterfaceGenerateRenderContext(void *, void *, unsigned short context, FEObject *object)
{
    void *render = *reinterpret_cast<void **>(0x91cfa8);
    NFS_ORIGINAL(0x585320, void(__thiscall *)(void *, unsigned short, FEObject *))(render, context, object);
}

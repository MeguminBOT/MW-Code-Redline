/**
 * The rim shop's brands past the ten in its code (original: CustomizeRims::GetCategoryBrandHash 0x7a6e50, CustomizeSub::
 * GetRimBrandIndex 0x7a6490, the options of CustomizeSub::SetupRimBrands 0x7bc3c0): Underground 2's and Carbon's rims
 * (tools/shopparts.py has the same list), each brand a category after the original's 0x702-0x70b.
 *
 * The shop shows a car's rims one size at a time, and a car offers every size any brand has now, so it leaves out the
 * sizes the brand on show has no rims in.
 *
 * Where the original asks a category's range for the rims as a whole (its unlock, its marker, the brand menu handing
 * the parts menu back its place), an added brand answers as the first original one.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "interface/menus/rimbrands.h"

NFS_REPLACE(0x7a6e50, RimShop::CategoryBrand)
NFS_REPLACE(0x7a6490, RimShop::BrandIndex)
NFS_REPLACE(0x7bb560, RimShop::AddOption)
NFS_REPLACE(0x7bdae0, RimShop::ScrollSizes)
NFS_REPLACE(0x7b7dd0, RimShop::BuildList)
NFS_REPLACE(0x7b5ef0, RimShop::UnlockHash)
NFS_REPLACE(0x7bc3c0, RimShop::SetupBrands)
NFS_REPLACE(0x7a57a0, RimCategoryMarker)
NFS_REPLACE(0x7afdb0, RimMarkerFromCategory)

namespace
{
struct Brand
{
    unsigned int brand;
    unsigned int icon;
    unsigned int name;
};
const Brand kBrands[] = {
    {0x0269d069, 0x0deb2f1c, 0xc1a3e8d6}, // ADVAN
    {0x0013053e, 0x36b9d991, 0xa105440b}, // AVUS
    {0x029e75d1, 0x0e1fd484, 0xc1d88e3e}, // DAVIN
    {0x00148bba, 0x36bb600d, 0xa106ca87}, // DONZ
    {0x0015a5c4, 0x36bc7a17, 0xa107e491}, // FOXX
    {0xa26ad820, 0xbe399df3, 0xdcc9b56d}, // GIANELLE
    {0xc3a1ac32, 0xdf707205, 0xfe00897f}, // GIOVANNA
    {0x66ba72e5, 0xe267a7f8, 0x0d3798f2}, // KAIZER
    {0x695fce00, 0xe50d0313, 0x0fdcf40d}, // LEXANI
    {0x00197af7, 0x36c04f4a, 0xa10bb9c4}, // MOMO
    {0x0365769e, 0x0ee6d551, 0xc29f8f0b}, // OASIS
    {0x001c39b5, 0x36c30e08, 0xa10e7882}, // ROTA
    {0xc8d1a75e, 0xba257ed1, 0x3ef38f0b}, // SPINNER
    {0xbf3608c0, 0x54dd88f3, 0x45708fad}, // WELDWHEEL
    {0x001ef762, 0x36c5cbb5, 0xa111362f}, // WORK
    {0x00000472, 0x6c2fa805, 0x05ca2bff}, // AR
    {0x000004ae, 0x6c2fa841, 0x05ca2c3b}, // CL
    {0x0000b0be, 0xf224c6b1, 0xbf0fc9eb}, // HRE
    {0xb026583f, 0xa17a2fb2, 0x26483fec}, // IFORGED
    {0x03c20f8f, 0x0f436e42, 0xc2fc27fc}, // TENZO
    {0x0000e3fd, 0xf224f9f0, 0xbf0ffd2a}, // TSW
};
const int kBrandCount = sizeof(kBrands) / sizeof(kBrands[0]);
const unsigned int kFirstCategory = 0x70c;
const unsigned int kFirstOriginalCategory = 0x702;
const unsigned int kLastOriginalCategory = 0x70b;
/** The first menu position past the original's (1 is Stock, 2-11 its brands). */
const int kFirstBrandIndex = 12;

const int kScreenCategory = 0x148;
const int kScreenFrom = 0x154;
const unsigned int kPartsFromRims = 0x801;
const int kScreenMenu = 0x2c;
const int kScreenCurrentOption = 0x38;
const int kScreenSize = 0x1bc;
const int kScreenSizeMin = 0x1c0;
const int kScreenSizeMax = 0x1c4;
const int kMenuIndexOf = 0x24;
const int kScreenRefresh = 0xc;
const int kSlotRim = 0x42;
const int kManagerCarRecord = 4;
const unsigned int kAttrBrand = 0xebb03e66;
const unsigned int kAttrSize = 0xeb0101e2;
char *const kCustomizeManager = reinterpret_cast<char *>(0x9b9ec0);

/** The sizes (bits) the brand has rims in for the car in the shop. */
/** Rim sizes the shop does not offer: 21 inch, whose rims stay in the table only for a saved car that has one. */
const unsigned int kHiddenSizes = 1u << 21;

unsigned int SizesOf(unsigned int brand)
{
    FECarRecord *record = *reinterpret_cast<FECarRecord **>(kCustomizeManager + kManagerCarRecord);
    if (!record)
        return 0;
    const CarType type = record->GetType();
    unsigned int sizes = 0;
    for (CarPart *p = CarPartDB.NewGetCarPart(type, kSlotRim, 0, nullptr, -1); p;
         p = CarPartDB.NewGetCarPart(type, kSlotRim, 0, p, -1))
    {
        if (p->GetAppliedAttributeUParam(kAttrBrand, 0) == brand)
        {
            const int size = static_cast<signed char>(p->GetAppliedAttributeIParam(kAttrSize, 0));
            if (size >= 0 && size < 32)
                sizes |= 1u << size;
        }
    }
    return sizes & ~kHiddenSizes;
}
} // namespace

namespace nfs::rimbrands
{
bool IsCategory(unsigned int category)
{
    return category >= kFirstCategory && category < kFirstCategory + kBrandCount;
}
} // namespace nfs::rimbrands

class RimShop
{
public:
    unsigned int CategoryBrand();
    int BrandIndex(unsigned int brand);
    int AddOption(const char *package, unsigned int icon, unsigned int name, unsigned int category);
    void ScrollSizes(int direction);
    void BuildList(int selected);
    unsigned int UnlockHash(unsigned int category, int part);
    void SetupBrands();
};

int __cdecl RimCategoryMarker(int category);
int __cdecl RimMarkerFromCategory(int category);

namespace
{
int AsRims(int category)
{
    return nfs::rimbrands::IsCategory(static_cast<unsigned int>(category)) ? static_cast<int>(kFirstOriginalCategory)
                                                                           : category;
}
} // namespace

int __cdecl RimCategoryMarker(int category)
{
    return NFS_ORIGINAL(0x7a57a0, int(__cdecl *)(int))(AsRims(category));
}

int __cdecl RimMarkerFromCategory(int category)
{
    return NFS_ORIGINAL(0x7afdb0, int(__cdecl *)(int))(AsRims(category));
}

/** The brand menu (original 0x7bc3c0) leaves the parts menu's place on Rims (its 0x801) once it has used a brand's
 * category for its own first option; the original does that for its own brands only, and the parts menu then looked
 * for an option as far down as the added brand's number. */
void RimShop::SetupBrands()
{
    NFS_ORIGINAL(0x7bc3c0, void(__thiscall *)(RimShop *))(this);
    unsigned int &from = *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(this) + kScreenFrom);
    if (nfs::rimbrands::IsCategory(from))
        from = kPartsFromRims;
}

unsigned int RimShop::CategoryBrand()
{
    const unsigned int category = *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(this) + kScreenCategory);
    if (nfs::rimbrands::IsCategory(category))
        return kBrands[category - kFirstCategory].brand;
    return NFS_ORIGINAL(0x7a6e50, unsigned int(__thiscall *)(RimShop *))(this);
}

int RimShop::BrandIndex(unsigned int brand)
{
    for (int i = 0; i < kBrandCount; ++i)
    {
        if (kBrands[i].brand == brand)
            return kFirstBrandIndex + i;
    }
    return NFS_ORIGINAL(0x7a6490, int(__thiscall *)(RimShop *, unsigned int))(this, brand);
}

int RimShop::AddOption(const char *package, unsigned int icon, unsigned int name, unsigned int category)
{
    typedef int(__thiscall * Add)(RimShop *, const char *, unsigned int, unsigned int, unsigned int);
    const Add add = NFS_ORIGINAL(0x7bb560, Add);
    const int result = add(this, package, icon, name, category);
    if (category == kLastOriginalCategory)
    {
        for (int i = 0; i < kBrandCount; ++i)
            add(this, package, kBrands[i].icon, kBrands[i].name, kFirstCategory + i);
    }
    return result;
}

void RimShop::ScrollSizes(int direction)
{
    char *const self = reinterpret_cast<char *>(this);
    int &size = *reinterpret_cast<int *>(self + kScreenSize);
    const int low = *reinterpret_cast<int *>(self + kScreenSizeMin);
    const int high = *reinterpret_cast<int *>(self + kScreenSizeMax);
    const unsigned int sizes = SizesOf(CategoryBrand());
    int next = size;
    for (int i = low; i <= high; ++i)
    {
        next += direction < 0 ? -1 : 1;
        if (next < low)
            next = high;
        else if (next > high)
            next = low;
        if (sizes & (1u << next))
            break;
    }
    if (next == size || !(sizes & (1u << next)))
        return;
    size = next;
    char *const menu = self + kScreenMenu;
    int index = 0;
    if (void *const current = *reinterpret_cast<void **>(self + kScreenCurrentOption))
    {
        typedef int(__thiscall * IndexOf)(char *, void *);
        index = (*reinterpret_cast<IndexOf *>(*reinterpret_cast<char **>(menu) + kMenuIndexOf))(menu, current);
    }
    BuildList(index);
    typedef void(__thiscall * Refresh)(RimShop *);
    (*reinterpret_cast<Refresh *>(*reinterpret_cast<char **>(self) + kScreenRefresh))(this);
}

void RimShop::BuildList(int selected)
{
    char *const self = reinterpret_cast<char *>(this);
    int &size = *reinterpret_cast<int *>(self + kScreenSize);
    const unsigned int sizes = SizesOf(CategoryBrand());
    if (sizes && !(size >= 0 && size < 32 && (sizes & (1u << size))))
    {
        const int low = *reinterpret_cast<int *>(self + kScreenSizeMin);
        const int high = *reinterpret_cast<int *>(self + kScreenSizeMax);
        for (int d = 1; d <= high - low; ++d)
        {
            if (size + d <= high && (sizes & (1u << (size + d))))
            {
                size += d;
                break;
            }
            if (size - d >= low && (sizes & (1u << (size - d))))
            {
                size -= d;
                break;
            }
        }
    }
    NFS_ORIGINAL(0x7b7dd0, void(__thiscall *)(RimShop *, int))(this, selected);
}

unsigned int RimShop::UnlockHash(unsigned int category, int part)
{
    return NFS_ORIGINAL(0x7b5ef0, unsigned int(__thiscall *)(RimShop *, unsigned int, int))(
        this, nfs::rimbrands::IsCategory(category) ? kFirstOriginalCategory : category, part);
}

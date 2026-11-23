/**
 * The front-end engine's remaining list and string pieces (original: zFEng FEMinList::Purge 0x5b3b20, FEList::RemNode
 * 0x5b3bd0, FERefList::RemNode 0x5b26e0, FERefList::Purge 0x5b3cd0, FEGroup::AddObject 0x5b5420, FEPackage::AddResponse
 * 0x5b3da0, CloseEnoughPosition 0x5b1140, CloseEnoughColor 0x5b1280, FETypeNode::GetField 0x5b2d10,
 * FEMultiImage::SetUVs 0x5b3630, GetUVs 0x5b3670, CopyString<short> 0x5b37c0, FEWideString copy constructor 0x5bff70,
 * destructor 0x5b3230, SetLength 0x5b91c0, AllocateString 0x5b9210, operator=(short const *) 0x5bcd20).
 *
 * Lists: count +4, first +8, last +0xc; a node's next +4, previous +8, marked 0xabadcafe when removed (a FERefList keeps
 * no count). A purge unlinks the first node before the loop that deletes them. A group's children: count +0x60, first
 * +0x64, last +0x68; a package's responses: count +0x58, first +0x5c, last +0x60. Positions are close within 1e-6 (in
 * the FPU's precision), colours within 1 per channel. A multi image's three UV pairs: top lefts at data +0x54, bottom
 * rights at +0x6c. FEWideString: its characters, then their room.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kRemoved = 0xabadcafe;
const double kClose = 9.999999974752427e-07;
using Malloc = void *(__cdecl *)(unsigned int);
using Free = void(__cdecl *)(void *);
const Malloc kMalloc = reinterpret_cast<Malloc>(0x652ac0);
const Free kFree = reinterpret_cast<Free>(0x652b10);

void Unlink(char *list, char *node, bool counted)
{
    if (node == At<char *>(list, 8))
        At<char *>(list, 8) = At<char *>(node, 4);
    if (node == At<char *>(list, 0xc))
        At<char *>(list, 0xc) = At<char *>(node, 8);
    if (char *prev = At<char *>(node, 8))
        At<char *>(prev, 4) = At<char *>(node, 4);
    if (char *next = At<char *>(node, 4))
        At<char *>(next, 8) = At<char *>(node, 8);
    At<unsigned int>(node, 4) = kRemoved;
    At<unsigned int>(node, 8) = kRemoved;
    if (counted)
        --At<int>(list, 4);
}

void Destroy(char *node)
{
    reinterpret_cast<void(__thiscall *)(void *, int)>((*reinterpret_cast<void ***>(node))[0])(node, 1);
}

void Purge(char *list, bool counted)
{
    char *node = At<char *>(list, 8);
    if (!node)
        return;
    Unlink(list, node, counted);
    while (node)
    {
        Destroy(node);
        node = At<char *>(list, 8);
        if (node)
            Unlink(list, node, counted);
    }
}

/** At the end of a list whose count, first and last are at `count`, `count` + 4 and + 8. */
void Append(char *owner, int count, char *node)
{
    char *last = At<char *>(owner, count + 8);
    if (!node)
        return;
    if (!last)
    {
        char *first = At<char *>(owner, count + 4);
        At<char *>(node, 4) = first;
        if (first)
            At<char *>(first, 8) = node;
        At<char *>(node, 8) = nullptr;
        At<char *>(owner, count + 4) = node;
    }
    else
    {
        char *next = At<char *>(last, 4);
        At<char *>(node, 4) = next;
        if (next)
            At<char *>(next, 8) = node;
        At<char *>(node, 8) = last;
        At<char *>(last, 4) = node;
    }
    if (At<char *>(owner, count + 8) == last)
        At<char *>(owner, count + 8) = node;
    ++At<int>(owner, count);
}

bool Close(float a, float b)
{
    return b <= static_cast<double>(a) + kClose && static_cast<double>(a) - kClose <= b;
}

void Copy(short *to, const short *from)
{
    if (!to)
        return;
    if (from)
        while (*from)
            *to++ = *from++;
    *to = 0;
}

unsigned int CountShorts(const short *s)
{
    unsigned int n = 0;
    if (s)
        while (s[n])
            ++n;
    return n;
}
} // namespace

void FEMinList::Purge()
{
    ::Purge(reinterpret_cast<char *>(this), true);
}

FENode *FEList::RemNode(FEMinNode *node)
{
    if (node)
        Unlink(reinterpret_cast<char *>(this), reinterpret_cast<char *>(node), true);
    return reinterpret_cast<FENode *>(node);
}

FEMinNode *FERefList::RemNode(FEMinNode *node)
{
    if (!node)
        return nullptr;
    Unlink(reinterpret_cast<char *>(this), reinterpret_cast<char *>(node), false);
    return node;
}

void FERefList::Purge()
{
    ::Purge(reinterpret_cast<char *>(this), false);
}

void FEGroup::AddObject(FEObject *o)
{
    Append(reinterpret_cast<char *>(this), 0x60, reinterpret_cast<char *>(o));
}

void FEPackage::AddResponse(FEMessageResponse *r)
{
    Append(reinterpret_cast<char *>(this), 0x58, reinterpret_cast<char *>(r));
}

bool __cdecl CloseEnoughPosition(FEVector3 const &first, FEVector3 const &second)
{
    const float *a = reinterpret_cast<const float *>(&first);
    const float *b = reinterpret_cast<const float *>(&second);
    return Close(a[0], b[0]) && Close(a[1], b[1]) && Close(a[2], b[2]);
}

bool __cdecl CloseEnoughColor(FEColor const &first, FEColor const &second)
{
    const int *a = reinterpret_cast<const int *>(&first);
    const int *b = reinterpret_cast<const int *>(&second);
    static const int kOrder[4] = {2, 1, 0, 3};
    for (int i : kOrder)
        if (b[i] > a[i] + 1 || a[i] - 1 > b[i])
            return false;
    return true;
}

FEFieldNode *FETypeNode::GetField(char const *name)
{
    for (char *f = At<char *>(this, 0x1c); f; f = At<char *>(f, 4))
        if (!bStrICmp(At<const char *>(f, 0xc), name))
            return reinterpret_cast<FEFieldNode *>(f);
    return nullptr;
}

struct UV
{
    float u, v;
};
void __fastcall MultiImageSetUVs(FEObject *o, void *, unsigned int index, float left, float top, float right,
                                 float bottom);
void __fastcall MultiImageGetUVs(FEObject *o, void *, unsigned int index, UV *topLeft, UV *bottomRight);
void __cdecl CopyShortString(short *to, const short *from);
NFS_REPLACE(0x5b3630, MultiImageSetUVs)
NFS_REPLACE(0x5b3670, MultiImageGetUVs)
NFS_REPLACE(0x5b37c0, CopyShortString)

void __fastcall MultiImageSetUVs(FEObject *o, void *, unsigned int index, float left, float top, float right,
                                 float bottom)
{
    if (index >= 3)
        return;
    At<UV>(o->pData, 0x54 + index * 8) = {left, top};
    At<UV>(o->pData, 0x6c + index * 8) = {right, bottom};
}

void __fastcall MultiImageGetUVs(FEObject *o, void *, unsigned int index, UV *topLeft, UV *bottomRight)
{
    if (index >= 3)
        return;
    *topLeft = At<UV>(o->pData, 0x54 + index * 8);
    *bottomRight = At<UV>(o->pData, 0x6c + index * 8);
}

void __cdecl CopyShortString(short *to, const short *from)
{
    Copy(to, from);
}

FEWideString::FEWideString(FEWideString const &other)
{
    short *&text = At<short *>(this, 0);
    text = nullptr;
    At<unsigned int>(this, 4) = 0;
    const short *from = At<const short *>(&other, 0);
    if (from)
    {
        const unsigned int n = CountShorts(from) + 1;
        if (n)
            SetLength(n);
        Copy(text, from);
    }
    At<unsigned int>(this, 4) = CountShorts(text);
}

FEWideString::~FEWideString()
{
    if (short *text = At<short *>(this, 0))
        kFree(text);
}

void FEWideString::SetLength(unsigned int length)
{
    short *&text = At<short *>(this, 0);
    if (CountShorts(text) >= length)
        return;
    At<unsigned int>(this, 4) = length;
    short *grown = static_cast<short *>(kMalloc(length * 2 + 2));
    Copy(grown, text);
    kFree(text);
    text = grown;
}

short *FEWideString::AllocateString(unsigned int length)
{
    if (At<unsigned int>(this, 4) < length)
        SetLength(length);
    return At<short *>(this, 0);
}

FEWideString &FEWideString::operator=(short const *from)
{
    if (from)
    {
        const unsigned int n = CountShorts(from) + 1;
        if (At<unsigned int>(this, 4) < n)
            SetLength(n);
        Copy(At<short *>(this, 0), from);
    }
    return *this;
}

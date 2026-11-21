/**
 * The front end's render objects (original: zFe2 FERenderObject, FERenderEPoly's pool and cFEngRender's dispatch and
 * builders for matrices, images, coloured images, multi-images, movies and strings, and FEngFont::RenderString): each FEObject that draws keeps a FERenderObject (0x70, pointed at by the object's +0x58) holding the polygons
 * made for it, already in screen space, so that a frame only redraws them.
 *
 * FERenderObject: +0x08 its FEObject, +0x0c flags (2: drawn once), +0x10 times drawn, +0x14 its texture, +0x18 its
 * polygon list, +0x20 their count, +0x30 the transform.
 * FERenderEPoly (0xc0): next, prev, +0x10 the ePoly, +0xb0 a texture of its own, +0xb4 the mask texture. They come from
 * FERenderEPolySlotPool, or a second pool made when it is full and deleted once it empties.
 * FEClipInfo: +0x00 four planes (a, b, c), +0x40 their d (aligned rectangles: top, right, bottom, left), +0x50 flags (1:
 * an aligned rectangle).
 *
 * Clipped polygons: the four planes are applied in turn, each pass writing the other of the two buffer sets. Positions
 * and texture coordinates end up in the first set and the colours are, as in the original, taken from the second (the
 * third pass's result). The fan's unused mask coordinate keeps the value the original leaves in it.
 *
 * Changes: when both polygon pools are exhausted a polygon is dropped instead of writing through null.
 */
#include <new>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "interface/components/padprompts.h"

NFS_REPLACE(0x56fa10, FreeRenderPoly)
NFS_REPLACE(0x58aff0, FreeRenderPolyList)
NFS_REPLACE(0x584800, FERenderClipAligned)
NFS_REPLACE(0x582d20, FERenderClipGeneral)
NFS_REPLACE(0x591ab0, CreateCachedRender)
NFS_REPLACE(0x5848b0, MakeObjectMatrix)
NFS_REPLACE(0x59a500, FERenderImage)
NFS_REPLACE(0x59a790, FERenderColoredImage)
NFS_REPLACE(0x599db0, FERenderMovie)
NFS_REPLACE(0x5a4390, FERenderString)
NFS_REPLACE(0x59a060, FERenderMultiImage)
NFS_REPLACE(0x585d60, FontPrintCharacter)

void __cdecl FreeRenderPoly(void *poly);
void __fastcall FreeRenderPolyList(void *list);
unsigned int __fastcall FERenderClipAligned(FERenderObject *, void *, FEClipInfo *clip, bVector3 *pos, bVector2 *uv,
                                            bVector4 *colour, bVector3 *posOut, bVector2 *uvOut, bVector4 *colourOut);
unsigned int __fastcall FERenderClipGeneral(FERenderObject *, void *, FEClipInfo *clip, float *pos, float *uv,
                                            float *colour, float *posOut, float *uvOut, float *colourOut);
FERenderObject *__fastcall CreateCachedRender(cFEngRender *, void *, FEObject *object, TextureInfo *texture);
void __fastcall FontPrintCharacter(FEngFont *font, void *, char *glyph, float x, float y, unsigned int *colours,
                                   FERenderObject *render, FEPackageRenderInfo *info);
FEClipInfo *__fastcall MakeObjectMatrix(cFEngRender *self, void *, char *data, float *matrix, int *colour, int context,
                                        float scale);
void __fastcall FERenderImage(cFEngRender *, void *, FEObject *object, FERenderObject *render, FEPackageRenderInfo *info);
void __fastcall FERenderColoredImage(cFEngRender *, void *, FEObject *object, FERenderObject *render,
                                     FEPackageRenderInfo *info);
void __fastcall FERenderMovie(cFEngRender *, void *, FEObject *object, FERenderObject *render, FEPackageRenderInfo *info);
void __fastcall FERenderString(cFEngRender *, void *, FEObject *object, FERenderObject *render, FEPackageRenderInfo *info);
void __fastcall FERenderMultiImage(cFEngRender *, void *, FEObject *object, FERenderObject *render,
                                   FEPackageRenderInfo *info);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

struct Node
{
    Node *next;
    Node *prev;
};

void Unlink(Node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

const int kPolyOffset = 0x10;
const int kPolyTexture = 0xb0;
const int kPolyMask = 0xb4;
const int kTransform = 0x30;
const float kToUnit = 0.003921569f;
/** A point counts as inside a clip plane down to this distance. */
const float kClipMargin = -0.5f;

enum ObjectType
{
    kObjectImage = 1,
    kObjectString = 2,
    kObjectMovie = 7,
    kObjectColoredImage = 9,
    kObjectMultiImage = 12,
};

/** FEObject flag: the render object is rebuilt rather than redrawn. */
const unsigned int kRedrawEveryFrame = 0x2000000;
/** MoviePlayer state: a picture is ready. */
const int kMoviePlaying = 5;
/** The large font's name hash: Japanese and Korean (languages 8, 9) draw it at twice the size. */
const unsigned int kLargeFontName = 0x9583aa1a;

eViewPlatInterface *FrontEndView()
{
    return reinterpret_cast<eViewPlatInterface *>(0x9195e0);
}

Node *Polys(void *object)
{
    return &At<Node>(object, 0x18);
}

ePoly *PolyOf(char *record)
{
    return reinterpret_cast<ePoly *>(record + kPolyOffset);
}

/** A new polygon at the end of the object's list, or nullptr when no pool has room. */
char *NewPoly(FERenderObject *object)
{
    SlotPool *pool = FERenderEPolySlotPool;
    void *memory;
    if (At<int>(pool, 0x18) != At<int>(pool, 0x2c))
        memory = pool->FastMalloc();
    else
    {
        if (!FERenderEPolySlotPoolOverflow)
        {
            FERenderEPolySlotPoolOverflow = bNewSlotPool(0xc0, 0x200, "FERenderEPolySlotPoolOverflow", 0);
            At<unsigned int>(FERenderEPolySlotPoolOverflow, 0x14) &= ~4u;
        }
        memory = FERenderEPolySlotPoolOverflow->Malloc();
    }
    if (!memory)
        return nullptr;
    char *record = static_cast<char *>(memory);
    new (PolyOf(record)) ePoly();
    At<TextureInfo *>(record, kPolyMask) = nullptr;
    At<TextureInfo *>(record, kPolyTexture) = nullptr;
    Node *list = Polys(object);
    Node *tail = list->prev;
    tail->next = reinterpret_cast<Node *>(record);
    list->prev = reinterpret_cast<Node *>(record);
    reinterpret_cast<Node *>(record)->prev = tail;
    reinterpret_cast<Node *>(record)->next = list;
    ++At<int>(object, 0x20);
    return record;
}

/** A rectangle at depth z, through the object's transform; z itself stays as given. */
void SetQuad(FERenderObject *object, ePoly *poly, float x0, float y0, float x1, float y1, float z)
{
    const float corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const bMatrix4 *transform = &At<bMatrix4>(object, kTransform);
    for (int i = 0; i < 4; ++i)
    {
        poly->Vertices[i][0] = corners[i][0];
        poly->Vertices[i][1] = corners[i][1];
        poly->Vertices[i][2] = z;
        bVector3 *vertex = reinterpret_cast<bVector3 *>(poly->Vertices[i]);
        bMulMatrix(vertex, transform, vertex);
        poly->Vertices[i][2] = z;
    }
}

void SetUVs(float uvs[4][2], float u0, float v0, float u1, float v1)
{
    uvs[0][0] = u0;
    uvs[0][1] = v0;
    uvs[1][0] = u1;
    uvs[1][1] = v0;
    uvs[2][0] = u1;
    uvs[2][1] = v1;
    uvs[3][0] = u0;
    uvs[3][1] = v1;
}

void SetColours(ePoly *poly, const unsigned int *colours)
{
    for (int i = 0; i < 4; ++i)
        reinterpret_cast<unsigned int *>(poly->Colours)[i] = colours[i];
}

/** The working vertices of one clipping pass. */
struct ClipSet
{
    float *pos;
    float *uv;
    float *colour;
};

float PlaneDistance(const float *plane, float d, const float *pos)
{
    return plane[0] * pos[0] + plane[1] * pos[1] + plane[2] * pos[2] + d;
}

/** Writes the point where the edge prev-cur crosses the plane as output vertex `out`. */
void EmitCrossing(const ClipSet &in, const ClipSet &o, unsigned int prev, unsigned int cur, unsigned int out,
                  const float *plane, float d)
{
    const float *p = in.pos + prev * 4;
    const float *c = in.pos + cur * 4;
    float *pos = o.pos + out * 4;
    const float dx = c[0] - p[0], dy = c[1] - p[1], dz = c[2] - p[2];
    const float t = -(PlaneDistance(plane, d, p) / (plane[0] * dx + plane[1] * dy + plane[2] * dz));
    pos[0] = p[0] + dx * t;
    pos[1] = p[1] + dy * t;
    pos[2] = p[2] + dz * t;
    for (int k = 0; k < 2; ++k)
        o.uv[out * 2 + k] = (in.uv[cur * 2 + k] - in.uv[prev * 2 + k]) * t + in.uv[prev * 2 + k];
    for (int k = 0; k < 4; ++k)
        o.colour[out * 4 + k] = (in.colour[cur * 4 + k] - in.colour[prev * 4 + k]) * t + in.colour[prev * 4 + k];
}

void CopyVertex(const ClipSet &in, const ClipSet &o, unsigned int from, unsigned int out)
{
    for (int k = 0; k < 3; ++k)
        o.pos[out * 4 + k] = in.pos[from * 4 + k];
    for (int k = 0; k < 2; ++k)
        o.uv[out * 2 + k] = in.uv[from * 2 + k];
    for (int k = 0; k < 4; ++k)
        o.colour[out * 4 + k] = in.colour[from * 4 + k];
}
} // namespace

/** FERenderEPoly::operator delete: back to whichever pool it came from; the overflow pool goes once empty. */
void __cdecl FreeRenderPoly(void *poly)
{
    if (FERenderEPolySlotPool->GetSlotNumber(poly) >= 0)
    {
        FERenderEPolySlotPool->Free(poly);
        return;
    }
    FERenderEPolySlotPoolOverflow->Free(poly);
    if (!At<int>(FERenderEPolySlotPoolOverflow, 0x18))
    {
        bDeleteSlotPool(FERenderEPolySlotPoolOverflow);
        FERenderEPolySlotPoolOverflow = nullptr;
    }
}

void __fastcall FreeRenderPolyList(void *listObject)
{
    Node *list = static_cast<Node *>(listObject);
    for (Node *n = list->next; n != list; n = list->next)
    {
        Unlink(n);
        FreeRenderPoly(n);
    }
}

FERenderObject::FERenderObject(FEObject *object, TextureInfo *texture)
{
    char *self = reinterpret_cast<char *>(this);
    At<FEObject *>(self, 8) = object;
    At<TextureInfo *>(self, 0x14) = texture;
    At<int>(self, 0xc) = 0;
    At<int>(self, 0x10) = 0;
    Node *list = Polys(self);
    list->next = list->prev = list;
    At<int>(self, 0x20) = 0;
    float *transform = &At<float>(self, kTransform);
    memset(transform, 0, 16 * sizeof(float));
    transform[0] = transform[5] = transform[10] = transform[15] = 1.0f;
}

FERenderObject::~FERenderObject()
{
    FreeRenderPolyList(Polys(this));
}

void FERenderObject::Clear(FEPackageRenderInfo *)
{
    FreeRenderPolyList(Polys(this));
    At<int>(this, 0x20) = 0;
    At<int>(this, 0x10) = 0;
    At<unsigned int>(this, 0xc) &= ~2u;
}

void FERenderObject::Render()
{
    eViewPlatInterface *view = FrontEndView();
    view->FEBeginBatchRender(At<int>(this, 0x20));
    Node *list = Polys(this);
    for (Node *n = list->next; n != list; n = n->next)
    {
        char *record = reinterpret_cast<char *>(n);
        TextureInfo *texture = At<TextureInfo *>(record, kPolyTexture);
        if (!texture)
            texture = At<TextureInfo *>(this, 0x14);
        texture = nfs::prompts::Swap(texture);
        if (PolyOf(record)->Flags & 4)
            view->FERender(PolyOf(record), texture, At<TextureInfo *>(record, kPolyMask), 0);
        else
            view->FERender(PolyOf(record), texture, 0);
    }
    view->FEEndBatchRender();
    ++At<int>(this, 0x10);
    At<unsigned int>(this, 0xc) |= 2;
}

FERenderEPoly *FERenderObject::AddPoly(float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1,
                                       float v1, unsigned int *colours, FEPackageRenderInfo *)
{
    char *record = NewPoly(this);
    if (!record)
        return nullptr;
    ePoly *poly = PolyOf(record);
    SetQuad(this, poly, x0, y0, x1, y1, z);
    SetUVs(poly->UVs, u0, v0, u1, v1);
    SetColours(poly, colours);
    poly->Shape = 1;
    poly->Flags = 1;
    return reinterpret_cast<FERenderEPoly *>(record);
}

void FERenderObject::AddPolyWithRotatedMask(float x0, float y0, float x1, float y1, float z, float u0, float v0,
                                            float u1, float v1, float maskU0, float maskV0, float maskU1, float maskV1,
                                            float maskU2, float maskV2, float maskU3, float maskV3,
                                            unsigned int *colours, TextureInfo *texture, TextureInfo *mask)
{
    char *record = NewPoly(this);
    if (!record)
        return;
    At<TextureInfo *>(record, kPolyTexture) = texture;
    At<TextureInfo *>(record, kPolyMask) = mask;
    ePoly *poly = PolyOf(record);
    SetQuad(this, poly, x0, y0, x1, y1, z);
    SetUVs(poly->UVs, u0, v0, u1, v1);
    const float masks[8] = {maskU0, maskV0, maskU1, maskV1, maskU2, maskV2, maskU3, maskV3};
    memcpy(poly->UVsMask, masks, sizeof(masks));
    SetColours(poly, colours);
    poly->Flags = 5;
    poly->Shape = 1;
}

/** FERenderObject::ClipAligned (thiscall): an axis-aligned clip rectangle, side by side (results in the first set). */
unsigned int __fastcall FERenderClipAligned(FERenderObject *, void *, FEClipInfo *clip, bVector3 *pos, bVector2 *uv,
                                            bVector4 *colour, bVector3 *posOut, bVector2 *uvOut, bVector4 *colourOut)
{
    unsigned int n = ClipLeft(posOut, uvOut, colourOut, pos, uv, colour, 4, At<float>(clip, 0x4c));
    if (n)
        n = ClipTop(pos, uv, colour, posOut, uvOut, colourOut, n, At<float>(clip, 0x40));
    if (n)
        n = ClipRight(posOut, uvOut, colourOut, pos, uv, colour, n, At<float>(clip, 0x44));
    if (n)
        n = ClipBottom(pos, uv, colour, posOut, uvOut, colourOut, n, At<float>(clip, 0x48));
    return n;
}

/** FERenderObject::ClipGeneral (thiscall): the polygon against four arbitrary planes (Sutherland-Hodgman). */
unsigned int __fastcall FERenderClipGeneral(FERenderObject *, void *, FEClipInfo *clip, float *pos, float *uv,
                                            float *colour, float *posOut, float *uvOut, float *colourOut)
{
    ClipSet in = {pos, uv, colour};
    ClipSet out = {posOut, uvOut, colourOut};
    unsigned int count = 4, written = 0;
    for (int plane = 0; plane < 4; ++plane)
    {
        if (!count)
            return 0;
        const float *p = &At<float>(clip, plane * 0x10);
        const float d = At<float>(clip, 0x40 + plane * 4);
        unsigned int prev = count - 1;
        bool prevInside = kClipMargin < PlaneDistance(p, d, in.pos + prev * 4);
        written = 0;
        if (prevInside)
            CopyVertex(in, out, prev, written++);
        for (unsigned int i = 0; i < count; ++i)
        {
            if (PlaneDistance(p, d, in.pos + i * 4) <= kClipMargin)
            {
                if (prevInside)
                {
                    EmitCrossing(in, out, prev, i, written++, p, d);
                    prevInside = false;
                }
            }
            else
            {
                if (!prevInside)
                {
                    EmitCrossing(in, out, prev, i, written++, p, d);
                    prevInside = true;
                }
                CopyVertex(in, out, i, written++);
            }
            prev = i;
        }
        count = written;
        const ClipSet swap = in;
        in = out;
        out = swap;
    }
    return written;
}

void FERenderObject::AddPoly(float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1, float v1,
                             unsigned int *colours, FEClipInfo *clip, FEPackageRenderInfo *info)
{
    if (!clip)
    {
        AddPoly(x0, y0, x1, y1, z, u0, v0, u1, v1, colours, info);
        return;
    }
    // Eight vertices at most, in two sets the clip passes alternate between.
    float pos[2][8][4] = {};
    float uv[2][8][2] = {};
    float colour[2][8][4] = {};
    const float corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float uvs[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(colours);
    const bMatrix4 *transform = &At<bMatrix4>(this, kTransform);
    for (int i = 0; i < 4; ++i)
    {
        pos[0][i][0] = corners[i][0];
        pos[0][i][1] = corners[i][1];
        pos[0][i][2] = z;
        uv[0][i][0] = uvs[i][0];
        uv[0][i][1] = uvs[i][1];
        for (int k = 0; k < 4; ++k)
            colour[0][i][k] = static_cast<float>(bytes[i * 4 + k]) * kToUnit;
        bVector3 *vertex = reinterpret_cast<bVector3 *>(pos[0][i]);
        bMulMatrix(vertex, transform, vertex);
    }

    unsigned int count;
    if (At<unsigned char>(clip, 0x50) & 1)
        count = FERenderClipAligned(this, nullptr, clip, reinterpret_cast<bVector3 *>(pos[0]),
                                    reinterpret_cast<bVector2 *>(uv[0]), reinterpret_cast<bVector4 *>(colour[0]),
                                    reinterpret_cast<bVector3 *>(pos[1]), reinterpret_cast<bVector2 *>(uv[1]),
                                    reinterpret_cast<bVector4 *>(colour[1]));
    else
        count = FERenderClipGeneral(this, nullptr, clip, pos[0][0], uv[0][0], colour[0][0], pos[1][0], uv[1][0],
                                    colour[1][0]);
    if (count < 3)
        return;

    unsigned char final[8][4];
    for (unsigned int i = 0; i < count; ++i)
        for (int k = 0; k < 4; ++k)
            final[i][k] = static_cast<unsigned char>(static_cast<int>(colour[1][i][k] * 255.0f));

    // A fan around the first vertex; each polygon's fourth corner repeats its third.
    for (unsigned int i = 0; i + 2 < count; ++i)
    {
        char *record = NewPoly(this);
        if (!record)
            return;
        ePoly *poly = PolyOf(record);
        const unsigned int corners2[4] = {0, i + 1, i + 2, i + 2};
        for (int k = 0; k < 4; ++k)
        {
            const unsigned int from = corners2[k];
            poly->Vertices[k][0] = pos[0][from][0];
            poly->Vertices[k][1] = pos[0][from][1];
            poly->Vertices[k][2] = pos[0][from][2];
            poly->UVs[k][0] = uv[0][from][0];
            poly->UVs[k][1] = uv[0][from][1];
            memcpy(poly->Colours[k], final[from], 4);
        }
        poly->UVsMask[0][0] = poly->UVs[2][0];
        poly->Shape = 1;
        poly->Flags = 1;
    }
}

cFEngRender::cFEngRender()
{
    char *self = reinterpret_cast<char *>(this);
    At<int>(self, 0) = 0;
    mpobFERenderObjectSlotPool = bNewSlotPool(0x70, 0x180, "FERenderObjectSlotPool", 0);
    FERenderEPolySlotPool = bNewSlotPool(0xc0, 0x348, "FERenderEPolySlotPool", 0);
    memset(self + 0x10, 0, 0x1ef0 * sizeof(int));
}

RenderContext *cFEngRender::GetRenderContext(unsigned short index)
{
    return reinterpret_cast<RenderContext *>(reinterpret_cast<char *>(this) + 0x10 + index * 0xb0);
}

void cFEngRender::PrepForPackage(FEPackage *package)
{
    *reinterpret_cast<float *>(0x91d90c) = -999999.0f;
    *reinterpret_cast<FEPackage **>(0x91cfac) = package;
}

void cFEngRender::PackageFinished(FEPackage *)
{
}

/** cFEngRender::CreateCachedRender (thiscall): an object's render object, recorded in it (+0x58). */
FERenderObject *__fastcall CreateCachedRender(cFEngRender *, void *, FEObject *object, TextureInfo *texture)
{
    void *memory = bOMalloc(mpobFERenderObjectSlotPool);
    FERenderObject *render = memory ? new (memory) FERenderObject(object, texture) : nullptr;
    At<FERenderObject *>(object, 0x58) = render;
    return render;
}

void cFEngRender::RemoveCachedRender(FEObject *object, FEPackageRenderInfo *info)
{
    FERenderObject *render = At<FERenderObject *>(object, 0x58);
    if (!render)
        return;
    At<FERenderObject *>(object, 0x58) = nullptr;
    render->Clear(info);
    render->~FERenderObject();
    bFree(mpobFERenderObjectSlotPool, render);
}

namespace
{
/** The object's matrix (made by MakeRenderMatrix, then moved to the screen's centre) and its colour. */
struct ScreenBuild
{
    float matrix[16];
    int colour[4];
    FEClipInfo *clip;

    ScreenBuild(cFEngRender *render, FEObject *object, float scale)
    {
        clip = MakeObjectMatrix(render, nullptr, static_cast<char *>(At<void *>(object, 0x2c)), matrix, colour,
                                At<unsigned short>(object, 0x20), scale);
        float screen[16] = {};
        screen[0] = screen[5] = screen[10] = screen[15] = 1.0f;
        screen[12] = 320.0f;
        screen[13] = 240.0f;
        bMatrix4 *m = reinterpret_cast<bMatrix4 *>(matrix);
        bMulMatrix(m, reinterpret_cast<const bMatrix4 *>(screen), m);
    }
};

/** The drawn share of a texture's power-of-two allocation (size: its stored width or height). */
float ShareOfAllocation(short size)
{
    const int sized = size;
    float wide = static_cast<float>(sized);
    if (sized < 0)
        wide += 4294967296.0f;
    int allocated = 0;
    if (sized)
    {
        unsigned int rest = static_cast<unsigned int>(sized - 1) >> 1;
        allocated = 2;
        while (rest)
        {
            rest >>= 1;
            allocated <<= 1;
        }
    }
    float allocatedSize = static_cast<float>(allocated);
    if (allocated < 0)
        allocatedSize += 4294967296.0f;
    return wide / allocatedSize;
}

/** A vertex colour from the colour components (the renderer's 0..128 range for colour; alpha as is). */
unsigned int PackColour(int a, int b, int c, int alpha)
{
    return (((static_cast<unsigned int>(a / 2) | (static_cast<unsigned int>(alpha) << 8)) << 8 |
             static_cast<unsigned int>(b / 2))
            << 8) |
           static_cast<unsigned int>(c / 2);
}

FERenderObject *CachedRender(cFEngRender *self, FEObject *object, FERenderObject *render, TextureInfo *texture,
                             FEPackageRenderInfo *info)
{
    if (!render)
        return CreateCachedRender(self, nullptr, object, texture);
    render->Clear(info);
    return render;
}

/** The unit quad centred on the origin with the object's matrix, one set of corner colours, drawn at once. */
void AddAndDraw(FERenderObject *render, const float *matrix, float u0, float v0, float u1, float v1,
                unsigned int *colours, FEClipInfo *clip, FEPackageRenderInfo *info, TextureInfo *texture)
{
    memcpy(&At<float>(render, kTransform), matrix, 16 * sizeof(float));
    render->AddPoly(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f, u0, v0, u1, v1, colours, clip, info);
    At<TextureInfo *>(render, 0x14) = texture;
    render->Render();
}

const float *ImageUVs(FEObject *object)
{
    return &At<float>(At<void *>(object, 0x2c), 0x44);
}

short TextureWidth(TextureInfo *texture)
{
    return At<short>(texture, 0x44);
}

short TextureHeight(TextureInfo *texture)
{
    return At<short>(texture, 0x46);
}
/**
 * The original rotate_uvs (a register argument, so inlined here): four texel corners turned by `angle` radians about
 * their centre, offset by (cx, cy).
 */
void RotateCorners(float *uv, float angle, float cx, float cy)
{
    const float hx = (uv[4] - uv[0]) * 0.5f;
    const float hy = (uv[5] - uv[1]) * 0.5f;
    for (int i = 0; i < 4; ++i)
    {
        uv[i * 2] = (uv[i * 2] - hx) - cx;
        uv[i * 2 + 1] = (uv[i * 2 + 1] - hy) - cy;
    }
    const unsigned short turn = static_cast<unsigned short>(static_cast<int>(angle * 10430.378f));
    const float s = bSin(turn);
    const float c = bCos(turn);
    for (int i = 0; i < 4; ++i)
    {
        const float x = uv[i * 2], y = uv[i * 2 + 1];
        uv[i * 2] = c * x + s * y + hx + cx;
        uv[i * 2 + 1] = c * y - s * x + hy + cy;
    }
}
} // namespace

/**
 * cFEngRender::MakeRenderMatrix (thiscall): the matrix of an object's data, position x pivot x rotation x -pivot x
 * scale, and its colour. FEObjData: +0 colour (four ints), +0x10 pivot, +0x1c position, +0x28 rotation quaternion,
 * +0x38 scale. Objects in a context (index > 0) take its matrix, tint (four bytes at the context's +0xa8) and clip
 * rectangle (at +0x40 when +0xa4 is set).
 */
FEClipInfo *__fastcall MakeObjectMatrix(cFEngRender *self, void *, char *data, float *matrix, int *colour, int context,
                                        float scale)
{
    const float *pivot = &At<float>(data, 0x10);
    const float *position = &At<float>(data, 0x1c);
    const float *rotation = &At<float>(data, 0x28);
    const float *scales = &At<float>(data, 0x38);
    const unsigned int kOneBits = 0x3f800000;
    const bool pivoted = !(pivot[0] == 0.0f && pivot[1] == 0.0f && pivot[2] == 0.0f);
    const bool scaled = !(At<unsigned int>(data, 0x38) == kOneBits && At<unsigned int>(data, 0x3c) == kOneBits &&
                          At<unsigned int>(data, 0x40) == kOneBits && scale == 1.0f);
    const bool rotated = !(rotation[0] == 0.0f && rotation[1] == 0.0f && rotation[2] == 0.0f &&
                           At<unsigned int>(data, 0x34) == kOneBits);

    float translate[16] = {}, toPivot[16] = {}, fromPivot[16] = {}, scaling[16] = {}, turning[16] = {};
    translate[0] = translate[5] = translate[10] = translate[15] = 1.0f;
    translate[12] = position[0];
    translate[13] = position[1];
    translate[14] = position[2];
    if (pivoted)
    {
        toPivot[0] = toPivot[5] = toPivot[10] = toPivot[15] = 1.0f;
        fromPivot[0] = fromPivot[5] = fromPivot[10] = fromPivot[15] = 1.0f;
        for (int i = 0; i < 3; ++i)
        {
            toPivot[12 + i] = pivot[i];
            fromPivot[12 + i] = -pivot[i];
        }
    }
    if (scaled)
    {
        scaling[0] = scale * scales[0];
        scaling[5] = scale * scales[1];
        scaling[10] = scale * scales[2];
        scaling[15] = 1.0f;
    }
    if (rotated)
    {
        turning[0] = turning[5] = turning[10] = turning[15] = 1.0f;
        const float quaternion[4] = {rotation[0], rotation[1], rotation[2], rotation[3]};
        NFS_ORIGINAL(0x466af0, void(__thiscall *)(const float *, float *))(quaternion, turning);
    }

    bMatrix4 *out = reinterpret_cast<bMatrix4 *>(matrix);
    memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
    bMulMatrix(out, out, reinterpret_cast<const bMatrix4 *>(translate));
    if (pivoted)
        bMulMatrix(out, out, reinterpret_cast<const bMatrix4 *>(toPivot));
    if (rotated)
        bMulMatrix(out, out, reinterpret_cast<const bMatrix4 *>(turning));
    if (pivoted)
        bMulMatrix(out, out, reinterpret_cast<const bMatrix4 *>(fromPivot));
    if (scaled)
        bMulMatrix(out, out, reinterpret_cast<const bMatrix4 *>(scaling));

    const int *base = &At<int>(data, 0);
    if (!context)
    {
        *reinterpret_cast<FEColor *>(colour) = *reinterpret_cast<const FEColor *>(base);
        return nullptr;
    }
    char *renderContext = reinterpret_cast<char *>(self) + context * 0xb0 + 0x10;
    colour[2] = (static_cast<int>(At<unsigned char>(renderContext, 0xa9)) * base[2] + 0x80) >> 8;
    colour[1] = (static_cast<int>(At<unsigned char>(renderContext, 0xaa)) * base[1] + 0x80) >> 8;
    colour[0] = (static_cast<int>(At<unsigned char>(renderContext, 0xab)) * base[0] + 0x80) >> 8;
    colour[3] = (static_cast<int>(At<unsigned char>(renderContext, 0xa8)) * base[3] + 0x80) >> 8;
    bMulMatrix(out, reinterpret_cast<const bMatrix4 *>(renderContext), out);
    return At<int>(renderContext, 0xa4) ? reinterpret_cast<FEClipInfo *>(renderContext + 0x40) : nullptr;
}

void __fastcall FERenderImage(cFEngRender *self, void *, FEObject *object, FERenderObject *render,
                              FEPackageRenderInfo *info)
{
    ScreenBuild build(self, object, 1.0f);
    TextureInfo *texture = GetTextureInfo(At<unsigned int>(object, 0x24), 1, 0);
    render = CachedRender(self, object, render, texture, info);
    const float uScale = ShareOfAllocation(TextureWidth(texture));
    const float vScale = ShareOfAllocation(TextureHeight(texture));
    const float *uvs = ImageUVs(object);
    const unsigned int colour = PackColour(build.colour[0], build.colour[1], build.colour[2], build.colour[3]);
    unsigned int colours[4] = {colour, colour, colour, colour};
    AddAndDraw(render, build.matrix, uScale * uvs[0], vScale * uvs[1], uScale * uvs[2], vScale * uvs[3], colours,
               build.clip, info, texture);
}

void __fastcall FERenderColoredImage(cFEngRender *self, void *, FEObject *object, FERenderObject *render,
                                     FEPackageRenderInfo *info)
{
    ScreenBuild build(self, object, 1.0f);
    TextureInfo *texture = GetTextureInfo(At<unsigned int>(object, 0x24), 1, 0);
    render = CachedRender(self, object, render, texture, info);
    const float uScale = ShareOfAllocation(TextureWidth(texture));
    const float vScale = ShareOfAllocation(TextureHeight(texture));
    const float *uvs = ImageUVs(object);
    // Corner colours follow the UVs: (r, g, b, a) four times, at +0x54 of the object's data.
    const int *corner = &At<int>(At<void *>(object, 0x2c), 0x54);
    unsigned int colours[4];
    for (int i = 0; i < 4; ++i, corner += 4)
        colours[i] = PackColour(corner[0], corner[1], corner[2], corner[3]);
    AddAndDraw(render, build.matrix, uScale * uvs[0], vScale * uvs[1], uScale * uvs[2], vScale * uvs[3], colours,
               build.clip, info, texture);
}

void __fastcall FERenderMovie(cFEngRender *self, void *, FEObject *object, FERenderObject *render,
                              FEPackageRenderInfo *info)
{
    if (!gMoviePlayer || At<int>(gMoviePlayer, 0x11c) != kMoviePlaying)
        return;
    TextureInfo *texture = gMoviePlayer->GetTexture();
    ScreenBuild build(self, object, 1.0f);
    render = CachedRender(self, object, render, texture, info);
    const unsigned int colour = PackColour(build.colour[0], build.colour[1], build.colour[2], build.colour[3]);
    unsigned int colours[4] = {colour, colour, colour, colour};
    const float uScale = ShareOfAllocation(TextureWidth(texture));
    const float vScale = ShareOfAllocation(TextureHeight(texture));
    // A settings copy on the stack; its +0xc turns the picture the other way up.
    unsigned char settings[0x118];
    char *copied = NFS_ORIGINAL(0x56b010, char *(__thiscall *)(MoviePlayer *, void *))(gMoviePlayer, settings);
    const bool flipped = At<int>(copied, 0xc) != 0;
    AddAndDraw(render, build.matrix, 0.0f, flipped ? 0.0f : vScale, uScale, flipped ? vScale : 0.0f, colours, build.clip,
               info, texture);
}

void __fastcall FERenderString(cFEngRender *self, void *, FEObject *object, FERenderObject *render,
                               FEPackageRenderInfo *info)
{
    FEngFont *font = FindFont(At<unsigned int>(object, 0x24));
    if (!font || !At<TextureInfo *>(font, 8))
        return;
    render = CachedRender(self, object, render, At<TextureInfo *>(font, 8), info);
    const int language = *reinterpret_cast<int *>(0x8f41c0);
    const float scale = (language == 8 || language == 9) && At<unsigned int>(object, 0x24) == kLargeFontName ? 2.0f : 1.0f;

    short localized[0x406];
    const short *text = At<const short *>(object, 0x64);
    if (!(At<unsigned char>(object, 0x1c) & 2) && GetLocalizedWideString(localized, 0x800, At<unsigned int>(object, 0x60)))
        text = localized;

    ScreenBuild build(self, object, scale);
    const int widthLimit = At<int>(object, 0x74);
    float maxWidth = static_cast<float>(widthLimit);
    if (widthLimit < 0)
        maxWidth += 4294967296.0f;
    if (maxWidth == 0.0f)
        maxWidth = 3.4028235e+38f;
    const float width = font->GetLineWidth(text, 0, 0, false);
    bMatrix4 *matrix = reinterpret_cast<bMatrix4 *>(build.matrix);
    if (widthLimit && width > maxWidth && !(At<unsigned char>(object, 0x6c) & 0x10))
    {
        // Squeezed to fit the object's width.
        float squeeze[16] = {};
        squeeze[0] = maxWidth / width;
        squeeze[5] = squeeze[10] = squeeze[15] = 1.0f;
        bMulMatrix(matrix, matrix, reinterpret_cast<const bMatrix4 *>(squeeze));
    }
    font->RenderString(*reinterpret_cast<const FEColor *>(build.colour), text, reinterpret_cast<FEString *>(object), matrix,
                       render, info);
}

void __fastcall FERenderMultiImage(cFEngRender *self, void *, FEObject *object, FERenderObject *render,
                                   FEPackageRenderInfo *info)
{
    ScreenBuild build(self, object, 1.0f);
    TextureInfo *texture = GetTextureInfo(At<unsigned int>(object, 0x24), 1, 0);
    TextureInfo *mask = GetTextureInfo(reinterpret_cast<FEMultiImage *>(object)->GetTexture(0), 1, 0);
    render = CachedRender(self, object, render, texture, info);

    const short width = TextureWidth(texture), height = TextureHeight(texture);
    const float uScale = ShareOfAllocation(width);
    const float vScale = ShareOfAllocation(height);
    float texelW = static_cast<float>(static_cast<int>(width));
    if (width < 0)
        texelW += 4294967296.0f;
    float texelH = static_cast<float>(static_cast<int>(height));
    if (height < 0)
        texelH += 4294967296.0f;
    char *data = static_cast<char *>(At<void *>(object, 0x2c));
    const float *uvs = ImageUVs(object);
    const unsigned int colour = PackColour(build.colour[0], build.colour[1], build.colour[2], build.colour[3]);
    unsigned int colours[4] = {colour, colour, colour, colour};

    // The mask rectangle in texels, turned about the object's mask pivot, then back to 0..1.
    const float maskLeft = texelW * uScale * At<float>(data, 0x54);
    const float maskTop = texelH * vScale * At<float>(data, 0x58);
    const float maskRight = texelW * uScale * At<float>(data, 0x6c);
    const float maskBottom = texelH * vScale * At<float>(data, 0x70);
    float corners[8] = {maskLeft, maskTop, maskRight, maskTop, maskRight, maskBottom, maskLeft, maskBottom};
    RotateCorners(corners, At<float>(data, 0x8c) * 0.017453292f, texelW * At<float>(data, 0x84) - texelW * 0.5f,
                  texelH * At<float>(data, 0x88) - texelH * 0.5f);
    const float toUnitW = 1.0f / texelW, toUnitH = 1.0f / texelH;
    for (int i = 0; i < 4; ++i)
    {
        corners[i * 2] *= toUnitW;
        corners[i * 2 + 1] *= toUnitH;
    }

    memcpy(&At<float>(render, kTransform), build.matrix, 16 * sizeof(float));
    render->AddPolyWithRotatedMask(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f, uScale * uvs[0], vScale * uvs[1], uScale * uvs[2],
                                   vScale * uvs[3], corners[0], corners[1], corners[2], corners[3], corners[4],
                                   corners[5], corners[6], corners[7], colours, texture, mask);
    At<TextureInfo *>(render, 0x14) = texture;
    render->Render();
}

void cFEngRender::RenderObject(FEObject *object, FEPackageRenderInfo *info)
{
    unsigned int &flags = At<unsigned int>(object, 0x1c);
    if (flags & 0x40)
        return;
    const int type = At<int>(object, 0x18);
    // Movies are rebuilt every frame.
    if (type == kObjectMovie)
        flags |= kRedrawEveryFrame;
    FERenderObject *render = At<FERenderObject *>(object, 0x58);
    if (render && (At<unsigned int>(render, 0xc) & 2) && !(flags & kRedrawEveryFrame))
    {
        render->Render();
        return;
    }
    switch (type)
    {
    case kObjectImage:
        FERenderImage(this, nullptr, object, render, info);
        break;
    case kObjectString:
        FERenderString(this, nullptr, object, render, info);
        break;
    case kObjectMovie:
        FERenderMovie(this, nullptr, object, render, info);
        break;
    case kObjectColoredImage:
        FERenderColoredImage(this, nullptr, object, render, info);
        break;
    case kObjectMultiImage:
        FERenderMultiImage(this, nullptr, object, render, info);
        break;
    }
}

void cFEngRender::AddToRenderList(FEObject *object)
{
    float z = At<float>(At<char *>(object, 0x2c), 0x24);
    const unsigned short context = At<unsigned short>(object, 0x20);
    if (context)
        z += At<float>(this, context * 0xb0 + 0x48);
    if (At<unsigned char>(object, 0x1c) & 1)
        return;
    // A new depth starts a new layer (the layer flush is empty on the PC).
    float &layer = *reinterpret_cast<float *>(0x91d90c);
    if (!(z == layer))
        layer = z;
    RenderObject(object, HACK_FEPkgMgr_GetPackageRenderInfo(*reinterpret_cast<FEPackage **>(0x91cfac)));
}

FEPackageRenderInfo *__cdecl HACK_FEPkgMgr_GetPackageRenderInfo(FEPackage *package)
{
    char *info = At<char *>(package, 0x40);
    return info ? reinterpret_cast<FEPackageRenderInfo *>(info + 0x2c) : nullptr;
}

namespace
{
const float kMinimumGlyphWidth = 4.0f;

/** The bitmap font data of a font (+0xc); +8 is its page texture, +0x24 the extra line gap, +0x28 the vertical offset. */
char *FontData(FEngFont *font)
{
    return At<char *>(font, 0xc);
}

float AlignedX(unsigned int flags, float width)
{
    if (flags & 1)
        return width * -0.5f;
    if (flags & 2)
        return -width;
    return 0.0f;
}

/** The glyph record for a character: looked up directly by its index, else by binary search. */
char *FindGlyph(FEngFont *font, unsigned short character)
{
    char *data = FontData(font);
    char *table = data + At<int>(data, 0x14);
    const int count = At<unsigned short>(data, 0xa);
    const int stride = (At<unsigned int>(data, 0xc) & 0x40000) ? 0x10 : 0xc;
    const int index = static_cast<int>(character) - 0x20;
    if (index < count && *reinterpret_cast<unsigned short *>(table + index * stride) == character)
        return table + index * stride;
    return NFS_ORIGINAL(0x7e88f0, char *(__cdecl *)(unsigned short, char *, int, int))(character, table, count, stride);
}
} // namespace

/**
 * FEngFont character draw (thiscall): one glyph as a polygon of the render object of the string. Glyph record: +2
 * width, +3 height, +4 x and +6 y in the page texture, +9 and +0xa the draw offsets.
 */
void __fastcall FontPrintCharacter(FEngFont *font, void *, char *glyph, float x, float y, unsigned int *colours,
                                   FERenderObject *render, FEPackageRenderInfo *info)
{
    char *page = reinterpret_cast<char *>(At<TextureInfo *>(font, 8));
    const float pageW = static_cast<float>(static_cast<int>(At<short>(page, 0x44)));
    const float pageH = static_cast<float>(static_cast<int>(At<short>(page, 0x46)));
    const int glyphW = At<unsigned char>(glyph, 2), glyphH = At<unsigned char>(glyph, 3);
    const int left = At<unsigned short>(glyph, 4), top = At<unsigned short>(glyph, 6);
    float drawW = static_cast<float>(glyphW);
    if (drawW < kMinimumGlyphWidth)
        drawW = kMinimumGlyphWidth;
    const float x0 = static_cast<float>(static_cast<int>(At<signed char>(glyph, 9))) + x;
    const float y0 = static_cast<float>(static_cast<int>(At<signed char>(glyph, 0xa))) + At<float>(font, 0x28) + y;
    render->AddPoly(x0, y0, x0 + drawW, static_cast<float>(glyphH) + y0, 1.0f, static_cast<float>(left - 1) / pageW,
                    static_cast<float>(top - 1) / pageH, static_cast<float>(left + glyphW + 1) / pageW,
                    static_cast<float>(top + glyphH + 1) / pageH, colours, info);
}

void FEngFont::RenderString(const FEColor &colourObject, const short *text, FEString *stringObject, bMatrix4 *matrix,
                            FERenderObject *render, FEPackageRenderInfo *info)
{
    char *string = reinterpret_cast<char *>(stringObject);
    const int *rgba = reinterpret_cast<const int *>(&colourObject);
    const unsigned int formatting = At<unsigned int>(string, 0x6c);
    const int leading = static_cast<int>(static_cast<float>(At<int>(string, 0x70)) * At<float>(this, 0x2c));
    const unsigned int widthLimit = At<unsigned int>(string, 0x74);
    const unsigned int flags = At<unsigned int>(string, 0x1c);
    const bool wrap = (formatting >> 4) & 1;
    unsigned int colours[4];
    colours[0] = colours[1] = colours[2] = colours[3] = PackColour(rgba[0], rgba[1], rgba[2], rgba[3]);

    const float width = GetLineWidth(text, flags, widthLimit, wrap);
    const float height = GetTextHeight(text, leading, flags, widthLimit, wrap);
    float y = 0.0f;
    if (formatting & 4)
        y = height * -0.5f;
    else if (formatting & 8)
        y = -height;
    float x = AlignedX(formatting, width);
    if (!At<TextureInfo *>(this, 8))
        return;
    memcpy(&At<float>(render, kTransform), matrix, 16 * sizeof(float));

    float lineStart = x;
    bool dollarEnds = true;
    unsigned int index = 0;
    const short *p = text;
    unsigned short character = static_cast<unsigned short>(*p++);
    const float lineGap = At<float>(this, 0x24);
    while (character)
    {
        const bool leadingSpace = character == 0x20 && x == lineStart && (At<unsigned int>(string, 0x6c) & 0x10);
        if (!leadingSpace)
        {
            const bool literal = (flags & 0x20) != 0;
            if (!literal && (character == 10 || character == 0x5e))
            {
                if (!*p)
                    break;
                x = AlignedX(formatting, GetLineWidth(p, flags, widthLimit, wrap));
                lineStart = x;
                y = static_cast<float>(leading) + lineGap + y;
            }
            else
            {
                bool wrapped = false;
                if (widthLimit && character == 0x20 && (At<unsigned int>(string, 0x6c) & 0x10))
                {
                    const float wordWidth = GetNextWordWidth(p - 1, At<unsigned int>(string, 0x1c));
                    const float used = (x - lineStart) + wordWidth;
                    float limit = static_cast<float>(static_cast<int>(widthLimit));
                    if (static_cast<int>(widthLimit) < 0)
                        limit += 4294967296.0f;
                    if (limit < used)
                    {
                        x = CalculateXOffset(formatting, GetLineWidth(p, flags, widthLimit, wrap));
                        lineStart = x;
                        y = static_cast<float>(leading) + lineGap + y;
                        wrapped = true;
                    }
                }
                if (!wrapped)
                {
                    if (!literal && !(flags & 0x800) && character == 0x24)
                    {
                        if (static_cast<unsigned short>(*p) == 0x24)
                        {
                            ++p;
                            ++index;
                            dollarEnds = false;
                        }
                        else if (dollarEnds)
                            return;
                    }
                    unsigned short drawn = character;
                    if (drawn >= 0xff80)
                        drawn &= 0xff;
                    if (drawn == 0x99)
                        drawn = 0x2122;
                    else if (drawn == 0x9c)
                        drawn = 0x153;
                    else if (drawn == 0xa0)
                        drawn = 0x20;
                    if (char *glyph = FindGlyph(this, drawn))
                    {
                        const unsigned short previous = static_cast<unsigned short>(p[-2]);
                        float kern = 0.0f;
                        if (previous && index)
                            kern = static_cast<float>(NFS_ORIGINAL(0x7e89d0, int(__thiscall *)(char *, char *, unsigned int))(
                                FontData(this), glyph, previous));
                        FontPrintCharacter(this, nullptr, glyph, kern + x, y, colours, render, info);
                        x += GetCharacterWidth(static_cast<short>(drawn), index ? static_cast<short>(previous) : 0, formatting);
                    }
                }
            }
        }
        ++index;
        character = static_cast<unsigned short>(*p++);
    }
    render->Render();
}

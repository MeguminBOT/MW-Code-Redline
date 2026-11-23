/**
 * The front end's text measuring (original: zFe2 FEngFont::CalculateXOffset 0x572300, CalculateYOffset 0x572330,
 * GetHeight 0x5722b0, GetCharacterWidth 0x586050, GetLineWidth 0x586130, GetTextWidth 0x586210, GetNextWordWidth
 * 0x591f60, GetTextHeight 0x591fd0; the glyph lookup 0x569bf0).
 *
 * FEngFont: +0xc its font (glyph count +0xa, flags +0xc (0x40000: 0x10-byte glyphs, else 0xc), glyph table at its own
 * +0x14 offset; a glyph: its character, then its advance at +0xe), +0x24 its line height. Characters 10 and '^' break
 * lines (unless flag 0x20), 13 is nothing; 0x99, 0x9c and 0xa0 stand for the trade mark, oe and a space, and anything
 * from 0xff80 is its low byte. Measures stay exactly the original's: a line's width passes the address of each
 * character (from the second on) where the previous character belongs, so kerning reads that address's low half, and a
 * text's width is the widest line measured from every character; heights add the leading and line height per line,
 * wrapping at the space before a word that would pass the width.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kLiteralBreaks = 0x20;
const unsigned int kNoWords = 0x200;
const double kUnsigned = 4294967296.0;
using BSearch = const char *(__cdecl *)(unsigned int, const char *, unsigned int, int);
const BSearch kBSearch = reinterpret_cast<BSearch>(0x7e88f0);
using Kern = int(__thiscall *)(void *, const char *, int);
const Kern kKern = reinterpret_cast<Kern>(0x7e89d0);

const char *Glyph(const void *font, unsigned int ch)
{
    const char *table = static_cast<const char *>(font) + At<int>(font, 0x14);
    const unsigned int count = At<unsigned short>(font, 0xa);
    const int stride = (At<unsigned int>(font, 0xc) & 0x40000) ? 0x10 : 0xc;
    const int index = static_cast<int>(ch & 0xffff) - 0x20;
    if (index < static_cast<int>(count) && At<short>(table, index * stride) == static_cast<short>(ch))
        return table + index * stride;
    return kBSearch(ch, table, count, stride);
}

bool LineBreak(int ch)
{
    return ch == 10 || ch == 0x5e;
}

double Unsigned(unsigned int v)
{
    double d = static_cast<double>(static_cast<int>(v));
    if (static_cast<int>(v) < 0)
        d += kUnsigned;
    return d;
}
} // namespace

float FEngFont::CalculateXOffset(unsigned int format, float width)
{
    if (format & 1)
        return width * -0.5f;
    if (format & 2)
        return -width;
    return 0.0f;
}

float FEngFont::CalculateYOffset(unsigned int format, float height)
{
    if (format & 4)
        return height * -0.5f;
    if (format & 8)
        return -height;
    return 0.0f;
}

float FEngFont::GetHeight()
{
    return At<float>(this, 0x24);
}

float FEngFont::GetCharacterWidth(short character, short previous, unsigned int flags)
{
    unsigned short ch = static_cast<unsigned short>(character);
    if ((!(flags & kLiteralBreaks) && LineBreak(ch)) || ch == 0xd)
        return 0.0f;
    if (ch >= 0xff80)
        ch &= 0xff;
    if (ch == 0x99)
        ch = 0x2122;
    else if (ch == 0x9c)
        ch = 0x153;
    else if (ch == 0xa0)
        ch = 0x20;
    void *font = At<void *>(this, 0xc);
    const char *glyph = Glyph(font, ch);
    if (!glyph)
        return 0.0f;
    const int advance = At<short>(glyph, 0xe);
    if (previous)
        return static_cast<float>(kKern(font, glyph, previous) + advance);
    return static_cast<float>(advance);
}

float FEngFont::GetLineWidth(short const *text, unsigned int flags, unsigned int maxWidth, bool wrap)
{
    if (!text)
        return 0.0f;
    float width = 0.0f;
    float lastSpace = 0.0f;
    double sum = 0.0;
    int index = 0;
    for (const short *p = text; *p; ++p, ++index)
    {
        const short ch = *p;
        if (LineBreak(ch))
            return static_cast<float>(sum);
        if (ch == 0x20)
            lastSpace = static_cast<float>(sum);
        const short previous = index ? static_cast<short>(reinterpret_cast<unsigned int>(p)) : 0;
        sum = static_cast<double>(GetCharacterWidth(ch, previous, flags)) + width;
        width = static_cast<float>(sum);
        if (maxWidth && Unsigned(maxWidth) < sum && wrap)
            return lastSpace > 0.0f ? lastSpace : static_cast<float>(sum);
    }
    return static_cast<float>(sum);
}

float FEngFont::GetTextWidth(short const *text, unsigned int flags)
{
    float widest = GetLineWidth(text, 0, 0, false);
    for (const short *p = text; *p;)
    {
        const short ch = *p++;
        if ((flags & kLiteralBreaks) || !LineBreak(ch))
        {
            const float w = GetLineWidth(p, 0, 0, false);
            if (w > widest)
                widest = w;
        }
    }
    return widest;
}

float FEngFont::GetNextWordWidth(short const *text, unsigned int flags)
{
    float width = 0.0f;
    if (flags & kNoWords)
        return width;
    const short *p = text;
    for (;;)
    {
        width = GetCharacterWidth(*p, p[-1], flags) + width;
        const unsigned short next = static_cast<unsigned short>(*++p);
        if (next == 0x20 || next == 0 || next == 10 || next == 0x5e)
            return width;
    }
}

float FEngFont::GetTextHeight(short const *text, int leading, unsigned int flags, unsigned int maxWidth, bool wrap)
{
    if (!text)
        return 0.0f;
    const float lineHeight = At<float>(this, 0x24);
    float height = 0.0f;
    float lineWidth = 0.0f;
    bool counted = true;
    bool wrapNow = false;
    int previous = 0;
    const void *font = At<void *>(this, 0xc);
    for (const short *p = text; *p; ++p)
    {
        const unsigned short ch = static_cast<unsigned short>(*p);
        bool newLine = LineBreak(ch);
        if (!newLine && ch != 0xd)
        {
            if (Glyph(font, ch & 0xff))
                counted = true;
            if (wrap && maxWidth)
            {
                if (ch == 0x20)
                {
                    const double word = static_cast<double>(GetNextWordWidth(p, flags)) + lineWidth;
                    if (Unsigned(maxWidth) < word)
                        wrapNow = true;
                }
                lineWidth = GetCharacterWidth(static_cast<short>(ch), static_cast<short>(previous), flags) + lineWidth;
            }
            newLine = wrapNow;
        }
        if (newLine)
        {
            height = static_cast<float>(static_cast<double>(leading) + height + lineHeight);
            wrapNow = false;
            lineWidth = 0.0f;
            counted = false;
        }
        previous = ch;
    }
    if (counted)
        height = height + lineHeight;
    return height;
}

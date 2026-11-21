/**
 * bWare string helpers (original: Speed/Indep/bWare/Src/Strings.cpp), 0x460bc0-0x4613e0.
 * Quirks of the originals are kept on purpose: signed-char hashing in bStringHashUpper, bStrNCmp reading the byte before
 * the strings on an early terminator, bToUpper returning the terminator, sign-extension in the char -> wide copies.
 */
#include "game/gen/api.h"

static inline char bUpper(char c)
{
    return (c > '`' && c < '{') ? static_cast<char>(c & 0x5f) : c;
}

unsigned int bStringHashUpper(const char *text)
{
    unsigned int hash = 0xFFFFFFFF;
    for (; *text; ++text)
        hash = hash * 33 + static_cast<int>(bUpper(*text));
    return hash;
}

unsigned int bStringHash(const char *text)
{
    unsigned int hash = 0xFFFFFFFF;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        hash = hash * 33 + *p;
    return hash;
}

unsigned int bStringHash(const char *text, int prefix_hash)
{
    unsigned int hash = static_cast<unsigned int>(prefix_hash);
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        hash = hash * 33 + *p;
    return hash;
}

int bStrLen(const char *text)
{
    int len = 0;
    if (text)
        while (text[len])
            ++len;
    return len;
}

char *bStrCpy(char *dst, const char *src)
{
    char *out = dst;
    while ((*out++ = *src++) != 0)
    {
    }
    return dst;
}

char *bStrNCpy(char *dst, const char *src, int count)
{
    char *out = dst;
    while (count != 0)
    {
        --count;
        if ((*out = *src++) == 0)
            break;
        ++out;
    }
    return dst;
}

char *bSafeStrCpy(char *dst, const char *src, int size)
{
    int i = 0;
    if (src && size - 1 >= 1)
        while (src[i] && i < size - 1)
        {
            dst[i] = src[i];
            ++i;
        }
    dst[i] = 0;
    return dst;
}

int bStrCmp(const char *a, const char *b)
{
    if (!a)
        return b ? -1 : 0;
    if (!b)
        return 1;
    char ca, cb;
    do
    {
        ca = *a++;
        cb = *b++;
    } while (ca && cb && ca == cb);
    return ca - cb;
}

int bStrNCmp(const char *a, const char *b, int count)
{
    if (!a)
        return b ? -1 : 0;
    if (!b)
        return 1;
    int left;
    char ca, cb;
    for (;;)
    {
        left = count - 1;
        if (count == 0 || (ca = *a) == 0 || (cb = *b) == 0)
            break;
        ++a;
        ++b;
        count = left;
        if (ca != cb)
            break;
    }
    if (left < 0)
        return 0;
    if (*a != 0)
        return 1;
    if (*b != 0)
        return -1;
    return a[-1] - b[-1];
}

int bStrICmp(const char *a, const char *b)
{
    if (!a)
        return b ? -1 : 0;
    if (!b)
        return 1;
    char ca, cb;
    do
    {
        ca = bUpper(*a++);
        cb = bUpper(*b++);
    } while (ca && cb && ca == cb);
    return ca - cb;
}

int bStrNICmp(const char *a, const char *b, int count)
{
    if (!a)
        return b ? -1 : 0;
    if (!b)
        return 1;
    int left;
    char ca, cb;
    for (;;)
    {
        left = count - 1;
        if (count == 0 || (ca = *a) == 0 || (cb = *b) == 0)
            break;
        ca = bUpper(ca);
        cb = bUpper(cb);
        ++b;
        ++a;
        count = left;
        if (ca != cb)
            break;
    }
    if (left < 0)
        return 0;
    if (*a != 0)
        return 1;
    if (*b != 0)
        return -1;
    return bUpper(a[-1]) - bUpper(b[-1]);
}

char *bStrCat(char *dst, const char *first, const char *second)
{
    int n = 0;
    for (; first[n]; ++n)
        dst[n] = first[n];
    for (; *second; ++second)
        dst[n++] = *second;
    dst[n] = 0;
    return dst;
}

char *bStrChr(const char *text, int ch)
{
    if (text)
    {
        char c;
        do
        {
            c = *text;
            if (c == ch)
                return const_cast<char *>(text);
            ++text;
        } while (c != 0);
    }
    return nullptr;
}

char *bToUpper(char *text)
{
    while (*text)
    {
        *text = bUpper(*text);
        ++text;
    }
    return text;
}

int bStrToLong(const char *text)
{
    if (text[0] == '0' && text[1] == 'x')
    {
        int value = 0;
        for (text += 2; *text; ++text)
        {
            char c = *text;
            int digit;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else
            {
                c = bUpper(c);
                if (c < 'A' || c > 'F')
                    return value;
                digit = c - 'A' + 10;
            }
            value = value * 16 + digit;
        }
        return value;
    }
    bool negative = false;
    if (*text == '-' || *text == '+')
        negative = *text++ == '-';
    int value = 0;
    for (char c = *text; c && c >= '0' && c <= '9'; c = *++text)
        value = c - '0' + value * 10;
    return negative ? -value : value;
}

int bStrLen(const unsigned short *text)
{
    int len = 0;
    if (text)
        while (text[len])
            ++len;
    return len;
}

unsigned short *bStrCpy(unsigned short *dst, const unsigned short *src)
{
    unsigned short *out = dst;
    while ((*out++ = *src++) != 0)
    {
    }
    return dst;
}

unsigned short *bStrCpy(unsigned short *dst, const char *src)
{
    unsigned short *out = dst;
    while ((*out++ = static_cast<unsigned short>(static_cast<short>(*src++))) != 0)
    {
    }
    return dst;
}

unsigned short *bStrNCpy(unsigned short *dst, const unsigned short *src, int count)
{
    unsigned short *out = dst;
    while (count != 0)
    {
        --count;
        if ((*out = *src++) == 0)
            break;
        ++out;
    }
    return dst;
}

unsigned short *bStrNCpy(unsigned short *dst, const char *src, int count)
{
    unsigned short *out = dst;
    while (count != 0)
    {
        --count;
        char c = *src++;
        *out = static_cast<unsigned short>(static_cast<short>(c));
        if (c == 0)
            break;
        ++out;
    }
    return dst;
}

int bStrCmp(const unsigned short *a, const unsigned short *b)
{
    unsigned short ca, cb;
    do
    {
        ca = *a++;
        cb = *b++;
    } while (ca && cb && ca == cb);
    return static_cast<int>(ca) - static_cast<int>(cb);
}

int bStrNCmp(const unsigned short *a, const unsigned short *b, int count)
{
    int left;
    unsigned short ca, cb;
    for (;;)
    {
        left = count - 1;
        if (count == 0 || (ca = *a) == 0 || (cb = *b) == 0)
            break;
        ++b;
        ++a;
        count = left;
        if (ca != cb)
            break;
    }
    if (left < 0)
        return 0;
    if (*a != 0)
        return 1;
    if (*b != 0)
        return -1;
    return static_cast<int>(a[-1]) - static_cast<int>(b[-1]);
}

char *bStrStr(const char *haystack, const char *needle)
{
    int len = 0;
    if (needle)
        while (needle[len])
            ++len;
    for (; *haystack; ++haystack)
        if (*haystack == *needle && bStrNCmp(haystack, needle, len) == 0)
            return const_cast<char *>(haystack);
    return nullptr;
}

char *bStrIStr(const char *haystack, const char *needle)
{
    int len = 0;
    if (needle)
        while (needle[len])
            ++len;
    for (; *haystack; ++haystack)
        if (bStrNICmp(haystack, needle, len) == 0)
            return const_cast<char *>(haystack);
    return nullptr;
}

unsigned short *bStrCat(unsigned short *dst, const unsigned short *first, const unsigned short *second)
{
    int n = 0;
    for (; first[n]; ++n)
        dst[n] = first[n];
    for (; *second; ++second)
        dst[n++] = *second;
    dst[n] = 0;
    return dst;
}

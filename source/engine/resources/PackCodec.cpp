/**
 * The packs' block codec (include/engine/resources/packcodec.h, docs/PACKS.md).
 *
 * A block is a run of sequences: a token byte (literal count in its high 4 bits, match length less 4 in its low 4; 15
 * in either goes on in bytes added to it, each 255 meaning another follows), the literals, then, unless the block
 * ends there, the match's distance back as a little-endian base-128 number (1 to 3 bytes) and the match. Matches
 * may overlap their own output (runs).
 */
#include "engine/resources/packcodec.h"

#include <emmintrin.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const int kMinMatch = 4;
/** Copies run in whole 16-byte steps past their end while that much room is left before the block's end. */
const size_t kWild = 16;
/** For a match nearer than 8 bytes: the first multiple of its distance of at least 8 (the output repeats with it). */
const uint8_t kPeriodStep[8] = {0, 8, 8, 9, 8, 10, 12, 14};

inline void Copy16(uint8_t *to, const uint8_t *from)
{
    _mm_storeu_si128(reinterpret_cast<__m128i *>(to), _mm_loadu_si128(reinterpret_cast<const __m128i *>(from)));
}

inline void Copy8(uint8_t *to, const uint8_t *from)
{
    _mm_storel_epi64(reinterpret_cast<__m128i *>(to), _mm_loadl_epi64(reinterpret_cast<const __m128i *>(from)));
}

bool ReadLength(const uint8_t *&ip, const uint8_t *end, size_t &value)
{
    uint8_t b;
    do
    {
        if (ip >= end)
            return false;
        b = *ip++;
        value += b;
    } while (b == 255);
    return true;
}
} // namespace

extern "C" int pack_decode(const uint8_t *in, size_t packed, uint8_t *out, size_t unpacked)
{
    const uint8_t *ip = in, *const iend = in + packed;
    uint8_t *op = out, *const oend = out + unpacked;
    while (ip < iend)
    {
        const unsigned int token = *ip++;
        size_t literals = token >> 4;
        if (literals == 15 && !ReadLength(ip, iend, literals))
            return 0;
        if (literals > static_cast<size_t>(iend - ip) || literals > static_cast<size_t>(oend - op))
            return 0;
        if (static_cast<size_t>(iend - ip) >= literals + kWild && static_cast<size_t>(oend - op) >= literals + kWild)
        {
            const uint8_t *from = ip;
            uint8_t *to = op;
            uint8_t *const stop = op + literals;
            do
            {
                Copy16(to, from);
                to += 16;
                from += 16;
            } while (to < stop);
        }
        else
            memcpy(op, ip, literals);
        op += literals;
        ip += literals;
        if (op == oend)
            break;
        size_t distance = 0;
        for (int shift = 0;; shift += 7)
        {
            if (ip >= iend || shift > 21)
                return 0;
            const uint8_t b = *ip++;
            distance |= static_cast<size_t>(b & 127) << shift;
            if (!(b & 128))
                break;
        }
        size_t length = (token & 15);
        if (length == 15 && !ReadLength(ip, iend, length))
            return 0;
        length += kMinMatch;
        if (!distance || distance > static_cast<size_t>(op - out) || length > static_cast<size_t>(oend - op))
            return 0;
        const uint8_t *from = op - distance;
        uint8_t *const stop = op + length;
        if (static_cast<size_t>(oend - stop) >= kWild)
        {
            if (distance >= 16)
                do
                {
                    Copy16(op, from);
                    op += 16;
                    from += 16;
                } while (op < stop);
            else if (distance >= 8)
                do
                {
                    Copy8(op, from);
                    op += 8;
                    from += 8;
                } while (op < stop);
            else if (distance == 1)
            {
                const __m128i run = _mm_set1_epi8(static_cast<char>(*from));
                do
                {
                    _mm_storeu_si128(reinterpret_cast<__m128i *>(op), run);
                    op += 16;
                } while (op < stop);
            }
            else
            {
                for (int i = 0; i < 8; ++i)
                    op[i] = from[i];
                from = op + 8 - kPeriodStep[distance];
                op += 8;
                while (op < stop)
                {
                    Copy8(op, from);
                    op += 8;
                    from += 8;
                }
            }
            op = stop;
        }
        else if (distance >= 8)
        {
            while (op + 8 <= stop)
            {
                memcpy(op, from, 8);
                op += 8;
                from += 8;
            }
            while (op < stop)
                *op++ = *from++;
        }
        else
        {
            for (size_t i = 0; i < length; ++i)
                op[i] = from[i];
            op = stop;
        }
    }
    return op == oend && ip == iend;
}

#ifdef PACK_CODEC_ENCODER

namespace
{
const int kHashBits = 17;
const size_t kMaxBlock = 16u << 20;

uint32_t Read32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

uint32_t Hash(const uint8_t *p)
{
    return (Read32(p) * 2654435761u) >> (32 - kHashBits);
}

struct Writer
{
    uint8_t *op, *end;
    bool full;

    void Byte(uint8_t b)
    {
        if (op >= end)
        {
            full = true;
            return;
        }
        *op++ = b;
    }

    void Length(size_t rest)
    {
        while (rest >= 255)
        {
            Byte(255);
            rest -= 255;
        }
        Byte(static_cast<uint8_t>(rest));
    }

    void Sequence(const uint8_t *literals, size_t count, size_t distance, size_t length)
    {
        const size_t l = count < 15 ? count : 15;
        const size_t m = length ? (length - kMinMatch < 15 ? length - kMinMatch : 15) : 0;
        Byte(static_cast<uint8_t>(l << 4 | m));
        if (l == 15)
            Length(count - 15);
        if (count > static_cast<size_t>(end - op))
        {
            full = true;
            return;
        }
        memcpy(op, literals, count);
        op += count;
        if (!length)
            return;
        do
        {
            Byte(static_cast<uint8_t>((distance & 127) | (distance > 127 ? 128 : 0)));
            distance >>= 7;
        } while (distance);
        if (m == 15)
            Length(length - kMinMatch - 15);
    }
};

struct Finder
{
    const uint8_t *in;
    size_t size;
    int32_t *head;
    int32_t *prev;
    int depth;

    void Insert(size_t at)
    {
        if (at + kMinMatch > size)
            return;
        const uint32_t h = Hash(in + at);
        prev[at] = head[h];
        head[h] = static_cast<int32_t>(at);
    }

    size_t Longest(size_t at, size_t &distance) const
    {
        if (at + kMinMatch > size)
            return 0;
        size_t best = 0;
        const size_t limit = size - at;
        int32_t candidate = head[Hash(in + at)];
        for (int steps = depth; candidate >= 0 && steps; --steps, candidate = prev[candidate])
        {
            const uint8_t *a = in + at, *b = in + candidate;
            if (best && (best >= limit || a[best] != b[best]))
                continue;
            size_t n = 0;
            while (n + 8 <= limit)
            {
                uint64_t x, y;
                memcpy(&x, a + n, 8);
                memcpy(&y, b + n, 8);
                if (x != y)
                    break;
                n += 8;
            }
            while (n < limit && a[n] == b[n])
                ++n;
            if (n > best)
            {
                best = n;
                distance = at - static_cast<size_t>(candidate);
                if (n == limit)
                    break;
            }
        }
        return best >= kMinMatch ? best : 0;
    }
};
} // namespace

extern "C" size_t pack_bound(size_t size)
{
    return size + size / 255 + 16;
}

extern "C" size_t pack_encode(const uint8_t *in, size_t size, uint8_t *out, size_t capacity, int level)
{
    if (!size || size > kMaxBlock)
        return 0;
    Finder f = {in, size, static_cast<int32_t *>(malloc(sizeof(int32_t) << kHashBits)),
                static_cast<int32_t *>(malloc(sizeof(int32_t) * size)), level < 1 ? 4 : level >= 9 ? 512 : level * 16};
    if (!f.head || !f.prev)
    {
        free(f.head);
        free(f.prev);
        return 0;
    }
    memset(f.head, 0xff, sizeof(int32_t) << kHashBits);
    Writer w = {out, out + (capacity < size ? capacity : size), false};
    size_t at = 0, anchor = 0;
    while (at < size && !w.full)
    {
        size_t distance = 0;
        size_t length = f.Longest(at, distance);
        if (length && level > 1 && at + 1 < size)
        {
            f.Insert(at);
            size_t next_distance = 0;
            const size_t next = f.Longest(at + 1, next_distance);
            if (next > length + 1)
            {
                ++at;
                length = next;
                distance = next_distance;
            }
            else
            {
                w.Sequence(in + anchor, at - anchor, distance, length);
                for (size_t i = at + 1; i < at + length; ++i)
                    f.Insert(i);
                at += length;
                anchor = at;
                continue;
            }
        }
        if (!length)
        {
            f.Insert(at++);
            continue;
        }
        w.Sequence(in + anchor, at - anchor, distance, length);
        for (size_t i = at; i < at + length; ++i)
            f.Insert(i);
        at += length;
        anchor = at;
    }
    if (anchor < size && !w.full)
        w.Sequence(in + anchor, size - anchor, 0, 0);
    free(f.head);
    free(f.prev);
    if (w.full)
        return 0;
    const size_t written = static_cast<size_t>(w.op - out);
    return written < size ? written : 0;
}

#endif

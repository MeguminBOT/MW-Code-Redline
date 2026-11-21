/**
 * WebM reading (webm.h): the EBML elements of a Matroska file, the subset a WebM movie has.
 */
#include "webm.h"

namespace
{
const unsigned int kEbml = 0x1A45DFA3;
const unsigned int kSegment = 0x18538067;
const unsigned int kInfo = 0x1549A966;
const unsigned int kTimecodeScale = 0x2AD7B1;
const unsigned int kDuration = 0x4489;
const unsigned int kTracks = 0x1654AE6B;
const unsigned int kTrackEntry = 0xAE;
const unsigned int kTrackNumber = 0xD7;
const unsigned int kTrackType = 0x83;
const unsigned int kCodecId = 0x86;
const unsigned int kCodecPrivate = 0x63A2;
const unsigned int kCodecDelay = 0x56AA;
const unsigned int kVideoElement = 0xE0;
const unsigned int kPixelWidth = 0xB0;
const unsigned int kPixelHeight = 0xBA;
const unsigned int kColour = 0x55B0;
const unsigned int kMatrix = 0x55B1;
const unsigned int kRange = 0x55B9;
const unsigned int kAudioElement = 0xE1;
const unsigned int kSampling = 0xB5;
const unsigned int kChannels = 0x9F;
const unsigned int kCluster = 0x1F43B675;
const unsigned int kTimecode = 0xE7;
const unsigned int kSimpleBlock = 0xA3;
const unsigned int kBlockGroup = 0xA0;
const unsigned int kBlock = 0xA1;
const unsigned int kTopLevel[] = {kCluster, 0x1C53BB6B, 0x1254C367, 0x1043A770, 0x1941A469, 0x114D9B74, kInfo, kTracks};
const int kReadChunk = 1 << 20;

void *Allocate(int bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(bytes));
}

void Free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

bool TopLevel(unsigned int id)
{
    for (unsigned int t : kTopLevel)
        if (t == id)
            return true;
    return false;
}

/** A vint of the block header: its length and value without the marker bit, 0 length when damaged. */
int BlockVint(const unsigned char *p, int available, unsigned long long &value)
{
    if (available < 1 || !p[0])
        return 0;
    int length = 1;
    while (!(p[0] & (0x80 >> (length - 1))))
        ++length;
    if (length > available)
        return 0;
    value = p[0] & (0xFF >> length);
    for (int i = 1; i < length; ++i)
        value = (value << 8) | p[i];
    return length;
}
} // namespace

namespace nfs::movies
{
WebmReader::~WebmReader()
{
    Close();
}

void WebmReader::Close()
{
    if (file_ != INVALID_HANDLE_VALUE)
        CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
    Free(buffer_);
    Free(block_);
    buffer_ = block_ = nullptr;
    capacity_ = blockCapacity_ = bufferLength_ = cursor_ = 0;
    trackCount_ = 0;
}

bool WebmReader::Open(const char *path)
{
    Close();
    file_ = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file_ == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file_, &size))
        return false;
    fileSize_ = size.QuadPart;
    bufferStart_ = 0;
    if (!ParseHead())
    {
        Close();
        return false;
    }
    return Rewind();
}

const WebmReader::Track *WebmReader::Video() const
{
    for (int i = 0; i < trackCount_; ++i)
        if (tracks_[i].type == kVideo && !lstrcmpA(tracks_[i].codec, "V_VP9"))
            return &tracks_[i];
    return nullptr;
}

const WebmReader::Track *WebmReader::Audio() const
{
    for (int i = 0; i < trackCount_; ++i)
        if (tracks_[i].type == kAudio && !lstrcmpA(tracks_[i].codec, "A_OPUS"))
            return &tracks_[i];
    return nullptr;
}

long long WebmReader::Position() const
{
    return bufferStart_ + cursor_;
}

bool WebmReader::Seek(long long position)
{
    if (position >= bufferStart_ && position <= bufferStart_ + bufferLength_)
    {
        cursor_ = static_cast<int>(position - bufferStart_);
        return true;
    }
    bufferStart_ = position;
    bufferLength_ = cursor_ = 0;
    return position <= fileSize_;
}

bool WebmReader::Fill(int bytes)
{
    if (cursor_ + bytes <= bufferLength_)
        return true;
    if (Position() + bytes > fileSize_)
        return false;
    const int keep = bufferLength_ - cursor_;
    const int wanted = bytes > kReadChunk ? bytes : kReadChunk;
    if (wanted > capacity_)
    {
        unsigned char *grown = static_cast<unsigned char *>(Allocate(wanted));
        if (!grown)
            return false;
        if (keep)
            CopyMemory(grown, buffer_ + cursor_, static_cast<SIZE_T>(keep));
        Free(buffer_);
        buffer_ = grown;
        capacity_ = wanted;
    }
    else if (keep)
        MoveMemory(buffer_, buffer_ + cursor_, static_cast<SIZE_T>(keep));
    bufferStart_ += cursor_;
    cursor_ = 0;
    bufferLength_ = keep;
    LARGE_INTEGER at;
    at.QuadPart = bufferStart_ + bufferLength_;
    if (!SetFilePointerEx(file_, at, nullptr, FILE_BEGIN))
        return false;
    while (bufferLength_ < bytes)
    {
        DWORD read = 0;
        const DWORD ask = static_cast<DWORD>(capacity_ - bufferLength_);
        if (!ReadFile(file_, buffer_ + bufferLength_, ask, &read, nullptr) || !read)
            return false;
        bufferLength_ += static_cast<int>(read);
    }
    return true;
}

bool WebmReader::ReadId(unsigned int &id)
{
    if (!Fill(1))
        return false;
    const unsigned char first = buffer_[cursor_];
    int length = 1;
    while (length <= 4 && !(first & (0x80 >> (length - 1))))
        ++length;
    if (length > 4 || !Fill(length))
        return false;
    id = 0;
    for (int i = 0; i < length; ++i)
        id = (id << 8) | buffer_[cursor_ + i];
    cursor_ += length;
    return true;
}

bool WebmReader::ReadSize(long long &size, bool &unknown)
{
    if (!Fill(1))
        return false;
    const unsigned char first = buffer_[cursor_];
    int length = 1;
    while (length <= 8 && !(first & (0x80 >> (length - 1))))
        ++length;
    if (length > 8 || !Fill(length))
        return false;
    unsigned long long value = first & (0xFF >> length);
    bool ones = value == (0xFFull >> length);
    for (int i = 1; i < length; ++i)
    {
        value = (value << 8) | buffer_[cursor_ + i];
        ones = ones && buffer_[cursor_ + i] == 0xFF;
    }
    cursor_ += length;
    unknown = ones;
    size = static_cast<long long>(value);
    return true;
}

bool WebmReader::ReadUint(long long size, unsigned long long &value)
{
    if (size < 0 || size > 8 || !Fill(static_cast<int>(size)))
        return false;
    value = 0;
    for (int i = 0; i < size; ++i)
        value = (value << 8) | buffer_[cursor_ + i];
    cursor_ += static_cast<int>(size);
    return true;
}

bool WebmReader::ReadFloat(long long size, double &value)
{
    unsigned long long bits;
    if ((size != 4 && size != 8) || !ReadUint(size, bits))
        return false;
    if (size == 4)
    {
        const unsigned int b = static_cast<unsigned int>(bits);
        float f;
        CopyMemory(&f, &b, 4);
        value = f;
    }
    else
        CopyMemory(&value, &bits, 8);
    return true;
}

bool WebmReader::Skip(long long size)
{
    if (size < 0 || Position() + size > fileSize_)
        return false;
    return Seek(Position() + size);
}

bool WebmReader::ParseHead()
{
    unsigned int id;
    long long size;
    bool unknown;
    if (!ReadId(id) || id != kEbml || !ReadSize(size, unknown) || unknown || !Skip(size))
        return false;
    if (!ReadId(id) || id != kSegment || !ReadSize(size, unknown))
        return false;
    segmentEnd_ = unknown ? fileSize_ : Position() + size;
    if (segmentEnd_ > fileSize_)
        segmentEnd_ = fileSize_;
    double duration = 0.0;
    while (Position() < segmentEnd_)
    {
        const long long start = Position();
        if (!ReadId(id) || !ReadSize(size, unknown))
            return false;
        const long long end = Position() + size;
        if (id == kCluster)
        {
            firstCluster_ = start;
            duration_ = static_cast<long long>(duration * static_cast<double>(scale_));
            return trackCount_ > 0;
        }
        if (unknown)
            return false;
        if (id == kInfo)
        {
            while (Position() < end)
            {
                unsigned int child;
                long long childSize;
                bool childUnknown;
                if (!ReadId(child) || !ReadSize(childSize, childUnknown) || childUnknown)
                    return false;
                unsigned long long value;
                if (child == kTimecodeScale)
                {
                    if (!ReadUint(childSize, value))
                        return false;
                    scale_ = value ? static_cast<long long>(value) : 1000000;
                }
                else if (child == kDuration)
                {
                    if (!ReadFloat(childSize, duration))
                        return false;
                }
                else if (!Skip(childSize))
                    return false;
            }
        }
        else if (id == kTracks)
        {
            if (!ParseTracks(end))
                return false;
        }
        else if (!Skip(size))
            return false;
    }
    return false;
}

bool WebmReader::ParseTracks(long long end)
{
    while (Position() < end)
    {
        unsigned int id;
        long long size;
        bool unknown;
        if (!ReadId(id) || !ReadSize(size, unknown) || unknown)
            return false;
        if (id == kTrackEntry && trackCount_ < static_cast<int>(sizeof(tracks_) / sizeof(tracks_[0])))
        {
            Track &t = tracks_[trackCount_];
            t = Track();
            if (!ParseTrack(Position() + size, t))
                return false;
            ++trackCount_;
        }
        else if (!Skip(size))
            return false;
    }
    return true;
}

bool WebmReader::ParseTrack(long long end, Track &t)
{
    while (Position() < end)
    {
        unsigned int id;
        long long size;
        bool unknown;
        if (!ReadId(id) || !ReadSize(size, unknown) || unknown)
            return false;
        unsigned long long value;
        const long long childEnd = Position() + size;
        switch (id)
        {
        case kTrackNumber:
            if (!ReadUint(size, value))
                return false;
            t.number = static_cast<int>(value);
            break;
        case kTrackType:
            if (!ReadUint(size, value))
                return false;
            t.type = static_cast<int>(value);
            break;
        case kCodecDelay:
            if (!ReadUint(size, value))
                return false;
            t.codecDelay = static_cast<long long>(value);
            break;
        case kCodecId:
        case kCodecPrivate:
        {
            if (!Fill(static_cast<int>(size)))
                return false;
            if (id == kCodecId)
            {
                const int n = size < static_cast<long long>(sizeof(t.codec)) - 1 ? static_cast<int>(size) : sizeof(t.codec) - 1;
                CopyMemory(t.codec, buffer_ + cursor_, static_cast<SIZE_T>(n));
                t.codec[n] = 0;
            }
            else
            {
                t.privSize = size < static_cast<long long>(sizeof(t.priv)) ? static_cast<int>(size) : sizeof(t.priv);
                CopyMemory(t.priv, buffer_ + cursor_, static_cast<SIZE_T>(t.privSize));
            }
            cursor_ += static_cast<int>(size);
            break;
        }
        case kVideoElement:
        case kAudioElement:
        case kColour:
            // Their children are read in place: the loop goes on inside them.
            continue;
        case kPixelWidth:
        case kPixelHeight:
        case kMatrix:
        case kRange:
        case kChannels:
            if (!ReadUint(size, value))
                return false;
            if (id == kPixelWidth)
                t.width = static_cast<int>(value);
            else if (id == kPixelHeight)
                t.height = static_cast<int>(value);
            else if (id == kMatrix)
                t.matrix = static_cast<int>(value);
            else if (id == kRange)
                t.fullRange = value == 2;
            else
                t.channels = static_cast<int>(value);
            break;
        case kSampling:
            if (!ReadFloat(size, t.rate))
                return false;
            break;
        default:
            if (!Skip(size))
                return false;
        }
        if (Position() != childEnd && !Seek(childEnd))
            return false;
    }
    return true;
}

bool WebmReader::Rewind()
{
    inCluster_ = false;
    lacedCount_ = lacedNext_ = 0;
    return Seek(firstCluster_);
}

bool WebmReader::ParseBlock(long long size, bool simple)
{
    if (size < 4 || size > (64 << 20) || !Fill(static_cast<int>(size)))
        return false;
    if (size > blockCapacity_)
    {
        Free(block_);
        blockCapacity_ = static_cast<int>(size) + 4096;
        block_ = static_cast<unsigned char *>(Allocate(blockCapacity_));
        if (!block_)
        {
            blockCapacity_ = 0;
            return false;
        }
    }
    CopyMemory(block_, buffer_ + cursor_, static_cast<SIZE_T>(size));
    cursor_ += static_cast<int>(size);
    const int total = static_cast<int>(size);
    unsigned long long track;
    int at = BlockVint(block_, total, track);
    if (!at || at + 3 > total)
        return false;
    const short relative = static_cast<short>((block_[at] << 8) | block_[at + 1]);
    const unsigned char flags = block_[at + 2];
    at += 3;
    bool known = false;
    for (int i = 0; i < trackCount_; ++i)
        known = known || (tracks_[i].number == static_cast<int>(track) &&
                          ((tracks_[i].type == kVideo && Video() == &tracks_[i]) ||
                           (tracks_[i].type == kAudio && Audio() == &tracks_[i])));
    if (!known)
        return false;
    Packet p;
    p.track = static_cast<int>(track);
    p.time = (clusterTime_ + relative) * scale_;
    p.key = simple ? (flags & 0x80) != 0 : true;
    const int lacing = (flags >> 1) & 3;
    if (!lacing)
    {
        p.data = block_ + at;
        p.size = total - at;
        lacedPacket_ = p;
        lacedCount_ = 1;
        lacedNext_ = 0;
        return true;
    }
    if (at >= total)
        return false;
    const int count = block_[at++] + 1;
    if (count > static_cast<int>(sizeof(laced_) / sizeof(laced_[0])))
        return false;
    int used = 0;
    if (lacing == 1)
    {
        for (int i = 0; i < count - 1; ++i)
        {
            int n = 0;
            while (at < total && block_[at] == 0xFF)
                n += block_[at++];
            if (at >= total)
                return false;
            n += block_[at++];
            laced_[i] = n;
            used += n;
        }
    }
    else if (lacing == 3)
    {
        unsigned long long first;
        int n = BlockVint(block_ + at, total - at, first);
        if (!n)
            return false;
        at += n;
        long long last = static_cast<long long>(first);
        laced_[0] = static_cast<int>(first);
        used = laced_[0];
        for (int i = 1; i < count - 1; ++i)
        {
            unsigned long long raw;
            n = BlockVint(block_ + at, total - at, raw);
            if (!n)
                return false;
            at += n;
            last += static_cast<long long>(raw) - ((1ll << (7 * n - 1)) - 1);
            if (last < 0)
                return false;
            laced_[i] = static_cast<int>(last);
            used += laced_[i];
        }
    }
    else
    {
        const int each = (total - at) / count;
        for (int i = 0; i < count - 1; ++i)
            laced_[i] = each;
        used = each * (count - 1);
    }
    laced_[count - 1] = total - at - used;
    if (laced_[count - 1] < 0)
        return false;
    p.data = block_ + at;
    p.size = laced_[0];
    lacedPacket_ = p;
    lacedCount_ = count;
    lacedNext_ = 0;
    lacedOffset_ = at;
    return true;
}

bool WebmReader::Next(Packet &packet)
{
    if (lacedNext_ < lacedCount_)
    {
        if (lacedNext_ == 0 || lacedCount_ == 1)
            packet = lacedPacket_;
        else
        {
            int offset = lacedOffset_;
            for (int i = 0; i < lacedNext_; ++i)
                offset += laced_[i];
            packet = lacedPacket_;
            packet.data = block_ + offset;
            packet.size = laced_[lacedNext_];
        }
        ++lacedNext_;
        return true;
    }
    lacedCount_ = lacedNext_ = 0;
    for (;;)
    {
        if (Position() >= segmentEnd_)
            return false;
        if (inCluster_ && !clusterUnknown_ && Position() >= clusterEnd_)
            inCluster_ = false;
        const long long start = Position();
        unsigned int id;
        long long size;
        bool unknown;
        if (!ReadId(id) || !ReadSize(size, unknown))
            return false;
        if (!inCluster_)
        {
            if (id == kCluster)
            {
                inCluster_ = true;
                clusterUnknown_ = unknown;
                clusterEnd_ = unknown ? segmentEnd_ : Position() + size;
                clusterTime_ = 0;
                continue;
            }
            if (unknown || !Skip(size))
                return false;
            continue;
        }
        if (clusterUnknown_ && TopLevel(id))
        {
            inCluster_ = false;
            Seek(start);
            continue;
        }
        if (unknown)
            return false;
        if (id == kTimecode)
        {
            unsigned long long value;
            if (!ReadUint(size, value))
                return false;
            clusterTime_ = static_cast<long long>(value);
            continue;
        }
        if (id == kSimpleBlock)
        {
            if (ParseBlock(size, true))
                return Next(packet);
            continue;
        }
        if (id == kBlockGroup)
        {
            const long long end = Position() + size;
            bool found = false;
            while (Position() < end)
            {
                unsigned int child;
                long long childSize;
                bool childUnknown;
                if (!ReadId(child) || !ReadSize(childSize, childUnknown) || childUnknown)
                    return false;
                if (child == kBlock && !found)
                    found = ParseBlock(childSize, false);
                else if (!Skip(childSize))
                    return false;
            }
            if (found)
                return Next(packet);
            continue;
        }
        if (!Skip(size))
            return false;
    }
}
} // namespace nfs::movies

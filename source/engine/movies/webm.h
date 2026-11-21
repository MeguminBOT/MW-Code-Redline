#pragma once

#include <windows.h>

namespace nfs::movies
{
/**
 * A WebM file read packet by packet: Matroska's EBML elements, the segment's info and tracks, then its clusters' blocks
 * (SimpleBlock and BlockGroup, laced or not) in file order. Clusters of unknown size end at the next top-level element.
 */
class WebmReader
{
  public:
    struct Track
    {
        int number = 0;
        int type = 0;
        char codec[24] = {};
        int width = 0;
        int height = 0;
        int matrix = 2;
        int fullRange = 0;
        double rate = 0.0;
        int channels = 0;
        unsigned char priv[256] = {};
        int privSize = 0;
        long long codecDelay = 0;
    };

    struct Packet
    {
        int track;
        long long time;
        const unsigned char *data;
        int size;
        bool key;
    };

    static const int kVideo = 1;
    static const int kAudio = 2;

    ~WebmReader();
    bool Open(const char *path);
    void Close();
    /** Back to the first cluster. */
    bool Rewind();
    /** The next packet; its data stays valid until the next call. False at the end or on a damaged file. */
    bool Next(Packet &packet);
    const Track *Video() const;
    const Track *Audio() const;
    /** The segment's duration (ns), 0 when the file does not say. */
    long long Duration() const
    {
        return duration_;
    }

  private:
    bool Fill(int bytes);
    bool ReadId(unsigned int &id);
    bool ReadSize(long long &size, bool &unknown);
    bool ReadUint(long long size, unsigned long long &value);
    bool ReadFloat(long long size, double &value);
    bool Skip(long long size);
    long long Position() const;
    bool Seek(long long position);
    bool ParseHead();
    bool ParseTracks(long long end);
    bool ParseTrack(long long end, Track &t);
    bool ParseBlock(long long size, bool simple);

    HANDLE file_ = INVALID_HANDLE_VALUE;
    long long fileSize_ = 0;
    unsigned char *buffer_ = nullptr;
    int capacity_ = 0;
    long long bufferStart_ = 0;
    int bufferLength_ = 0;
    int cursor_ = 0;
    Track tracks_[4];
    int trackCount_ = 0;
    long long scale_ = 1000000;
    long long duration_ = 0;
    long long segmentEnd_ = 0;
    long long firstCluster_ = 0;
    long long clusterEnd_ = 0;
    bool inCluster_ = false;
    bool clusterUnknown_ = false;
    long long clusterTime_ = 0;
    unsigned char *block_ = nullptr;
    int blockCapacity_ = 0;
    int laced_[64] = {};
    int lacedCount_ = 0;
    int lacedNext_ = 0;
    int lacedOffset_ = 0;
    Packet lacedPacket_ = {};
};
} // namespace nfs::movies

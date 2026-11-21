/**
 * The WebM movie player (include/engine/movies/movies.h, docs/MOVIES.md): VP9 pictures through vpx.dll and Opus sound
 * through opus.dll, both loaded at the first movie (the game plays its VP6 movies as before without them). A thread
 * reads the file and decodes ahead: pictures into a few BGRA frames (the movie texture's A8R8G8B8 layout), the sound
 * into an SDL stream on the game's output device. The movie's clock is the sound the device has played, or the wall
 * clock for a movie without sound; the game's frame takes the newest picture due.
 *
 * The front end draws a movie in its 4:3 area, and the PC's movies carry their black bars (16:9 pictures in 1024 x 512
 * frames): a wider picture goes into a 4:3 frame between black bars here, so it keeps its shape.
 */
#include "engine/movies/movies.h"

#if __has_include("vpx/vpx_decoder.h") && __has_include("opus_multistream.h")
#define NFS_WEBM_MOVIES 1
#include <emmintrin.h>

#include "core/config.h"
#include "core/log.h"
#include "core/sdl.h"
#include "engine/audio/sound.h"
#include "webm.h"

#include "opus_multistream.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

namespace
{
const int kFrames = 4;
const int kMaxChannels = 8;
const int kOpusRate = 48000;
const int kMaxOpusFrames = 5760;
const long long kSecond = 1000000000ll;
/** Opus's sample in ns (the exe has no 64-bit division helper: sample counts become time as doubles). */
const double kSampleNs = 1e9 / 48000.0;
/** Sound queued ahead of the device (s) and the decode thread's nap when it is ahead. */
const double kAudioAhead = 0.75;
const DWORD kNap = 4;
/** Sound frames left in the stream that count as played out (10 ms): SDL keeps less than a device chunk. */
const int kTail = 480;
/** Pictures read ahead and not decoded yet: the file's sound can lie well behind its pictures, and the sound must not
 * wait for a free frame. Read on while fewer than kPendingWanted wait (or the sound runs low), up to kPending of them and
 * kPendingBytes. */
const int kPending = 512;
const int kPendingWanted = 8;
const int kPendingBytes = 64 << 20;

struct Vpx
{
    HMODULE dll = nullptr;
    decltype(&vpx_codec_vp9_dx) iface = nullptr;
    decltype(&vpx_codec_dec_init_ver) init = nullptr;
    decltype(&vpx_codec_decode) decode = nullptr;
    decltype(&vpx_codec_get_frame) frame = nullptr;
    decltype(&vpx_codec_destroy) destroy = nullptr;
    decltype(&vpx_codec_error) error = nullptr;
};

struct Opus
{
    HMODULE dll = nullptr;
    decltype(&opus_multistream_decoder_create) create = nullptr;
    decltype(&opus_multistream_decode_float) decode = nullptr;
    decltype(&opus_multistream_decoder_destroy) destroy = nullptr;
};

Vpx g_vpx;
Opus g_opus;
int g_loaded = -1;

template <typename T> bool Bind(HMODULE dll, const char *name, T &to)
{
    to = reinterpret_cast<T>(GetProcAddress(dll, name));
    return to != nullptr;
}

/** vpx.dll and opus.dll beside the exe, once. */
bool LoadDecoders()
{
    if (g_loaded >= 0)
        return g_loaded != 0;
    g_loaded = 0;
    g_vpx.dll = LoadLibraryA("vpx.dll");
    g_opus.dll = LoadLibraryA("opus.dll");
    if (!g_vpx.dll || !g_opus.dll)
    {
        nfs::Log("movies: %s missing beside the game: WebM movies are off", !g_vpx.dll ? "vpx.dll" : "opus.dll");
        return false;
    }
    const bool ok = Bind(g_vpx.dll, "vpx_codec_vp9_dx", g_vpx.iface) &&
                    Bind(g_vpx.dll, "vpx_codec_dec_init_ver", g_vpx.init) &&
                    Bind(g_vpx.dll, "vpx_codec_decode", g_vpx.decode) &&
                    Bind(g_vpx.dll, "vpx_codec_get_frame", g_vpx.frame) &&
                    Bind(g_vpx.dll, "vpx_codec_destroy", g_vpx.destroy) &&
                    Bind(g_vpx.dll, "vpx_codec_error", g_vpx.error) &&
                    Bind(g_opus.dll, "opus_multistream_decoder_create", g_opus.create) &&
                    Bind(g_opus.dll, "opus_multistream_decode_float", g_opus.decode) &&
                    Bind(g_opus.dll, "opus_multistream_decoder_destroy", g_opus.destroy);
    if (!ok)
        nfs::Log("movies: vpx.dll or opus.dll lacks a function the player calls: WebM movies are off");
    g_loaded = ok ? 1 : 0;
    return ok;
}

/** YUV to RGB in 13 fractional bits: the luma scale, V into red, U and V out of green, U into blue. */
struct Matrix
{
    short y, rv, gu, gv, bu;
    int offset;
};

Matrix MatrixOf(int matrix, bool full, int height)
{
    const bool bt709 = matrix == 1 || (matrix == 2 && height >= 720);
    if (full)
        return bt709 ? Matrix{8192, 12901, 1534, 3835, 15201, 0} : Matrix{8192, 11485, 2819, 5850, 14516, 0};
    return bt709 ? Matrix{9539, 14686, 1747, 4366, 17305, 16} : Matrix{9539, 13075, 3209, 6660, 16525, 16};
}

unsigned char Clamp(int v)
{
    return static_cast<unsigned char>(v < 0 ? 0 : v > 255 ? 255 : v);
}

/** One row of I420 to BGRA: eight pixels at a time with SSE2, the rest one by one. */
void ConvertRow(const unsigned char *yRow, const unsigned char *uRow, const unsigned char *vRow, unsigned char *out,
                int width, const Matrix &m)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i yOffset = _mm_set1_epi16(static_cast<short>(m.offset));
    const __m128i c128 = _mm_set1_epi16(128);
    const __m128i rCoef = _mm_set_epi16(m.rv, m.y, m.rv, m.y, m.rv, m.y, m.rv, m.y);
    const __m128i bCoef = _mm_set_epi16(m.bu, m.y, m.bu, m.y, m.bu, m.y, m.bu, m.y);
    const __m128i gCoef = _mm_set_epi16(static_cast<short>(-m.gu), m.y, static_cast<short>(-m.gu), m.y,
                                        static_cast<short>(-m.gu), m.y, static_cast<short>(-m.gu), m.y);
    const __m128i gvCoef = _mm_set_epi16(0, static_cast<short>(-m.gv), 0, static_cast<short>(-m.gv), 0,
                                         static_cast<short>(-m.gv), 0, static_cast<short>(-m.gv));
    const __m128i round = _mm_set1_epi32(1 << 12);
    const __m128i alpha = _mm_set1_epi8(static_cast<char>(0xFF));
    int x = 0;
    for (; x + 8 <= width; x += 8)
    {
        const __m128i y = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(yRow + x)), zero),
                                        yOffset);
        __m128i u = _mm_cvtsi32_si128(*reinterpret_cast<const int *>(uRow + x / 2));
        __m128i v = _mm_cvtsi32_si128(*reinterpret_cast<const int *>(vRow + x / 2));
        u = _mm_sub_epi16(_mm_unpacklo_epi8(u, zero), c128);
        v = _mm_sub_epi16(_mm_unpacklo_epi8(v, zero), c128);
        u = _mm_unpacklo_epi16(u, u);
        v = _mm_unpacklo_epi16(v, v);
        const __m128i yvLo = _mm_unpacklo_epi16(y, v), yvHi = _mm_unpackhi_epi16(y, v);
        const __m128i yuLo = _mm_unpacklo_epi16(y, u), yuHi = _mm_unpackhi_epi16(y, u);
        const __m128i v0Lo = _mm_unpacklo_epi16(v, zero), v0Hi = _mm_unpackhi_epi16(v, zero);
        const __m128i r = _mm_packs_epi32(_mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(yvLo, rCoef), round), 13),
                                          _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(yvHi, rCoef), round), 13));
        const __m128i b = _mm_packs_epi32(_mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(yuLo, bCoef), round), 13),
                                          _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(yuHi, bCoef), round), 13));
        const __m128i gLo = _mm_add_epi32(_mm_madd_epi16(yuLo, gCoef), _mm_madd_epi16(v0Lo, gvCoef));
        const __m128i gHi = _mm_add_epi32(_mm_madd_epi16(yuHi, gCoef), _mm_madd_epi16(v0Hi, gvCoef));
        const __m128i g = _mm_packs_epi32(_mm_srai_epi32(_mm_add_epi32(gLo, round), 13),
                                          _mm_srai_epi32(_mm_add_epi32(gHi, round), 13));
        const __m128i r8 = _mm_packus_epi16(r, r), g8 = _mm_packus_epi16(g, g), b8 = _mm_packus_epi16(b, b);
        const __m128i bg = _mm_unpacklo_epi8(b8, g8), ra = _mm_unpacklo_epi8(r8, alpha);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(out + x * 4), _mm_unpacklo_epi16(bg, ra));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(out + x * 4 + 16), _mm_unpackhi_epi16(bg, ra));
    }
    for (; x < width; ++x)
    {
        const int y = (yRow[x] - m.offset) * m.y, u = uRow[x / 2] - 128, v = vRow[x / 2] - 128;
        unsigned char *p = out + x * 4;
        p[0] = Clamp((y + u * m.bu + (1 << 12)) >> 13);
        p[1] = Clamp((y - u * m.gu - v * m.gv + (1 << 12)) >> 13);
        p[2] = Clamp((y + v * m.rv + (1 << 12)) >> 13);
        p[3] = 0xFF;
    }
}

struct Frame
{
    unsigned char *pixels = nullptr;
    long long time = 0;
    int state = 0;
};

enum
{
    kFree,
    kReady,
    kShown,
};

class Player
{
  public:
    bool Open(const char *path, bool loop, bool sound, float volume, bool bottomUp);
    void Close();
    const unsigned char *NextFrame(bool *ended);
    void Pause(bool paused);
    /** The frame (the movie texture's size) and the picture's height and first row in it. */
    int width = 0;
    int height = 0;
    int picture = 0;
    int top = 0;
    bool open = false;

  private:
    static DWORD WINAPI Run(void *self);
    void Decode();
    void DecodeVideo(int slot);
    bool ReadAhead();
    void DecodeAudio(const nfs::movies::WebmReader::Packet &p);
    bool OpenAudio(bool sound, float volume);
    long long Clock();
    int FreeFrame();

    nfs::movies::WebmReader reader_;
    vpx_codec_ctx_t vpx_ = {};
    bool vpxOpen_ = false;
    OpusMSDecoder *opus_ = nullptr;
    SDL_AudioStream *audio_ = nullptr;
    int videoTrack_ = 0;
    int audioTrack_ = 0;
    int channels_ = 0;
    int order_[kMaxChannels] = {};
    int preSkip_ = 0;
    int skip_ = 0;
    float *pcm_ = nullptr;
    float *mixed_ = nullptr;
    Matrix matrix_ = {};
    Frame frames_[kFrames];
    CRITICAL_SECTION lock_;
    bool lockMade_ = false;
    HANDLE thread_ = nullptr;
    HANDLE wake_ = nullptr;
    volatile LONG stop_ = 0;
    volatile LONG paused_ = 0;
    bool loop_ = false;
    bool bottomUp_ = false;
    long long loopOffset_ = 0;
    long long passEnd_ = 0;
    volatile LONG decodedAll_ = 0;
    long long pushedFrames_ = 0;
    long long audioStart_ = -1;
    LARGE_INTEGER started_ = {};
    long long pausedFor_ = 0;
    LARGE_INTEGER pausedAt_ = {};
    long long lastShown_ = -1;
    int shown_ = -1;
    bool flushed_ = false;
    long long lastClock_ = 0;
    long long starvedFrom_ = -1;
    LARGE_INTEGER starvedAt_ = {};
    struct Pending
    {
        unsigned char *data;
        int size;
        long long time;
    };
    Pending pending_[kPending] = {};
    int pendingHead_ = 0;
    int pendingCount_ = 0;
    int pendingBytes_ = 0;
    bool fileEnded_ = false;
};

Player g_player;

void *Allocate(SIZE_T bytes)
{
    return VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void Release(void *p)
{
    if (p)
        VirtualFree(p, 0, MEM_RELEASE);
}

long long Elapsed(const LARGE_INTEGER &from)
{
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    return static_cast<long long>(static_cast<double>(now.QuadPart - from.QuadPart) * 1e9 / static_cast<double>(rate.QuadPart));
}

bool Player::OpenAudio(bool sound, float volume)
{
    const nfs::movies::WebmReader::Track *a = reader_.Audio();
    if (!a || !sound || a->privSize < 19)
        return true;
    const unsigned char *h = a->priv;
    if (h[0] != 'O' || h[1] != 'p' || h[2] != 'u' || h[3] != 's')
        return true;
    const int channels = h[9];
    preSkip_ = h[10] | (h[11] << 8);
    const int family = h[18];
    int streams = 1, coupled = channels > 1 ? 1 : 0;
    unsigned char mapping[kMaxChannels] = {0, 1};
    if (channels < 1 || channels > kMaxChannels)
        return true;
    if (family != 0)
    {
        if (a->privSize < 21 + channels)
            return true;
        streams = h[19];
        coupled = h[20];
        for (int i = 0; i < channels; ++i)
            mapping[i] = h[21 + i];
    }
    int error = 0;
    opus_ = g_opus.create(kOpusRate, channels, streams, coupled, mapping, &error);
    if (!opus_)
    {
        nfs::Log("movies: the Opus decoder does not start (%d)", error);
        return true;
    }
    channels_ = channels;
    // Opus's channel order (Vorbis's) to SDL's: 5.1 is L C R Ls Rs LFE in the file, L R C LFE Ls Rs for SDL.
    for (int i = 0; i < channels; ++i)
        order_[i] = i;
    if (channels == 6)
    {
        const int sdl[6] = {0, 2, 1, 5, 3, 4};
        for (int i = 0; i < 6; ++i)
            order_[i] = sdl[i];
    }
    pcm_ = static_cast<float *>(Allocate(sizeof(float) * kMaxOpusFrames * kMaxChannels));
    mixed_ = static_cast<float *>(Allocate(sizeof(float) * kMaxOpusFrames * kMaxChannels));
    if (!nfs::sdl::Start(SDL_INIT_AUDIO))
        return true;
    SDL_AudioSpec spec = {SDL_AUDIO_F32, channels, kOpusRate};
    audio_ = SDL_OpenAudioDeviceStream(static_cast<SDL_AudioDeviceID>(nfs::sound::OutputDevice()), &spec, nullptr,
                                       nullptr);
    if (!audio_)
    {
        nfs::Log("movies: no sound device for the movie (%s)", SDL_GetError());
        return true;
    }
    SDL_SetAudioStreamGain(audio_, volume * static_cast<float>(nfs::sound::MasterVolume()) / 100.0f);
    SDL_ResumeAudioStreamDevice(audio_);
    audioTrack_ = a->number;
    skip_ = preSkip_;
    return true;
}

bool Player::Open(const char *path, bool loop, bool sound, float volume, bool bottomUp)
{
    Close();
    if (!LoadDecoders() || !reader_.Open(path))
        return false;
    const nfs::movies::WebmReader::Track *v = reader_.Video();
    if (!v || v->width <= 0 || v->height <= 0 || v->width > 8192 || v->height > 8192)
    {
        nfs::Log("movies: %s has no VP9 picture the player takes", path);
        reader_.Close();
        return false;
    }
    vpx_codec_dec_cfg_t cfg = {};
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    cfg.threads = info.dwNumberOfProcessors > 8 ? 8 : info.dwNumberOfProcessors;
    cfg.w = static_cast<unsigned int>(v->width);
    cfg.h = static_cast<unsigned int>(v->height);
    if (g_vpx.init(&vpx_, g_vpx.iface(), &cfg, 0, VPX_DECODER_ABI_VERSION) != VPX_CODEC_OK)
    {
        nfs::Log("movies: the VP9 decoder does not start");
        reader_.Close();
        return false;
    }
    vpxOpen_ = true;
    width = v->width;
    picture = v->height;
    height = (width * 3 / 4 + 1) & ~1;
    if (height < picture)
        height = picture;
    top = (height - picture) / 2;
    videoTrack_ = v->number;
    matrix_ = MatrixOf(v->matrix, v->fullRange != 0, v->height);
    for (Frame &f : frames_)
    {
        f.pixels = static_cast<unsigned char *>(Allocate(static_cast<SIZE_T>(width) * height * 4));
        f.state = kFree;
        if (!f.pixels)
        {
            Close();
            return false;
        }
        unsigned int *black = reinterpret_cast<unsigned int *>(f.pixels);
        for (int i = 0, n = width * height; i < n; ++i)
            black[i] = 0xFF000000u;
    }
    OpenAudio(sound, volume);
    loop_ = loop;
    bottomUp_ = bottomUp;
    stop_ = paused_ = decodedAll_ = 0;
    loopOffset_ = passEnd_ = pushedFrames_ = pausedFor_ = 0;
    audioStart_ = -1;
    lastShown_ = -1;
    flushed_ = false;
    lastClock_ = 0;
    starvedFrom_ = -1;
    shown_ = -1;
    InitializeCriticalSection(&lock_);
    lockMade_ = true;
    wake_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    QueryPerformanceCounter(&started_);
    thread_ = CreateThread(nullptr, 0, Run, this, 0, nullptr);
    if (!thread_)
    {
        Close();
        return false;
    }
    SetThreadPriority(thread_, THREAD_PRIORITY_ABOVE_NORMAL);
    open = true;
    static const bool log = nfs::config::Int("Debug", "MovieLog", 0) != 0;
    if (log)
        nfs::Log("movies: %s, %dx%d in a %dx%d frame, sound %s", path, width, picture, width, height,
                 audio_ ? (channels_ == 6 ? "5.1" : channels_ == 2 ? "stereo" : "on") : "off");
    return true;
}

void Player::Close()
{
    if (thread_)
    {
        InterlockedExchange(&stop_, 1);
        SetEvent(wake_);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (wake_)
        CloseHandle(wake_);
    wake_ = nullptr;
    if (audio_)
        SDL_DestroyAudioStream(audio_);
    audio_ = nullptr;
    if (opus_)
        g_opus.destroy(opus_);
    opus_ = nullptr;
    if (vpxOpen_)
        g_vpx.destroy(&vpx_);
    vpxOpen_ = false;
    reader_.Close();
    for (Frame &f : frames_)
    {
        Release(f.pixels);
        f = Frame();
    }
    for (; pendingCount_ > 0; --pendingCount_)
    {
        HeapFree(GetProcessHeap(), 0, pending_[pendingHead_].data);
        pending_[pendingHead_] = Pending();
        pendingHead_ = (pendingHead_ + 1) % kPending;
    }
    pendingHead_ = pendingBytes_ = 0;
    fileEnded_ = false;
    Release(pcm_);
    Release(mixed_);
    pcm_ = mixed_ = nullptr;
    if (lockMade_)
        DeleteCriticalSection(&lock_);
    lockMade_ = false;
    audioTrack_ = videoTrack_ = channels_ = 0;
    open = false;
}

DWORD WINAPI Player::Run(void *self)
{
    static_cast<Player *>(self)->Decode();
    return 0;
}

int Player::FreeFrame()
{
    EnterCriticalSection(&lock_);
    int found = -1;
    for (int i = 0; i < kFrames && found < 0; ++i)
        if (frames_[i].state == kFree)
            found = i;
    LeaveCriticalSection(&lock_);
    return found;
}

void Player::DecodeVideo(int slot)
{
    Pending &p = pending_[pendingHead_];
    const bool decoded = g_vpx.decode(&vpx_, p.data, static_cast<unsigned int>(p.size), nullptr, 0) == VPX_CODEC_OK;
    const long long time = p.time;
    HeapFree(GetProcessHeap(), 0, p.data);
    pendingBytes_ -= p.size;
    p = Pending();
    pendingHead_ = (pendingHead_ + 1) % kPending;
    --pendingCount_;
    if (!decoded)
        return;
    vpx_codec_iter_t iter = nullptr;
    while (const vpx_image_t *img = g_vpx.frame(&vpx_, &iter))
    {
        if (slot < 0 || img->fmt != VPX_IMG_FMT_I420 || static_cast<int>(img->d_w) != width ||
            static_cast<int>(img->d_h) != picture)
            continue;
        unsigned char *out = frames_[slot].pixels;
        for (int y = 0; y < picture; ++y)
        {
            const int row = top + y;
            ConvertRow(img->planes[0] + y * img->stride[0], img->planes[1] + (y / 2) * img->stride[1],
                       img->planes[2] + (y / 2) * img->stride[2],
                       out + static_cast<SIZE_T>(bottomUp_ ? height - 1 - row : row) * width * 4, width, matrix_);
        }
        EnterCriticalSection(&lock_);
        frames_[slot].time = time;
        frames_[slot].state = kReady;
        LeaveCriticalSection(&lock_);
        slot = -1;
    }
}

void Player::DecodeAudio(const nfs::movies::WebmReader::Packet &p)
{
    const int n = g_opus.decode(opus_, p.data, p.size, pcm_, kMaxOpusFrames, 0);
    if (n <= 0)
        return;
    int from = 0;
    if (skip_ > 0)
    {
        from = skip_ < n ? skip_ : n;
        skip_ -= from;
    }
    if (from >= n)
        return;
    const int frames = n - from;
    const float *in = pcm_ + from * channels_;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels_; ++c)
            mixed_[i * channels_ + c] = in[i * channels_ + order_[c]];
    // The clock reads the stream and the count together.
    EnterCriticalSection(&lock_);
    if (audioStart_ < 0)
        audioStart_ = p.time + loopOffset_ + static_cast<long long>(from * kSampleNs);
    SDL_PutAudioStreamData(audio_, mixed_, frames * channels_ * static_cast<int>(sizeof(float)));
    pushedFrames_ += frames;
    LeaveCriticalSection(&lock_);
    const long long end = p.time + static_cast<long long>(n * kSampleNs);
    if (end > passEnd_)
        passEnd_ = end;
}

/** One packet read: a picture into the queue, sound decoded into the stream; at the file's end the movie starts again
 * when it loops. False when nothing was read. */
bool Player::ReadAhead()
{
    nfs::movies::WebmReader::Packet p;
    if (!reader_.Next(p))
    {
        if (!loop_)
        {
            fileEnded_ = true;
            return false;
        }
        reader_.Rewind();
        loopOffset_ += passEnd_ + kSecond / 30;
        passEnd_ = 0;
        skip_ = preSkip_;
        return true;
    }
    if (p.track == videoTrack_)
    {
        unsigned char *copy = static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(p.size)));
        if (!copy)
            return true;
        CopyMemory(copy, p.data, static_cast<SIZE_T>(p.size));
        Pending &q = pending_[(pendingHead_ + pendingCount_) % kPending];
        q.data = copy;
        q.size = p.size;
        q.time = p.time + loopOffset_;
        ++pendingCount_;
        pendingBytes_ += p.size;
        if (p.time > passEnd_)
            passEnd_ = p.time;
    }
    else if (p.track == audioTrack_ && audio_)
        DecodeAudio(p);
    return true;
}

void Player::Decode()
{
    while (!stop_)
    {
        if (paused_)
        {
            WaitForSingleObject(wake_, 50);
            continue;
        }
        bool worked = false;
        if (pendingCount_ > 0)
        {
            const int slot = FreeFrame();
            if (slot >= 0)
            {
                DecodeVideo(slot);
                worked = true;
            }
        }
        const bool soundLow =
            audio_ && SDL_GetAudioStreamQueued(audio_) < static_cast<int>(kAudioAhead * kOpusRate * channels_ * sizeof(float));
        if (!fileEnded_ && pendingCount_ < kPending && pendingBytes_ < kPendingBytes &&
            (pendingCount_ < kPendingWanted || soundLow))
            worked = ReadAhead() || worked;
        if (fileEnded_ && !pendingCount_)
            break;
        if (!worked)
            WaitForSingleObject(wake_, kNap);
    }
    InterlockedExchange(&decodedAll_, 1);
}

long long Player::Clock()
{
    if (audio_ && audioStart_ >= 0)
    {
        const int frameBytes = channels_ * static_cast<int>(sizeof(float));
        const int queued = SDL_GetAudioStreamQueued(audio_);
        const long long played = pushedFrames_ - (queued > 0 ? queued / frameBytes : 0);
        long long clock = audioStart_ + static_cast<long long>(static_cast<double>(played) * kSampleNs);
        if (paused_)
            return lastClock_;
        if (queued < frameBytes * kTail)
        {
            // The sound ran out (it ends before the pictures, or decoding fell behind): the wall clock goes on from it.
            if (starvedFrom_ < 0)
            {
                starvedFrom_ = clock;
                QueryPerformanceCounter(&starvedAt_);
            }
            clock = starvedFrom_ + Elapsed(starvedAt_);
        }
        else
            starvedFrom_ = -1;
        if (clock < lastClock_)
            clock = lastClock_;
        lastClock_ = clock;
        return clock;
    }
    if (paused_)
        return Elapsed(started_) - pausedFor_ - Elapsed(pausedAt_);
    return Elapsed(started_) - pausedFor_;
}

const unsigned char *Player::NextFrame(bool *ended)
{
    *ended = false;
    EnterCriticalSection(&lock_);
    if (decodedAll_ && audio_ && !flushed_)
    {
        // No more sound comes: SDL plays out what is left of its last chunk too.
        SDL_FlushAudioStream(audio_);
        flushed_ = true;
    }
    const bool drained =
        decodedAll_ && (!audio_ || SDL_GetAudioStreamQueued(audio_) < channels_ * static_cast<int>(sizeof(float)) * kTail);
    // Past the sound's end, what pictures are left are due.
    const long long now = drained ? 0x7FFFFFFFFFFFFFFFll : Clock();
    int best = -1, ready = 0;
    for (int i = 0; i < kFrames; ++i)
        if (frames_[i].state == kReady)
        {
            ++ready;
            if (frames_[i].time <= now && (best < 0 || frames_[i].time > frames_[best].time))
                best = i;
        }
    const unsigned char *out = nullptr;
    if (best >= 0)
    {
        for (int i = 0; i < kFrames; ++i)
            if ((frames_[i].state == kReady && frames_[i].time < frames_[best].time) || frames_[i].state == kShown)
                frames_[i].state = kFree;
        frames_[best].state = kShown;
        lastShown_ = frames_[best].time;
        shown_ = best;
        out = frames_[best].pixels;
        --ready;
    }
    if (drained && !ready)
        *ended = true;
    static const bool log = nfs::config::Int("Debug", "MovieLog", 0) != 0;
    static DWORD lastLog;
    if (log && GetTickCount() - lastLog > 1000)
    {
        lastLog = GetTickCount();
        nfs::Log("movies: clock %d ms, shown %d ms, %d ready, decoded all %d, sound queued %d bytes, pushed %d",
                 static_cast<int>(static_cast<double>(now) * 1e-6), static_cast<int>(static_cast<double>(lastShown_) * 1e-6), ready,
                 static_cast<int>(decodedAll_), audio_ ? SDL_GetAudioStreamQueued(audio_) : -1,
                 static_cast<int>(pushedFrames_));
    }
    LeaveCriticalSection(&lock_);
    if (out)
        SetEvent(wake_);
    return out;
}

void Player::Pause(bool paused)
{
    if (!open || (paused_ != 0) == paused)
        return;
    if (paused)
        QueryPerformanceCounter(&pausedAt_);
    else
        pausedFor_ += Elapsed(pausedAt_);
    InterlockedExchange(&paused_, paused ? 1 : 0);
    if (audio_)
    {
        if (paused)
            SDL_PauseAudioStreamDevice(audio_);
        else
            SDL_ResumeAudioStreamDevice(audio_);
    }
    SetEvent(wake_);
}
} // namespace
#endif

namespace nfs::movies
{
#if NFS_WEBM_MOVIES
bool Open(const char *path, bool loop, bool sound, float volume, bool bottomUp)
{
    return g_player.Open(path, loop, sound, volume, bottomUp);
}

bool Playing()
{
    return g_player.open;
}

int Width()
{
    return g_player.width;
}

int Height()
{
    return g_player.height;
}

const unsigned char *NextFrame(bool *ended)
{
    if (!g_player.open)
    {
        *ended = true;
        return nullptr;
    }
    return g_player.NextFrame(ended);
}

void Pause(bool paused)
{
    g_player.Pause(paused);
}

void Close()
{
    if (g_player.open)
        g_player.Close();
}
#else
bool Open(const char *, bool, bool, float, bool)
{
    return false;
}

bool Playing()
{
    return false;
}

int Width()
{
    return 0;
}

int Height()
{
    return 0;
}

const unsigned char *NextFrame(bool *ended)
{
    *ended = true;
    return nullptr;
}

void Pause(bool)
{
}

void Close()
{
}
#endif
} // namespace nfs::movies

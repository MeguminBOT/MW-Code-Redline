/**
 * The sound output without DirectSound (docs/VULKAN.md, "No DirectX"): the game's EA audio layer (xEASndAudio
 * 0x8205c5, 0x820762, 0x820907) mixes everything itself into one looping DirectSound buffer, half a second long, and
 * keeps writing ahead of the buffer's write cursor. These objects take DirectSound's place for it: the buffer is a ring
 * in memory that an SDL audio stream plays from (core/sdl.h: WASAPI, converted to the device's format and speakers),
 * moving the cursors on as SDL takes the data. build.py points the game's DirectSoundCreate import here.
 *
 * What the game uses: DirectSoundCreate, SetCooperativeLevel, GetCaps (the highest sample rate), the primary buffer's
 * SetFormat (which channel counts the speakers take: the game offers 5.1 only when 6 channels succeed), one secondary
 * buffer: Lock, Unlock, Play (looping), GetStatus, GetCurrentPosition (its write cursor), Restore. The voice chat's
 * DirectSoundCaptureCreate fails: there is no online play to talk in.
 *
 * Settings of its own (engine/audio/sound.h, speed.ini [Sound]): MasterVolume (percent of the mix, applied to the samples as
 * SDL takes them), OutputDevice (a device's name; empty for the system's default, which SDL moves with Windows'
 * choice) and BackgroundSound (0: the sound fades out while another window has the focus).
 */
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include <string.h>

#include "core/config.h"
#include "core/log.h"
#include "core/sdl.h"
#include "engine/audio/sound.h"

namespace
{
/** How far the write cursor leads the play cursor (DirectSound's is 10 to 30 ms). */
const DWORD kWriteLeadMs = 30;

/** Nonzero while the game is the active application (WM_ACTIVATEAPP in the window procedure). */
const volatile int *const g_gameActive = reinterpret_cast<const volatile int *>(0x982bec);
const int kMaxDevices = 32;

/** [Sound]: read once, then changed by the Audio page. The audio thread reads master and background. */
struct Settings
{
    bool read;
    volatile LONG master;
    volatile LONG background;
    char device[128];
    /** The device list as last made (RefreshDevices). */
    int count;
    SDL_AudioDeviceID ids[kMaxDevices];
    char names[kMaxDevices][128];
};

Settings g_settings;

Settings &Read()
{
    Settings &s = g_settings;
    if (s.read)
        return s;
    s.read = true;
    const int master = nfs::config::Int("Sound", "MasterVolume", 100);
    s.master = master < 0 ? 0 : master > 100 ? 100 : master;
    s.background = nfs::config::Int("Sound", "BackgroundSound", 1) != 0;
    // The name as written (config::String lower-cases): matched without regard to case.
    nfs::config::String("Sound", "OutputDevice", "", s.device, sizeof(s.device));
    return s;
}

/** The chosen device's id from its name, or the system's default. */
SDL_AudioDeviceID ChosenDevice()
{
    Settings &s = Read();
    if (!s.device[0] || !nfs::sdl::Start(SDL_INIT_AUDIO))
        return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    int count = 0;
    SDL_AudioDeviceID *ids = SDL_GetAudioPlaybackDevices(&count);
    SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    for (int i = 0; ids && i < count; ++i)
    {
        const char *name = SDL_GetAudioDeviceName(ids[i]);
        if (name && !lstrcmpiA(name, s.device))
        {
            found = ids[i];
            break;
        }
    }
    SDL_free(ids);
    if (found == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
        nfs::Log("sound: output device \"%s\" not found; the system's default plays", s.device);
    return found;
}

/** How many channels the output device mixes in (the speakers), 2 when it cannot be asked. */
int SpeakerChannels()
{
    static int channels = 0;
    if (channels)
        return channels;
    channels = 2;
    SDL_AudioSpec spec;
    int frames = 0;
    if (nfs::sdl::Start(SDL_INIT_AUDIO) && SDL_GetAudioDeviceFormat(ChosenDevice(), &spec, &frames))
        channels = spec.channels;
    return channels;
}

/** Scales 16-bit samples from `gain` towards `target` across them (no click when the volume changes). */
void ApplyGain(short *samples, int count, float &gain, float target)
{
    if (gain == 1.0f && target == 1.0f)
        return;
    const float step = count > 0 ? (target - gain) / count : 0.0f;
    float g = gain;
    for (int i = 0; i < count; ++i)
    {
        g += step;
        const float v = samples[i] * g;
        samples[i] = static_cast<short>(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v);
    }
    gain = target;
}

/** IUnknown's reference count for the objects below. */
template <typename Interface> class Object : public Interface
{
  public:
    virtual ~Object()
    {
    }
    HRESULT __stdcall QueryInterface(REFIID, void **object) override
    {
        if (object)
            *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG __stdcall Release() override
    {
        const LONG left = InterlockedDecrement(&refs_);
        if (!left)
            delete this;
        return static_cast<ULONG>(left);
    }

  private:
    LONG refs_ = 1;
};

/** The primary buffer: only its format matters, as the test of what the speakers take. */
class PrimaryBuffer : public Object<IDirectSoundBuffer>
{
  public:
    HRESULT __stdcall GetCaps(LPDSBCAPS caps) override
    {
        if (!caps)
            return DSERR_INVALIDPARAM;
        caps->dwFlags = DSBCAPS_PRIMARYBUFFER;
        caps->dwBufferBytes = 0;
        return DS_OK;
    }
    HRESULT __stdcall GetCurrentPosition(LPDWORD play, LPDWORD write) override
    {
        if (play)
            *play = 0;
        if (write)
            *write = 0;
        return DS_OK;
    }
    HRESULT __stdcall GetFormat(LPWAVEFORMATEX, DWORD, LPDWORD written) override
    {
        if (written)
            *written = 0;
        return DSERR_UNSUPPORTED;
    }
    HRESULT __stdcall GetVolume(LPLONG volume) override
    {
        if (volume)
            *volume = 0;
        return DS_OK;
    }
    HRESULT __stdcall GetPan(LPLONG pan) override
    {
        if (pan)
            *pan = 0;
        return DS_OK;
    }
    HRESULT __stdcall GetFrequency(LPDWORD) override
    {
        return DSERR_UNSUPPORTED;
    }
    HRESULT __stdcall GetStatus(LPDWORD status) override
    {
        if (status)
            *status = DSBSTATUS_PLAYING | DSBSTATUS_LOOPING;
        return DS_OK;
    }
    HRESULT __stdcall Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override
    {
        return DSERR_ALREADYINITIALIZED;
    }
    HRESULT __stdcall Lock(DWORD, DWORD, LPVOID *, LPDWORD, LPVOID *, LPDWORD, DWORD) override
    {
        return DSERR_PRIOLEVELNEEDED;
    }
    HRESULT __stdcall Play(DWORD, DWORD, DWORD) override
    {
        return DS_OK;
    }
    HRESULT __stdcall SetCurrentPosition(DWORD) override
    {
        return DSERR_INVALIDCALL;
    }
    /** Up to the speakers' channel count: the game picks its speaker setup by which counts succeed. */
    HRESULT __stdcall SetFormat(LPCWAVEFORMATEX format) override
    {
        if (!format || format->wBitsPerSample != 16 || !format->nChannels)
            return DSERR_BADFORMAT;
        return format->nChannels <= SpeakerChannels() ? DS_OK : DSERR_BADFORMAT;
    }
    HRESULT __stdcall SetVolume(LONG) override
    {
        return DS_OK;
    }
    HRESULT __stdcall SetPan(LONG) override
    {
        return DS_OK;
    }
    HRESULT __stdcall SetFrequency(DWORD) override
    {
        return DSERR_UNSUPPORTED;
    }
    HRESULT __stdcall Stop() override
    {
        return DS_OK;
    }
    HRESULT __stdcall Unlock(LPVOID, DWORD, LPVOID, DWORD) override
    {
        return DS_OK;
    }
    HRESULT __stdcall Restore() override
    {
        return DS_OK;
    }
};

/** The game's mix buffer: a ring the game writes ahead in, which the thread below plays from. */
class StreamBuffer : public Object<IDirectSoundBuffer>
{
  public:
    StreamBuffer(const WAVEFORMATEX &format, DWORD bytes) : bytes_(bytes)
    {
        memset(&format_, 0, sizeof(format_));
        memcpy(&format_, &format, format.wFormatTag == WAVE_FORMAT_EXTENSIBLE ? sizeof(WAVEFORMATEXTENSIBLE)
                                                                               : sizeof(WAVEFORMATEX));
        format_.Format.cbSize = format.wFormatTag == WAVE_FORMAT_EXTENSIBLE ? 22 : 0;
        ring_ = static_cast<unsigned char *>(::operator new(bytes));
        memset(ring_, 0, bytes);
        InitializeCriticalSection(&lock_);
        QueryPerformanceFrequency(&frequency_);
    }
    ~StreamBuffer() override
    {
        if (s_active == this)
            s_active = nullptr;
        // Destroying the stream closes its device and waits for SDL's audio thread to leave the callback.
        if (stream_)
            SDL_DestroyAudioStream(stream_);
        DeleteCriticalSection(&lock_);
        ::operator delete(ring_);
    }

    HRESULT __stdcall GetCaps(LPDSBCAPS caps) override
    {
        if (!caps)
            return DSERR_INVALIDPARAM;
        caps->dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCSOFTWARE;
        caps->dwBufferBytes = bytes_;
        caps->dwUnlockTransferRate = 0;
        caps->dwPlayCpuOverhead = 0;
        return DS_OK;
    }
    /** The play cursor is the next byte the engine takes. The write cursor leads it by kWriteLead, as DirectSound's
     * does: the engine takes a period's worth at once, and the game writes ahead of the write cursor by little more
     * than a frame. */
    HRESULT __stdcall GetCurrentPosition(LPDWORD play, LPDWORD write) override
    {
        if (!stream_ && playing_)
            KeepTime();
        EnterCriticalSection(&lock_);
        const DWORD at = position_;
        LeaveCriticalSection(&lock_);
        if (play)
            *play = at;
        if (write)
        {
            const DWORD block = format_.Format.nBlockAlign;
            const DWORD lead = format_.Format.nSamplesPerSec * kWriteLeadMs / 1000 * block;
            *write = (at + lead) % bytes_;
        }
        return DS_OK;
    }
    HRESULT __stdcall GetFormat(LPWAVEFORMATEX format, DWORD room, LPDWORD written) override
    {
        const DWORD size = sizeof(WAVEFORMATEX) + format_.Format.cbSize;
        if (written)
            *written = size;
        if (format)
            memcpy(format, &format_, room < size ? room : size);
        return DS_OK;
    }
    HRESULT __stdcall GetVolume(LPLONG volume) override
    {
        if (volume)
            *volume = 0;
        return DS_OK;
    }
    HRESULT __stdcall GetPan(LPLONG pan) override
    {
        if (pan)
            *pan = 0;
        return DS_OK;
    }
    HRESULT __stdcall GetFrequency(LPDWORD frequency) override
    {
        if (frequency)
            *frequency = format_.Format.nSamplesPerSec;
        return DS_OK;
    }
    HRESULT __stdcall GetStatus(LPDWORD status) override
    {
        if (status)
            *status = playing_ ? DSBSTATUS_PLAYING | DSBSTATUS_LOOPING : 0;
        return DS_OK;
    }
    HRESULT __stdcall Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override
    {
        return DSERR_ALREADYINITIALIZED;
    }
    HRESULT __stdcall Lock(DWORD offset, DWORD count, LPVOID *first, LPDWORD firstBytes, LPVOID *second,
                           LPDWORD secondBytes, DWORD flags) override
    {
        if (flags & DSBLOCK_ENTIREBUFFER)
        {
            offset = 0;
            count = bytes_;
        }
        if (!first || !firstBytes || offset >= bytes_ || count > bytes_)
            return DSERR_INVALIDPARAM;
        *first = ring_ + offset;
        const DWORD toEnd = bytes_ - offset;
        *firstBytes = count < toEnd ? count : toEnd;
        if (second)
            *second = count > toEnd ? ring_ : nullptr;
        if (secondBytes)
            *secondBytes = count > toEnd ? count - toEnd : 0;
        return DS_OK;
    }
    HRESULT __stdcall Play(DWORD, DWORD, DWORD) override
    {
        playing_ = true;
        if (!stream_ && !tried_)
            Open();
        QueryPerformanceCounter(&last_);
        return DS_OK;
    }
    HRESULT __stdcall SetCurrentPosition(DWORD position) override
    {
        EnterCriticalSection(&lock_);
        position_ = position < bytes_ ? position - position % format_.Format.nBlockAlign : 0;
        LeaveCriticalSection(&lock_);
        return DS_OK;
    }
    HRESULT __stdcall SetFormat(LPCWAVEFORMATEX) override
    {
        return DSERR_INVALIDCALL;
    }
    HRESULT __stdcall SetVolume(LONG) override
    {
        return DS_OK;
    }
    HRESULT __stdcall SetPan(LONG) override
    {
        return DS_OK;
    }
    HRESULT __stdcall SetFrequency(DWORD) override
    {
        return DS_OK;
    }
    HRESULT __stdcall Stop() override
    {
        playing_ = false;
        return DS_OK;
    }
    HRESULT __stdcall Unlock(LPVOID, DWORD, LPVOID, DWORD) override
    {
        return DS_OK;
    }
    HRESULT __stdcall Restore() override
    {
        return DS_OK;
    }

  private:
    /** The game's format as SDL's, and the stream that plays the ring. Without a device (or SDL) the cursors keep time
     * with the clock instead, so the game keeps mixing: it restarts its sound when the cursor stands still. */
    void Open()
    {
        tried_ = true;
        if (!nfs::sdl::Start(SDL_INIT_AUDIO))
            return;
        SDL_AudioSpec spec = {SDL_AUDIO_S16LE, format_.Format.nChannels, static_cast<int>(format_.Format.nSamplesPerSec)};
        stream_ = SDL_OpenAudioDeviceStream(ChosenDevice(), &spec, Feed, this);
        s_active = this;
        if (!stream_)
        {
            nfs::Log("sound: no audio device (%s)", SDL_GetError());
            return;
        }
        SDL_ResumeAudioStreamDevice(stream_);
        nfs::Log("sound: %d channels at %d Hz through SDL (%s)", spec.channels, spec.freq, SDL_GetCurrentAudioDriver());
    }

    /** SDL's audio thread asks for `wanted` more bytes: the ring's from the play cursor on (silence while stopped). */
    static void SDLCALL Feed(void *self, SDL_AudioStream *stream, int wanted, int)
    {
        StreamBuffer &b = *static_cast<StreamBuffer *>(self);
        unsigned char chunk[8192];
        const DWORD block = b.format_.Format.nBlockAlign;
        while (wanted > 0)
        {
            DWORD n = static_cast<DWORD>(wanted) < sizeof(chunk) ? static_cast<DWORD>(wanted) : sizeof(chunk);
            n -= n % block;
            if (!n)
                n = block;
            b.Take(chunk, n / block);
            const Settings &s = g_settings;
            const float target = (*g_gameActive || s.background) ? s.master / 100.0f : 0.0f;
            ApplyGain(reinterpret_cast<short *>(chunk), static_cast<int>(n / 2), b.gain_, target);
            SDL_PutAudioStreamData(stream, chunk, static_cast<int>(n));
            wanted -= static_cast<int>(n);
        }
    }

  public:
    /** The stream again on the chosen device (the Audio page's Output Device); the ring and its cursors carry on. */
    void Reopen()
    {
        if (stream_)
            SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        QueryPerformanceCounter(&last_);
        tried_ = false;
        if (playing_)
            Open();
    }

    static StreamBuffer *s_active;

  private:
    /** Takes `frames` from the ring at the play cursor into `out` (silence while stopped) and moves the cursor on. */
    void Take(unsigned char *out, DWORD frames)
    {
        const DWORD block = format_.Format.nBlockAlign;
        DWORD want = frames * block;
        if (!playing_)
        {
            if (out)
                memset(out, 0, want);
            return;
        }
        EnterCriticalSection(&lock_);
        DWORD at = position_;
        while (want)
        {
            const DWORD run = want < bytes_ - at ? want : bytes_ - at;
            if (out)
            {
                memcpy(out, ring_ + at, run);
                out += run;
            }
            want -= run;
            at = (at + run) % bytes_;
        }
        position_ = at;
        LeaveCriticalSection(&lock_);
    }

    /** No device: the play cursor moves on by the time since it last did. */
    void KeepTime()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double rate = format_.Format.nSamplesPerSec;
        const LONGLONG frames = static_cast<LONGLONG>(static_cast<double>(now.QuadPart - last_.QuadPart) * rate /
                                                      static_cast<double>(frequency_.QuadPart));
        if (frames <= 0)
            return;
        last_.QuadPart += static_cast<LONGLONG>(static_cast<double>(frames) * static_cast<double>(frequency_.QuadPart) / rate);
        const DWORD ring = bytes_ / format_.Format.nBlockAlign;
        Take(nullptr, frames >= ring ? static_cast<DWORD>(frames) % ring : static_cast<DWORD>(frames));
    }

    WAVEFORMATEXTENSIBLE format_;
    unsigned char *ring_;
    DWORD bytes_;
    DWORD position_ = 0;
    volatile bool playing_ = false;
    bool tried_ = false;
    CRITICAL_SECTION lock_;
    SDL_AudioStream *stream_ = nullptr;
    LARGE_INTEGER frequency_, last_ = {};
    /** The volume the last samples were played at (the audio thread's). */
    float gain_ = 1.0f;
};

StreamBuffer *StreamBuffer::s_active = nullptr;

class Sound : public Object<IDirectSound>
{
  public:
    HRESULT __stdcall CreateSoundBuffer(LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *buffer, LPUNKNOWN) override
    {
        if (!desc || !buffer)
            return DSERR_INVALIDPARAM;
        *buffer = nullptr;
        if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER)
        {
            *buffer = new PrimaryBuffer;
            return DS_OK;
        }
        const WAVEFORMATEX *format = desc->lpwfxFormat;
        if (!format || format->wBitsPerSample != 16 || !format->nChannels || !format->nBlockAlign ||
            desc->dwBufferBytes < format->nBlockAlign || format->nChannels > SpeakerChannels())
            return DSERR_BADFORMAT;
        *buffer = new StreamBuffer(*format, desc->dwBufferBytes - desc->dwBufferBytes % format->nBlockAlign);
        return DS_OK;
    }
    HRESULT __stdcall GetCaps(LPDSCAPS caps) override
    {
        if (!caps)
            return DSERR_INVALIDPARAM;
        const DWORD size = caps->dwSize;
        memset(caps, 0, size < sizeof(DSCAPS) ? size : sizeof(DSCAPS));
        caps->dwSize = size;
        caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY16BIT | DSCAPS_CONTINUOUSRATE |
                        DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY16BIT;
        caps->dwMinSecondarySampleRate = 100;
        caps->dwMaxSecondarySampleRate = 200000;
        caps->dwMaxHwMixingAllBuffers = caps->dwMaxHwMixingStaticBuffers = caps->dwMaxHwMixingStreamingBuffers = 1;
        return DS_OK;
    }
    HRESULT __stdcall DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER *copy) override
    {
        if (copy)
            *copy = nullptr;
        return DSERR_UNSUPPORTED;
    }
    HRESULT __stdcall SetCooperativeLevel(HWND, DWORD) override
    {
        return DS_OK;
    }
    HRESULT __stdcall Compact() override
    {
        return DS_OK;
    }
    HRESULT __stdcall GetSpeakerConfig(LPDWORD config) override
    {
        if (config)
            *config = SpeakerChannels() >= 6 ? DSSPEAKER_5POINT1 : SpeakerChannels() >= 4 ? DSSPEAKER_QUAD
                                                                                          : DSSPEAKER_STEREO;
        return DS_OK;
    }
    HRESULT __stdcall SetSpeakerConfig(DWORD) override
    {
        return DS_OK;
    }
    HRESULT __stdcall Initialize(LPCGUID) override
    {
        return DSERR_ALREADYINITIALIZED;
    }
};
} // namespace

namespace nfs::sound
{
int MasterVolume()
{
    return Read().master;
}

void SetMasterVolume(int percent)
{
    Read().master = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    nfs::config::WriteInt("Sound", "MasterVolume", g_settings.master);
}

bool BackgroundSound()
{
    return Read().background != 0;
}

void SetBackgroundSound(bool on)
{
    Read().background = on;
    nfs::config::WriteInt("Sound", "BackgroundSound", on ? 1 : 0);
}

int RefreshDevices()
{
    Settings &s = Read();
    s.count = 0;
    if (!nfs::sdl::Start(SDL_INIT_AUDIO))
        return 0;
    int count = 0;
    SDL_AudioDeviceID *ids = SDL_GetAudioPlaybackDevices(&count);
    for (int i = 0; ids && i < count && s.count < kMaxDevices; ++i)
    {
        const char *name = SDL_GetAudioDeviceName(ids[i]);
        if (!name)
            continue;
        s.ids[s.count] = ids[i];
        lstrcpynA(s.names[s.count], name, sizeof(s.names[0]));
        ++s.count;
    }
    SDL_free(ids);
    return s.count;
}

const char *DeviceName(int index)
{
    const Settings &s = Read();
    return index >= 0 && index < s.count ? s.names[index] : "System Default";
}

int CurrentDevice()
{
    const Settings &s = Read();
    if (!s.device[0])
        return -1;
    for (int i = 0; i < s.count; ++i)
        if (!lstrcmpiA(s.names[i], s.device))
            return i;
    return -1;
}

void SetDevice(int index)
{
    Settings &s = Read();
    const char *name = index >= 0 && index < s.count ? s.names[index] : "";
    if (!lstrcmpiA(name, s.device))
        return;
    lstrcpynA(s.device, name, sizeof(s.device));
    nfs::config::WriteString("Sound", "OutputDevice", name);
    if (StreamBuffer::s_active)
        StreamBuffer::s_active->Reopen();
    nfs::Log("sound: output device %s", name[0] ? name : "(the system's default)");
}

unsigned int OutputDevice()
{
    return ChosenDevice();
}

bool Heard()
{
    return *g_gameActive || Read().background;
}
} // namespace nfs::sound

extern "C" HRESULT WINAPI DirectSoundCreate(LPCGUID, LPDIRECTSOUND *sound, LPUNKNOWN)
{
    if (!sound)
        return DSERR_INVALIDPARAM;
    *sound = new Sound;
    return DS_OK;
}

/** The voice chat's microphone: online play is gone, so there is none. */
extern "C" HRESULT WINAPI DirectSoundCaptureCreate(LPCGUID, LPDIRECTSOUNDCAPTURE *capture, LPUNKNOWN)
{
    if (capture)
        *capture = nullptr;
    return DSERR_NODRIVER;
}

/**
 * The movie player (original: zPlatform MoviePlayer, its VP6 frames through EA's RCMP AV_PLAYER) and WebM movies.
 *
 * The movie player's frames into the renderer (FillInTextureInfo 0x6caec0). A VP6 frame (YUV with 0x60 texels of
 * border) becomes the 32-bit movie image (0x982d18) in MoviePlayer 0x5176d0, and the image the movie texture
 * (MovieTextureInfo 0x91cb28, its platform record 0x983e40). With Direct3D 9 the image was copied in through the
 * texture's LockRect; the renderer's textures have no Direct3D methods, and that call went through the end of their
 * three-entry vtable to address 0: the crash at every movie's first frame since Direct3D was removed (2026-11-12). Here
 * the image is uploaded.
 *
 * Movies are WebM only (docs/MOVIES.md): a movie without data/movies/<name>.webm is skipped as SkipMovies skips it, and
 * EA's VP6 player is not used. Play 0x542a40 opens the WebM (engine/movies/movies.h),
 * makes the movie texture its size as PlatSetFirstMovieFrame 0x6e1880 does for a VP6 movie, and sets the player
 * playing (state 5); UpdateFunction 0x525f70, which Update 0x542c50 calls each frame while it plays, uploads the picture
 * due and sets the next state 0 at the end, after which Update finishes as for a VP6 movie (the "movie_done" message).
 * Pause 0x517660, UnPause 0x517690, Stop 0x517610 and the destructor 0x525e60 reach it too. The player's settings: +0
 * the volume (0 to 127, GetMovieCategoryVolume), +0x14 loop, +0x15 sound, +0x18 the file name.
 *
 * -set Debug.MovieLog=1 logs each movie played: its file, and the handle the movie library gave and the player's state,
 * or the WebM's size and sound.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/movies/movies.h"
#include "engine/rendering/vulkan/renderer.h"

NFS_REPLACE(0x6caec0, MovieFillInTextureInfo)
NFS_REPLACE(0x542a40, MoviePlay)
NFS_REPLACE(0x525f70, MovieUpdateFunction)
NFS_REPLACE(0x517610, MovieStop)
NFS_REPLACE(0x517660, MoviePause)
NFS_REPLACE(0x517690, MovieUnPause)
NFS_REPLACE(0x525e60, MovieDestroy)

void __fastcall MovieFillInTextureInfo(MoviePlayer *self, void *, unsigned int *decodeBuffer, TextureInfo *info,
                                       void *frame);
void __fastcall MoviePlay(MoviePlayer *self, void *);
void __fastcall MovieUpdateFunction(MoviePlayer *self, void *);
void __fastcall MovieStop(MoviePlayer *self, void *);
void __fastcall MoviePause(MoviePlayer *self, void *);
void __fastcall MovieUnPause(MoviePlayer *self, void *);
void __fastcall MovieDestroy(MoviePlayer *self, void *);
/** source/engine/resources/EcstasyMisc.cpp (0x4fd070). */
void __fastcall InitTextureInfo(char *t, void *, const unsigned int *name, void *data, unsigned int width, int height,
                                int format, void *palette, unsigned short flags, unsigned char alphaBits, unsigned char shift,
                                int);

namespace
{
char *const *const kMovieImage = reinterpret_cast<char *const *>(0x982d18);
const int *const kMovieImageFlags = reinterpret_cast<const int *>(0x8faf18);
void *const *const kMovieTexture = reinterpret_cast<void *const *>(0x983e58);
char *const kMovieTextureInfo = reinterpret_cast<char *>(0x91cb28);
char *const kMoviePlat = reinterpret_cast<char *>(0x983e40);
char **const kMovieTextureOwner = reinterpret_cast<char **>(0x982d1c);
char *const *const kMovieClock = reinterpret_cast<char *const *>(0x91cb1c);
const int *const kSkipMovies = reinterpret_cast<const int *>(0x926144);
const int kImageData = 8;
const int kTextureImageBytes = 0x40;
const int kPlayerVolume = 0;
const int kPlayerTopDown = 0xc;
const int kPlayerLoop = 0x14;
const int kPlayerSound = 0x15;
const int kPlayerFile = 0x18;
const int kPlayerPlays = 0x118;
const int kPlayerState = 0x11c;
const int kPlayerNextState = 0x120;
const int kPlayerMovie = 0x140;
const int kPlayerFrame = 0x144;
const int kStatePlaying = 5;
const int kFormatArgb = 0x20;
const int kPlatFormat = 0x14;
const int kD3dArgb = 0x15;
const int kClockStart = 0x18;
const int kInfoWidth = 0x44;
const int kInfoHeight = 0x46;
const float kFullVolume = 127.0f;

/** The movie texture's first image, opaque black, kept for the texture's life (Init reads it). */
unsigned int *g_blank;
unsigned int g_blankPixels;
/** The player's movie is the WebM one. */
bool g_webm;

template <typename T> T &At(MoviePlayer *self, int offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<char *>(self) + offset);
}

bool MovieLog()
{
    static const bool log = nfs::config::Int("Debug", "MovieLog", 0) != 0;
    return log;
}

/** The power of two at or above n, as cFEngRender::RenderMovie 0x599db0 takes it. */
int Pot(int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

/** The movie texture at a WebM's size, as PlatSetFirstMovieFrame makes it for a VP6 movie's first frame. RenderMovie
 * draws the texture's Width / Pot(Width) and Height / Pot(Height) of it, as Direct3D 9 made a movie of another size a
 * power-of-two texture with the picture in a corner; the renderer's texture is the movie's size, so the texture's
 * record says the powers of two once the texture is made, and the whole picture is drawn. Its first image is as large
 * as those, in case the texture were made again from the record. */
bool MakeTexture(int width, int height)
{
    const unsigned int pixels = static_cast<unsigned int>(Pot(width)) * static_cast<unsigned int>(Pot(height));
    if (pixels > g_blankPixels)
    {
        if (g_blank)
            VirtualFree(g_blank, 0, MEM_RELEASE);
        g_blank = static_cast<unsigned int *>(VirtualAlloc(nullptr, pixels * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        g_blankPixels = g_blank ? pixels : 0;
        for (unsigned int i = 0; i < g_blankPixels; ++i)
            g_blank[i] = 0xFF000000u;
    }
    if (!g_blank)
        return false;
    *reinterpret_cast<char **>(kMovieTextureInfo) = kMoviePlat;
    *kMovieTextureOwner = kMovieTextureInfo;
    unsigned int name[6] = {};
    lstrcpynA(reinterpret_cast<char *>(name), "MovieTexture", sizeof(name));
    InitTextureInfo(kMovieTextureInfo, nullptr, name, g_blank, static_cast<unsigned int>(width), height, kFormatArgb,
                    nullptr, 0, 0, 0, 0);
    *reinterpret_cast<int *>(kMoviePlat + kPlatFormat) = kD3dArgb;
    *reinterpret_cast<int *>(kMoviePlat + 8) = 0;
    *reinterpret_cast<int *>(kMoviePlat + 0xc) = 0;
    *reinterpret_cast<int *>(kMoviePlat + 0x10) = 0;
    reinterpret_cast<TextureInfoPlatInterface *>(kMovieTextureInfo)->Init();
    *reinterpret_cast<short *>(kMovieTextureInfo + kInfoWidth) = static_cast<short>(Pot(width));
    *reinterpret_cast<short *>(kMovieTextureInfo + kInfoHeight) = static_cast<short>(Pot(height));
    return *kMovieTexture != nullptr;
}

bool PlayWebm(MoviePlayer *self)
{
    char path[MAX_PATH];
    const char *file = &At<char>(self, kPlayerFile);
    if (!nfs::movies::FindWebm(file, path, sizeof(path)))
        return false;
    const float volume = static_cast<float>(At<int>(self, kPlayerVolume)) / kFullVolume;
    // RenderMovie draws the texture bottom row first unless the player's +0xc is set (see MakeTexture).
    if (!nfs::movies::Open(path, At<char>(self, kPlayerLoop) != 0, At<char>(self, kPlayerSound) != 0,
                           volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume, At<int>(self, kPlayerTopDown) == 0))
        return false;
    if (!MakeTexture(nfs::movies::Width(), nfs::movies::Height()))
    {
        nfs::movies::Close();
        return false;
    }
    g_webm = true;
    At<int>(self, kPlayerMovie) = 0;
    At<int>(self, kPlayerFrame) = 0;
    ++At<int>(self, kPlayerPlays);
    if (char *clock = *kMovieClock)
        *reinterpret_cast<unsigned int *>(clock + kClockStart) = bGetTicker();
    At<int>(self, kPlayerState) = kStatePlaying;
    At<int>(self, kPlayerNextState) = kStatePlaying;
    if (MovieLog())
        nfs::Log("movies: play %s as %s", file, path);
    return true;
}
} // namespace

void __fastcall MovieFillInTextureInfo(MoviePlayer *self, void *, unsigned int *, TextureInfo *info, void *frame)
{
    char *image = *kMovieImage;
    if (!image)
        return;
    NFS_ORIGINAL(0x5176d0, void(__thiscall *)(MoviePlayer *, void *, void *, int))(self, frame, image, *kMovieImageFlags);
    const int at = *reinterpret_cast<const int *>(image + kImageData);
    nfs::vk::Texture *texture = static_cast<nfs::vk::Texture *>(*kMovieTexture);
    if (at && texture && nfs::vk::g_renderer)
        nfs::vk::g_renderer->UploadTexture(
            texture, 0, 0, image + at, *reinterpret_cast<const unsigned int *>(reinterpret_cast<char *>(info) + kTextureImageBytes));
}

void __fastcall MoviePlay(MoviePlayer *self, void *)
{
    nfs::movies::Close();
    g_webm = false;
    if (!*kSkipMovies && PlayWebm(self))
        return;
    // No WebM (or movies skipped): the movie is done at once, as Play does with SkipMovies; VP6 is not played.
    int *const skip = const_cast<int *>(kSkipMovies);
    const int was = *skip;
    *skip = 1;
    NFS_ORIGINAL(0x542a40, void(__thiscall *)(MoviePlayer *))(self);
    *skip = was;
    if (MovieLog())
        nfs::Log("movies: no WebM for %s, skipped", &At<char>(self, kPlayerFile));
}

void __fastcall MovieUpdateFunction(MoviePlayer *self, void *)
{
    if (!g_webm)
    {
        NFS_ORIGINAL(0x525f70, void(__thiscall *)(MoviePlayer *))(self);
        return;
    }
    bool ended = false;
    const unsigned char *frame = nfs::movies::NextFrame(&ended);
    nfs::vk::Texture *texture = static_cast<nfs::vk::Texture *>(*kMovieTexture);
    if (frame && texture && nfs::vk::g_renderer)
        nfs::vk::g_renderer->UploadTexture(texture, 0, 0, frame,
                                           static_cast<unsigned int>(nfs::movies::Width() * nfs::movies::Height() * 4));
    if (ended)
    {
        At<int>(self, kPlayerNextState) = 0;
        nfs::movies::Close();
    }
}

void __fastcall MovieStop(MoviePlayer *self, void *)
{
    nfs::movies::Close();
    NFS_ORIGINAL(0x517610, void(__thiscall *)(MoviePlayer *))(self);
}

void __fastcall MoviePause(MoviePlayer *self, void *)
{
    if (g_webm)
        nfs::movies::Pause(true);
    else
        NFS_ORIGINAL(0x517660, void(__thiscall *)(MoviePlayer *))(self);
}

void __fastcall MovieUnPause(MoviePlayer *self, void *)
{
    if (g_webm)
        nfs::movies::Pause(false);
    else
        NFS_ORIGINAL(0x517690, void(__thiscall *)(MoviePlayer *))(self);
}

void __fastcall MovieDestroy(MoviePlayer *self, void *)
{
    nfs::movies::Close();
    g_webm = false;
    NFS_ORIGINAL(0x525e60, void(__thiscall *)(MoviePlayer *))(self);
}

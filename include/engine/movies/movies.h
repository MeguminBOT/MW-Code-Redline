#pragma once

#include <stddef.h>

/**
 * The game's movies (docs/MOVIES.md): the file a movie name finds (source/engine/movies/MovieFiles.cpp) and the WebM
 * player that plays data/movies/<name>.webm in place of MOVIES\<name>_<language>_<region>.vp6 when it is there
 * (WebmPlayer.cpp, MoviePlayer.cpp).
 */
namespace nfs::movies
{
/** The file to open for a path the game asks for: the path itself, or for a movie its WebM, written into buffer. */
const char *FindFile(const char *path, char *buffer, size_t size);

/** data/movies/<name>.webm (in [Data] Folder) for one of the game's movie file names, when it is there and [Movies]
 * WebM is on (the default). */
bool FindWebm(const char *gameName, char *path, size_t size);

/** Opens a WebM movie and starts decoding it: loop plays it again at its end, sound off leaves its sound out, volume
 * 0 to 1, bottomUp puts the pictures' rows bottom first (as the movie texture holds a VP6 movie's). False when it
 * cannot play (vpx.dll or opus.dll missing, no VP9 picture). One movie at a time. */
bool Open(const char *path, bool loop, bool sound, float volume, bool bottomUp);
bool Playing();
int Width();
int Height();
/** The newest picture due by the movie's clock (BGRA rows, Width x Height), null when the one shown last is still the
 * newest; ended when the movie is over. */
const unsigned char *NextFrame(bool *ended);
void Pause(bool paused);
void Close();
} // namespace nfs::movies

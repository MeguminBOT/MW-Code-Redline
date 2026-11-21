#pragma once

/**
 * Sampling profiler for the game's main thread (source/dev/Sampler.cpp, tools/samples.py): with -sample, where the
 * main thread is during races is recorded about 4000 times a second into nfs_samples.bin next to the exe.
 */
namespace nfs::sampler
{
/** Starts sampling the calling thread (the main thread) with -sample; does nothing otherwise or when running. */
void Start();
} // namespace nfs::sampler

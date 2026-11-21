#pragma once

/**
 * Differential self-tests, run with `speed.exe -nfstest` (results in nfstest.log next to the exe).
 * A test compares a ported function against the original machine code, which stays at its original address in the
 * rebuilt image. Use NFS_ORIGINAL(address, Signature) to call the original.
 */
#include "legacy/link.h"

namespace nfs::test
{
using TestFn = void (*)();

struct Registration
{
    Registration(const char *name, TestFn fn);
};

void Fail(const char *test, const char *fmt, ...);
void Log(const char *fmt, ...);
int RunAll();

/** Deterministic pseudo-random numbers so failures reproduce. */
unsigned int Random();
/** Restarts Random() from a seed (to build the same random data twice); returns the seed it replaces. */
unsigned int SetSeed(unsigned int seed);
/** Uniform in [lo, hi). */
float RandomFloat(float lo, float hi);

/**
 * Float comparison for ported x87 code. Results usually match bit for bit; a different rounding of an intermediate may
 * move the last bits, so values within `rel` relative error (or 1e-6 absolute near zero) count as equal. NaNs are equal
 * to NaNs. Records the largest relative error seen by the current test, which RunAll reports.
 */
void ExpectNear(const char *label, int line, const char *expr, float actual, float expected, float rel);
/** Absolute tolerance, for values that come out of cancellation (matrix inverses and the like). */
void ExpectClose(const char *label, int line, const char *expr, float actual, float expected, float abs_tol);
} // namespace nfs::test

#define NFS_TEST(name)                                                                                                 \
    static void name();                                                                                                \
    static nfs::test::Registration name##_registration(#name, name);                                                   \
    static void name()

#define NFS_EXPECT_NEAR(label, a, b, rel) nfs::test::ExpectNear(label, __LINE__, #a, (a), (b), (rel))
#define NFS_EXPECT_CLOSE(label, a, b, tol) nfs::test::ExpectClose(label, __LINE__, #a, (a), (b), (tol))

#define NFS_EXPECT_EQ(label, a, b)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        auto va_ = (a);                                                                                                \
        auto vb_ = (b);                                                                                                \
        if (!(va_ == vb_))                                                                                             \
            nfs::test::Fail(label, "line %d: %s != %s (0x%x vs 0x%x)", __LINE__, #a, #b, (unsigned int)va_,            \
                            (unsigned int)vb_);                                                                        \
    } while (0)

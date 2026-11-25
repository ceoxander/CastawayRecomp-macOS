#pragma once
#include <cstddef>
#include <cstdint>

// Offsets shared with fiber_arm64.S.
struct Arm64FiberContext
{
    uint64_t x19ThroughX29[11];
    uint64_t lr;
    uint64_t sp;
    uint64_t d8ThroughD15[8];
    uint64_t fpcr;
    uint64_t fpsr;
};
static_assert(offsetof(Arm64FiberContext, lr) == 88);
static_assert(offsetof(Arm64FiberContext, sp) == 96);
static_assert(offsetof(Arm64FiberContext, d8ThroughD15) == 104);
static_assert(offsetof(Arm64FiberContext, fpcr) == 168);

extern "C" void Fiber_SwapArm64(Arm64FiberContext* from, const Arm64FiberContext* to);

#include "fiber.h"
#include "../log.h"

#ifdef _WIN32
#include <windows.h>

namespace {
struct Start {
    void (*fn)(void*);
    void* arg;
};

void WINAPI FiberProc(void* p) {
    Start s = *(Start*)p;
    delete (Start*)p;
    s.fn(s.arg);
}
} // namespace

HostFiber* Fiber_ConvertThread() {
    return (HostFiber*)ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
}

HostFiber* Fiber_Create(size_t stackReserve, void (*fn)(void*), void* arg) {
    void* f = CreateFiberEx(64 * 1024, stackReserve, FIBER_FLAG_FLOAT_SWITCH, FiberProc, new Start{ fn, arg });
    if (!f) FATAL("CreateFiberEx failed ({})", GetLastError());
    return (HostFiber*)f;
}

void Fiber_Switch(HostFiber* f) {
    SwitchToFiber(f);
}

void Fiber_Delete(HostFiber* f) {
    DeleteFiber(f);
}
#else
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(__aarch64__)
#include "fiber_arm64.h"
#else
#include <ucontext.h>
#endif
#include <cerrno>
#include <cstdint>
#include <cstring>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#ifndef MAP_STACK
#define MAP_STACK 0
#endif

struct HostFiber {
#if defined(__APPLE__) && defined(__aarch64__)
    Arm64FiberContext ctx{};
#else
    ucontext_t ctx;
#endif
    void* stack = nullptr;
    size_t stackSize = 0;
    void (*fn)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {
thread_local HostFiber* t_current = nullptr;

[[noreturn]] void RunFiber(HostFiber* f) {
    f->fn(f->arg);
    FATAL("fiber function returned");
}
#if !defined(__APPLE__) || !defined(__aarch64__)
void Entry(unsigned lo, unsigned hi) {
    RunFiber(reinterpret_cast<HostFiber*>((static_cast<uintptr_t>(hi) << 32) | lo));
}
#endif
} // namespace

#if defined(__APPLE__) && defined(__aarch64__)
extern "C" [[noreturn]] void Fiber_EntryArm64() { RunFiber(t_current); }
#endif

HostFiber* Fiber_ConvertThread() {
    t_current = new HostFiber();
    return t_current;
}

HostFiber* Fiber_Create(size_t stackReserve, void (*fn)(void*), void* arg) {
    HostFiber* f = new HostFiber();
    const size_t guardSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const size_t stackSize = (stackReserve + guardSize - 1) & ~(guardSize - 1);
    f->stackSize = stackSize + guardSize;
    f->stack = mmap(nullptr, f->stackSize, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
    if (f->stack == MAP_FAILED) {
        FATAL("fiber stack allocation failed ({})", strerror(errno));
    }
    if (mprotect(f->stack, guardSize, PROT_NONE) != 0)
        FATAL("fiber stack guard failed ({})", strerror(errno));
    auto* stackBottom = static_cast<uint8_t*>(f->stack) + guardSize;
    f->fn = fn;
    f->arg = arg;
#if defined(__APPLE__) && defined(__aarch64__)
    f->ctx.sp = reinterpret_cast<uintptr_t>(stackBottom + stackSize);
    f->ctx.lr = reinterpret_cast<uintptr_t>(&Fiber_EntryArm64);
#else
    if (getcontext(&f->ctx) != 0) FATAL("getcontext failed ({})", strerror(errno));
    f->ctx.uc_stack.ss_sp = stackBottom;
    f->ctx.uc_stack.ss_size = stackSize;
    f->ctx.uc_link = nullptr;
    uintptr_t p = (uintptr_t)f;
    makecontext(&f->ctx, (void (*)())Entry, 2, (unsigned)(p & 0xFFFFFFFF), (unsigned)(p >> 32));
#endif
    return f;
}

void Fiber_Switch(HostFiber* f) {
    HostFiber* self = t_current;
    t_current = f;
#if defined(__APPLE__) && defined(__aarch64__)
    Fiber_SwapArm64(&self->ctx, &f->ctx);
#else
    if (swapcontext(&self->ctx, &f->ctx) != 0)
        FATAL("swapcontext failed ({})", strerror(errno));
#endif
}

void Fiber_Delete(HostFiber* f) {
    if (f->stack) munmap(f->stack, f->stackSize);
    delete f;
}
#endif

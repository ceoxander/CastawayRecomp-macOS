#include "memory.h"
#include "../log.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

uint8_t* g_base = nullptr;

namespace
{
// The guest RAM views: cached/uncached mirrors alias the same physical pages
struct Region { uint32_t addr; uint32_t size; int mem; };   // mem: 0 MEM1, 1 MEM2, 2 locked cache
constexpr Region kRegions[] = {
{ 0x00000000, MEM1_SIZE, 0 },   // physical (translation off) aliases
{ 0x10000000, MEM2_SIZE, 1 },
{ 0x80000000, MEM1_SIZE, 0 },
{ 0x90000000, MEM2_SIZE, 1 },
{ 0xC0000000, MEM1_SIZE, 0 },
{ 0xD0000000, MEM2_SIZE, 1 },
{ LOCKED_CACHE_ADDR, 0x10000, 2 },
};
constexpr uint32_t kMemSizes[] = { MEM1_SIZE, MEM2_SIZE, 0x10000 };
constexpr uint64_t kTotal = 0x100000000ull;
}   // namespace

#ifdef _WIN32
typedef PVOID(WINAPI* VirtualAlloc2_t)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
typedef PVOID(WINAPI* MapViewOfFile3_t)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);

// Reserve a 4 GiB placeholder and map the guest RAM views into it.
bool Memory_Init()
{
HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
auto pVirtualAlloc2 = (VirtualAlloc2_t)GetProcAddress(kb, "VirtualAlloc2");
auto pMapViewOfFile3 = (MapViewOfFile3_t)GetProcAddress(kb, "MapViewOfFile3");
if (!pVirtualAlloc2 || !pMapViewOfFile3)
{
    LOG_ERROR("VirtualAlloc2/MapViewOfFile3 unavailable (need Windows 10 1803+)");
    return false;
}
uint8_t* base = (uint8_t*)pVirtualAlloc2(nullptr, nullptr, kTotal, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
if (!base)
{
    LOG_ERROR("failed to reserve guest address space ({})", GetLastError());
    return false;
}
HANDLE sections[3];
for (int i = 0; i < 3; i++)
    sections[i] = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kMemSizes[i], nullptr);
// Split the placeholder so each region is its own placeholder, then map into them.
for (auto& r : kRegions)
    if (!VirtualFree(base + r.addr, r.size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
    {
        LOG_ERROR("placeholder split failed at {:08x} ({})", r.addr, GetLastError());
        return false;
    }
for (auto& r : kRegions)
{
    void* v = pMapViewOfFile3(sections[r.mem], GetCurrentProcess(), base + r.addr, 0, r.size, MEM_REPLACE_PLACEHOLDER,
                              PAGE_READWRITE, nullptr, 0);
    if (v != base + r.addr)
    {
        LOG_ERROR("map view failed at {:08x} ({})", r.addr, GetLastError());
        return false;
    }
}
g_base = base;
LOG_INFO("guest memory at {}", (void*)base);
return true;
}
#elif defined(__APPLE__)
// Reserve 4 GiB of address space and alias backing pages across guest RAM views using shared backing objects.
bool Memory_Init()
{
    auto* base = static_cast<uint8_t*>(
        mmap(nullptr, kTotal, PROT_NONE,
             MAP_PRIVATE | MAP_ANON, -1, 0));

    if (base == MAP_FAILED)
    {
        LOG_ERROR("Guest reserve failed: {}", strerror(errno));
        return false;
    }

    int fds[3] = {-1, -1, -1};

    auto fail = [&]() {
        for (int fd : fds)
            if (fd >= 0) close(fd);
        munmap(base, kTotal);
        return false;
    };

    for (int i = 0; i < 3; ++i)
    {
        const char* tmp = std::getenv("TMPDIR");
        std::string pattern = std::string(tmp && *tmp ? tmp : "/tmp") + "/castaway-ram.XXXXXX";
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');

        fds[i] = mkstemp(name.data());
        if (fds[i] < 0)
        {
            LOG_ERROR("guest RAM backing creation failed: {}", strerror(errno));
            return fail();
        }

        // Keep the object alive through its mappings without leaving a named file.
        if (unlink(name.data()) != 0)
        {
            LOG_ERROR("guest RAM backing unlink failed: {}", strerror(errno));
            return fail();
        }

        if (ftruncate(fds[i], kMemSizes[i]) != 0)
        {
            LOG_ERROR("ftruncate failed: {}", strerror(errno));
            return fail();
        }
    }

    for (const auto& r : kRegions)
    {
        void* v = mmap(base + r.addr, r.size,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_FIXED,
                       fds[r.mem], 0);

        if (v != base + r.addr)
        {
            LOG_ERROR("Guest map failed at {:08x}: {}",
                      r.addr, strerror(errno));
            return fail();
        }
    }

    for (int fd : fds)
        close(fd);

    g_base = base;

    LOG_INFO("guest memory at {}", (void*)base);
    LOG_INFO("macOS shared guest memory initialized");

    return true;
}

#else
// Reserve 4 GiB of address space and map shared-memory files over the guest RAM views.
bool Memory_Init()
{
uint8_t* base = (uint8_t*)mmap(nullptr, kTotal, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
if (base == MAP_FAILED)
{
    LOG_ERROR("failed to reserve guest address space ({})", strerror(errno));
    return false;
}
int fds[3];
for (int i = 0; i < 3; i++)
{
    fds[i] = memfd_create("castaway-guest-ram", MFD_CLOEXEC);
    if (fds[i] < 0 || ftruncate(fds[i], kMemSizes[i]) != 0)
    {
        LOG_ERROR("guest RAM backing failed ({})", strerror(errno));
        return false;
    }
}
for (auto& r : kRegions)
{
    void* v = mmap(base + r.addr, r.size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fds[r.mem], 0);
    if (v != base + r.addr)
    {
        LOG_ERROR("map view failed at {:08x} ({})", r.addr, strerror(errno));
        return false;
    }
}
g_base = base;
LOG_INFO("guest memory at {}", (void*)base);
return true;
}
#endif

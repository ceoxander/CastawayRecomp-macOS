#ifdef __APPLE__
#include "save_backup.h"
#include "ios.h"
#include "../kernel/host.h"
#include "../log.h"
#include "../region.h"

#include <SDL3/SDL.h>
#include <atomic>
#include <filesystem>

namespace
{
namespace fs = std::filesystem;
constexpr uint64_t kInterval = 10 * 60 * 1000;
constexpr uint64_t kSettle = 1500;
constexpr int kSlots = 6;

std::atomic<bool> s_requested{false};
std::atomic<uint64_t> s_noticeUntil{0};
uint64_t s_lastCommit = 0;
uint64_t s_lastBackup = 0;
uint64_t s_nextCheck = 0;

bool Snapshot()
{
    const fs::path nand = IOS_NandRoot();
    const fs::path relative = fs::path(Region_TitleDataDir()).relative_path() / "SaveGame";
    const fs::path source = nand / relative;
    std::error_code ec;
    if (!fs::is_regular_file(source, ec) || !fs::file_size(source, ec) || ec)
    {
        LOG_WARN("NAND backup: save a game before making a backup");
        return false;
    }

    const fs::path dir = nand.parent_path() / "backups" / Region_Current().gameId;
    fs::create_directories(dir, ec);
    if (ec)
    {
        LOG_WARN("NAND backup: {}", ec.message());
        return false;
    }

    fs::path slot;
    auto oldest = fs::file_time_type::max();
    for (int i = 1; i <= kSlots; ++i)
    {
        fs::path candidate = dir / std::format("SaveGame.{}", i);
        auto written = fs::last_write_time(candidate, ec);
        if (ec == std::errc::no_such_file_or_directory)
        {
            slot = std::move(candidate);
            break;
        }
        if (!ec && written < oldest)
        {
            oldest = written;
            slot = std::move(candidate);
        }
    }
    if (slot.empty())
    {
        LOG_WARN("NAND backup: could not choose a slot");
        return false;
    }

    fs::path temporary = slot;
    temporary += ".partial";
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, ec);
    if (!ec) fs::rename(temporary, slot, ec);
    if (ec)
    {
        LOG_WARN("NAND backup: {}", ec.message());
        return false;
    }
    LOG_INFO("NAND backup: {}", slot.string());
    return true;
}
}

void SaveBackup_OnCommit()
{
    s_lastCommit = SDL_GetTicks();
    s_noticeUntil = s_lastCommit + 2800;
}

void SaveBackup_Request() { s_requested = true; }

uint64_t SaveBackup_NoticeUntil() { return s_noticeUntil.load(); }

void SaveBackup_Tick()
{
    const uint64_t now = SDL_GetTicks();
    if (s_lastCommit && now - s_lastCommit < kSettle) return;
    const bool requested = s_requested.exchange(false);
    if (!requested && now < s_nextCheck) return;
    s_nextCheck = now + 1000;
    const bool automatic = s_lastCommit > s_lastBackup &&
                         (!s_lastBackup || now - s_lastBackup >= kInterval);
    if ((requested || automatic) && Snapshot()) s_lastBackup = now;
}
#endif

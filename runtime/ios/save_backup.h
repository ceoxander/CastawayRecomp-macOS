#pragma once
#include <cstdint>

// macOS host snapshots of the last committed Wii save.
void SaveBackup_OnCommit();
void SaveBackup_Tick();
void SaveBackup_Request();
uint64_t SaveBackup_NoticeUntil();

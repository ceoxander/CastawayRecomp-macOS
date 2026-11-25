// Read-only game hooks for TAS scripts: they report what the game does and change nothing.
#include "tas.h"
#include "../game_addrs.h"
#include "../kernel/memory.h"
#ifdef CASTAWAY_DXMT
#include "../gx/video.h"
#include "../cpu/cpu.h"
#include "../ui/overlay.h"

namespace
{
PPCFunc s_newGameDialogOriginal = nullptr;
void NewGameDialogClosed(PPCContext& ctx, uint8_t* base)
{
    const bool accepted = ctx.r[4] == 1;
    s_newGameDialogOriginal(ctx, base);
    Overlay_NewGameDialogClosed(accepted);
}
}

void InstallMacMainMenuHooks()
{
    // USA R42E69: MMUTarget::OnNewGameDialogClose(int), called indirectly.
    constexpr uint32_t address = 0x801CF82C;
    s_newGameDialogOriginal = ppc_lookup(address);
    if (s_newGameDialogOriginal) Cpu_SetFunction(address, NewGameDialogClosed);
}
#endif

// StateMachineStatus::SetState(StateMachineState*): every state machine (boot screens, main
// menu, Create a Sim, loading screens, the island) enters its states through it.
PPC_OVERRIDE(StateMachineStatusSetState)
{
    uint32_t state = ctx.r[4];
    uint32_t vtable = 0;
    if (state >> 28 == 0x8 && (state & 0x0FFFFFFF) + 4 <= MEM1_SIZE) vtable = Read32(state);
    else if (state >> 28 == 0x9 && (state & 0x0FFFFFFF) + 4 <= MEM2_SIZE) vtable = Read32(state);
#ifdef CASTAWAY_DXMT
    Video_NotifyGuestState(ctx.r[3], vtable);
#endif
    Tas_OnState(ctx.r[3], vtable);
    PPC_ORIGINAL(StateMachineStatusSetState);
}

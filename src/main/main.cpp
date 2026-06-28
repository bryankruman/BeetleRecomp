// BeetleRecomp — entry point (SKELETON).
//
// A real recomp runtime initializes N64ModernRuntime (librecomp / ultramodern),
// loads the player's ROM, sets up RT64 for rendering plus audio and input, then
// enters the recompiled game's entrypoint (0x80000400). See Zelda64Recompiled's
// src/main/ for the reference implementation to adapt:
//   https://github.com/Zelda64Recomp/Zelda64Recomp/tree/dev/src/main
//
// This stub exists only so the tree is coherent; it does NOT boot the game yet.

#include <cstdio>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::printf("BeetleRecomp: scaffold only — runtime not yet wired.\n");
    std::printf("See README.md (Roadmap) and BUILDING.md for next steps.\n");
    return 0;
}

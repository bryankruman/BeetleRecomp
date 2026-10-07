// Synchronous cartridge word reads used by BAR's boot thread-copy loop.
// The runtime supplies the normalized, big-endian ROM bytes; N64 RDRAM is word-swapped.
#include "recomp.h"
#include "librecomp/game.hpp"

extern "C" void osPiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    constexpr uint32_t rdram_size = 8 * 1024 * 1024;
    const uint32_t device = static_cast<uint32_t>(ctx->r4);
    const uint32_t destination = static_cast<uint32_t>(ctx->r5);
    const uint32_t segment = device & 0xF0000000u;
    const uint32_t offset = device & 0x0FFFFFFFu;
    const uint32_t ram_segment = destination & 0xE0000000u;
    const uint32_t ram_offset = destination & 0x1FFFFFFFu;
    const auto rom = recomp::get_rom();
    ctx->r2 = -1;
    if (rdram == nullptr ||
        (segment != 0 && segment != 0x10000000u && segment != 0x90000000u && segment != 0xB0000000u) ||
        (ram_segment != 0x80000000u && ram_segment != 0xA0000000u) ||
        (device & 3u) != 0 || (destination & 3u) != 0 ||
        ram_offset > rdram_size - 4 || offset > rom.size() || rom.size() - offset < 4) {
        return;
    }
    const uint32_t value = (uint32_t(rom[offset]) << 24) | (uint32_t(rom[offset + 1]) << 16) |
        (uint32_t(rom[offset + 2]) << 8) | uint32_t(rom[offset + 3]);
    const int32_t cached_destination = static_cast<int32_t>(0x80000000u | ram_offset);
    MEM_W(0, cached_destination) = static_cast<int32_t>(value);
    ctx->r2 = 0;
}

// Older symbol tables call the internal spelling or the access-lock wrapper.
// ROM contents are immutable during play, so all three reads share this synchronous path.
extern "C" void __osPiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    osPiRawReadIo_recomp(rdram, ctx);
}
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    osPiRawReadIo_recomp(rdram, ctx);
}

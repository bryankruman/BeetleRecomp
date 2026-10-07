#include "recomp.h"
#include "main/bar_input.hpp"
#include <ultramodern/ultramodern.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C" void __osSiRawStartDma_recomp(uint8_t*, recomp_context*);
static std::array<bool, 4> connected{};
static std::array<bar::input_config::PakType, 4> accessories{};
static int completions = 0;
static int storage_calls = 0;
bool bar_port_connected(int port) { return connected.at(port); }
bar::input_config::PakType bar_port_pak(int port) { return accessories.at(port); }
extern "C" uint16_t bar_poll_keyboard(int, int8_t*, int8_t*) { return 0; }
extern "C" void bar_pak_motor(int, bool) { throw std::logic_error("unexpected motor command"); }
extern "C" void bar_rt64_set_hud_anchor(int) {}
extern "C" void bar_rt64_set_game_state(unsigned int) {}
extern "C" void bar_rt64_set_hud_paused(int) {}
extern "C" void bar_rt64_start_burst(const char*, int) { throw std::logic_error("unexpected renderer capture"); }
namespace bar_cheats { void apply_frame(uint8_t*) {} }
namespace ultramodern { void send_si_message() { ++completions; } }
namespace bar::input {
bool mempak_read(int, int, uint8_t*) { ++storage_calls; return false; }
bool mempak_write(int, int, const uint8_t*) { ++storage_calls; return false; }
}

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main() {
    try {
        std::vector<uint8_t> memory(8 * 1024 * 1024);
        uint8_t* rdram = memory.data();
        constexpr int64_t base = int32_t(0x80001000u);
        recomp_context context{};
        context.r4 = 0;
        context.r5 = base;
        for (int port = 0; port < 4; ++port) {
            for (bool padded : {false, true}) {
                for (int command : {2, 3}) {
                    for (int scenario = 0; scenario < 3; ++scenario) {
                        // No controller, no accessory, and unreadable existing save.
                        connected.fill(false);
                        accessories.fill(bar::input_config::PakType::None);
                        connected[port] = scenario != 0;
                        if (scenario == 2) accessories[port] = bar::input_config::PakType::ControllerPak;
                        for (int i = 0; i < 64; ++i) MEM_B(i, base) = int8_t(0xFF);
                        for (int i = 0; i < port; ++i) MEM_B(i, base) = 0;
                        const int offset = port + int(padded);
                        MEM_B(offset, base) = command == 2 ? 3 : 35;
                        MEM_B(offset + 1, base) = command == 2 ? 33 : 1;
                        MEM_B(offset + 2, base) = command;
                        MEM_B(offset + 3, base) = 0; // Controller Pak block zero.
                        MEM_B(offset + 4, base) = 0;
                        const int old_completions = completions;
                        const int old_storage_calls = storage_calls;
                        __osSiRawStartDma_recomp(rdram, &context);
                        require((MEM_BU(offset + 1, base) & 0x80) != 0, "missing/unreadable accessory was acknowledged");
                        require((MEM_BU(offset + 1, base) & 0x3F) == (command == 2 ? 33 : 1), "error destroyed response length");
                        require(completions == old_completions + 1, "SI command did not complete exactly once");
                        require(storage_calls == old_storage_calls + int(scenario == 2), "unavailable accessory touched save storage");
                    }
                }
            }
        }
        // A 64-byte DMA buffer must fit wholly in the emulated memory.
        context.r5 = int32_t(0x807FFFFFu);
        const int old_completions = completions;
        __osSiRawStartDma_recomp(rdram, &context);
        require(completions == old_completions + 1, "invalid DMA did not complete");
        std::cout << "PASS: four-port short/padded Pak errors, save failure propagation, and DMA bounds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

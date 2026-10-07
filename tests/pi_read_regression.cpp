#include "recomp.h"
#include "librecomp/game.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C" void osPiRawReadIo_recomp(uint8_t*, recomp_context*);
extern "C" void __osPiRawReadIo_recomp(uint8_t*, recomp_context*);
extern "C" void osPiReadIo_recomp(uint8_t*, recomp_context*);
static std::vector<uint8_t> cartridge{0x80,0x37,0x12,0x40,0x12,0x34,0x56,0x78};
namespace recomp { std::span<const uint8_t> get_rom() { return cartridge; } }

int main() {
    try {
        std::vector<uint8_t> memory(8*1024*1024,0xCD);
        auto* rdram=memory.data();
        recomp_context ctx{};
        for (auto read : {osPiRawReadIo_recomp,__osPiRawReadIo_recomp,osPiReadIo_recomp}) {
            for (uint32_t base : {0u,0x10000000u,0x90000000u,0xB0000000u}) {
                for (uint32_t ram : {0x80001000u,0xA0001000u,0x807FFFFCu}) {
                    ctx.r4=base+4;ctx.r5=static_cast<int32_t>(ram);ctx.r2=99;
                    read(rdram,&ctx);
                    const int32_t cached=int32_t(0x80000000u | (ram & 0x1FFFFFFFu));
                    if(ctx.r2!=0 || uint32_t(MEM_W(0,cached))!=0x12345678u)
                        throw std::runtime_error("cartridge word/address alias read failed");
                }
            }
        }
        const std::array<std::array<uint32_t,2>,7> bad{{
            {{1,0x80001000u}},{{8,0x80001000u}},{{0x08000000u,0x80001000u}},
            {{0,0x80001001u}},{{0,0x80800000u}},{{0,0x7FFFFFFCu}},{{0xFFFFFFFFu,0x80001000u}}
        }};
        for (auto pair : bad) {
            MEM_W(0,int32_t(0x80001000u))=int32_t(0x89ABCDEFu);
            const auto before=memory;
            ctx.r4=pair[0];ctx.r5=int32_t(pair[1]);ctx.r2=99;
            osPiRawReadIo_recomp(rdram,&ctx);
            if(ctx.r2!=gpr(-1) || memory!=before) throw std::runtime_error("invalid PI read modified memory or returned success");
        }
        cartridge.clear();ctx.r4=0;ctx.r5=int32_t(0x80001000u);
        osPiRawReadIo_recomp(rdram,&ctx);
        if(ctx.r2!=gpr(-1)) throw std::runtime_error("empty ROM read returned success");
        std::cout << "PASS: three PI entrypoints, big-endian words, device/RDRAM aliases, final word, alignment, bounds, and empty ROM\n";
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

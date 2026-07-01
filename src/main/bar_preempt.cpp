// bar_preempt.cpp — cooperative preemption for the audio producer thread.
//
// THE PROBLEM: ultramodern runs N64 threads cooperatively with NO preemption — a thread holds the run
// token until it does an os message op (osRecvMesg/osSendMesg) or blocks. So a long stretch of straight-line
// game-thread compute (a heavy physics/AI/collision/gfx-list frame) never yields, and the higher-priority
// audio-manager thread (which only runs when the game thread yields) is starved: no new PCM is produced and
// the host SDL audio queue drains to empty -> underrun crackle. On real N64 the audio thread is preemptive
// (priority + timer interrupts) so this never happens.
//
// THE FIX (cooperative preemption, NOT forcible OS preemption — see docs): a host timer raises a global
// "should-yield" flag every few ms; a one-line poll injected at every recompiled-function prologue (by
// scripts/fix-recompiled.sh rule F) consumes the flag and calls ultramodern's yield_self_1ms(), letting the
// audio manager run. Because the yield only ever fires at a point the game thread *chose* (a function entry),
// exactly one N64 thread is ever on the token — the single-writer-to-RDRAM determinism is preserved by
// construction. This is the general form of the targeted MIDI-spin yield (fix-recompiled.sh rule E).
//
// We deliberately do NOT use SuspendThread-style forcible preemption: it would resume the scheduler while the
// game thread is mid-mutation of the (lockless) running_queue, can deadlock on the CRT/heap lock, and can tear
// RDRAM message-queue writes — all of which corrupt game state.
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>   // R6 bisect: std::getenv(BAR_NO_PREEMPT)

#include <ultramodern/ultramodern.hpp>   // is_game_thread, is_game_started, set_native_thread_name

static std::atomic<bool> g_should_yield{false};
static std::thread       g_preempt_thread;
static std::atomic<bool> g_preempt_run{true};

// Called from every recompiled-function prologue (rule F). Returns 1 at most once per timer tick, and only on
// a game thread once the game is running, so the injected caller then does yield_self_1ms(rdram). The hot path
// is a single relaxed load + predicted-not-taken branch (the flag is clear ~99.99% of calls); the thread-id
// gate and the consuming exchange run only on the ~500 Hz set-path.
// Audio manager / scheduler N64 thread priorities to never yield ON (yielding on them would stall their
// own work — e.g. the audio manager could never finish a sequencing pass, which silences audio). Only the
// low-priority App thread (pri ~10), the one running the heavy straight-line compute, should yield.
static constexpr int kBarMaxPreemptPriority = 100;   // audio mgr ~110, scheduler 127 are above this

extern "C" int bar_consume_yield(uint8_t* rdram) {
    static const bool bar_no_preempt = std::getenv("BAR_NO_PREEMPT") != nullptr;  // R6 bisect: BAR_NO_PREEMPT=1 disables cooperative-preempt
    if (bar_no_preempt) return 0;
    if (!g_should_yield.load(std::memory_order_relaxed)) return 0;   // hot path: one relaxed load
    if (!ultramodern::is_game_thread())  return 0;                   // never yield off game threads (RSP task / gfx)
    if (!ultramodern::is_game_started()) return 0;
    if (ultramodern::this_thread_priority(rdram) >= kBarMaxPreemptPriority) return 0;  // only the App thread, never audio/sched
    bool expected = true;
    return g_should_yield.compare_exchange_strong(expected, false) ? 1 : 0;  // one yield per tick
}

// Raise the yield flag at ~500 Hz. 2 ms is the single tuning knob: raise toward 4 ms if a frame regresses,
// lower toward 1 ms if audio stays tight at a low buffer. A dedicated thread (not the 60 Hz VI thread) is
// required — 60 Hz is too coarse to keep a ~1 VI audio buffer fed across a multi-VI compute frame.
void bar_start_preempt_timer() {
    g_preempt_run.store(true, std::memory_order_relaxed);
    g_preempt_thread = std::thread([] {
        ultramodern::set_native_thread_name("BAR Preempt");
        while (g_preempt_run.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            g_should_yield.store(true, std::memory_order_relaxed);
        }
    });
}

void bar_stop_preempt_timer() {
    g_preempt_run.store(false, std::memory_order_relaxed);
    if (g_preempt_thread.joinable()) g_preempt_thread.join();
}

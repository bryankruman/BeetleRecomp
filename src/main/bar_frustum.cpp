// src/main/bar_frustum.cpp — draw distance, and the frustum BAR culls against.
//
// BAR builds every perspective projection in exactly one function, uvfmtx_rom's glFrustum:
//
//     func_uvfmtx_rom_00401F74(Mtx4F *dst, float left, float right, float top, float bottom,
//                              float near, float far)
//
// and its caller, uvchannel_rom's case 4, stores those same six values in the camera channel
// (unkDC..unkF0) and then calls func_uvchannel_rom_00401658, which derives the SIX CULLING PLANES
// from them. func_uvchannel_rom_004014E8 is the sphere-vs-frustum test the game asks before
// submitting anything. So one set of six numbers decides both what the projection draws and what the
// game bothers to submit — changing the matrix alone would move the far plane while the game went on
// culling everything past the old one, and nothing would appear.
//
// Measured in a race with BAR_DBG_FRUSTUM=1, two projections are built per frame:
//
//   dst=0x80099E1C  l/r = +-0.7673  t/b = +-0.5711  near = 1  far = 300     the racing camera
//   dst=0x80025920  same extents                    near = 1  far = 27000   a second, static matrix
//
// So the racing camera's draw distance is `far = 300`, and 0.7673/0.5711 = 1.343, i.e. the game's
// own frustum is 4:3 — which is the other half of this file's job, because in widescreen RT64 draws
// a wider view than that while the game keeps culling to it.
//
// Reaching the channel without a module data symbol. The channel struct is allocated on the heap by
// the relocatable uvchannel module, so its address cannot be baked in (this is the same constraint
// documented in patches/viewport_patch.c). It does not have to be: `dst` IS `&channel->unk4`, and
// unk4 is at offset 4, so the channel is `dst - 4`. Before writing anything there, all six stored
// fields are checked against the six arguments -- if the memory does not hold exactly what we were
// passed, this is not a channel and it is left alone. That guard is what keeps the second, non-channel
// projection at 0x80025920 safe.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "recomp.h"

namespace {

// Offsets into the channel struct (uvchannel_rom.c). unk4 is the projection matrix the caller hands
// us as `dst`; unkDC..unkF0 are left, right, top, bottom, near, far.
constexpr int32_t kChanUnk4 = 0x04;
constexpr int32_t kChanLeft = 0xDC;
constexpr int32_t kChanRight = 0xE0;
constexpr int32_t kChanTop = 0xE4;
constexpr int32_t kChanBottom = 0xE8;
constexpr int32_t kChanNear = 0xEC;
constexpr int32_t kChanFar = 0xF0;

// A projection this deep is not the racing camera -- the measured second matrix is at far = 27000 --
// and multiplying it would only cost depth precision. Anything at or beyond this is left alone.
constexpr float kSkyFarMin = 5000.0f;

float asFloat(unsigned bits) {
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

unsigned asBits(float v) {
    unsigned bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

float envScale(const char *name, float fallback, float maxValue) {
    const char *e = std::getenv(name);
    float parsed = 0.0f;
    if ((e != nullptr) && (std::sscanf(e, "%f", &parsed) == 1) && (parsed > 0.0f) && (parsed <= maxValue)) {
        return parsed;
    }
    return fallback;
}

// Set from the frontend's Graphics tab (bar_set_draw_distance). Zero means nothing has been pushed
// yet -- during boot, or in the no-frontend build, which has no settings menu at all.
std::atomic<float> gDrawDistanceSetting{ 0.0f };
std::atomic<float> gCullScale{ 1.0f };

// How far to push the far plane, as a multiple of the game's own.
//
// BAR_DRAW_DIST wins when it is set, because it is the debugging override and needs to be able to
// pin a value while the menu is being changed; otherwise the menu's Draw Distance setting decides,
// and 1x is the fallback until one arrives.
// Hard ceiling on the far-plane multiplier, applied to the MENU value only.
//
// Extending the far plane is what makes the game submit objects past its design range, and past some
// fixed capacity it drops them -- including the player's car (docs/KNOWN_ISSUES.md). Until that is
// fixed at its source, the safe multiplier is the game's own.
//
// This is capped HERE, in code, rather than only defaulted in the frontend, because a default cannot
// reach an install that already has a graphics.json -- and that file is rewritten by the frontend from
// its own state, so editing it by hand does not stick either. Both of those were tried and both failed
// to take effect. Code is the only place a saved value cannot get past.
//
// A PER-COURSE ceiling, rather than one global cap.
//
// Wicked Woods drops dynamic objects -- the player's own car, and the trees near it -- on its big jump
// once the far plane is extended too far. Bisected with Daniel on that exact jump:
//
//     1.05x   clean
//     1.5x    clean, and the extra scenery is visible
//     1.75x   the car vanishes on the approach to landing, but survives the rest of the jump
//     2x, 4x  the car is gone for most of the time it is airborne
//
// The failure is **progressive**, not a cliff: raising the far plane makes the car vanish earlier in
// the jump rather than suddenly. 1.5x is therefore a full step below the first observed failure rather
// than a value balanced on the edge of one.
//
// The root cause is NOT known. docs/KNOWN_ISSUES.md lists nine refuted hypotheses, including every
// per-frame pool -- all of which were measured WITH HEADROOM at the moment of failure (vertices
// 232/1000, matrices 103/200, display list 1586/7500). The leading untested lead is uvsort_rom's depth
// buckets (4 x 50): a full bucket is a silent no-op that drops the object BEFORE it emits any geometry,
// which is precisely why no demand counter could ever see it -- a refused object stops consuming.
//
// Only Wicked Woods is capped, because only Wicked Woods has been shown to fail. The other courses keep
// the full range the menu offers; they have NOT been tested at 2x or 4x, and if one of them turns out
// to drop objects too, it gets added here.
// MEASURED on the course with BAR_DBG_FRUSTUM=1, not inferred: `currentTrack` reads **6** during a
// Wicked Woods race. The field is 1-based (Coventry 1 .. Wicked Woods 6), which matches gNumOpenTracks
// = 6 covering Coventry through Wicked Woods.
//
// The first version of this cap used 5, taken from the zero-based order of the course list in
// lib/bar-decomp/tools/checkModuleHash.py -- a hashing helper, not a runtime source. The cap compiled,
// the logic was right and it silently never fired. **Read an index off the running game before keying
// behaviour on it**; a list in a tools script is not evidence of what the game stores.
constexpr int32_t kTrackWickedWoods = 6;
constexpr float kWickedWoodsMaxFarScale = 1.5f;

// gGameSettings + 0x44 -- `/* 0x0044 */ s32 currentTrack;` (lib/bar-decomp/include/structs.h:87).
// Same sign-extended addressing os_unimpl_stubs.cpp uses for this struct.
constexpr uint32_t kCurrentTrackAddr = 0x80025D34u;

float courseCappedScale(uint8_t *rdram, float scale) {
    if (scale <= kWickedWoodsMaxFarScale) {
        return scale;
    }
    if (MEM_W(0, (int64_t)(int32_t)kCurrentTrackAddr) == kTrackWickedWoods) {
        return kWickedWoodsMaxFarScale;
    }
    return scale;
}

float drawDistanceScale(uint8_t *rdram) {
    // BAR_DRAW_DIST is the debugging override and deliberately bypasses the per-course cap -- pinning a
    // value above it is exactly how the ceiling was bisected, and how it gets re-tested if the root
    // cause is ever found.
    static const char *const envSet = std::getenv("BAR_DRAW_DIST");
    if (envSet != nullptr) {
        static const float scale = envScale("BAR_DRAW_DIST", 4.0f, 64.0f);
        return scale;
    }

    // 1x until a setting arrives. This used to be 4x, which meant the no-frontend build and every
    // frame before the menu loads extended the far plane -- and extending it is what drops objects.
    const float fromMenu = gDrawDistanceSetting.load(std::memory_order_relaxed);
    const float wanted = (fromMenu > 0.0f) ? fromMenu : 1.0f;
    return courseCappedScale(rdram, wanted);
}

// How much wider than its own 4:3 frustum the game should be willing to CULL against.
//
// This is the widescreen half. RT64's Expand draws a wider view than the game's frustum describes,
// but the game goes on testing objects against its own 4:3 planes, so anything that is only visible
// in the widened margins is never submitted -- it pops in as the camera turns towards it. Widening
// the planes fixes that at the source, in the game's own culling.
//
// It is deliberately NOT applied to the projection matrix, only to the planes: RT64 already widens
// the drawn view, and widening it here as well would stack and double-widen it. So the game draws
// the same 4:3 projection it always did (which RT64 then expands) while agreeing to submit what
// falls in the wider view.
//
// RT64 publishes the actual frame's widening, including Original and window resize.
// A fixed 21:9 factor submitted excess scenery at 4:3 and excluded visible ultrawide scenery.
// BAR_CULL_WIDEN remains an explicit diagnostic override.
float cullWidenScale() {
    static const bool overridden = std::getenv("BAR_CULL_WIDEN") != nullptr;
    static const float overrideScale = envScale("BAR_CULL_WIDEN", 1.0f, 8.0f);
    return overridden ? overrideScale : gCullScale.load(std::memory_order_relaxed);
}

bool traceEnabled() {
    static const bool dbg = std::getenv("BAR_DBG_FRUSTUM") != nullptr;
    return dbg;
}

// BAR_FAR_CHANNEL=0 -- diagnostic only, NOT a shipping option.
//
// The scaled far plane is written to two places, and they do two different jobs. The one in the
// projection MATRIX only widens the depth range that gets drawn. The one in the CHANNEL is what moves
// the game's far culling plane, and so what makes the game submit objects it would otherwise have
// skipped -- out to 4x its design range at the default setting.
//
// Setting this to 0 keeps the matrix write and drops the channel write, which separates "the extra
// depth range" from "the extra submitted objects" in a single run. Draw distance stops working while
// it is off (the game culls at its own 300 while the matrix draws to 1200), which is precisely why
// this is a probe and not a setting.
bool farReachesChannel() {
    static const bool on = [] {
        const char *e = std::getenv("BAR_FAR_CHANNEL");
        return (e == nullptr) || (std::strcmp(e, "0") != 0);
    }();
    return on;
}

// Widening the channel's stored left/right is not confined to the culling planes: the game builds a
// SECOND projection from the same fields on a later frame (measured at dst=0x80025920, near=1,
// far=27000 -- the distant/sky matrix), and it picked up the widened sides, rendering that layer at
// aspect 2.35 against the main view's 1.34.
//
// So remember the exact bit patterns written, and whenever a projection is built from them, hand the
// originals back for the matrix. Matching on the bits is exact -- a value we did not write cannot
// collide with one we did -- so no projection is narrowed by accident. The culling planes still see
// the widened values, which is the whole point.
struct WidenedPair {
    unsigned widenedLeft, widenedRight, origLeft, origRight;
};
constexpr int kWidenedSlots = 8;
WidenedPair gWidened[kWidenedSlots] = {};
int gWidenedNext = 0;

void rememberWidened(unsigned wl, unsigned wr, unsigned ol, unsigned orr) {
    for (int i = 0; i < kWidenedSlots; i++) {
        if ((gWidened[i].widenedLeft == wl) && (gWidened[i].widenedRight == wr)) {
            return;
        }
    }
    gWidened[gWidenedNext] = { wl, wr, ol, orr };
    gWidenedNext = (gWidenedNext + 1) % kWidenedSlots;
}

bool restoreWidened(unsigned *l, unsigned *r) {
    for (int i = 0; i < kWidenedSlots; i++) {
        if ((gWidened[i].widenedLeft == *l) && (gWidened[i].widenedRight == *r) &&
            (gWidened[i].widenedLeft != gWidened[i].origLeft)) {
            *l = gWidened[i].origLeft;
            *r = gWidened[i].origRight;
            return true;
        }
    }
    return false;
}

} // namespace

extern "C" void bar_set_cull_scale(float scale) {
    if (scale >= 1.0f && scale <= 8.0f) {
        gCullScale.store(scale, std::memory_order_relaxed);
    }
}

// Pushed by the frontend once the settings menu has loaded, and again whenever it changes. Values
// outside the sane range are ignored rather than clamped, so a bad push cannot quietly halve the
// draw distance -- the last good value simply stays.
extern "C" void bar_set_draw_distance(float scale) {
    if ((scale > 0.0f) && (scale <= 64.0f)) {
        const float previous = gDrawDistanceSetting.exchange(scale, std::memory_order_relaxed);
        if (traceEnabled() && (previous != scale)) {
            std::fprintf(stderr, "[frustum] Draw Distance setting -> x%.2f\n", scale);
            std::fflush(stderr);
        }
    }
}

// Called from the top of the recompiled func_uvfmtx_rom_00401F74, before it reads its arguments.
// The six values are passed by pointer so they can be rewritten in place; the caller writes them
// back into the registers and stack slots the function is about to load them from.
extern "C" void bar_frustum_adjust(uint8_t *rdram, unsigned dst, unsigned *l, unsigned *r,
                                   unsigned *t, unsigned *b, unsigned *n, unsigned *f) {
    // If this projection is being built from sides we widened for culling, build it from the
    // originals instead -- see the note on WidenedPair.
    restoreWidened(l, r);

    // Which course the game thinks it is on, reported whenever it changes. The per-course cap keys on
    // this, and the index it compares against was originally TAKEN FROM A TOOLS SCRIPT'S COURSE LIST
    // rather than measured -- which is exactly why the cap failed to engage on Wicked Woods. Read the
    // number here, on the course, instead of inferring it.
    if (traceEnabled()) {
        static int32_t lastTrack = -0x7FFFFFFF;
        const int32_t track = MEM_W(0, (int64_t)(int32_t)kCurrentTrackAddr);
        if (track != lastTrack) {
            lastTrack = track;
            std::fprintf(stderr, "[frustum] currentTrack -> %d\n", track);
            std::fflush(stderr);
        }
    }

    const float left = asFloat(*l);
    const float right = asFloat(*r);
    const float nearZ = asFloat(*n);
    const float farZ = asFloat(*f);

    if (traceEnabled()) {
        static unsigned last[7] = { 1, 1, 1, 1, 1, 1, 1 };
        const unsigned cur[7] = { dst, *l, *r, *t, *b, *n, *f };
        if (std::memcmp(last, cur, sizeof(cur)) != 0) {
            std::memcpy(last, cur, sizeof(cur));
            std::fprintf(stderr,
                         "[frustum] dst=%08X l=%.4f r=%.4f t=%.4f b=%.4f near=%.4f far=%.4f (aspect=%.3f)\n",
                         dst, left, right, asFloat(*t), asFloat(*b), nearZ, farZ,
                         (asFloat(*b) != asFloat(*t)) ? ((right - left) / (asFloat(*b) - asFloat(*t))) : 0.0f);
            std::fflush(stderr);
        }
    }

    const float distScale = drawDistanceScale(rdram);
    const float widenScale = cullWidenScale();
    const bool wantDistance = (distScale != 1.0f) && (farZ > 0.0f) && (farZ < kSkyFarMin) && (nearZ > 0.0f);
    const bool wantWiden = (widenScale != 1.0f) && (right > left);
    if (!wantDistance && !wantWiden) {
        return;
    }

    const float newFar = wantDistance ? (farZ * distScale) : farZ;

    // The far plane goes into the projection matrix this call is about to build, so the extra
    // distance is actually drawn. The widened sides deliberately do not -- see cullWidenScale().
    if (wantDistance) {
        *f = asBits(newFar);
    }

    // Both go into the channel, which is what func_uvchannel_rom_00401658 derives the culling planes
    // from a moment later. Only when the memory really is a channel holding exactly these six values.
    const int64_t chan = (int64_t)(int32_t)(dst - kChanUnk4);
    const bool isChannel = dst >= 0x80000004u && dst <= 0x80800000u - kChanFar &&
        ((unsigned)MEM_W(kChanLeft, chan) == *l) && ((unsigned)MEM_W(kChanRight, chan) == *r) &&
        ((unsigned)MEM_W(kChanTop, chan) == *t) && ((unsigned)MEM_W(kChanBottom, chan) == *b) &&
        ((unsigned)MEM_W(kChanNear, chan) == *n) && ((unsigned)MEM_W(kChanFar, chan) == asBits(farZ));
    if (isChannel) {
        if (wantDistance && farReachesChannel()) {
            MEM_W(kChanFar, chan) = (int32_t)asBits(newFar);
        }
        if (wantWiden) {
            const float centre = (left + right) * 0.5f;
            const float halfWidth = (right - left) * 0.5f * widenScale;
            const unsigned widenedLeft = asBits(centre - halfWidth);
            const unsigned widenedRight = asBits(centre + halfWidth);
            MEM_W(kChanLeft, chan) = (int32_t)widenedLeft;
            MEM_W(kChanRight, chan) = (int32_t)widenedRight;
            rememberWidened(widenedLeft, widenedRight, *l, *r);
        }
    }

    if (traceEnabled()) {
        // Re-announce whenever the APPLIED scale changes, not just once: the per-course cap means the
        // same menu setting yields different scales on different courses, and a one-shot line cannot
        // show that. This is what verifies the cap engaging on Wicked Woods and lifting elsewhere.
        static float lastAnnounced = -1.0f;
        if (lastAnnounced != distScale) {
            lastAnnounced = distScale;
            std::fprintf(stderr,
                         "[frustum] draw distance x%.2f (far %.1f -> %.1f), cull widen x%.2f; channel %s\n",
                         distScale, farZ, newFar, widenScale,
                         isChannel ? "found, planes follow" : "NOT found, planes unchanged");
            std::fflush(stderr);
        }
    }
}

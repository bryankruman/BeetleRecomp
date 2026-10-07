// Make BAR draw into the whole framebuffer instead of its overscan-safe inset.
//
// BAR sets its 3D viewport and scissor through uvGfxClipRect (uvgfxmgr_rom.c). On real hardware the
// game deliberately kept a margin for CRT overscan: racing is drawn into x 22..297 of 320 and
// y 17..224 of 240 (measured from the scissor rectangles the game emits -- see BAR_DBG_SCISSOR in
// the rt64 fork). A television bezel hid that margin. A monitor shows every pixel, so it appears as
// black bars on all four sides, and it has caused two further problems in the renderer:
//
//   1. RT64 only widens a projection for widescreen if it reaches the framebuffer's edges, so the
//      inset viewport was never widened and Expand did nothing during a race.
//   2. Once that check was relaxed, the widened 3D drew OUTSIDE the game's own margin fills, which
//      stay at their 4:3 positions -- the "columns" seen in widescreen captures.
//
// Fixing it here, at the source, removes the cause of all three rather than compensating for each in
// the renderer. The body below is the original function verbatim; the only change is that the four
// known overscan edges are snapped to the true screen edges before the maths runs. Interior edges
// (a split-screen boundary, the cinematic's letterbox) are left exactly as the game asked for, so
// two-player viewports and the intro's letterbox keep their proportions.
//
// Deliberately no data symbols: this module is relocatable at runtime (its recompiled code reaches
// sScreenWidth through a section-relative relocation), so a patch baking absolute addresses could
// read or write the wrong memory. Screen size comes from the module's own exported getters instead,
// and the four "last rectangle" globals the original wrote are left alone -- the caller,
// func_uvgfxmgr_rom_00401BD4, invokes func_uvgfxmgr_rom_00401C5C straight after this, which stores
// the same values from vp->x0..y1 anyway.
#include "patch_helpers.h"

#ifndef RECOMP_PATCH
#  define RECOMP_PATCH  __attribute__((section(".recomp_patch")))
#endif

// From uvgfxmgr_rom.c; the typedef lives in the .c file rather than a header.
typedef struct uvGfxViewport_s {
    /* 0x0 */ s16 unk0;
    /* 0x2 */ s16 unk2;
    /* 0x4 */ s16 unk4;
    /* 0x6 */ s16 unk6;
    /* 0x8 */ s16 x0;
    /* 0xA */ s16 x1;
    /* 0xC */ s16 y0;
    /* 0xE */ s16 y1;
    /* 010 */ Vp vp;
} uvGfxViewport;

extern s32 uvGetScreenWidth(void);
extern s32 uvGetScreenHeight(void);

// The inset BAR uses on a 320x240 screen. Only snapped when the screen really is 320x240, so a
// different video mode is left untouched rather than mis-snapped.
//
// Y is BOTTOM-UP in this API: func_uvgfxmgr_rom_00401C5C emits the scissor as
// (x0, sScreenHeight - y1, x1, sScreenHeight - y0), so the measured scissor rows 17..224 correspond
// to y0 = 240 - 224 = 16 and y1 = 240 - 17 = 223. The first version of this patch used the scissor
// rows directly and the vertical snap silently never matched.
#define BAR_INSET_X0 22
#define BAR_INSET_X1 297
#define BAR_INSET_Y0 16
#define BAR_INSET_Y1 223

RECOMP_PATCH void uvGfxClipRect(uvGfxViewport *vp, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
    s32 sScreenWidth = uvGetScreenWidth();
    s32 sScreenHeight = uvGetScreenHeight();
    s32 var_a2;
    s32 var_a3;

    if ((sScreenWidth == 320) && (sScreenHeight == 240)) {
        if (arg1 == BAR_INSET_X0) arg1 = 0;
        if (arg2 == BAR_INSET_X1) arg2 = sScreenWidth;
        if (arg3 == BAR_INSET_Y0) arg3 = 0;
        if (arg4 == BAR_INSET_Y1) arg4 = sScreenHeight;
    }

    // --- original body from here on ---
    vp->x0 = arg1;
    vp->x1 = arg2;
    vp->y0 = arg3;
    vp->y1 = arg4;
    if (vp->x0 < 0) {
        vp->x0 = 0;
    } else if (vp->x0 > sScreenWidth) {
        vp->x0 = sScreenWidth;
    }

    if (vp->x1 < 0) {
        vp->x1 = 0;
    } else if (vp->x1 > sScreenWidth) {
        vp->x1 = sScreenWidth;
    }

    if (vp->y1 < 0) {
        vp->y1 = 0;
    } else if (vp->y1 > sScreenHeight) {
        vp->y1 = sScreenHeight;
    }

    if (vp->y0 < 0) {
        vp->y0 = 0;
    } else if (vp->y0 > sScreenHeight) {
        vp->y0 = sScreenHeight;
    }

    vp->unk0 = vp->x0;
    if (vp->unk0 < 0) {
        vp->unk0 = 0;
    }
    vp->unk2 = vp->x1;
    if (vp->unk2 > sScreenWidth - 1) {
        vp->unk2 = sScreenWidth - 1;
    }
    vp->unk4 = vp->y0;
    if (vp->unk4 < 0) {
        vp->unk4 = 0;
    }
    vp->unk6 = vp->y1;
    if (vp->unk6 > sScreenHeight - 1) {
        vp->unk6 = sScreenHeight - 1;
    }

    var_a2 = vp->unk2 - vp->unk0;
    var_a3 = vp->unk6 - vp->unk4;

    vp->vp.vp.vscale[0] = (var_a2 << 1);
    vp->vp.vp.vscale[1] = (var_a3 << 1);
    vp->vp.vp.vscale[2] = 0x1FF;
    vp->vp.vp.vscale[3] = 0;
    vp->vp.vp.vtrans[0] = (u16) ((vp->unk0 + (var_a2 >> 1)) & 0xFFFF) << 2;
    vp->vp.vp.vtrans[1] = (u16) (((sScreenHeight - vp->unk4) - (var_a3 >> 1)) & 0xFFFF) << 2;
    vp->vp.vp.vtrans[2] = 0x1FF;
    vp->vp.vp.vtrans[3] = 0;
    // D_uvgfxmgr_rom_00402408..0E deliberately not written here; see the header comment.
}

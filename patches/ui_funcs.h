#ifndef BAR_UI_FUNCS_H
#define BAR_UI_FUNCS_H

// Mod-facing recompui API surface for this project.
//
// WHY THIS FILE EXISTS: recompui hard-codes `#include "../../../../../patches/ui_funcs.h"` in
// src/api/ui_api_events.cpp — a relative path that deliberately escapes the library and lands in the
// CONSUMING project's patches/ directory (its own comment there reads "TODO: Forced game includes").
// So every project embedding RecompFrontend must provide this header, or recompui will not compile.
//
// It supplies the C-ABI event types the UI API marshals into guest memory: ui_api_events.cpp does
//     ctx->r29 -= sizeof(RecompuiEventData);
//     RecompuiEventData* event_data = TO_PTR(RecompuiEventData, stack_frame);
// to hand an event to recompiled MIPS code. The layout is therefore a real ABI shared between the
// host and any future mod compiled for this game — it must not be redefined independently here, or
// the two sides would silently disagree about field offsets.
//
// RecompFrontend already defines exactly those types in recompui/include/recompui/event_structs.h,
// so this header just re-exports them and keeps a single source of truth. The include is written
// relative to this file rather than relying on an include directory, because this header is also
// intended to be reachable from the MIPS patch toolchain (patches/Makefile), which does not carry
// recompui's include paths.
//
// When mod support is taken on (out of scope for the beta), the guest-callable `recomp_` UI
// entry points belong here alongside these types.

#include "../lib/RecompFrontend/recompui/include/recompui/event_structs.h"

#endif // BAR_UI_FUNCS_H

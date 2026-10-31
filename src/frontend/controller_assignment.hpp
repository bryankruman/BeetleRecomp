#pragma once

#include <array>
#include <string>
#include <vector>

namespace bar::frontend::controller_assignment {

struct Slot {
    std::string controller;
    bool keyboard = false;
    bool reserved = false;
    bool share_keyboard = false;
    bool operator==(const Slot&) const = default;
};
using Slots = std::array<Slot, 4>;

struct Device {
    std::string key;
    std::string guid;
    std::string uid;
};
struct LegacyDevice {
    std::string guid;
    std::string uid;
};

inline bool has_keyboard(bool assigned, bool enabled, int profile) {
    return assigned && (enabled || profile >= 0);
}

// Re-evaluate legacy GUID-only reservations on every device change. An absent
// UID is deliberately a GUID fallback, never a fabricated serial or ordinal.
inline void resolve_legacy(Slots& preferred, const std::array<LegacyDevice, 4>& legacy,
                           const std::vector<Device>& connected) {
    std::vector<bool> used(connected.size());
    for (size_t port = 0; port < preferred.size(); ++port) {
        if (legacy[port].guid.empty()) continue;
        preferred[port].controller.clear();
        for (size_t i = 0; i < connected.size(); ++i) {
            const auto& device = connected[i];
            if (used[i] || device.guid != legacy[port].guid ||
                (!legacy[port].uid.empty() && device.uid != legacy[port].uid)) continue;
            preferred[port].controller = device.key;
            used[i] = true;
            break;
        }
    }
    // A reservation wins over an automatically filled free slot when its pad
    // reconnects. A physical controller must never appear on two ports.
    for (size_t port = 0; port < preferred.size(); ++port) {
        if (!legacy[port].guid.empty()) continue;
        for (size_t i = 0; i < connected.size(); ++i) {
            if (used[i] && preferred[port].controller == connected[i].key) {
                preferred[port].controller.clear();
            }
        }
    }
}


// Controllers come first: with any pad connected, nobody is given the keyboard automatically; it is
// added deliberately in Assign players. With no pad, the keyboard is player one -- shared during
// automatic discovery, so a pad plugged in later merges with it, or a port of its own for the
// explicit Auto-configure action.
inline Slots automatic(const std::vector<std::string>& connected, bool separate_keyboard) {
    Slots slots{};
    for (size_t port = 0; port < slots.size() && port < connected.size(); ++port) {
        slots[port].controller = connected[port];
    }
    if (connected.empty()) {
        slots[0].keyboard = true;
        slots[0].share_keyboard = !separate_keyboard;
    }
    return slots;
}

// Keep connected pads in their existing ports. Imported/manual reservations
// survive unplug; automatic slots may be reused by a replacement controller.
inline Slots reconcile(Slots& preferred, const std::vector<std::string>& connected) {
    const auto present = [&](const std::string& key) {
        for (const auto& device : connected) if (device == key) return true;
        return false;
    };
    Slots result = preferred;
    for (size_t i = 0; i < preferred.size(); ++i) {
        if (!preferred[i].controller.empty() && !present(preferred[i].controller)) {
            result[i].controller.clear();
            if (!preferred[i].reserved) preferred[i].controller.clear();
        }
    }
    for (const auto& device : connected) {
        bool assigned = false;
        for (const auto& slot : result) assigned |= slot.controller == device;
        if (assigned) continue;
        for (size_t i = 0; i < result.size(); ++i) {
            if (preferred[i].reserved || !result[i].controller.empty() ||
                (result[i].keyboard && !preferred[i].share_keyboard)) continue;
            result[i].controller = preferred[i].controller = device;
            break;
        }
    }
    return result;
}

} // namespace bar::frontend::controller_assignment

#define SDL_MAIN_HANDLED
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "frontend/controller_defaults.hpp"
#include "frontend/controller_assignment.hpp"

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        using namespace recompinput;
        bar::frontend::register_controller_defaults();
        const auto mapping = [](GameInput input) -> const std::vector<InputField>& {
            return get_default_mapping_for_input(InputDevice::Controller, input);
        };
        require(mapping(GameInput::B) == std::vector{InputField::controller_digital(SDL_CONTROLLER_BUTTON_B)}, "BAR B must use the east face button");
        require(mapping(GameInput::R) == std::vector{InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)}, "BAR R must use the right shoulder");
        require(mapping(GameInput::C_DOWN) == std::vector{InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTY, true)}, "right shoulder must not also press C-down");
        require(mapping(GameInput::Z) == std::vector{InputField::controller_analog(SDL_CONTROLLER_AXIS_TRIGGERLEFT, true)}, "BAR Z trigger changed");
        require(mapping(GameInput::X_AXIS_NEG) == std::vector{InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTX, false)}, "steering direction changed");
        require(mapping(GameInput::TOGGLE_MENU) == std::vector{InputField::controller_digital(SDL_CONTROLLER_BUTTON_BACK)}, "menu defaults were overwritten");
        require(mapping(GameInput::APPLY_MENU).size() == 2, "menu alternate binding was lost");

        using namespace bar::frontend::controller_assignment;
        auto preferred = automatic({}, false);
        auto slots = reconcile(preferred, {});
        require(slots[0].keyboard && slots[0].controller.empty(), "keyboard-only first launch is inactive");
        slots = reconcile(preferred, {"pad-A"});
        require(slots[0].controller == "pad-A" && slots[0].keyboard, "hotplug must merge the first pad with P1 keys");
        slots = reconcile(preferred, {"pad-A", "pad-B"});
        require(slots[1].controller == "pad-B", "second pad must get P2");
        slots = reconcile(preferred, {"pad-B"});
        require(slots[1].controller == "pad-B" && slots[0].controller.empty(), "unplug must not move the surviving pad");
        slots = reconcile(preferred, {"pad-B", "replacement"});
        require(slots[0].controller == "replacement" && slots[1].controller == "pad-B", "replacement must fill free automatic port");

        preferred = automatic({"pad-A", "pad-B"}, true);
        slots = reconcile(preferred, {"pad-A", "pad-B"});
        require(slots[0].controller == "pad-A" && !slots[0].keyboard && slots[2].keyboard, "explicit auto-configure must give keyboard its own next port");
        slots = reconcile(preferred, {"pad-A", "pad-B", "pad-C"});
        require(slots[2].keyboard && slots[2].controller.empty() && slots[3].controller == "pad-C", "new pad stole the dedicated keyboard port");
        preferred = automatic({"A", "B", "C", "D", "E"}, true);
        slots = reconcile(preferred, {"A", "B", "C", "D", "E"});
        require(slots[3].controller == "D" && !slots[3].keyboard, "four-controller limit changed");

        preferred = {};
        preferred[2].controller = "imported";
        preferred[2].reserved = true;
        preferred[0].keyboard = true;
        slots = reconcile(preferred, {"other"});
        require(slots[2].controller.empty() && slots[0].keyboard && slots[1].controller == "other", "unplugged imported controller lost its reserved slot");
        slots = reconcile(preferred, {"other", "imported"});
        require(slots[2].controller == "imported", "replug did not recover imported port");

        preferred = {};
        for (auto& slot : preferred) slot.reserved = true;
        preferred[2].controller = "manual";
        slots = reconcile(preferred, {"manual", "new"});
        require(slots[0].controller.empty() && !slots[0].keyboard && slots[2].controller == "manual" && slots[3].controller.empty(), "hotplug overwrote deliberate manual slots or gaps");
        slots = reconcile(preferred, {});
        require(slots[2].controller.empty() && preferred[2].controller == "manual", "manual reservation erased on unplug");
        slots = reconcile(preferred, {"manual"});
        require(slots[2].controller == "manual", "manual pad replug changed port");
        require(has_keyboard(true, false, 3), "manual P1 keyboard profile was ignored");
        require(!has_keyboard(false, false, 0), "unassigned stale profile created a keyboard player");
        require(!has_keyboard(true, false, -1), "controller-only slot acquired keys");
        preferred = {};
        preferred[2].reserved = true;
        std::array<LegacyDevice, 4> legacy{};
        legacy[2].guid = "model-GUID";
        resolve_legacy(preferred, legacy, {});
        require(preferred[2].controller.empty(), "unplugged legacy pad invented an identity");
        const std::vector<Device> real_serial{{"real-key", "model-GUID", "model-GUID!real-serial"}};
        resolve_legacy(preferred, legacy, real_serial);
        slots = reconcile(preferred, {"real-key"});
        require(slots[2].controller == "real-key", "GUID-only reservation failed to reclaim real-serial pad");
        legacy[1].guid = "model-GUID";
        resolve_legacy(preferred, legacy, real_serial);
        require(preferred[1].controller == "real-key" && preferred[2].controller.empty(), "same legacy pad assigned to two ports");
        legacy[1] = {};
        legacy[2].uid = "model-GUID!expected";
        resolve_legacy(preferred, legacy, real_serial);
        require(preferred[2].controller.empty(), "serial-specific legacy reservation matched a different pad");
        const std::vector<Device> expected_serial{{"expected-key", "model-GUID", "model-GUID!expected"}};
        preferred[0].controller = "expected-key";
        resolve_legacy(preferred, legacy, expected_serial);
        require(preferred[2].controller == "expected-key" && preferred[0].controller.empty(), "reclaimed reservation duplicated an automatically assigned pad");
        std::cout << "PASS: BAR defaults, menu bindings, automatic discovery, explicit keyboard seat, imported reservations and manual hotplug policy\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

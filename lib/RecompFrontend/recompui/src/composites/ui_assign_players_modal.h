#pragma once

#include "recompinput/recompinput.h"
#include "elements/ui_element.h"
#include "elements/ui_document.h"
#include "elements/ui_svg.h"
#include "elements/ui_button.h"
#include "elements/ui_label.h"
#include "ui_player_card.h"

namespace recompui {

class AssignPlayersModal : public Element {
protected:
    bool is_open = false;
    bool was_assigning = false;
    Element* player_elements_wrapper = nullptr;
    Element* fake_focus_button = nullptr;
    std::vector<PlayerCard*> player_elements = {};

    // What to do next, rewritten as the session changes.
    Label* status_label = nullptr;
    std::string status_text;
    Button* start_over_button = nullptr;
    Button* undo_button = nullptr;
    Button* cancel_button = nullptr;
    Button* add_keyboard_button = nullptr;
    Button* confirm_button = nullptr;
    // Slot picked as the first half of a swap/move, or -1.
    int selected_slot = -1;

    virtual void process_event(const Event &e) override;
    std::string_view get_type_name() override { return "AssignPlayersModal"; }
private:
    void init_pending_state();
    void update_controls();
    void on_card_pressed(int player_index);
    void create_player_elements();
    void set_fake_focus_enabled(bool enabled);
    void lock_focus(bool lock);
public:
    AssignPlayersModal(ResourceId rid, Document *parent);
    virtual ~AssignPlayersModal();
    static void init();
    // Pass false to force player assignment.
    static void open();
    static void close();
};

} // namespace recompui

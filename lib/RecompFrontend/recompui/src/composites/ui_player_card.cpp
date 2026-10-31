#include "ui_player_card.h"
#include "recompinput/recompinput.h"
#include "recompinput/profiles.h"
#include "elements/ui_label.h"
#include "ultramodern/ultramodern.hpp"

namespace recompui {

PlayerCard::PlayerCard(
    ResourceId rid,
    Element *parent,
    int player_index,
    bool is_assignment_card
) : Element(rid, parent, Events(EventType::Focus, EventType::Update, EventType::Click),
        is_assignment_card ? "button" : "div", false),
    player_index(player_index),
    is_assignment_card(is_assignment_card)
{
    const float size = is_assignment_card ? assign_player_card_size : static_player_card_size;
    const float icon_size = is_assignment_card ? assign_player_card_icon_size : static_player_card_icon_size;

    recompui::ContextId context = get_current_context();

    set_display(Display::Flex);
    set_flex_direction(FlexDirection::Column);
    set_align_items(AlignItems::FlexStart);
    set_justify_content(JustifyContent::FlexStart);
    set_width(size);
    set_height_auto();
    set_gap(8.0f);
    
    if (!is_assignment_card) {
        set_as_navigation_container(NavigationType::GridCol);
        set_padding_top(64.0f);
        set_padding_bottom(64.0f);
        auto player_label = context.create_element<Label>(this, "Player " + std::to_string(player_index + 1), LabelStyle::Small);
    }

    card = context.create_element<Element>(this, 0, "div", false);
    card->set_display(Display::Flex);
    card->set_position(Position::Relative);
    card->set_flex_direction(FlexDirection::Column);
    card->set_align_items(AlignItems::Center);
    card->set_justify_content(JustifyContent::Center);
    card->set_width(size);
    card->set_height(size);
    card->set_border_color(theme::color::BorderSoft);
    card->set_border_width(theme::border::width, Unit::Dp);
    card->set_border_radius(theme::border::radius_sm, Unit::Dp);
    card->set_background_color(theme::color::Transparent);

    if (is_assignment_card) {
        // Clicking (or accepting) a card selects it for a swap; see AssignPlayersModal.
        enable_focus();
        set_cursor(Cursor::Pointer);

        auto player_label = context.create_element<Label>(card, "P" + std::to_string(player_index + 1), LabelStyle::Annotation);
        player_label->set_position(Position::Absolute);
        player_label->set_top(8.0f);
        player_label->set_left(8.0f);
        player_label->set_color(theme::color::TextDim);

        remove_button = context.create_element<IconButton>(card, "icons/X.svg", ButtonStyle::Tertiary, IconButtonSize::Mini);
        remove_button->set_position(Position::Absolute);
        remove_button->set_top(6.0f);
        remove_button->set_right(6.0f);
        remove_button->set_display(Display::None);
        remove_button->set_debug_id("RemovePlayer" + std::to_string(player_index + 1));
        remove_button->add_pressed_callback([this]() {
            // The click also bubbles to this card; don't treat it as a selection.
            remove_pressed = true;
            recompinput::playerassignment::remove_player(this->player_index);
        });
    }

    icon = context.create_element<Svg>(card, "icons/RecordBorder.svg");
    icon->set_width(icon_size, Unit::Dp);
    icon->set_height(icon_size, Unit::Dp);
    icon->set_color(theme::color::TextDim);
    icon->set_display(Display::None);

    if (is_assignment_card) {
        caption = context.create_element<Label>(this, "", LabelStyle::Small);
        caption->set_width(100.0f, Unit::Percent);
        caption->set_text_align(TextAlign::Center);
        caption->set_color(theme::color::TextDim);
    }

    if (!is_assignment_card) {
        auto player = recompinput::players::get_player(player_index, is_assignment_card);
        update_player_card_icon();

        auto profile_label = context.create_element<Label>(this, "Profile", LabelStyle::Small);

        profile_label->set_margin_top(8.0f);

        bool has_controller = recompinput::players::get_player_input_device(player_index, is_assignment_card) == recompinput::InputDevice::Controller;
        recompinput::InputDevice device = has_controller ? recompinput::InputDevice::Controller : recompinput::InputDevice::Keyboard;
        const std::vector<int> profile_indices = recompinput::profiles::get_indices_for_custom_profiles(device);
        int cur_profile = recompinput::profiles::get_input_profile_for_player(player_index, device);

        std::vector<SelectOption> options;

        int device_profile_index = has_controller
            ? recompinput::profiles::get_controller_profile_index_from_sdl_controller(player.controller)
            : recompinput::profiles::get_or_create_mp_keyboard_profile_index(player_index);

        if (device_profile_index >= 0) {
            options.emplace_back(
                recompinput::profiles::get_input_profile_name(device_profile_index),
                std::to_string(device_profile_index)
            );
        }

        if (has_controller) {
            options.emplace_back(
                recompinput::profiles::get_input_profile_name(recompinput::profiles::get_sp_controller_profile_index()),
                std::to_string(recompinput::profiles::get_sp_controller_profile_index())
            );
        } else {
            options.emplace_back(
                recompinput::profiles::get_input_profile_name(recompinput::profiles::get_sp_keyboard_profile_index()),
                std::to_string(recompinput::profiles::get_sp_keyboard_profile_index())
            );
        }

        for (size_t i = 0; i < profile_indices.size(); ++i) {
            int profile_index = profile_indices[i];
            std::string profile_name = recompinput::profiles::get_input_profile_name(profile_index);
            options.emplace_back(profile_name, std::to_string(profile_index));
        }

        auto select = context.create_element<Select>(
            this,
            options,
            std::to_string(cur_profile)
        );

        select->add_change_callback([this](SelectOption& option, int option_index) {
            this->on_select_player_profile(std::stoi(option.value));
        });
        select->set_width(100.0f, Unit::Percent);

        bool player_is_assigned = recompinput::players::get_player_is_assigned(player_index, is_assignment_card);
        select->set_enabled(player_is_assigned);

        edit_profile_button = context.create_element<Button>(this, "Edit Profile", ButtonStyle::Secondary, ButtonSize::Medium);
        edit_profile_button->add_pressed_callback([this]() {
            this->on_edit_profile();
        });
        edit_profile_button->set_width(100.0f, Unit::Percent);
        edit_profile_button->set_enabled(player_is_assigned);
        // The context sends one update once the card is registered; process_event then re-queues it
        // every frame so the card lights up while its pad is in use. Queueing here, mid-construction,
        // is rejected because the card is not in the context yet.
    }
}

PlayerCard::~PlayerCard() {
}

void PlayerCard::update_activity_highlight() {
    bool use_temp_state = is_assignment_card && recompinput::playerassignment::is_active();
    bool active = false;
    if (recompinput::players::get_player_is_assigned(player_index, use_temp_state)) {
        active = recompinput::players::is_controller_active(recompinput::players::get_player(player_index, use_temp_state).controller);
    }
    if (active == activity_highlighted) {
        return;
    }

    activity_highlighted = active;
    // Assignment cards repaint their border and background every frame in update_assignment_player_card.
    if (!is_assignment_card) {
        card->set_border_color(active ? theme::color::SuccessL : theme::color::BorderSoft);
        if (active) {
            card->set_background_color(theme::color::SuccessA30);
        } else {
            card->set_background_color(theme::color::Transparent);
        }
    }
}

void PlayerCard::process_event(const Event &e) {
    switch (e.type) {
    case EventType::Focus: {
        bool focus_active = std::get<EventFocus>(e.variant).active;
        if (!is_assignment_card) {
            if (focus_active) {
                bool smooth_scroll = true;
                if (force_instant_scroll) {
                    force_instant_scroll = false;
                    smooth_scroll = false;
                }
                scroll_into_view(smooth_scroll);
            }
        } 
        break;
    }
    case EventType::Update:
        if (queue_focus_on_edit_profile_button) {
            queue_focus_on_edit_profile_button = false;
            if (edit_profile_button != nullptr) {
                force_instant_scroll = true;
                edit_profile_button->focus();
            }
        }
        if (!is_assignment_card) {
            update_activity_highlight();
            queue_update();
        }
        break;
    case EventType::Click:
        if (remove_pressed) {
            remove_pressed = false;
        } else if (is_assignment_card && on_pressed_callback) {
            on_pressed_callback(player_index);
        }
        break;
    default:
        break;
    }
}

void PlayerCard::on_select_player_profile(int profile_index) {
    if (this->on_select_profile_callback) {
        this->on_select_profile_callback(this->player_index, profile_index);
    }
}

void PlayerCard::on_edit_profile() {
    if (this->on_edit_profile_callback) {
        this->on_edit_profile_callback(this->player_index);
    }
}

void PlayerCard::update_player_card_icon() {
    bool use_temp_state = is_assignment_card && recompinput::playerassignment::is_active();
    if (recompinput::players::get_player_is_assigned(player_index, use_temp_state)) {
        if (recompinput::players::get_player_input_device(player_index, use_temp_state) == recompinput::InputDevice::Controller) {
            cur_icon = PlayerCardIcon::Controller;
        } else {
            cur_icon = PlayerCardIcon::Keyboard;
        }
    } else {
        if (is_assignment_card) {
            cur_icon = recompinput::playerassignment::is_player_currently_assigning(player_index)
                ? PlayerCardIcon::Recording
                : PlayerCardIcon::Waiting;
        } else {
            cur_icon = PlayerCardIcon::None;
        }
    }

    switch (cur_icon) {
        case PlayerCardIcon::None:
            icon->set_display(Display::None);
            break;
        case PlayerCardIcon::Keyboard:
            icon->set_display(Display::Block);
            icon->set_src("icons/Keyboard.svg");
            break;
        case PlayerCardIcon::Controller:
            icon->set_display(Display::Block);
            icon->set_src("icons/Cont.svg");
            break;
        case PlayerCardIcon::Waiting:
            icon->set_display(Display::Block);
            icon->set_src("icons/RecordBorder.svg");
            break;
        case PlayerCardIcon::Recording:
            icon->set_display(Display::Block);
            icon->set_src("icons/RecordSpinner.svg");
            break;
    }
}

void PlayerCard::update_assignment_player_card() {
    static const float scale_anim_duration = 0.25f;

    update_player_card_icon();
    update_activity_highlight();

    if (cur_icon == PlayerCardIcon::Recording) {
        auto millis_since_start = std::chrono::duration_cast<std::chrono::milliseconds>(ultramodern::time_since_start()).count();
        icon->set_rotation(millis_since_start * 0.001f * 360.0f / 2.0f);
    } else {
        icon->set_rotation(0.0f);
    }

    bool player_is_currently_assigning = recompinput::playerassignment::is_player_currently_assigning(player_index);

    if (player_is_currently_assigning && !was_player_assigning) {
        scroll_into_view(true);
    }
    was_player_assigning = player_is_currently_assigning;

    bool use_temp_state = is_assignment_card && recompinput::playerassignment::is_active();
    bool assigned = recompinput::players::get_player_is_assigned(player_index, use_temp_state);

    // Name the device so identical pads can be told apart (together with the live highlight).
    std::string text;
    if (assigned) {
        const auto& player = recompinput::players::get_player(player_index, use_temp_state);
        if (player.controller != nullptr) {
            const int number = recompinput::players::get_controller_number(player.controller);
            text = number > 0 ? "Pad " + std::to_string(number) : "Controller";
            if (player.keyboard_enabled) text += " + keys";
        } else {
            text = "Keyboard";
        }
    } else {
        text = player_is_currently_assigning ? "Press a button" : "Empty";
    }
    if (text != caption_text) {
        caption_text = text;
        caption->set_text(text);
    }
    remove_button->set_display(assigned && recompinput::playerassignment::is_active() ? Display::Block : Display::None);

    // Selected for a swap, then in use right now, then the slot that fills next.
    if (selected) {
        card->set_border_color(theme::color::SecondaryL);
        card->set_background_color(theme::color::SecondaryA30);
    } else if (activity_highlighted) {
        card->set_border_color(theme::color::SuccessL);
        card->set_background_color(theme::color::SuccessA30);
    } else if (player_is_currently_assigning) {
        card->set_border_color(theme::color::PrimaryL);
        card->set_background_color(theme::color::PrimaryL, 255/5);
    } else {
        card->set_border_color(theme::color::BorderSoft);
        card->set_background_color(assigned ? theme::color::PrimaryA20 : theme::color::Transparent);
    }

    if (!assigned) {
        icon->set_scale_2D(player_is_currently_assigning ? 1.1f : 1.0f, player_is_currently_assigning ? 1.1f : 1.0f);
        icon->set_color(player_is_currently_assigning ? theme::color::PrimaryL : theme::color::TextDim);
        return;
    }

    bool has_controller = recompinput::players::get_player_input_device(player_index, use_temp_state) == recompinput::InputDevice::Controller;

    std::chrono::steady_clock::duration time_since_last_button_press = recompinput::playerassignment::get_player_time_since_last_button_press(player_index);
    auto millis = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(time_since_last_button_press).count());
    float seconds = millis / 1000.0f;

    if (seconds > 0 && seconds < scale_anim_duration) {
        float t = 1.0f - (seconds / scale_anim_duration);
        float scale = 1.0f + t * 0.15f;
        icon->set_scale_2D(scale, scale);
        icon->set_color(theme::color::Text, 200 + static_cast<int>(t * 55.0f));
    } else {
        icon->set_scale_2D(1.0f, 1.0f);
        icon->set_color(theme::color::Text, 200);
    }
}

} // namespace recompui

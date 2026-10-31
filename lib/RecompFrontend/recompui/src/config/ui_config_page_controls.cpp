#include "ui_config_page_controls.h"
#include "composites/ui_assign_players_modal.h"
#include "elements/ui_button.h"
#include "elements/ui_label.h"
#include "elements/ui_toggle.h"
#include "elements/ui_container.h"
#include "elements/ui_binding_button.h"
#include "elements/ui_select.h"
#include "elements/ui_text_input.h"
#include "librecomp/game.hpp"
#include <algorithm>
#include "recompinput/profiles.h"
#include "recompui/config.h"

namespace recompui {

ConfigPageControls *controls_page = nullptr;
static std::function<void()> auto_configure_players_callback;
void set_auto_configure_players_callback(std::function<void()> callback) {
    auto_configure_players_callback = std::move(callback);
}

const std::string_view active_state_style_name = "cont_opt_active";

// Separating this out, may be a good component later to expose.
class ScrollBuffer : public Element {
protected:
    std::string_view get_type_name() override { return "ScrollBuffer"; }
public:
    ScrollBuffer(ResourceId rid, Element *parent, float buffer_amount) : Element(rid, parent, 0, "div", false) {
        set_position(Position::Absolute);
        set_inset(-buffer_amount);
        set_pointer_events(PointerEvents::None);
    }
};

GameInputRow::GameInputRow(
    ResourceId rid,
    Element *parent,
    const GameInputContext &input_ctx,
    std::function<void()> on_active_callback,
    on_bind_click_callback on_bind_click,
    on_clear_or_reset_callback on_clear_or_reset
) : Element(rid, parent, Events(EventType::Hover, EventType::Focus), "div", false) {
    this->input_id = input_ctx.input_id;
    this->on_active_callback = on_active_callback;

    set_display(Display::Flex);
    set_position(Position::Relative);
    set_flex_direction(FlexDirection::Row);
    set_align_items(AlignItems::Center);
    set_justify_content(JustifyContent::SpaceBetween);
    set_width(100.0f, Unit::Percent);
    // Have to set height here for the scroll buffer to work.
    // Otherwise it fails to calculate the correct size (RmlUI issue)
    set_height(64.0f);

    set_padding_top(4.0f);
    set_padding_right(16.0f);
    set_padding_bottom(4.0f);
    set_padding_left(GameInputRow::left_padding);
    set_border_radius(theme::border::radius_sm);
    set_background_color(theme::color::Transparent);

    active_style.set_background_color(theme::color::ElevatedSoft);
    add_style(&active_style, active_state_style_name);

    recompui::ContextId context = get_current_context();

    set_debug_id("GameInputRow (" + input_ctx.name + ")");

    auto label = context.create_element<Label>(this, input_ctx.name, theme::Typography::LabelMD);
    label->set_flex_grow(2.0f);
    label->set_flex_shrink(1.0f);
    label->set_flex_basis(300.0f);
    label->set_height_auto();
    // TODO: whitespace nowrap impl

    auto bindings_container = context.create_element<Element>(this, 0, "div", false);
    {
        bindings_container->set_display(Display::Flex);
        bindings_container->set_position(Position::Relative);
        bindings_container->set_flex_grow(2.0f);
        bindings_container->set_flex_shrink(1.0f);
        bindings_container->set_flex_basis(400.0f);
        bindings_container->set_flex_direction(FlexDirection::Row);
        bindings_container->set_align_items(AlignItems::Center);
        bindings_container->set_justify_content(JustifyContent::SpaceBetween);
        bindings_container->set_width(100.0f, Unit::Percent);
        bindings_container->set_height(56.0f);
        bindings_container->set_padding_right(12.0f);
        bindings_container->set_padding_left(4.0f);
        bindings_container->set_gap(4.0f);

        for (size_t i = 0; i < recompinput::num_bindings_per_input; i++) {
            BindingButton *binding_button = context.create_element<BindingButton>(bindings_container, "");
            binding_button->add_pressed_callback([this, i, on_bind_click]() {
               on_bind_click(this->input_id, i);
            });
            binding_button->set_debug_id("BindingButton (" + input_ctx.name + " Binding " + std::to_string(i) + ")");
            binding_buttons.push_back(binding_button);
        }
        binding_buttons[0]->set_as_primary_focus(true);
    }

    if (input_ctx.clearable) {
        auto clear_button = context.create_element<IconButton>(this, "icons/Trash.svg", ButtonStyle::Danger, IconButtonSize::Large);
        clear_button->add_pressed_callback([this, on_clear_or_reset]() {
            on_clear_or_reset(this->input_id, false);
        });
    } else {
        auto reset_button = context.create_element<IconButton>(this, "icons/Reset.svg", ButtonStyle::Warning, IconButtonSize::Large);
        reset_button->add_pressed_callback([this, on_clear_or_reset]() {
            on_clear_or_reset(this->input_id, true);
        });
    }

    bindings.resize(recompinput::num_bindings_per_input);
    for (size_t i = 0; i < recompinput::num_bindings_per_input; i++) {
        bindings[i] = recompinput::InputField();
    }

    scroll_buffer = context.create_element<ScrollBuffer>(this, 16.0f);
}

GameInputRow::~GameInputRow() {
}

void GameInputRow::update_bindings(BindingList &new_bindings) {
    for (size_t i = 0; i < new_bindings.size(); i++) {
        binding_buttons[i]->set_is_binding(false);

        // skip update if no changes
        if (
            new_bindings[i].input_id == bindings[i].input_id &&
            new_bindings[i].input_type == bindings[i].input_type) {
            continue;
        }

        binding_buttons[i]->set_binding(new_bindings[i].to_string());
        bindings[i] = new_bindings[i];
    }
}

void GameInputRow::process_event(const Event &e) {
    switch (e.type) {
    case EventType::Hover:
        {
            bool hover_active = std::get<EventHover>(e.variant).active;
            set_style_enabled(active_state_style_name, hover_active);
            if (hover_active && on_active_callback) {
                on_active_callback();
            }
        }
        break;
    case EventType::Focus:
        {
            bool focus_active = std::get<EventFocus>(e.variant).active;
            set_style_enabled(active_state_style_name, focus_active);
            if (focus_active && on_active_callback) {
                on_active_callback();
            }
            scroll_buffer->scroll_into_view();
        }
        break;
    default:
        break;
    }
}

void GameInputRow::focus_on_first_binding() {
    if (binding_buttons.size() > 0) {
        binding_buttons[0]->focus();
    }
}

ConfigPageControls::ConfigPageControls(ResourceId rid, Element *parent) : ConfigPage(rid, parent) {
    controls_page = this;

    multiplayer_enabled = recompinput::players::is_single_player_mode() == false;
    this->max_num_players = multiplayer_enabled ? recompinput::players::get_max_number_of_players() : 1;
    multiplayer_view_mappings = !multiplayer_enabled;

    set_selected_player(selected_player);

    set_as_navigation_container(NavigationType::Vertical);
    set_debug_id("ConfigPageControls");

    render_all();
}

ConfigPageControls::~ConfigPageControls() {
    controls_page = nullptr;
}

void ConfigPageControls::create_game_input_contexts() {
    game_input_sections.n64.contexts.clear();
    game_input_sections.menu.contexts.clear();
    game_input_sections.other.clear();

    for (int i = 0; i < static_cast<int>(recompinput::GameInput::COUNT); i++) {
        recompinput::GameInput input = static_cast<recompinput::GameInput>(i);
        if (recompinput::get_game_input_disabled(input)) {
            continue;
        }

        GameInputContext input_ctx = {
            recompinput::get_game_input_name(input),
            recompinput::get_game_input_description(input),
            input,
            recompinput::get_game_input_clearable(input)
        };

        if (recompinput::get_game_input_is_menu(input)) {
            game_input_sections.menu.contexts.push_back(input_ctx);
        } else {
            game_input_sections.n64.contexts.push_back(input_ctx);
        }
    }

    // TODO: fetch other (e.g. mod) inputs to game_input_sections.other
}

void ConfigPageControls::process_event(const Event &e) {
    switch (e.type) {
    case EventType::Update:
        if (awaiting_binding && !recompinput::binding::is_binding()) {
            awaiting_binding = false;
            update_control_mappings();

            if (awaiting_binding_for_menu_action_button) {
                config::get_config_modal()->render_menu_actions();
            }
        }
        {
            std::vector<uintptr_t> assignment;
            for (size_t i = 0; i < recompinput::players::get_max_number_of_players(); i++) {
                const int player = static_cast<int>(i);
                const bool assigned = recompinput::players::get_player_is_assigned(player);
                assignment.push_back(assigned ? 1 + static_cast<uintptr_t>(recompinput::players::get_player_input_device(player)) : 0);
                assignment.push_back(assigned ? reinterpret_cast<uintptr_t>(recompinput::players::get_player(player).controller) : 0);
            }
            if (assignment != rendered_assignment) {
                rendered_assignment = std::move(assignment);
                force_update();
            }
        }
        if (last_update_index != update_index) {
            last_update_index = update_index;
            render_all();
        }
        queue_update();
        break;
    default:
        break;
    }
}

void ConfigPageControls::force_update() {
    update_index++;
}

void ConfigPageControls::render_all() {
    set_current_profile_index();
    render_header();
    render_body();
    render_footer();
}

static void show_or_hide_if_exists(Element* element, bool show) {
    if (element != nullptr) {
        if (show) {
            element->display_show();
        } else {
            element->display_hide();
        }
    }
}

void ConfigPageControls::render_header() {
    // Header attempts to not re-render elements if not necessary, instead swapping visibility / updating text.

    if (!multiplayer_enabled) {
        hide_header();
        return;
    }

    recompui::ContextId context = get_current_context();
    add_header();
    header->set_as_navigation_container(NavigationType::Horizontal);

    // header left
    {
        auto header_left = header->get_left();

        if (multiplayer_view_mappings) {
            std::string profile_name = "Editing: " + recompinput::profiles::get_input_profile_name(selected_profile_index);
            if (header_elements.left.profile_name_label == nullptr) {
                header_elements.left.profile_name_label = context.create_element<Label>(
                    header_left,
                    profile_name,
                    LabelStyle::Normal
                );
            } else if (header_elements.left.current_profile_name != profile_name) {
                header_elements.left.profile_name_label->set_text(profile_name);
            }
            header_elements.left.current_profile_name = profile_name;
            show_or_hide_if_exists(header_elements.left.profile_name_label, true);
        } else {
            show_or_hide_if_exists(header_elements.left.profile_name_label, false);
            // Nothing rendered here as of now.. maybe single player toggle
        }
    }

    // header right
    {
        auto header_right = header->get_right();

        if (multiplayer_view_mappings) {
            if (header_elements.right.go_back_button == nullptr) {
                header_elements.right.go_back_button = context.create_element<Button>(header_right, "Go back", ButtonStyle::Tertiary);
                header_elements.right.go_back_button->add_pressed_callback([this]() {
                    this->multiplayer_view_mappings = false;
                    this->queue_selected_player_profile_edit_focus = true;
                    this->force_update();
                });
                header_elements.right.go_back_button->set_as_primary_focus(true);
            }

            show_or_hide_if_exists(header_elements.right.go_back_button, true);
            show_or_hide_if_exists(header_elements.right.assign_players_button, false);
            show_or_hide_if_exists(header_elements.right.auto_configure_button, false);
        } else {
            if (header_elements.right.assign_players_button == nullptr) {
                header_elements.right.assign_players_button = context.create_element<Button>(header_right, "Assign players", ButtonStyle::Primary);
                header_elements.right.assign_players_button->set_debug_id("AssignPlayersButton");
                header_elements.right.assign_players_button->add_pressed_callback([]() {
                    // Edit the current layout rather than re-entering every player.
                    recompinput::playerassignment::start_from_current();
                    recompui::AssignPlayersModal::open();
                });
                header_elements.right.assign_players_button->set_as_primary_focus(true);
                if (auto_configure_players_callback) {
                    auto auto_button = context.create_element<Button>(header_right, "Auto-configure", ButtonStyle::Secondary);
                    auto_button->set_debug_id("AutoConfigureControllers");
                    auto_button->add_pressed_callback([this]() {
                        if (auto_configure_players_callback) auto_configure_players_callback();
                        force_update();
                    });
                    header_elements.right.auto_configure_button = auto_button;
                }
            }

            show_or_hide_if_exists(header_elements.right.go_back_button, false);
            show_or_hide_if_exists(header_elements.right.assign_players_button, true);
            show_or_hide_if_exists(header_elements.right.auto_configure_button, true);
        }
    }
}

bool ConfigPageControls::should_show_mappings() {
    return (multiplayer_enabled && multiplayer_view_mappings) || !multiplayer_enabled;
}

void ConfigPageControls::render_body() {
    recompui::ContextId context = get_current_context();

    if (should_show_mappings()) {
        body->get_right()->set_display(Display::Flex);
        render_body_mappings();
    } else {
        body->get_right()->set_display(Display::None);
        render_body_players();
    }
}

void ConfigPageControls::render_body_mappings() {
    recompui::ContextId context = get_current_context();
    body->set_as_navigation_container(NavigationType::Horizontal);

    // Create the description before restoring mapping-row focus.
    // right side
    {
        auto right = body->get_right();
        description_container = nullptr;
        right->clear_children();
        right->set_flex_direction(FlexDirection::Column);
        right->set_gap(12.0f);
        right->set_overflow_y(Overflow::Auto);
        right->set_height(100.0f, Unit::Percent);
        right->set_max_height(100.0f, Unit::Percent);
        right->set_as_navigation_container(NavigationType::Vertical);
        render_templates();
        description_container = context.create_element<Element>(right, 0, "p", true);
        description_container->set_text("");
    }
    render_control_mappings();
}

void ConfigPageControls::render_templates() {
    using namespace recompinput;
    auto context = get_current_context();
    Element* restore_focus = nullptr;
    auto panel = context.create_element<Container>(body->get_right(), FlexDirection::Column, JustifyContent::FlexStart, 0);
    panel->set_gap(8.0f);
    panel->set_width(100.0f, Unit::Percent);
    panel->set_as_navigation_container(NavigationType::Vertical);
    context.create_element<Label>(panel, "Binding templates", LabelStyle::Normal);
    context.create_element<Label>(panel,
        "Apply gameplay mappings to this profile. Menu controls stay unchanged. Templates work with any device of the same type.", LabelStyle::Small);
    const auto device = get_player_input_device();
    if (template_device != static_cast<int>(device)) {
        template_device = static_cast<int>(device);
        selected_template_index = -1;
        template_name.clear();
        template_status.clear();
        confirm_template_delete = false;
    }
    const auto custom = profiles::get_indices_for_custom_profiles(device);
    if (std::find(custom.begin(), custom.end(), selected_template_index) == custom.end()) selected_template_index = -1;
    std::vector<SelectOption> options{{device == InputDevice::Controller ? "Default Gamepad" : "Default Keyboard", "-1"}};
    for (int index : custom) options.emplace_back(profiles::get_input_profile_name(index), std::to_string(index));
    auto select = context.create_element<Select>(panel, options, std::to_string(selected_template_index));
    select->set_debug_id("BindingTemplateSelect");
    if (template_focus == 1) restore_focus = select;
    select->add_change_callback([this](SelectOption& option, int) {
        template_focus = 1;
        selected_template_index = std::stoi(option.value);
        template_name = selected_template_index < 0 ? "" : profiles::get_input_profile_name(selected_template_index);
        confirm_template_delete = false;
        template_status.clear();
        template_management_open = false;
        force_update();
    });
    // Template operations save immediately. Ordinary remapping also retains
    // the Controls tab's existing save-on-close behavior.
    auto finish = [this](bool success, const std::string& message) {
        if (!success) template_status = "No change: use a shorter, unique name for this device type.";
        else if (!profiles::save_controls_config(recomp::get_config_path() / "controls.json"))
            template_status = "Changed in memory, but saving failed. Check the configuration folder and try again.";
        else template_status = message;
        confirm_template_delete = false;
        template_focus = 1;
        force_update();
    };
    auto apply = context.create_element<Button>(panel, "Apply template", ButtonStyle::Primary, ButtonSize::Medium);
    apply->set_debug_id("ApplyBindingTemplate");
    apply->add_pressed_callback([this, finish]() {
        bool success = true;
        if (selected_template_index < 0) {
            // Gameplay fields precede TOGGLE_MENU. Keep Accept/Back/Toggle
            // untouched so applying a layout cannot strand menu navigation.
            for (int i = 0; i < static_cast<int>(GameInput::TOGGLE_MENU); ++i)
                profiles::reset_input_binding(selected_profile_index, get_player_input_device(), static_cast<GameInput>(i));
        } else success = profiles::apply_input_profile(selected_template_index, selected_profile_index);
        finish(success, "Gameplay template applied. Menu controls were preserved.");
    });
    context.create_element<Label>(panel, "Template name (optional for Save new)", LabelStyle::Small);
    auto name = context.create_element<TextInput>(panel);
    name->set_debug_id("BindingTemplateName");
    name->set_max_length(64);
    name->set_text(template_name);
    name->add_text_changed_callback([this](const std::string& text) {
        template_name = text;
        confirm_template_delete = false;
    });
    auto save = context.create_element<Button>(panel, "Save new template", ButtonStyle::Secondary, ButtonSize::Medium);
    save->add_pressed_callback([this, finish]() {
        std::string name = template_name;
        if (name.empty()) {
            const auto device = get_player_input_device();
            const std::string base = device == InputDevice::Controller ? "Gamepad template " : "Keyboard template ";
            for (int n = 1; ; ++n) {
                name = base + std::to_string(n);
                bool exists = false;
                for (int index : profiles::get_indices_for_custom_profiles(device)) {
                    const auto& existing = profiles::get_input_profile_name(index);
                    exists |= existing.size() == name.size() && std::equal(existing.begin(), existing.end(), name.begin(),
                        [](unsigned char a, unsigned char b) {
                            const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
                            return lower(a) == lower(b);
                        });
                }
                if (!exists) break;
            }
        }
        int saved = -1;
        const bool success = profiles::save_input_profile_as(selected_profile_index, name, saved);
        if (success) {
            selected_template_index = saved;
            template_name = profiles::get_input_profile_name(saved);
        }
        finish(success, "Template saved. The current profile is still selected for editing.");
    });
    auto manage = context.create_element<Button>(panel, template_management_open ? "Hide template actions" : "Manage selected template", ButtonStyle::Tertiary, ButtonSize::Medium);
    manage->set_enabled(selected_template_index >= 0);
    if (template_focus == 2) restore_focus = manage;
    manage->add_pressed_callback([this]() {
        template_focus = 2;
        template_management_open = !template_management_open;
        confirm_template_delete = false;
        force_update();
    });
    if (template_management_open && selected_template_index >= 0) {
        auto update = context.create_element<Button>(panel, "Replace selected template", ButtonStyle::Secondary, ButtonSize::Medium);
        update->set_enabled(selected_template_index >= 0);
        update->add_pressed_callback([this, finish]() {
            int saved = -1;
            const auto name = profiles::get_input_profile_name(selected_template_index);
            finish(profiles::save_input_profile_as(selected_profile_index, name, saved, true), "Selected template replaced with the current mappings.");
        });
        auto rename = context.create_element<Button>(panel, "Rename selected template", ButtonStyle::Tertiary, ButtonSize::Medium);
        rename->set_enabled(selected_template_index >= 0);
        rename->add_pressed_callback([this, finish]() {
            finish(profiles::rename_input_profile(selected_template_index, template_name), "Template renamed.");
        });
        auto remove = context.create_element<Button>(panel, confirm_template_delete ? "Confirm delete template" : "Delete selected template", ButtonStyle::Danger, ButtonSize::Medium);
        remove->set_enabled(selected_template_index >= 0);
        if (template_focus == 3) restore_focus = remove;
        remove->add_pressed_callback([this, finish]() {
            if (!confirm_template_delete) {
                template_focus = 3;
                confirm_template_delete = true;
                template_status = "Press Confirm delete template to remove it. Active controllers retain their mappings.";
                force_update();
                return;
            }
            const bool success = profiles::delete_input_profile(selected_template_index);
            if (success) {
                if (selected_profile_index == selected_template_index)
                    selected_profile_index = profiles::get_input_profile_for_player(selected_player, get_player_input_device());
                selected_template_index = -1;
                template_name.clear();
            }
            finish(success, "Template deleted. Active controllers retain their mappings.");
        });
    }
    context.create_element<Label>(panel, template_status, LabelStyle::Small);
    if (restore_focus != nullptr) restore_focus->focus();
    template_focus = 0;
}

void ConfigPageControls::render_body_players() {
    recompui::ContextId context = get_current_context();
    body->set_as_navigation_container(NavigationType::Horizontal);
    body->set_max_height(100.0f, Unit::Percent);

    auto body_left = body->get_left();
    body_left->clear_children();
    player_cards.clear();

    body_left->set_display(Display::Block);
    // Top/bottom padding is within the player cards so that controller nav puts them more into view.
    body_left->set_padding_top(0.0f);
    body_left->set_padding_bottom(0.0f);
    body_left->set_padding_right(64.0f);
    body_left->set_padding_left(64.0f);
    body_left->set_as_navigation_container(NavigationType::GridCol);
    body_left->set_position(Position::Relative);
    body_left->set_height_auto();
    body_left->set_max_height(100.0f, Unit::Percent);
    body_left->set_overflow_y(Overflow::Auto);

    bool make_fakes = max_num_players > 4;

    recompui::Element *player_grid = nullptr;
    // Grid supports groups of 4
    for (int i = 0; i < max_num_players; i += 4) {
        player_grid = context.create_element<Element>(body_left, 0, "div", false);
        player_grid->set_as_navigation_container(NavigationType::GridRow);
        player_grid->set_display(Display::Flex);
        player_grid->set_flex_direction(FlexDirection::Row);
        player_grid->set_justify_content(JustifyContent::Center);
        player_grid->set_align_items(AlignItems::Center);
        player_grid->set_width(100.0f, Unit::Percent);
        player_grid->set_height_auto();
        player_grid->set_gap(64.0f);
        // Remove extra margin already added as padding from player cards.
        // The player cards still need the padding for controller nav visibility.
        player_grid->set_margin_bottom(-64.0f);

        for (int j = i; j < i + 4; j++) {
            if (!make_fakes && j >= max_num_players) {
                break;
            }

            if (make_fakes && j >= 4 && j >= max_num_players) {
                auto fake = context.create_element<Element>(player_grid);
                fake->set_width(PlayerCard::static_player_card_size, Unit::Dp);
                continue;
            }

            auto player_card = context.create_element<PlayerCard>(
                player_grid,
                j,
                false
            );
            player_card->set_on_select_profile_callback([this](int player_index, int profile_index) {
                this->on_select_player_profile(player_index, profile_index);
            });
            player_card->set_on_edit_profile_callback([this](int player_index) {
                this->on_edit_player_profile(player_index);
            });
            player_cards.push_back(player_card);
            if (i == 0 && j == 0) {
                player_card->set_as_primary_focus(true);
            }
        }
    }

    // Retain 64px bottom margin on the last row.
    if (player_grid != nullptr) {
        player_grid->set_margin_bottom(0.0f);
    }

    if (queue_selected_player_profile_edit_focus) {
        queue_selected_player_profile_edit_focus = false;
        if (selected_player >= 0 && selected_player < static_cast<int>(player_cards.size())) {
            player_cards[selected_player]->focus_on_edit_profile_button();
        }
    }
}

void ConfigPageControls::on_select_player_profile(int player_index, int profile_index) {
    recompinput::InputDevice device = recompinput::players::get_player_input_device(player_index);
    if (device != recompinput::InputDevice::COUNT) {
        // Copy into the device's independent, persisted working profile. Selecting
        // a template must not alias its bindings or disappear after reconnect.
        recompinput::profiles::apply_input_profile_for_player(player_index, profile_index);
        force_update();
    }
}

void ConfigPageControls::on_edit_player_profile(int player_index) {
    selected_player = player_index;
    single_player_show_keyboard_mappings = false;
    recompinput::InputDevice device = recompinput::players::get_player_input_device(player_index);
    if (device != recompinput::InputDevice::COUNT) {
        selected_profile_index = recompinput::profiles::get_input_profile_for_player(player_index, device);
        multiplayer_view_mappings = true;
        queue_first_game_input_row_focus = true;
        force_update();
    }
}

void ConfigPageControls::render_footer() {
    if (multiplayer_enabled && !multiplayer_view_mappings) {
        hide_footer();
        return;
    }

    recompui::ContextId context = get_current_context();

    add_footer();
    footer->set_as_navigation_container(NavigationType::Horizontal);
    {
        auto footer_left = footer->get_left();
        footer_left->clear_children();
        const bool has_keyboard_fallback = multiplayer_enabled &&
            recompinput::players::get_player_input_device(selected_player) == recompinput::InputDevice::Controller &&
            recompinput::profiles::get_input_profile_for_player(selected_player, recompinput::InputDevice::Keyboard) >= 0;
        if (!multiplayer_enabled || has_keyboard_fallback) {
            keyboard_toggle = context.create_element<IconToggle>(footer_left, "icons/Cont.svg", "icons/Keyboard.svg", ToggleSize::Large);
            keyboard_toggle->set_checked(single_player_show_keyboard_mappings);
            keyboard_toggle->add_checked_callback([this](bool checked) {
                this->single_player_show_keyboard_mappings = checked;
                this->set_current_profile_index();
                this->update_control_mappings();
                this->force_update();
            });
        }
    }
    {
        auto footer_right = footer->get_right();
        footer_right->clear_children();
        auto reset_to_defaults_button = context.create_element<Button>(footer_right, "Reset to defaults", ButtonStyle::Warning);
        reset_to_defaults_button->add_pressed_callback([this]() {
            recompinput::profiles::reset_profile_bindings(this->selected_profile_index, this->get_player_input_device());
            this->update_control_mappings();
        });
    }
}

void ConfigPageControls::render_control_mappings() {
    recompui::ContextId context = get_current_context();

    auto body_left = body->get_left();
    body_left->clear_children();

    body_left->set_display(Display::Block);
    body_left->set_padding(0.0f);
    body_left->set_as_navigation_container(NavigationType::Vertical);
    body_left->set_position(Position::Relative);
    body_left->set_height(100.0f, Unit::Percent);
    body_left->set_max_height(100.0f, Unit::Percent);
    body_left->set_overflow_y(Overflow::Auto);
    
    {
        auto body_left_scroll = context.create_element<Element>(body_left, 0, "div", false);
        body_left_scroll->set_display(Display::Block);
        body_left_scroll->set_width(100.0f, Unit::Percent);
        body_left_scroll->set_max_height(100.0f, Unit::Percent);
        body_left_scroll->set_overflow_y(Overflow::Scroll);
        body_left_scroll->set_as_navigation_container(NavigationType::GridCol);
        body_left_scroll->set_padding_left(8.0f);
        body_left_scroll->set_padding_right(8.0f);
        body_left_scroll->set_debug_id("Mappings Scroll Container");

        game_input_rows.clear();

        create_game_input_contexts();
        set_current_profile_index();

        rows_wrappers.clear();
        for (auto *section : game_input_sections.get_all_sections()) {
            if (section->contexts.size() == 0) {
                continue;
            }
            GameInputRowsWrapper *section_wrapper = context.create_element<GameInputRowsWrapper>(
                body_left_scroll, section->name, section == &game_input_sections.menu);
            rows_wrappers.push_back(section_wrapper);

            for (auto &input_ctx : section->contexts) {
                recompinput::GameInput input_id = input_ctx.input_id;
                GameInputRow *row = context.create_element<GameInputRow>(
                    static_cast<Element*>(section_wrapper),
                    input_ctx,
                    [this, input_id]() {
                        this->on_option_hover(input_id);
                    },
                    [this](recompinput::GameInput game_input, int input_index) {
                        this->on_bind_click(game_input, input_index);
                    },
                    [this](recompinput::GameInput game_input, bool reset) {
                        this->on_clear_or_reset_game_input(game_input, reset);
                    }
                );
                game_input_rows.push_back(row);
                row->set_as_navigation_container(NavigationType::GridRow);
            }
        }
    }

    update_control_mappings();
    if (queue_first_game_input_row_focus && game_input_rows.size() > 0) {
        game_input_rows[0]->focus_on_first_binding();
        queue_first_game_input_row_focus = false;
    }
}

void ConfigPageControls::set_current_profile_index() {
    if (!multiplayer_enabled) {
        selected_profile_index = single_player_show_keyboard_mappings
            ? recompinput::profiles::get_sp_keyboard_profile_index()
            : recompinput::profiles::get_sp_controller_profile_index();
    } else if (multiplayer_view_mappings) {
        const auto device = get_player_input_device();
        if (device == recompinput::InputDevice::COUNT) {
            // A pad may unplug while its editor is open. Return to player cards
            // instead of retaining a stale profile or capturing the wrong device.
            multiplayer_view_mappings = false;
            selected_profile_index = -1;
        } else {
            selected_profile_index = recompinput::profiles::get_input_profile_for_player(selected_player, device);
        }
    }
}

void ConfigPageControls::update_control_mappings() {
    if (!multiplayer_enabled) {
        selected_player = 0;
        set_current_profile_index();
    } else if (!multiplayer_view_mappings) {
        return;
    }

    // Decide section visibility, reset margins
    for (auto *wrapper : rows_wrappers) {
        wrapper->set_margin_top(0.0f);
        wrapper->set_margin_bottom(0.0f);
        if (wrapper->is_menu) {
            if (get_player_input_device() == recompinput::InputDevice::Keyboard) {
                wrapper->display_hide();
            } else {
                wrapper->display_show();
            }
            break;
        }
    }

    recompui::GameInputRowsWrapper *first = nullptr;
    recompui::GameInputRowsWrapper *last = nullptr;
    for (auto *wrapper : rows_wrappers) {
        bool visible = wrapper->get_display() != Display::None;
        if (visible) {
            if (first == nullptr) {
                first = wrapper;
            }
            last = wrapper;
        }
    }

    // Need to use margins here instead of padding on the scroll parent.
    // RmlUI scroll areas weren't accounting for the bottom padding and this was the only way
    // to circumvent that.
    if (first != nullptr) {
        first->set_margin_top(8.0f);
    }
    if (last != nullptr) {
        last->set_margin_bottom(8.0f);
    }

    PlayerBindings game_input_bindings;
    for (auto *ctx : game_input_sections.get_all_contexts()) {
        game_input_bindings[ctx->input_id] = {};

        for (int j = 0; j < recompinput::num_bindings_per_input; j++) {
            game_input_bindings[ctx->input_id].push_back(recompinput::profiles::get_input_binding(selected_profile_index, ctx->input_id, j));
        }
    }

    for (size_t i = 0; i < game_input_rows.size(); i++) {
        game_input_rows[i]->update_bindings(
            game_input_bindings.at(game_input_rows[i]->get_input_id())
        );
    }
}

recompinput::InputDevice ConfigPageControls::get_player_input_device() {
    if (multiplayer_enabled) {
        const auto device = recompinput::players::get_player_input_device(this->selected_player);
        if (device == recompinput::InputDevice::Controller && single_player_show_keyboard_mappings &&
            recompinput::profiles::get_input_profile_for_player(selected_player, recompinput::InputDevice::Keyboard) >= 0)
            return recompinput::InputDevice::Keyboard;
        return device;
    }

    return single_player_show_keyboard_mappings
        ? recompinput::InputDevice::Keyboard
        : recompinput::InputDevice::Controller;
}

void ConfigPageControls::on_bind_click(recompinput::GameInput game_input, int input_index) {
    recompinput::InputDevice device = get_player_input_device();

    recompinput::binding::start_scanning(this->selected_player, game_input, input_index, device);
    awaiting_binding = true;
    awaiting_binding_for_menu_action_button = get_game_input_is_menu(game_input);
}

void ConfigPageControls::on_clear_or_reset_game_input(recompinput::GameInput game_input, bool reset) {
    if (!reset) {
        recompinput::profiles::clear_input_binding(selected_profile_index, game_input);
    } else {
        recompinput::InputDevice device = get_player_input_device();
        recompinput::profiles::reset_input_binding(selected_profile_index, device, game_input);
    }
    update_control_mappings();
}

void ConfigPageControls::set_selected_player(int player) {
    selected_player = player;
}

void ConfigPageControls::on_option_hover(recompinput::GameInput input_id) {
    if (description_container) {
        GameInputContext *ctx = game_input_sections.get_context_by_input_id(input_id);
        if (ctx != nullptr) {
            description_container->set_text(ctx->description);
        } else {
            description_container->set_text("");
        }
    }
}


} // namespace recompui

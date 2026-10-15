/*
 * Copyright (C) 2026 Dave Hammer
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ui_lutron_tx.hpp"

#include "baseband_api.hpp"
#include "lutron_store.hpp"
#include "portapack_shared_memory.hpp"
#include "string_format.hpp"
#include "ui_textentry.hpp"

#include <cstring>

using namespace portapack;

namespace ui::external_app::lutron_tx {

LutronTxView::LutronTxView(NavigationView& nav)
    : nav_{nav} {
    baseband::run_prepared_image(portapack::memory::map::m4_code.base());
    /* The modulator is 62.5 kbaud at 1 MHz. Keep that rate. Start quiet. */
    transmitter_model.set_sampling_rate(1000000);
    transmitter_model.set_baseband_bandwidth(500000);
    transmitter_model.set_tx_gain(0);
    transmitter_model.set_rf_amp(false);

    add_children({&text_device,
                  &button_prev,
                  &button_next,
                  &button_serial,
                  &button_name,
                  &text_target,
                  &options_target,
                  &text_level,
                  &field_level,
                  &text_button,
                  &options_button,
                  &text_repeats,
                  &field_repeats,
                  &text_action,
                  &options_action,
                  &text_status,
                  &text_hint,
                  &tx_view});

    field_repeats.set_value(lutron::tx_repeat_default, false);
    field_level.set_value(100, false);
    load_files();

    button_prev.on_select = [this](Button&) { on_prev(); };
    button_next.on_select = [this](Button&) { on_next(); };
    button_serial.on_select = [this](Button&) { on_serial(); };
    button_name.on_select = [this](Button&) { on_name(); };
    options_button.on_change = [this](size_t, OptionsField::value_t) { disarm(); };
    options_action.on_change = [this](size_t, OptionsField::value_t) { disarm(); };
    options_target.on_change = [this](size_t, OptionsField::value_t value) {
        disarm();
        if (value == 1) {
            const std::string net = subnet_.known ? to_string_hex(lutron::subnet_word(subnet_), 6) : "no subnet yet";
            show_status("level " + to_string_dec_uint(field_level.value()) + "% " + net);
        }
    };
    field_level.on_change = [this](int32_t value) {
        disarm();
        if (dimmer_selected()) show_status(to_string_dec_uint(value) + "%");
    };
    field_repeats.on_change = [this](int32_t) { disarm(); };
    tx_view.on_start = [this]() { on_start(); };
    tx_view.on_stop = [this]() { on_stop(); };
}

void LutronTxView::focus() {
    button_next.focus();
}

void LutronTxView::load_files() {
    lutron_store::load_ids(ids_, aliases_);
    lutron_store::load_toggles(toggles_);
    lutron_store::load_subnet(subnet_);
    if (ids_.size() != 0) selected_ = 0;
    show_device();
}

void LutronTxView::show_status(const std::string& text) {
    text_status.set(text);
}

void LutronTxView::disarm() {
    if (!armed_ || sending_) return;
    armed_ = false;
    show_status("");
}

bool LutronTxView::dimmer_selected() const {
    return options_target.selected_index_value() == 1;
}

bool LutronTxView::current_id(uint32_t& id) const {
    if (selected_ < 0 || ids_.size() == 0) return false;
    id = ids_.at(static_cast<size_t>(selected_));
    return true;
}

void LutronTxView::show_device() {
    uint32_t id = 0;
    if (!current_id(id)) {
        text_device.set("no serial yet");
        return;
    }
    const char* name = aliases_.find(id);
    text_device.set(to_string_hex(id, 8) + ' ' + (name ? name : "unnamed"));
}

void LutronTxView::select_id(uint32_t id) {
    const int index = ids_.index_of(id);
    if (index >= 0) selected_ = index;
    show_device();
}

void LutronTxView::on_prev() {
    if (ids_.size() == 0 || sending_) return;
    selected_ = static_cast<int>((static_cast<size_t>(selected_) + ids_.size() - 1) % ids_.size());
    disarm();
    show_device();
}

void LutronTxView::on_next() {
    if (ids_.size() == 0 || sending_) return;
    selected_ = static_cast<int>((static_cast<size_t>(selected_) + 1) % ids_.size());
    disarm();
    show_device();
}

void LutronTxView::on_serial() {
    if (sending_) return;
    uint32_t id = 0;
    serial_buffer_ = current_id(id) ? to_string_hex(id, 8) : "00000000";
    text_prompt(
        nav_,
        serial_buffer_,
        8,
        ENTER_KEYBOARD_MODE_HEX,
        [this](std::string& text) {
            uint32_t parsed = 0;
            if (!lutron::parse_id(text.data(), text.size(), parsed)) {
                show_status("serial is 8 hex digits");
                return;
            }
            if (!lutron_store::remember_id(ids_, parsed)) {
                show_status(ids_.contains(parsed) ? "cannot write devices.txt" : "ID list full");
                if (!ids_.contains(parsed)) return;
            }
            disarm();
            select_id(parsed);
            show_status("serial " + to_string_hex(parsed, 8));
        });
}

void LutronTxView::on_name() {
    if (sending_) return;
    uint32_t id = 0;
    if (!current_id(id)) {
        show_status("pick or enter a serial");
        return;
    }
    const char* current = aliases_.find(id);
    name_buffer_ = current ? current : "";
    text_prompt(
        nav_,
        name_buffer_,
        lutron::AliasTable::name_len,
        ENTER_KEYBOARD_MODE_ALPHA,
        [this, id](std::string& name) {
            if (name.empty()) return;
            if (!lutron_store::remember_alias(aliases_, id, name)) {
                show_status("cannot write aliases.txt");
                return;
            }
            show_device();
            show_status("named " + to_string_hex(id, 8));
        });
}

void LutronTxView::on_start() {
    if (sending_) return;
    if (!armed_) {
        uint32_t id = 0;
        if (!current_id(id)) {
            show_status("pick or enter a serial");
            return;
        }
        armed_ = true;
        const char* name = aliases_.find(id);
        std::string again = std::string("again: ") + (name ? name : to_string_hex(id, 8));
        if (dimmer_selected()) {
            again += " " + to_string_dec_uint(field_level.value()) + "%";
            again += subnet_.known ? " " + to_string_hex(lutron::subnet_word(subnet_), 6) : " no subnet";
        }
        show_status(again);
        return;
    }
    armed_ = false;
    start_tx();
}

void LutronTxView::on_stop() {
    baseband::set_lutron_tx(0);
    transmitter_model.disable();
    sending_ = false;
    armed_ = false;
    tx_view.set_transmitting(false);
    show_status("stopped");
}

void LutronTxView::on_done(bool done) {
    if (!done) return;
    transmitter_model.disable();
    sending_ = false;
    tx_view.set_transmitting(false);
    show_status("sent");
}

bool LutronTxView::start_tx() {
    uint32_t id = 0;
    if (!current_id(id)) {
        show_status("pick or enter a serial");
        return false;
    }
    int32_t repeats = field_repeats.value();
    if (repeats < 1) repeats = 1;
    if (repeats > lutron::tx_repeat_cap) repeats = lutron::tx_repeat_cap;

    uint8_t frames[lutron::tx_repeat_cap][lutron::frame_bytes];
    std::string sending = "sending " + to_string_dec_uint(repeats);
    if (dimmer_selected()) {
        if (!subnet_.known) {
            show_status("hear the bridge in Lutron RX first");
            return false;
        }
        /* Slot 0 is the one-shot. The rest are the repeat train, 75 ms apart. */
        const uint8_t type = level_type_;
        const auto percent = static_cast<uint8_t>(field_level.value());
        for (int32_t i = 0; i < repeats; i++) {
            lutron::LevelBuild level{};
            level.target = id;
            level.type = type;
            level.percent = percent;
            level.subnet = subnet_;
            level.train = i > 0;
            level.seq = i == 0 ? 0x01 : static_cast<uint8_t>((0x82 + 6 * (i - 1)) & 0xFF);
            if (!lutron::build_level(level, frames[i])) {
                show_status("cannot build that level");
                return false;
            }
        }
        level_type_ = static_cast<uint8_t>(0x81 + ((type - 0x81 + 1) % 3));
        sending = "sending " + to_string_dec_uint(percent) + "%";
    } else {
        lutron::Build build{};
        build.serial = id;
        build.button = static_cast<uint8_t>(options_button.selected_index_value());
        const bool press = options_action.selected_index_value() == 0;
        build.action = press ? lutron::Action::Press : lutron::Action::Release;
        /* toggles.txt holds the next press. A release uses the bit that press used, and does not advance it. */
        const uint8_t next_toggle = toggles_.get(id);
        build.toggle = press ? next_toggle : static_cast<uint8_t>(next_toggle ^ 1);

        for (int32_t i = 0; i < repeats; i++) {
            build.seq = static_cast<uint8_t>((6 * i) & 0xFF);
            if (!lutron::build_frame(build, frames[i])) {
                show_status("cannot build that button");
                return false;
            }
        }
        /* Advance before the radio comes on, so a press cut short still uses a new toggle next time. */
        if (press) lutron_store::remember_toggle(toggles_, id, static_cast<uint8_t>(next_toggle ^ 1));
    }
    memcpy(shared_memory.bb_data.data, frames, static_cast<size_t>(repeats) * lutron::frame_bytes);

    transmitter_model.set_sampling_rate(1000000);
    transmitter_model.enable();
    baseband::set_lutron_tx(static_cast<uint8_t>(repeats));
    sending_ = true;
    tx_view.set_transmitting(true);
    show_status(sending);
    return true;
}

}  // namespace ui::external_app::lutron_tx

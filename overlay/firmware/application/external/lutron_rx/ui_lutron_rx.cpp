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

#include "ui_lutron_rx.hpp"
#include "baseband_api.hpp"
#include "string_format.hpp"
#include "file.hpp"
#include "file_reader.hpp"
#include "lutron_store.hpp"
#include "ui_textentry.hpp"

#include "ch.h"

#include <cstring>

using namespace portapack;

namespace ui::external_app::lutron_rx {

namespace {

/* Returned by value. A global path constructor is linked at 0xAE while its init pointer stays in the boot table. */
std::filesystem::path saved_path() {
    return std::filesystem::path{u"LUTRON/saved.txt"};
}
std::filesystem::path log_path() {
    return std::filesystem::path{u"LUTRON/log.csv"};
}

std::string button_text(uint8_t code) {
    const char* name = lutron::button_name(code);
    if (name[0] == '?') return "BTN" + to_string_dec_uint(code);  // a button number not seen yet
    return name;
}

/* 1234 ms -> "1.2 s" */
std::string seconds(uint32_t ms) {
    return to_string_dec_uint(ms / 1000) + '.' + to_string_dec_uint((ms % 1000) / 100) + " s";
}

std::string hex_bytes(const uint8_t* bytes, size_t count) {
    std::string s;
    for (size_t i = 0; i < count; i++) s += to_string_hex(bytes[i], 2);
    return s;
}

}  // namespace

std::string LutronRxView::label(uint64_t id) const {
    const char* alias = aliases_.find(id);
    std::string s = alias ? std::string{alias} : to_string_hex(id, 8);
    s.resize(10, ' ');
    return s;
}

/* "Kitchen    RAISE/5 T1 14/14", the button code follows the name */
std::string LutronRxView::describe(const lutron::PressVoter::Press& press) const {
    std::string line = label(press.frame.id) + ' ';
    line += button_text(press.frame.button) + '/' + to_string_dec_uint(press.frame.button);
    line += " T" + to_string_dec_uint(press.frame.toggle);
    line += press.frame.release ? " R " : " P ";
    line += to_string_dec_uint(press.agree) + '/' + to_string_dec_uint(press.total);
    return line;
}

LutronRxView::LutronRxView(NavigationView& nav)
    : nav_{nav} {
    /* the baseband image travels inside the .ppma; the loader has already copied it to M4 RAM */
    baseband::run_prepared_image(portapack::memory::map::m4_code.base());

    add_children({&rssi,
                  &channel,
                  &field_rf_amp,
                  &field_lna,
                  &field_vga,
                  &field_frequency,
                  &text_live,
                  &text_stats,
                  &text_info,
                  &text_id,
                  &button_prev,
                  &button_next,
                  &button_name,
                  &button_save,
                  &check_log,
                  &console});

    field_frequency.set_step(5000);

    load_files();
    show_info();

    button_prev.on_select = [this](Button&) { on_prev(); };
    button_next.on_select = [this](Button&) { on_next(); };
    button_name.on_select = [this](Button&) { on_name(); };
    button_save.on_select = [this](Button&) { on_save(); };
    check_log.on_select = [this](Checkbox&, bool on) {
        if (on && !logging_) {
            auto error = log_.append(log_path());
            logging_ = !error;
            if (error) check_log.set_value(false);
        } else if (!on) {
            logging_ = false;
        }
        text_info.set(logging_ ? "logging to LUTRON/log.csv" : "logging off");
    };

    receiver_model.enable();
}

void LutronRxView::focus() {
    field_frequency.focus();
}

void LutronRxView::load_files() {
    lutron_store::load_ids(ids_, aliases_);
    lutron_store::load_subnet(subnet_);
    if (ids_.size() != 0) selected_ = 0;
    show_id();
}

void LutronRxView::note_subnet(const lutron::Frame& frame) {
    if (!frame.valid) return;
    lutron::Subnet seen{};
    if (!lutron::bridge_subnet(frame.raw, seen)) return;
    const bool changed = !subnet_.known || subnet_.b0 != seen.b0 || subnet_.b1 != seen.b1 || subnet_.b2 != seen.b2;
    if (!changed) return;
    if (!lutron_store::remember_subnet(subnet_, seen)) {
        if (!subnet_warned_) {
            console.writeln("SUBNET not saved");
            subnet_warned_ = true;
        }
        return;
    }
    subnet_warned_ = false;
    console.writeln("SUBNET " + to_string_hex(lutron::subnet_word(subnet_), 6));
}

void LutronRxView::show_id() {
    if (selected_ < 0 || ids_.size() == 0) {
        text_id.set("no Pico ID yet");
        return;
    }
    const uint32_t id = ids_.at(static_cast<size_t>(selected_));
    const char* name = aliases_.find(id);
    text_id.set(to_string_hex(id, 8) + ' ' + (name ? name : "unnamed"));
}

void LutronRxView::select_id(uint32_t id) {
    const int index = ids_.index_of(id);
    if (index >= 0) selected_ = index;
    show_id();
}

bool LutronRxView::record_id(uint32_t id) {
    if (ids_.contains(id)) return true;
    if (!lutron_store::remember_id(ids_, id)) {
        text_info.set(ids_.contains(id) ? "cannot write LUTRON/devices.txt" : "ID list full");
        if (ids_.contains(id)) select_id(id);
        return false;
    }
    select_id(id);
    console.writeln("ID " + to_string_hex(id, 8));
    return true;
}

void LutronRxView::on_prev() {
    if (ids_.size() == 0) return;
    selected_ = static_cast<int>((static_cast<size_t>(selected_) + ids_.size() - 1) % ids_.size());
    show_id();
}

void LutronRxView::on_next() {
    if (ids_.size() == 0) return;
    selected_ = static_cast<int>((static_cast<size_t>(selected_) + 1) % ids_.size());
    show_id();
}

void LutronRxView::show_info() {
    std::string line = "pico " + to_string_dec_uint(bursts_) + " ids " + to_string_dec_uint(ids_.size()) + " bad " + to_string_dec_uint(bad_);
    if (subnet_.known) line += " net " + to_string_hex(lutron::subnet_word(subnet_), 6);
    text_info.set(line);
}

void LutronRxView::on_frame(const LutronFrameMessage& message) {
    const lutron::Frame frame = lutron::parse_frame(message.bytes, message.bits);
    note_subnet(frame);
    /* a transmission that does not stop where a Pico's frame ends is not a Pico (the bridge talking to a dimmer) */
    const bool bridge = message.long_frame || message.bits < lutron::min_pico_bits;

    if (logging_) {
        log_.write_raw(to_string_dec_uint(chTimeNow()) + (bridge ? ",B," : (frame.valid ? ",P," : ",X,")) + to_string_hex(frame.id, 8) + ',' + to_string_dec_uint(frame.button) + ',' +
                       to_string_dec_uint(frame.toggle) + ',' + to_string_dec_uint(message.bits) + ',' + to_string_dec_uint(message.level) + ',' +
                       hex_bytes(message.bytes, (message.bits + 7) / 8));
    }

    if (bridge) {
        bridge_++;
        if (memcmp(bridge_prev_, message.bytes, sizeof(bridge_prev_)) != 0) {
            memcpy(bridge_prev_, message.bytes, sizeof(bridge_prev_));
            console.writeln("BRIDGE " + hex_bytes(message.bytes, 10));
        }
        show_info();
        return;
    }
    if (!lutron::is_pico_frame(frame)) {
        /* bit errors, or a bridge frame that happens to be Pico-length (set-level, beacon) */
        if (!frame.valid) bad_++;
        else bridge_++;
        show_info();
        return;
    }

    const bool recorded = record_id(static_cast<uint32_t>(frame.id));
    bursts_++;
    lutron::PressVoter::Press finished;
    if (voter_.add(frame, chTimeNow(), finished)) {
        last_ = current_;
        last_.frame = finished.frame;
        current_ = PressRaw{};
        show_press(finished);
    }

    /* the press in progress, with the running vote; Save keeps the latest burst that agrees with it */
    const auto now = voter_.current();
    if (frame == now.frame && (frame.long_form || !current_.frame.long_form)) {
        current_.frame = frame;
        current_.valid = true;
    }
    text_live.set(describe(now));
    text_stats.set("burst " + to_string_dec_uint(now.total) + "  held " + seconds(now.duration_ms) + "  ctr " + to_string_dec_uint(frame.counter));
    if (recorded) show_info();
}

void LutronRxView::on_tick() {
    lutron::PressVoter::Press finished;
    if (voter_.idle(chTimeNow(), finished)) {
        last_ = current_;
        last_.frame = finished.frame;
        current_ = PressRaw{};
        show_press(finished);
        text_stats.set("presses " + to_string_dec_uint(presses_));
    }
}

void LutronRxView::show_press(const lutron::PressVoter::Press& press) {
    presses_++;
    console.writeln(describe(press));
}

/* the press in progress, else the last finished one */
bool LutronRxView::target(PressRaw*& raw) {
    if (current_.valid) {
        raw = &current_;
        return true;
    }
    if (last_.valid) {
        raw = &last_;
        return true;
    }
    return false;
}

void LutronRxView::on_name() {
    if (selected_ < 0 || ids_.size() == 0) {
        text_info.set("hear a Pico first");
        return;
    }
    const uint32_t id = ids_.at(static_cast<size_t>(selected_));
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
                text_info.set("cannot write LUTRON/aliases.txt");
                return;
            }
            show_id();
            text_info.set("named " + to_string_hex(id, 8));
            console.writeln(label(id) + " = " + to_string_hex(id, 8));
        });
}

/* Stores the press for the TX app: "serial,k,button,toggle,24 bytes of the burst" */
void LutronRxView::on_save() {
    PressRaw* raw = nullptr;
    if (!target(raw)) {
        text_info.set("nothing to save");
        return;
    }
    LogFile file;
    if (file.append(saved_path())) {
        text_info.set("cannot write LUTRON/saved.txt");
        return;
    }
    const lutron::Frame& f = raw->frame;
    file.write_raw(to_string_hex(f.id, 8) + ',' + to_string_hex(f.k, 4) + ',' + to_string_dec_uint(f.button) + ',' + to_string_dec_uint(f.toggle) + ',' + hex_bytes(f.raw, lutron::frame_bytes));
    text_info.set("saved " + label(f.id) + " " + button_text(f.button));
}

LutronRxView::~LutronRxView() {
    receiver_model.disable();
    baseband::shutdown();
}

}  // namespace ui::external_app::lutron_rx

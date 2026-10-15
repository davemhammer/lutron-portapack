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

#ifndef __UI_LUTRON_TX_H__
#define __UI_LUTRON_TX_H__

#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_transmitter.hpp"
#include "app_settings.hpp"
#include "radio_state.hpp"
#include "message.hpp"
#include "lutron_pico.hpp"

#include <string>

namespace ui::external_app::lutron_tx {

/* Sends one Pico press or release, or a dimmer set-level.
 * Prev/Next walk LUTRON/devices.txt and LUTRON/aliases.txt (the list Lutron RX records).
 * Serial types an id. Name stores an alias for the id on screen. START asks once, then sends.
 * PICO sends the button. DIM sends Lvl (0-100) to that serial.
 * The set-level header comes from LUTRON/subnet.txt, which Lutron RX writes.
 * Gain comes up at 0 and the RF amp off. Sample rate stays 1 MHz: the modulator is 16 samples per symbol. */
class LutronTxView : public View {
   public:
    LutronTxView(NavigationView& nav);

    void focus() override;

    std::string title() const override { return "Lutron TX"; };

   private:
    void load_files();
    void show_device();
    void show_status(const std::string& text);
    void disarm();
    bool dimmer_selected() const;
    bool current_id(uint32_t& id) const;
    void select_id(uint32_t id);
    void on_prev();
    void on_next();
    void on_serial();
    void on_name();
    void on_start();
    void on_stop();
    void on_done(bool done);
    bool start_tx();

    NavigationView& nav_;
    TxRadioState radio_state_{
        433592000,
        500000,
        1000000};
    app_settings::SettingsManager settings_{
        "tx_lutron", app_settings::Mode::TX};

    lutron::IdTable ids_{};
    lutron::AliasTable aliases_{};
    lutron::ToggleTable toggles_{};
    lutron::Subnet subnet_{};
    int selected_{-1};
    bool armed_{false};
    bool sending_{false};
    uint8_t level_type_{0x81};
    std::string serial_buffer_{};
    std::string name_buffer_{};

    Text text_device{
        {UI_POS_X(0), UI_POS_Y(0), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)},
        "no serial yet"};
    Button button_prev{
        {UI_POS_X(0), UI_POS_Y(1), UI_POS_WIDTH(7), UI_POS_HEIGHT(2)},
        "Prev"};
    Button button_next{
        {UI_POS_X(8), UI_POS_Y(1), UI_POS_WIDTH(7), UI_POS_HEIGHT(2)},
        "Next"};
    Button button_serial{
        {UI_POS_X(16), UI_POS_Y(1), UI_POS_WIDTH(7), UI_POS_HEIGHT(2)},
        "Serial"};
    Button button_name{
        {UI_POS_X(24), UI_POS_Y(1), UI_POS_WIDTH(6), UI_POS_HEIGHT(2)},
        "Name"};
    Text text_target{
        {UI_POS_X(0), UI_POS_Y(3), UI_POS_WIDTH(3), UI_POS_HEIGHT(1)},
        "To"};
    OptionsField options_target{
        {UI_POS_X(3), UI_POS_Y(3)},
        4,
        {{"PICO", 0},
         {"DIM", 1}}};
    Text text_level{
        {UI_POS_X(12), UI_POS_Y(3), UI_POS_WIDTH(4), UI_POS_HEIGHT(1)},
        "Lvl"};
    NumberField field_level{
        {UI_POS_X(16), UI_POS_Y(3)},
        3,
        {0, 100},
        1,
        ' '};
    Text text_button{
        {UI_POS_X(0), UI_POS_Y(4), UI_POS_WIDTH(4), UI_POS_HEIGHT(1)},
        "Btn"};
    OptionsField options_button{
        {UI_POS_X(4), UI_POS_Y(4)},
        8,
        {{"ON", lutron::button_on},
         {"OFF", lutron::button_off},
         {"RAISE", lutron::button_raise},
         {"LOWER", lutron::button_lower},
         {"FAVORITE", lutron::button_favorite}}};
    Text text_action{
        {UI_POS_X(14), UI_POS_Y(5), UI_POS_WIDTH(4), UI_POS_HEIGHT(1)},
        "Act"};
    OptionsField options_action{
        {UI_POS_X(18), UI_POS_Y(5)},
        8,
        {{"PRESS", 0},
         {"RELEASE", 1}}};
    Text text_repeats{
        {UI_POS_X(0), UI_POS_Y(5), UI_POS_WIDTH(4), UI_POS_HEIGHT(1)},
        "Rep"};
    NumberField field_repeats{
        {UI_POS_X(4), UI_POS_Y(5)},
        2,
        {1, lutron::tx_repeat_cap},
        1,
        ' '};
    Text text_status{
        {UI_POS_X(0), UI_POS_Y(7), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)},
        ""};
    Text text_hint{
        {UI_POS_X(0), UI_POS_Y(8), UI_POS_MAXWIDTH, UI_POS_HEIGHT(2)},
        "DIM uses Lvl. START twice."};

    TransmitterView tx_view{
        (int16_t)UI_POS_Y_BOTTOM(4),
        10000,
        150};

    MessageHandlerRegistration message_handler_tx_progress{
        Message::ID::TXProgress,
        [this](Message* const p) {
            this->on_done(static_cast<const TXProgressMessage*>(p)->done);
        }};
};

}  // namespace ui::external_app::lutron_tx

#endif /*__UI_LUTRON_TX_H__*/

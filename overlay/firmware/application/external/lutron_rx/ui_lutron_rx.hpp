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

#ifndef __UI_LUTRON_RX_H__
#define __UI_LUTRON_RX_H__

#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_freq_field.hpp"
#include "app_settings.hpp"
#include "radio_state.hpp"
#include "message.hpp"
#include "log_file.hpp"
#include "lutron_pico.hpp"

#include <string>

namespace ui::external_app::lutron_rx {

/* Receives Lutron Caseta Pico remotes (2-FSK 62.5 kbaud near 433.6 MHz) and lists the presses:
 * device (alias or serial number), button and its number, toggle bit, voted over the ~14 repeated bursts of each press.
 * Longer transmissions on the same frequency (the bridge's commands to dimmers) are shown apart.
 *
 * SD card, folder LUTRON (shared with Lutron TX):
 *   devices.txt  one serial per line, written when a Pico is heard
 *   aliases.txt  "02DD0ABF Kitchen" (Name appends a line; later lines win)
 *   saved.txt    presses stored with Save, "serial,k,button,toggle,24 bytes"
 *   log.csv      every burst, while the Log box is ticked
 *   subnet.txt   six hex digits, bytes 3-5 of a bridge frame, written when one is heard */
class LutronRxView : public View {
   public:
    LutronRxView(NavigationView& nav);
    ~LutronRxView();

    void focus() override;

    std::string title() const override { return "Lutron RX"; };

   private:
    /* the last burst of the press in progress (the long form if there was one), for Save and Name */
    struct PressRaw {
        lutron::Frame frame{};
        bool valid{false};
    };

    void on_frame(const LutronFrameMessage& message);
    void on_tick();
    void show_press(const lutron::PressVoter::Press& press);
    void on_name();
    void on_save();
    void on_prev();
    void on_next();
    void show_info();
    void show_id();
    void load_files();
    void note_subnet(const lutron::Frame& frame);
    void select_id(uint32_t id);
    bool record_id(uint32_t id);
    bool target(PressRaw*& raw);
    std::string label(uint64_t id) const;
    std::string describe(const lutron::PressVoter::Press& press) const;

    NavigationView& nav_;
    /* Picos transmit on one carrier near 433.594 MHz; 433.61 MHz decoded the most bursts, see lutron_demod.hpp */
    RxRadioState radio_state_{
        433610000 /* frequency */,
        500000 /* bandwidth */,
        1000000 /* sampling rate */,
        ReceiverModel::Mode::WidebandFMAudio};
    app_settings::SettingsManager settings_{
        "rx_lutron", app_settings::Mode::RX};

    lutron::PressVoter voter_{};
    lutron::AliasTable aliases_{};
    lutron::IdTable ids_{};
    int selected_{-1};
    PressRaw current_{};  // the press in progress
    PressRaw last_{};     // the last finished press
    std::string name_buffer_{};
    LogFile log_{};
    bool logging_{false};
    uint32_t presses_{0};
    uint32_t bursts_{0};
    uint32_t bridge_{0};
    uint32_t bad_{0};
    lutron::Subnet subnet_{};
    bool subnet_warned_{false};
    uint8_t bridge_prev_[12]{};

    RxFrequencyField field_frequency{
        {UI_POS_X(0), UI_POS_Y(0)},
        nav_};
    RFAmpField field_rf_amp{
        {UI_POS_X(13), UI_POS_Y(0)}};
    LNAGainField field_lna{
        {UI_POS_X(15), UI_POS_Y(0)}};
    VGAGainField field_vga{
        {UI_POS_X(18), UI_POS_Y(0)}};
    RSSI rssi{
        {UI_POS_X(21), 0, UI_POS_WIDTH_REMAINING(22), 4}};
    Channel channel{
        {UI_POS_X(21), 5, UI_POS_WIDTH_REMAINING(22), 4}};

    Text text_live{
        {UI_POS_X(0), UI_POS_Y(1), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)},
        "Waiting for a Pico"};
    Text text_stats{
        {UI_POS_X(0), UI_POS_Y(2), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)},
        ""};
    Text text_info{
        {UI_POS_X(0), UI_POS_Y(3), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)},
        ""};
    Text text_id{
        {UI_POS_X(0), UI_POS_Y(4), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)},
        "no Pico ID yet"};
    Button button_prev{
        {UI_POS_X(0), UI_POS_Y(5), UI_POS_WIDTH(5), UI_POS_HEIGHT(2)},
        "Prev"};
    Button button_next{
        {UI_POS_X(5), UI_POS_Y(5), UI_POS_WIDTH(5), UI_POS_HEIGHT(2)},
        "Next"};
    Button button_name{
        {UI_POS_X(11), UI_POS_Y(5), UI_POS_WIDTH(6), UI_POS_HEIGHT(2)},
        "Name"};
    Button button_save{
        {UI_POS_X(18), UI_POS_Y(5), UI_POS_WIDTH(6), UI_POS_HEIGHT(2)},
        "Save"};
    Checkbox check_log{
        {UI_POS_X(25), UI_POS_Y(5)},
        4,
        "Log"};
    Console console{
        {UI_POS_X(0), UI_POS_Y(7), UI_POS_MAXWIDTH, UI_POS_HEIGHT_REMAINING(8)}};

    MessageHandlerRegistration message_handler_frame{
        Message::ID::LutronFrame,
        [this](Message* const p) {
            this->on_frame(*static_cast<const LutronFrameMessage*>(p));
        }};
    MessageHandlerRegistration message_handler_frame_sync{
        Message::ID::DisplayFrameSync,
        [this](const Message* const) {
            this->on_tick();
        }};
};

}  // namespace ui::external_app::lutron_rx

#endif /*__UI_LUTRON_RX_H__*/

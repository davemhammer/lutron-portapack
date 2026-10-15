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

#include "proc_lutron_tx.hpp"

#include "event_m4.hpp"
#include "portapack_shared_memory.hpp"
#include "sine_table_int8.hpp"

#include <cstdint>

void LutronTXProcessor::execute(const buffer_c8_t& buffer) {
    if (!configured_) {
        for (size_t i = 0; i < buffer.count; i++) buffer.p[i] = {0, 0};
        return;
    }

    for (size_t i = 0; i < buffer.count; i++) {
        if (!mod_.running()) {
            buffer.p[i] = {0, 0};
            if (!done_sent_) {
                txprogress_message.done = true;
                shared_memory.application_queue.push(txprogress_message);
                done_sent_ = true;
                configured_ = false;
            }
            continue;
        }

        const int8_t tone = mod_.next();
        if (tone == 0) {
            phase_ = 0;
            buffer.p[i] = {0, 0};
            continue;
        }
        phase_ += static_cast<uint32_t>(static_cast<int32_t>(tone) * static_cast<int32_t>(deviation_step));
        buffer.p[i] = {
            sine_table_i8[(phase_ + 0x40000000u) >> 24],
            sine_table_i8[phase_ >> 24],
        };
    }
}

void LutronTXProcessor::on_message(const Message* const msg) {
    if (msg->id != Message::ID::LutronTxConfigure) return;
    const auto& message = *reinterpret_cast<const LutronTxConfigureMessage*>(msg);
    if (message.count == 0 || message.count > lutron::tx_repeat_cap) {
        mod_.stop();
        configured_ = false;
        phase_ = 0;
        return;
    }
    mod_.start(shared_memory.bb_data.data, message.count);
    phase_ = 0;
    done_sent_ = false;
    txprogress_message.progress = 0;
    txprogress_message.done = false;
    configured_ = true;
}

int main() {
    EventDispatcher event_dispatcher{std::make_unique<LutronTXProcessor>()};
    event_dispatcher.run();
    return 0;
}

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

#include "proc_lutron.hpp"

#include "portapack_shared_memory.hpp"
#include "event_m4.hpp"

#include <cstring>

void LutronProcessor::execute(const buffer_c8_t& buffer) {
    lutron::Demod::Result result;

    for (size_t i = 0; i < buffer.count; i++) {
        if (demod.process(buffer.p[i].real(), buffer.p[i].imag(), result)) {
            LutronFrameMessage message;
            memcpy(message.bytes, result.bytes, sizeof(message.bytes));
            message.bits = result.bits;
            message.long_frame = result.long_frame;
            message.level = result.level;
            shared_memory.application_queue.push(message);
        }
    }
}

int main() {
    EventDispatcher event_dispatcher{std::make_unique<LutronProcessor>()};
    event_dispatcher.run();
    return 0;
}

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

#ifndef __PROC_LUTRON_TX_H__
#define __PROC_LUTRON_TX_H__

#include "baseband_processor.hpp"
#include "baseband_thread.hpp"
#include "lutron_mod.hpp"

class LutronTXProcessor : public BasebandProcessor {
   public:
    void execute(const buffer_c8_t& buffer) override;
    void on_message(const Message* const msg) override;

   private:
    static constexpr uint32_t deviation_hz = 37000;
    /* phase step of one tone at 1 MHz: deviation * 2^32 / sample_rate */
    static constexpr uint32_t deviation_step = 158913790;

    bool configured_{false};
    bool done_sent_{false};
    uint32_t phase_{0};
    lutron::Mod mod_{};
    TXProgressMessage txprogress_message{};

    /* NB: Threads should be the last members in the class definition. */
    BasebandThread baseband_thread{lutron::Mod::sample_rate, this, baseband::Direction::Transmit};
};

#endif /*__PROC_LUTRON_TX_H__*/

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

#ifndef __PROC_LUTRON_H__
#define __PROC_LUTRON_H__

#include "baseband_processor.hpp"
#include "baseband_thread.hpp"
#include "rssi_thread.hpp"

#include "lutron_demod.hpp"

class LutronProcessor : public BasebandProcessor {
   public:
    void execute(const buffer_c8_t& buffer) override;

   private:
    static constexpr size_t baseband_fs = 1000000;  // 62.5 kbaud = 16 samples per symbol

    lutron::Demod demod{};

    /* NB: Threads should be the last members in the class definition. */
    BasebandThread baseband_thread{baseband_fs, this, baseband::Direction::Receive};
    RSSIThread rssi_thread{};
};

#endif /*__PROC_LUTRON_H__*/

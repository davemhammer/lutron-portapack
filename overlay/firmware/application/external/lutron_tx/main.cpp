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

#include "ui.hpp"
#include "ui_lutron_tx.hpp"
#include "ui_navigation.hpp"
#include "external_app.hpp"

namespace ui::external_app::lutron_tx {
void initialize_app(ui::NavigationView& nav) {
    nav.push<LutronTxView>();
}
}  // namespace ui::external_app::lutron_tx

extern "C" {

__attribute__((section(".external_app.app_lutron_tx.application_information"), used)) application_information_t _application_information_lutron_tx = {
    /*.memory_location = */ (uint8_t*)0x00000000,
    /*.externalAppEntry = */ ui::external_app::lutron_tx::initialize_app,
    /*.header_version = */ CURRENT_HEADER_VERSION,
    /*.app_version = */ VERSION_MD5,

    /*.app_name = */ "Lutron TX",
    /*.bitmap_data = */ {// a remote: outline with a column of buttons
                         0xF0, 0x0F, 0x10, 0x08, 0xD0, 0x0B, 0x10, 0x08, 0xD0, 0x0B, 0x10, 0x08, 0xD0, 0x0B, 0x10, 0x08, 0xD0, 0x0B, 0x10, 0x08, 0xD0, 0x0B, 0x10, 0x08, 0xD0, 0x0B, 0x10, 0x08, 0xF0, 0x0F, 0x00, 0x00},
    /*.icon_color = */ ui::Color::yellow().v,
    /*.menu_location = */ app_location_t::TX,
    /*.desired_menu_position = */ -1,

    /*.m4_app_tag = portapack::spi_flash::image_tag_lutron_tx */ {'P', 'L', 'U', 'X'},
    /*.m4_app_offset = */ 0x00000000,  // will be filled at compile time
};
}

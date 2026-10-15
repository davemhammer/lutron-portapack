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

/*
 * SD card files shared by Lutron RX and Lutron TX. Header only.
 *
 *   LUTRON/devices.txt  one serial per line, "02DD0ABF", recorded by RX or typed on TX
 *   LUTRON/aliases.txt  "02DD0ABF Kitchen", later lines for the same serial win
 *   LUTRON/toggles.txt  "02DD0ABF 1", the toggle bit the next press of that serial will use
 *   LUTRON/subnet.txt   "AABBCC", bytes 3-5 of a bridge frame, learned by RX
 */

#ifndef __LUTRON_STORE_H__
#define __LUTRON_STORE_H__

#include "file.hpp"
#include "file_reader.hpp"
#include "log_file.hpp"
#include "lutron_pico.hpp"
#include "string_format.hpp"

#include <string>

namespace lutron_store {

/* Returned by value. A function-local static path emits a global constructor.
 * That constructor is linked into the external app at 0xAExxxxxx while its
 * .init_array pointer stays in the M0 boot table and hard-faults before the LCD. */
inline std::filesystem::path devices_path() {
    return std::filesystem::path{u"LUTRON/devices.txt"};
}

inline std::filesystem::path aliases_path() {
    return std::filesystem::path{u"LUTRON/aliases.txt"};
}

inline std::filesystem::path toggles_path() {
    return std::filesystem::path{u"LUTRON/toggles.txt"};
}

inline std::filesystem::path subnet_path() {
    return std::filesystem::path{u"LUTRON/subnet.txt"};
}

template <typename Fn>
inline void for_each_line(const std::filesystem::path& path, Fn&& fn) {
    File file;
    if (file.open(path)) return;
    auto reader = FileLineReader(file);
    for (const auto& line : reader) fn(line.data(), line.size());
}

inline bool append_line(const std::filesystem::path& path, const std::string& line) {
    LogFile file;
    if (file.append(path)) return false;
    return !file.write_raw(line);
}

/* Loads devices, then any serial that already has an alias. */
inline void load_ids(lutron::IdTable& ids, lutron::AliasTable& aliases) {
    for_each_line(devices_path(), [&](const char* line, size_t n) { ids.add_line(line, n); });
    for_each_line(aliases_path(), [&](const char* line, size_t n) { aliases.add_line(line, n); });
    for (size_t i = 0; i < aliases.size(); i++) {
        const uint64_t id = aliases.id_at(i);
        if (id <= 0xFFFFFFFFULL) ids.add(static_cast<uint32_t>(id));
    }
}

inline void load_toggles(lutron::ToggleTable& toggles) {
    for_each_line(toggles_path(), [&](const char* line, size_t n) { toggles.add_line(line, n); });
}

/* Writes the serial once. False if the table is full or the card could not be written. */
inline bool remember_id(lutron::IdTable& ids, uint32_t id) {
    if (id == 0 || ids.contains(id)) return true;
    if (!ids.add(id)) return false;
    return append_line(devices_path(), to_string_hex(id, 8));
}

inline bool remember_alias(lutron::AliasTable& aliases, uint32_t id, const std::string& name) {
    if (name.empty() || !aliases.set(id, name.c_str())) return false;
    return append_line(aliases_path(), to_string_hex(id, 8) + ' ' + name);
}

inline bool remember_toggle(lutron::ToggleTable& toggles, uint32_t id, uint8_t bit) {
    if (!toggles.set(id, bit)) return false;
    return append_line(toggles_path(), to_string_hex(id, 8) + ' ' + ((bit & 1) ? '1' : '0'));
}

inline void load_subnet(lutron::Subnet& subnet) {
    subnet = {};
    for_each_line(subnet_path(), [&](const char* line, size_t n) {
        lutron::Subnet parsed{};
        if (lutron::parse_subnet_line(line, n, parsed)) subnet = parsed;
    });
}

/* Appends only when the three bytes change. False if the card could not be written. */
inline bool remember_subnet(lutron::Subnet& saved, const lutron::Subnet& seen) {
    if (!seen.known) return true;
    if (saved.known && saved.b0 == seen.b0 && saved.b1 == seen.b1 && saved.b2 == seen.b2) return true;
    if (!append_line(subnet_path(), to_string_hex(lutron::subnet_word(seen), 6))) return false;
    saved = seen;
    return true;
}

}  // namespace lutron_store

#endif /*__LUTRON_STORE_H__*/

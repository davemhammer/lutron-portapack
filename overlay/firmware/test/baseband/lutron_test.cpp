/*
 * Copyright (C) 2026 Dave Hammer
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
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

#include "lutron_demod.hpp"
#include "lutron_mod.hpp"
#include "lutron_pico.hpp"
#include "doctest.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace lutron;

namespace {

/* Frames received from a real Pico (device 0577285FB4, ON, toggle 1): the bytes after the sync word.
 * It sends two kinds of burst per slot, 241 and 244 bits long. */
const uint8_t real_on_241[payload_bytes] = {
    0x28, 0xD0, 0x12, 0x05, 0x77, 0x28, 0x5F, 0xB4, 0x24, 0x41, 0x60, 0x40, 0x12, 0x04, 0x01, 0x19,
    0xC6, 0x71, 0x9C, 0x67, 0x19, 0xC6, 0x71, 0x9C, 0x67, 0x7B, 0x5A, 0x10, 0x00, 0x00, 0x00, 0x00};
const size_t real_on_241_bits = 241;
const uint8_t real_on_244[payload_bytes] = {
    0x28, 0xC0, 0x12, 0x05, 0x77, 0x28, 0x5F, 0xB4, 0x24, 0x41, 0x60, 0x40, 0x12, 0x04, 0x01, 0x19,
    0xC6, 0x71, 0x9C, 0x67, 0x19, 0xC6, 0x71, 0x9C, 0x67, 0x5A, 0xDE, 0x70, 0x00, 0x00, 0x00, 0x00};
const size_t real_on_244_bits = 244;

std::vector<int> payload_as_bits(const uint8_t* payload, size_t bits) {
    std::vector<int> v;
    for (int i = 0; i < 90; i++) v.push_back((i & 1) ? 0 : 1);  // preamble 1010...
    for (int i = sync_bits - 1; i >= 0; i--) v.push_back((sync_word >> i) & 1);
    for (size_t i = 0; i < bits; i++) v.push_back((payload[i >> 3] >> (7 - (i & 7))) & 1);
    return v;
}

/* 1 MS/s 8-bit I/Q of a 2-FSK burst (16 us per bit, smoothed tone changes) between stretches of noise. */
std::vector<int8_t> make_signal(const std::vector<int>& bits, double carrier_hz, double deviation_hz, double amplitude, double noise, bool mirror) {
    uint32_t seed = 12345;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return ((seed >> 8) & 0xFFFF) / 65535.0 - 0.5;
    };

    std::vector<double> freq(3000, 0.0);  // silence before the burst
    std::vector<bool> active(3000, false);
    for (int b : bits) {
        for (int k = 0; k < Demod::sps; k++) {
            freq.push_back(carrier_hz + (b ? deviation_hz : -deviation_hz));
            active.push_back(true);
        }
    }
    for (int k = 0; k < 3000; k++) {
        freq.push_back(0.0);
        active.push_back(false);
    }

    std::vector<int8_t> out;
    double phase = 0.0;
    for (size_t n = 0; n < freq.size(); n++) {
        double f = 0.0;  // 8-sample moving average of the tone: the transitions are not instantaneous
        for (int k = 0; k < 8; k++) f += freq[n >= (size_t)k ? n - k : 0];
        f /= 8.0;
        phase += 2.0 * M_PI * f / 1e6;
        const double a = active[n] ? amplitude : 0.0;
        double i = a * std::cos(phase) + noise * rnd();
        double q = a * std::sin(phase) + noise * rnd();
        if (mirror) q = -q;
        out.push_back(static_cast<int8_t>(std::lround(i)));
        out.push_back(static_cast<int8_t>(std::lround(q)));
    }
    return out;
}

/* Runs the signal through the demodulator, returns the number of frames and keeps the last. */
int run(const std::vector<int8_t>& iq, Demod::Result& last) {
    Demod demod;
    int frames = 0;
    for (size_t k = 0; k + 1 < iq.size(); k += 2) {
        Demod::Result r;
        if (demod.process(iq[k], iq[k + 1], r)) {
            frames++;
            last = r;
        }
    }
    return frames;
}

/* the first `bits` bits of two payloads are the same */
bool same_bits(const uint8_t* a, const uint8_t* b, size_t bits) {
    for (size_t i = 0; i < bits; i++) {
        if (((a[i >> 3] >> (7 - (i & 7))) & 1) != ((b[i >> 3] >> (7 - (i & 7))) & 1)) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("lutron: fields of a real frame") {
    uint8_t body[frame_bytes];
    Build build{0x02dd0abf, button_on, Action::Press, 0, 1};
    REQUIRE(build_frame(build, body));
    uint8_t bits[payload_bytes]{};
    frame_to_bits(body, bits);
    const Frame f = parse_frame(bits, frame_bits);
    CHECK(f.valid);
    CHECK(f.id == 0x02dd0abfULL);
    CHECK(f.button == button_on);
    CHECK(f.toggle == 1);
    CHECK_FALSE(f.release);
    CHECK(strcmp(button_name(f.button), "ON") == 0);
}

TEST_CASE("lutron: button names") {
    CHECK(strcmp(button_name(button_on), "ON") == 0);
    CHECK(strcmp(button_name(button_off), "OFF") == 0);
    CHECK(strcmp(button_name(button_raise), "RAISE") == 0);
    CHECK(strcmp(button_name(button_lower), "LOWER") == 0);
    CHECK(strcmp(button_name(button_favorite), "FAVORITE") == 0);
    CHECK(strcmp(button_name(0), "?") == 0);
}

TEST_CASE("lutron: bits are read big-endian from the start of the payload") {
    const uint8_t bytes[] = {0xA5, 0x0F};
    CHECK(get_bits(bytes, 0, 4) == 0xA);
    CHECK(get_bits(bytes, 4, 8) == 0x50);
    CHECK(get_bits(bytes, 12, 4) == 0xF);
    CHECK(get_bits(bytes, 0, 16) == 0xA50F);
}

TEST_CASE("lutron: bursts of one press are grouped and voted, corrupted bursts are outvoted") {
    PressVoter voter;
    PressVoter::Press done{};
    const Frame good{0x0577285FB4ULL, button_on, 1};
    const Frame bad_id{0x0577285F94ULL, button_on, 1};
    const Frame bad_button{0x0577285FB4ULL, button_off, 1};

    for (int k = 0; k < 14; k++) {
        const Frame& f = (k == 3) ? bad_id : (k == 9) ? bad_button
                                                      : good;
        CHECK_FALSE(voter.add(f, 1000 + 75 * k, done));  // a burst every 75 ms
    }
    CHECK(voter.active());
    CHECK(voter.current().agree == 12);

    CHECK_FALSE(voter.idle(1000 + 75 * 13 + 499, done));  // not quiet long enough yet
    REQUIRE(voter.idle(1000 + 75 * 13 + 500, done));
    CHECK(done.frame == good);
    CHECK(done.agree == 12);
    CHECK(done.total == 14);
    CHECK(done.start_ms == 1000);
    CHECK(done.duration_ms == 75 * 13);
    CHECK_FALSE(voter.active());
}

TEST_CASE("lutron: a burst after a pause starts the next press and reports the previous one") {
    PressVoter voter;
    PressVoter::Press done{};
    const Frame on{0x0577285FB4ULL, button_on, 1};
    const Frame off{0x0577285FB4ULL, button_off, 0};

    CHECK_FALSE(voter.add(on, 0, done));
    CHECK_FALSE(voter.add(on, 75, done));
    REQUIRE(voter.add(off, 2000, done));  // the gap closes the ON press
    CHECK(done.frame == on);
    CHECK(done.total == 2);
    CHECK(voter.current().frame == off);
    CHECK(voter.current().total == 1);
}

TEST_CASE("lutron: the toggle bit separates two presses of the same button") {
    PressVoter voter;
    PressVoter::Press done{};
    const Frame t0{0x04EF405594ULL, button_raise, 0};
    const Frame t1{0x04EF405594ULL, button_raise, 1};
    voter.add(t0, 0, done);
    REQUIRE(voter.add(t1, 600, done));
    CHECK(done.frame.toggle == 0);
    CHECK(voter.current().frame.toggle == 1);
}

TEST_CASE("lutron: demodulates a clean burst and measures its length") {
    for (int kind = 0; kind < 2; kind++) {
        const uint8_t* payload = kind ? real_on_244 : real_on_241;
        const size_t bits = kind ? real_on_244_bits : real_on_241_bits;
        Demod::Result r;
        const auto iq = make_signal(payload_as_bits(payload, bits), 0, 37e3, 40, 2, false);
        REQUIRE(run(iq, r) == 1);
        CHECK_FALSE(r.long_frame);
        CHECK(r.bits == bits);
        CHECK(same_bits(r.bytes, payload, bits));
    }
}

TEST_CASE("lutron: the length of a burst is recovered exactly") {
    for (size_t bits : {110, 150, 200, 232, 239, 240, 241, 243, 244, 250}) {
        for (double amplitude : {12.0, 40.0, 110.0}) {
            Demod::Result r;
            const auto iq = make_signal(payload_as_bits(real_on_244, bits), 0, 37e3, amplitude, amplitude / 8 + 1, false);
            INFO("bits " << bits << " amplitude " << amplitude);
            REQUIRE(run(iq, r) == 1);
            CHECK_FALSE(r.long_frame);
            CHECK(r.bits == bits);
        }
    }
}

TEST_CASE("lutron: a transmission that goes on past a Pico's frame is flagged (the bridge)") {
    std::vector<int> bits = payload_as_bits(real_on_244, real_on_244_bits);
    for (int i = 0; i < 400; i++) bits.push_back((i * 7 + (i >> 3)) & 1);  // a long frame
    Demod::Result r;
    const auto iq = make_signal(bits, 0, 37e3, 40, 2, false);
    REQUIRE(run(iq, r) == 1);
    CHECK(r.long_frame);
    CHECK(r.bits == max_payload_bits);
}

TEST_CASE("lutron: demodulates with the I/Q spectrum mirrored") {
    Demod::Result r;
    const auto iq = make_signal(payload_as_bits(real_on_241, real_on_241_bits), 0, 37e3, 40, 2, true);
    REQUIRE(run(iq, r) == 1);
    CHECK(r.bits == real_on_241_bits);
    CHECK(same_bits(r.bytes, real_on_241, real_on_241_bits));
}

TEST_CASE("lutron: demodulates carriers 40 kHz off the tuned frequency") {
    for (double offset : {-35e3, -10e3, 10e3, 40e3}) {
        Demod::Result r;
        const auto iq = make_signal(payload_as_bits(real_on_241, real_on_241_bits), offset, 37e3, 40, 2, false);
        INFO("offset " << offset);
        REQUIRE(run(iq, r) == 1);
        CHECK(r.bits == real_on_241_bits);
        CHECK(same_bits(r.bytes, real_on_241, real_on_241_bits));
    }
}

TEST_CASE("lutron: demodulates a weak burst in noise") {
    Demod::Result r;
    const auto iq = make_signal(payload_as_bits(real_on_241, real_on_241_bits), 20e3, 37e3, 12, 6, false);
    REQUIRE(run(iq, r) == 1);
    CHECK(r.bits == real_on_241_bits);
    CHECK(same_bits(r.bytes, real_on_241, real_on_241_bits));
}

TEST_CASE("lutron: noise alone is not a frame") {
    Demod::Result r;
    const std::vector<int> nothing;
    const auto iq = make_signal(nothing, 0, 37e3, 0, 20, false);
    CHECK(run(iq, r) == 0);
}

TEST_CASE("lutron: a sync word without the preamble in front of it is ignored") {
    Demod::Result r;
    std::vector<int> bits;
    for (int i = 0; i < 90; i++) bits.push_back(1);  // no alternation: not a Lutron preamble
    for (int i = sync_bits - 1; i >= 0; i--) bits.push_back((sync_word >> i) & 1);
    for (size_t i = 0; i < 240; i++) bits.push_back(i & 1);
    const auto iq = make_signal(bits, 0, 37e3, 40, 2, false);
    CHECK(run(iq, r) == 0);
}

TEST_CASE("lutron: aliases are read from lines, later lines win") {
    AliasTable t;
    const char* l1 = "0577285FB4 Kitchen Pico";
    const char* l2 = "04ef405594,Basement\r";
    const char* l3 = "0577285FB4=Kitchen Dim";
    CHECK(t.add_line(l1, strlen(l1)));
    CHECK(t.add_line(l2, strlen(l2)));
    CHECK(strcmp(t.find(0x0577285FB4ULL), "Kitchen Pico") == 0);
    CHECK(strcmp(t.find(0x04EF405594ULL), "Basement") == 0);
    CHECK(t.add_line(l3, strlen(l3)));
    CHECK(strcmp(t.find(0x0577285FB4ULL), "Kitchen Dim") == 0);
    CHECK(t.size() == 2);
    CHECK(t.find(0x1234ULL) == nullptr);
}

TEST_CASE("lutron: alias lines that are not a device and a name are rejected") {
    AliasTable t;
    CHECK_FALSE(t.add_line("", 0));
    CHECK_FALSE(t.add_line("# comment", 9));
    CHECK_FALSE(t.add_line("0577285FB4", 10));       // no name
    CHECK_FALSE(t.add_line("0577285FB4   ", 13));    // only blanks after the id
    CHECK_FALSE(t.add_line("zz name", 7));           // not hex
    CHECK(t.size() == 0);
}

TEST_CASE("lutron: release is a separate press from the button that was held") {
    PressVoter voter;
    PressVoter::Press done{};
    const Frame press{0x02dd0abfULL, button_on, 1, false};
    const Frame release{0x02dd0abfULL, button_on, 1, true};
    CHECK_FALSE(voter.add(press, 0, done));
    REQUIRE(voter.add(release, 80, done));
    CHECK_FALSE(done.frame.release);
    CHECK(voter.current().frame.release);
}

static bool load_body(uint8_t* body, const char* hex) {
    for (size_t i = 0; i < frame_bytes; i++) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        body[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

static bool same_body(const uint8_t* body, const char* hex) {
    uint8_t expect[frame_bytes];
    return load_body(expect, hex) && std::memcmp(body, expect, frame_bytes) == 0;
}

TEST_CASE("lutron: built frames match captured and published packets") {
    struct Sample {
        uint32_t serial;
        uint8_t button;
        Action action;
        uint8_t seq;
        uint8_t toggle;
        const char* hex;
    };
    const Sample samples[] = {
        {0x02dd0abf, button_on, Action::Press, 0, 1, "8a0002dd0abf210403000200ccccccccccccccccccccadcf"},
        {0x02dd0abf, button_on, Action::Press, 6, 1, "8a0602dd0abf210403000200cccccccccccccccccccc7d46"},
        {0x02dd0abf, button_on, Action::Release, 0x0c, 0, "890c02dd0abf210e0300020102dd0abf004000200000149a"},
        {0x02a24c77, button_on, Action::Press, 0, 0, "880002a24c77210403000200cccccccccccccccccccc6141"},
        {0x02a24c77, button_on, Action::Release, 0, 0, "890002a24c77210e0300020102a24c77004000200000436b"},
        {0x02a24c77, button_off, Action::Press, 0, 0, "880002a24c77210403000400cccccccccccccccccccc2c50"},
        {0x02a24c77, button_favorite, Action::Press, 0, 1, "8a0002a24c77210403000300cccccccccccccccccccc60c3"},
        {0x02a24c77, button_raise, Action::Press, 0, 1, "8a0002a24c77210c0300050002a24c7700420003cccca2a7"},
        {0x02a24c77, button_lower, Action::Press, 0, 0, "880002a24c77210c0300060002a24c7700420002cccc5225"},
        {0x02a24c77, button_raise, Action::Release, 0, 1, "8b0002a24c77210e0300050102a24c77004202010133ea38"},
        {0x02a24c77, button_lower, Action::Release, 0, 0, "890002a24c77210e0300060102a24c770042020001331aba"},
    };
    for (const auto& sample : samples) {
        uint8_t body[frame_bytes];
        Build build{sample.serial, sample.button, sample.action, sample.seq, sample.toggle};
        INFO(sample.hex);
        REQUIRE(build_frame(build, body));
        CHECK(same_body(body, sample.hex));
    }
    uint8_t unused[frame_bytes];
    CHECK_FALSE(build_frame(Build{0, button_on, Action::Press, 0, 0}, unused));
    CHECK_FALSE(build_frame(Build{0x02dd0abf, 9, Action::Press, 0, 0}, unused));
}

TEST_CASE("lutron: set-level uses the subnet it is given") {
    const Subnet net{0x91, 0x6F, 0x00, true};
    uint8_t body[frame_bytes];
    REQUIRE(build_level(LevelBuild{0x033ca7a6, 0x83, 0x01, false, 0, net}, body));
    CHECK(same_body(body, "8301a1916f00210e00033ca7a6fe4002000000010000271d"));
    REQUIRE(build_level(LevelBuild{0x033ca7a6, 0x83, 0x82, true, 0, net}, body));
    CHECK(same_body(body, "8382a3916f00210e00033ca7a6fe4002000000010000626c"));
    REQUIRE(build_level(LevelBuild{0x033ca7a6, 0x81, 0x01, false, 100, net}, body));
    CHECK(same_body(body, "8101a1916f00210e00033ca7a6fe4002feff00010000be62"));
    Subnet learned{};
    REQUIRE(bridge_subnet(body, learned));
    CHECK(learned.b0 == 0x91);
    CHECK(learned.b1 == 0x6F);
    CHECK(learned.b2 == 0x00);
    uint8_t beacon[frame_bytes];
    REQUIRE(load_body(beacon, "9343a3916f00210c00ffffffffff0802916f1a02cccc5b47"));
    REQUIRE(bridge_subnet(beacon, learned));
    CHECK(subnet_word(learned) == 0x916F00);
    body[23] ^= 0xFF;
    CHECK_FALSE(bridge_subnet(body, learned));
    Subnet parsed{};
    REQUIRE(parse_subnet_line("916F00", 6, parsed));
    CHECK(subnet_word(parsed) == 0x916F00);
    CHECK_FALSE(parse_subnet_line("916F", 4, parsed));
    CHECK(level_word(0) == 0);
    CHECK(level_word(100) == level_full);
    CHECK(level_word(46) == 0x754C);
    CHECK_FALSE(build_level(LevelBuild{0, 0x81, 0x01, false, 0, net}, body));
    CHECK_FALSE(build_level(LevelBuild{0x033ca7a6, 0x80, 0x01, false, 0, net}, body));
    CHECK_FALSE(build_level(LevelBuild{0x033ca7a6, 0x81, 0x01, false, 101, net}, body));
    CHECK_FALSE(build_level(LevelBuild{0x033ca7a6, 0x81, 0x01, false, 0, {}}, body));
    uint8_t pico[frame_bytes];
    REQUIRE(build_frame(Build{0x02dd0abf, button_on, Action::Press, 0, 1}, pico));
    CHECK_FALSE(bridge_subnet(pico, learned));
}

TEST_CASE("lutron: a built burst is the python air format, then 75 ms of silence") {
    uint8_t body[frame_bytes];
    REQUIRE(build_frame(Build{0x02dd0abf, button_on, Action::Press, 0, 1}, body));
    const char* air =
        "10101010101010101010101010101010101010101010101010101010101010101010101010101010"
        "011111111100101111110011110111"
        "001010001100000000010010000001010111011100101000010111111011010000100100010000010110000001000000000100100000010000000001000110011100011001110001100111000110011100011001110001100111000110011100011001110001100111000110011101011010110111100111";
    Mod mod;
    mod.start(body, 1);
    REQUIRE(std::strlen(air) == Mod::burst_bits);
    for (uint32_t s = 0; s < Mod::burst_samples; s++) {
        const int8_t tone = mod.next();
        if ((s % Mod::sps) != 0) continue;
        const uint32_t bit = s / Mod::sps;
        INFO("bit " << bit);
        CHECK(tone == (air[bit] == '1' ? 1 : -1));
    }
    CHECK(mod.next() == 0);
    CHECK(mod.running());
    for (uint32_t s = Mod::burst_samples + 1; s < Mod::slot_samples - 1; s++) CHECK(mod.next() == 0);
    CHECK(mod.next() == 0);
    CHECK_FALSE(mod.running());
}

TEST_CASE("lutron: the second slot sends the second frame") {
    uint8_t frames[2][frame_bytes];
    REQUIRE(build_frame(Build{0x02a24c77, button_off, Action::Press, 0, 0}, frames[0]));
    REQUIRE(build_frame(Build{0x02a24c77, button_off, Action::Press, 6, 0}, frames[1]));
    uint8_t second[payload_bytes]{};
    frame_to_bits(frames[1], second);
    Mod mod;
    mod.start(&frames[0][0], 2);
    for (uint32_t s = 0; s < Mod::slot_samples; s++) mod.next();
    CHECK(mod.running());
    CHECK(mod.frame_index() == 1);
    const uint32_t lead = (Mod::preamble_bits + Mod::prefix_bits) * Mod::sps;
    for (uint32_t s = 0; s < lead; s++) mod.next();
    for (uint32_t bit = 0; bit < frame_bits; bit++) {
        for (int k = 0; k < Mod::sps; k++) {
            const int8_t tone = mod.next();
            if (k != 0) continue;
            const bool want = ((second[bit >> 3] >> (7 - (bit & 7))) & 1) != 0;
            INFO("body bit " << bit);
            CHECK((tone > 0) == want);
        }
    }
}

TEST_CASE("lutron: recorded ids are eight hex digits, aliases and toggles keep the latest line") {
    IdTable ids;
    const char* device = "02DD0ABF\n";
    CHECK(ids.add_line(device, strlen(device)));
    CHECK(ids.add_line("02dd0abf again", 14));
    CHECK(ids.size() == 1);
    CHECK(ids.at(0) == 0x02dd0abf);
    CHECK_FALSE(ids.add_line("02DD0ABF0 extra", 15));  // nine digits
    CHECK_FALSE(ids.add_line("02DD0AB", 7));
    uint32_t parsed = 0;
    CHECK(parse_id("02EE0135", 8, parsed));
    CHECK(parsed == 0x02ee0135);
    CHECK_FALSE(parse_id("00000000", 8, parsed));
    CHECK(ids.add(parsed));
    CHECK_FALSE(ids.add(parsed));

    ToggleTable toggles;
    const char* first = "02DD0ABF 0\n";
    const char* second = "02DD0ABF 1";
    CHECK(toggles.add_line(first, strlen(first)));
    CHECK(toggles.get(0x02dd0abf) == 0);
    CHECK(toggles.add_line(second, strlen(second)));
    CHECK(toggles.get(0x02dd0abf) == 1);
    CHECK(toggles.get(0x11111111) == 0);
}

TEST_CASE("lutron: names are cut to fit and the table has a limit") {
    AliasTable t;
    const char* l = "01 This name is far too long";
    CHECK(t.add_line(l, strlen(l)));
    CHECK(strlen(t.find(1)) == AliasTable::name_len);
    for (uint64_t id = 2; id <= AliasTable::max_entries; id++) CHECK(t.set(id, "x"));
    CHECK_FALSE(t.set(0x999, "one too many"));
    CHECK(t.set(5, "renamed"));  // an existing id can always be renamed
}

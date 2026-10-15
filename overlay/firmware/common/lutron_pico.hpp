/*
 * Lutron Caseta Pico remotes (Clear Connect Type A, 2-FSK 62.5 kbaud near 433.6 MHz): frame
 * layout, CRC, frame builder, and the press grouping / majority vote.
 *
 * Header only and free of firmware dependencies so the baseband (M4), the application (M0)
 * and the host unit tests share the same code. The byte layout matches l-re/lutron_pico.py.
 *
 * On the air: 80 bits of 1010..., UART bytes FF FA DE (the 28-bit sync word sits inside those
 * three bytes), then 24 bytes. Each byte is start 0, 8 data bits LSB first, stop 1. The carrier
 * then stops. The next burst is 75 ms after the start of this one. Repeats of one press step
 * byte 1 by +6. A new press flips the toggle bit.
 *
 * The 24 bytes, after FA DE:
 *   0       press 0x88/0x8a, release 0x89/0x8b. Bit 0x02 is the toggle. Bit 0x01 means release.
 *   1       sequence
 *   2-5     the Pico's serial, big endian (the number printed on the label)
 *   6       0x21
 *   7-11    on/off/favorite press: 04 03 00 <button> 00
 *           raise/lower press:     0c 03 00 <button> 00
 *           any release:           0e 03 00 <button> 01
 *   10      button: 2 On, 3 Favorite, 4 Off, 5 Raise, 6 Lower
 *   12-21   on/off/favorite press: ten 0xCC
 *           otherwise the serial again and a short tail (see build_frame)
 *   22,23   CRC-16, big endian, of the first 22 bytes. Polynomial 0xCA0F, init 0, no reflection,
 *           no final XOR. The data byte is added into the low half of the register. A textbook
 *           CRC of the same polynomial does not match; body_crc() is the update that does.
 */

#ifndef __LUTRON_PICO_H__
#define __LUTRON_PICO_H__

#include <cstddef>
#include <cstdint>

namespace lutron {

constexpr uint32_t sync_bits = 28;
constexpr uint32_t sync_word = 0b1111111100101111110011110111;
constexpr uint32_t sync_mask = (1u << sync_bits) - 1;

/* A Pico burst carries 240 bits after the sync word and then the carrier stops (it measures as 239 to
 * 244 because of the ring-down), a longer transmission (the bridge's commands to dimmers etc.) keeps
 * going. Up to max_payload_bits are kept, the real length is measured from the moment the carrier drops. */
constexpr size_t max_payload_bits = 256;
constexpr size_t payload_bytes = max_payload_bits / 8;
constexpr size_t frame_bytes = 24;
constexpr size_t frame_bits = frame_bytes * 10;
constexpr size_t min_pico_bits = 232;  // fewer than this is not a Pico frame

enum Button : uint8_t {
    button_on = 2,
    button_favorite = 3,
    button_off = 4,
    button_raise = 5,
    button_lower = 6,
};

inline const char* button_name(uint8_t number) {
    switch (number) {
        case button_on:
            return "ON";
        case button_off:
            return "OFF";
        case button_raise:
            return "RAISE";
        case button_lower:
            return "LOWER";
        case button_favorite:
            return "FAVORITE";
        default:
            return "?";
    }
}

/* n (<= 32) bits starting at bit `pos`, bit 0 = MSB of byte 0. */
inline uint32_t get_bits(const uint8_t* bytes, uint32_t pos, uint32_t n) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t bit = pos + i;
        value = (value << 1) | ((bytes[bit >> 3] >> (7 - (bit & 7))) & 1);
    }
    return value;
}

/* Lutron's CRC-16: polynomial 0xCA0F, initial value 0, MSB first, no final XOR. */
inline uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; bit++) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0xCA0F) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

/* What the CRC field should be for k = 0 (see the layout above). */
inline uint16_t frame_crc(const uint8_t* b) {
    return static_cast<uint16_t>(crc16(b, 20) ^ ((b[20] << 8) | b[21]));
}

/* The Pico's constant, from a frame it really sent. */
inline uint16_t frame_k(const uint8_t* b) {
    return static_cast<uint16_t>(frame_crc(b) ^ ((b[22] << 8) | b[23]));
}

/* The 24 bytes out of `bits` serial-style bits (MSB first): false if there are too few or a start/stop bit is wrong. */
inline bool unframe(const uint8_t* bits, size_t count, uint8_t* out) {
    if (count < frame_bits) return false;
    for (size_t i = 0; i < frame_bytes; i++) {
        const size_t at = i * 10;
        if (get_bits(bits, static_cast<uint32_t>(at), 1) != 0 || get_bits(bits, static_cast<uint32_t>(at + 9), 1) != 1) return false;
        uint8_t value = 0;
        for (int k = 0; k < 8; k++) value |= static_cast<uint8_t>(get_bits(bits, static_cast<uint32_t>(at + 1 + k), 1) << k);
        out[i] = value;
    }
    return true;
}

/* 24 bytes -> 240 serial-style bits (MSB first, left aligned). */
inline void frame_to_bits(const uint8_t* bytes, uint8_t* out) {
    for (size_t i = 0; i < payload_bytes; i++) out[i] = 0;
    for (size_t i = 0; i < frame_bytes; i++) {
        const size_t at = i * 10;
        // start bit 0 is already zero
        for (int k = 0; k < 8; k++) {
            if ((bytes[i] >> k) & 1) out[(at + 1 + k) >> 3] |= static_cast<uint8_t>(0x80 >> ((at + 1 + k) & 7));
        }
        out[(at + 9) >> 3] |= static_cast<uint8_t>(0x80 >> ((at + 9) & 7));  // stop bit 1
    }
}

struct Frame {
    uint64_t id;      // the serial number
    uint8_t button;   // button number
    uint8_t toggle;
    bool release;     // type 0x89/0x8b; a release is not another copy of the press
    uint8_t counter;
    bool long_form;
    bool valid;       // all start and stop bits were right
    uint16_t k;       // textbook CRC difference; 0 when body_crc matches the frame
    uint8_t raw[frame_bytes];

    /* what makes two bursts the same press */
    bool operator==(const Frame& o) const {
        return id == o.id && button == o.button && toggle == o.toggle && release == o.release;
    }
};

/* `bytes` holds the payload bits after the sync word, `bits` of them. */
inline Frame parse_frame(const uint8_t* bytes, size_t bits) {
    Frame f{};
    f.valid = unframe(bytes, bits, f.raw);
    f.id = (static_cast<uint64_t>(f.raw[2]) << 24) | (static_cast<uint64_t>(f.raw[3]) << 16) | (static_cast<uint64_t>(f.raw[4]) << 8) | f.raw[5];
    f.button = f.raw[10];
    f.toggle = (f.raw[0] >> 1) & 1;
    f.release = (f.raw[0] & 1) != 0;
    f.counter = f.raw[1];
    f.long_form = (f.raw[7] == 0x0C) || (f.raw[7] == 0x0E);
    f.k = frame_k(f.raw);
    return f;
}

/* CRC of the 22-byte body. The update adds each byte into the low half of the register. */
inline uint16_t body_crc(const uint8_t* data) {
    uint16_t crc = 0;
    for (size_t i = 0; i < 22; i++) {
        uint16_t table = static_cast<uint16_t>(crc >> 8) << 8;
        for (int bit = 0; bit < 8; bit++) {
            table = (table & 0x8000) ? static_cast<uint16_t>((table << 1) ^ 0xCA0F) : static_cast<uint16_t>(table << 1);
        }
        crc = static_cast<uint16_t>((((crc << 8) & 0xFF00) + data[i]) ^ table);
    }
    return crc;
}

inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Eight hex digits, the serial printed on the Pico. Zero is rejected. */
inline bool parse_id(const char* text, size_t length, uint32_t& id) {
    if (length != 8) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < 8; i++) {
        const int digit = hex_digit(text[i]);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    if (value == 0) return false;
    id = value;
    return true;
}

enum class Action : uint8_t { Press, Release };

constexpr uint8_t tx_repeat_default = 14;
constexpr uint8_t tx_repeat_cap = 20;

/* A press or release the bridge and a paired dimmer accept. False if the button is unknown. */
struct Build {
    uint32_t serial{0};
    uint8_t button{button_on};
    Action action{Action::Press};
    uint8_t seq{0};
    uint8_t toggle{0};
};

inline bool known_button(uint8_t button) {
    return button >= button_on && button <= button_lower;
}

inline bool is_pico_frame(const Frame& f) {
    return f.valid && ((f.raw[0] & 0xFC) == 0x88) && f.raw[6] == 0x21 && known_button(f.button);
}

inline bool build_frame(const Build& p, uint8_t* b) {
    if (!known_button(p.button) || p.serial == 0) return false;
    const bool dim = p.button == button_raise || p.button == button_lower;
    const bool press = p.action == Action::Press;
    uint8_t type = press ? 0x88 : 0x89;
    if (p.toggle & 1) type = static_cast<uint8_t>(type | 0x02);
    b[0] = type;
    b[1] = p.seq;
    for (int i = 0; i < 4; i++) b[2 + i] = static_cast<uint8_t>(p.serial >> (24 - 8 * i));
    b[6] = 0x21;
    b[8] = 0x03;
    b[9] = 0x00;
    b[10] = p.button;
    if (press && !dim) {
        b[7] = 0x04;
        b[11] = 0x00;
        for (int i = 12; i < 22; i++) b[i] = 0xCC;
    } else if (press) {
        b[7] = 0x0C;
        b[11] = 0x00;
        for (int i = 0; i < 4; i++) b[12 + i] = b[2 + i];
        b[16] = 0x00;
        b[17] = 0x42;
        b[18] = 0x00;
        b[19] = p.button == button_raise ? 0x03 : 0x02;
        b[20] = 0xCC;
        b[21] = 0xCC;
    } else if (!dim) {
        b[7] = 0x0E;
        b[11] = 0x01;
        for (int i = 0; i < 4; i++) b[12 + i] = b[2 + i];
        b[16] = 0x00;
        b[17] = 0x40;
        b[18] = 0x00;
        b[19] = static_cast<uint8_t>(0x1E + p.button);
        b[20] = 0x00;
        b[21] = 0x00;
    } else {
        b[7] = 0x0E;
        b[11] = 0x01;
        for (int i = 0; i < 4; i++) b[12 + i] = b[2 + i];
        b[16] = 0x00;
        b[17] = 0x42;
        b[18] = 0x02;
        b[19] = p.button == button_raise ? 0x01 : 0x00;
        b[20] = 0x01;
        b[21] = 0x33;
    }
    const uint16_t crc = body_crc(b);
    b[22] = static_cast<uint8_t>(crc >> 8);
    b[23] = static_cast<uint8_t>(crc);
    return true;
}

/* Bytes 3-5 of a bridge frame. Byte 2 is the A1/A3 flag and is not part of this.
 * Learned from the air and stored in LUTRON/subnet.txt. Not a device serial. */
struct Subnet {
    uint8_t b0{0};
    uint8_t b1{0};
    uint8_t b2{0};
    bool known{false};
};

inline uint32_t subnet_word(const Subnet& subnet) {
    return (static_cast<uint32_t>(subnet.b0) << 16) | (static_cast<uint32_t>(subnet.b1) << 8) | subnet.b2;
}

/* Six hex digits, "AABBCC". Later lines win. */
inline bool parse_subnet_line(const char* text, size_t length, Subnet& out) {
    size_t i = 0;
    while (i < length && (text[i] == ' ' || text[i] == '\t')) i++;
    if (length - i < 6) return false;
    uint32_t value = 0;
    for (int k = 0; k < 6; k++) {
        const int digit = hex_digit(text[i + static_cast<size_t>(k)]);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    for (size_t j = i + 6; j < length; j++) {
        if (text[j] != ' ' && text[j] != '\t' && text[j] != '\r') return false;
    }
    out.b0 = static_cast<uint8_t>(value >> 16);
    out.b1 = static_cast<uint8_t>(value >> 8);
    out.b2 = static_cast<uint8_t>(value);
    out.known = true;
    return true;
}

/* True when `raw` is a CRC-good bridge body (set-level or beacon). Pico frames are not. */
inline bool bridge_subnet(const uint8_t* raw, Subnet& out) {
    if (raw == nullptr) return false;
    if ((raw[0] & 0xFC) == 0x88) return false;
    if (raw[2] != 0xA1 && raw[2] != 0xA3) return false;
    if (raw[6] != 0x21) return false;
    const uint16_t crc = body_crc(raw);
    if (raw[22] != static_cast<uint8_t>(crc >> 8) || raw[23] != static_cast<uint8_t>(crc)) return false;
    out.b0 = raw[3];
    out.b1 = raw[4];
    out.b2 = raw[5];
    out.known = true;
    return true;
}

/* Bridge set-level. Bytes 9-12 are the dimmer serial. Bytes 3-5 come from `subnet`.
 * 0 is off and 100 is 0xFEFF. Fade is one quarter-second.
 * `train` false is the single 0xA1 burst (seq 0x01). `train` true is one slot of the
 * 0xA3 repeat train (seq starts at 0x82 and steps by 6). */
constexpr uint16_t level_full = 0xFEFF;

inline uint16_t level_word(uint8_t percent) {
    if (percent >= 100) return level_full;
    return static_cast<uint16_t>((static_cast<uint32_t>(percent) * level_full) / 100);
}

struct LevelBuild {
    uint32_t target{0};
    uint8_t type{0x81};  // 0x81, 0x82, or 0x83, one value for a whole command
    uint8_t seq{0x01};
    bool train{false};
    uint8_t percent{0};
    Subnet subnet{};
};

inline bool build_level(const LevelBuild& p, uint8_t* b) {
    if (p.target == 0 || p.percent > 100 || !p.subnet.known) return false;
    if (p.type < 0x81 || p.type > 0x83) return false;
    const uint16_t word = level_word(p.percent);
    b[0] = p.type;
    b[1] = p.seq;
    b[2] = p.train ? 0xA3 : 0xA1;
    b[3] = p.subnet.b0;
    b[4] = p.subnet.b1;
    b[5] = p.subnet.b2;
    b[6] = 0x21;
    b[7] = 0x0E;
    b[8] = 0x00;
    for (int i = 0; i < 4; i++) b[9 + i] = static_cast<uint8_t>(p.target >> (24 - 8 * i));
    b[13] = 0xFE;
    b[14] = 0x40;
    b[15] = 0x02;
    b[16] = static_cast<uint8_t>(word >> 8);
    b[17] = static_cast<uint8_t>(word);
    b[18] = 0x00;
    b[19] = 0x01;
    b[20] = 0x00;
    b[21] = 0x00;
    const uint16_t crc = body_crc(b);
    b[22] = static_cast<uint8_t>(crc >> 8);
    b[23] = static_cast<uint8_t>(crc);
    return true;
}

/*
 * Names for device ids, loaded from "id name" lines (later lines win): "0577285FB4 Kitchen Pico".
 * Fixed size, no allocation (the app has very little RAM).
 */
class AliasTable {
   public:
    static constexpr size_t max_entries = 32;
    static constexpr size_t name_len = 12;

    /* One line of the file; false if it is not "<1-10 hex digits><space or , or =><name>". */
    bool add_line(const char* line, size_t length) {
        size_t i = 0;
        uint64_t id = 0;
        size_t digits = 0;
        while (i < length && hex_value(line[i]) >= 0 && digits < 10) {
            id = (id << 4) | static_cast<uint64_t>(hex_value(line[i]));
            i++;
            digits++;
        }
        if (digits == 0 || i >= length) return false;
        if (line[i] != ' ' && line[i] != '\t' && line[i] != ',' && line[i] != '=') return false;
        while (i < length && (line[i] == ' ' || line[i] == '\t' || line[i] == ',' || line[i] == '=')) i++;
        size_t end = length;
        while (end > i && (line[end - 1] == ' ' || line[end - 1] == '\r' || line[end - 1] == '\n' || line[end - 1] == '\t')) end--;
        if (end == i) return false;
        char name[name_len + 1];
        size_t n = 0;
        while (i < end && n < name_len) name[n++] = line[i++];
        name[n] = 0;
        return set(id, name);
    }

    bool set(uint64_t id, const char* name) {
        size_t slot = count_;
        for (size_t k = 0; k < count_; k++) {
            if (id_[k] == id) {
                slot = k;
                break;
            }
        }
        if (slot == max_entries) return false;
        if (slot == count_) count_++;
        id_[slot] = id;
        size_t n = 0;
        while (name[n] && n < name_len) {
            name_[slot][n] = name[n];
            n++;
        }
        name_[slot][n] = 0;
        return true;
    }

    /* nullptr if the id has no name */
    const char* find(uint64_t id) const {
        for (size_t k = 0; k < count_; k++) {
            if (id_[k] == id) return name_[k];
        }
        return nullptr;
    }

    size_t size() const { return count_; }
    uint64_t id_at(size_t index) const { return id_[index]; }

   private:
    static int hex_value(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    uint64_t id_[max_entries]{};
    char name_[max_entries][name_len + 1]{};
    size_t count_{0};
};

/* Serial numbers recorded from the air or typed in. Fixed size, no allocation. */
class IdTable {
   public:
    static constexpr size_t max_entries = 32;

    bool contains(uint32_t id) const { return index_of(id) >= 0; }

    /* False when the table is full or the id is already present. */
    bool add(uint32_t id) {
        if (id == 0 || contains(id)) return false;
        if (count_ == max_entries) return false;
        id_[count_++] = id;
        return true;
    }

    /* A line "02DD0ABF" or "02DD0ABF anything". False if it is not an 8-digit serial. */
    bool add_line(const char* line, size_t length) {
        uint32_t id = 0;
        if (!take_id(line, length, id)) return false;
        if (contains(id)) return true;
        return add(id);
    }

    int index_of(uint32_t id) const {
        for (size_t i = 0; i < count_; i++) {
            if (id_[i] == id) return static_cast<int>(i);
        }
        return -1;
    }

    uint32_t at(size_t index) const { return id_[index]; }
    size_t size() const { return count_; }

   private:
    static bool take_id(const char* line, size_t length, uint32_t& id) {
        size_t i = 0;
        while (i < length && (line[i] == ' ' || line[i] == '\t')) i++;
        if (length - i < 8) return false;
        if (!parse_id(line + i, 8, id)) return false;
        if (i + 8 < length && hex_digit(line[i + 8]) >= 0) return false;
        return true;
    }

    uint32_t id_[max_entries]{};
    size_t count_{0};
};

/* Toggle bit of the next press for a serial. A press uses it and then flips it.
 * A release uses the other bit (the press it belongs to) and leaves this value alone. */
class ToggleTable {
   public:
    static constexpr size_t max_entries = 32;

    uint8_t get(uint32_t id) const {
        const int slot = index_of(id);
        return slot < 0 ? 0 : bit_[slot];
    }

    bool set(uint32_t id, uint8_t bit) {
        bit = static_cast<uint8_t>(bit & 1);
        const int slot = index_of(id);
        if (slot >= 0) {
            bit_[slot] = bit;
            return true;
        }
        if (count_ == max_entries) return false;
        id_[count_] = id;
        bit_[count_] = bit;
        count_++;
        return true;
    }

    /* "02DD0ABF 1". Later lines for the same serial win. */
    bool add_line(const char* line, size_t length) {
        size_t i = 0;
        while (i < length && (line[i] == ' ' || line[i] == '\t')) i++;
        uint32_t id = 0;
        if (length - i < 8 || !parse_id(line + i, 8, id)) return false;
        i += 8;
        if (i < length && hex_digit(line[i]) >= 0) return false;
        while (i < length && (line[i] == ' ' || line[i] == '\t' || line[i] == ',' || line[i] == '=')) i++;
        if (i >= length || (line[i] != '0' && line[i] != '1')) return false;
        return set(id, static_cast<uint8_t>(line[i] - '0'));
    }

    size_t size() const { return count_; }

   private:
    int index_of(uint32_t id) const {
        for (size_t i = 0; i < count_; i++) {
            if (id_[i] == id) return static_cast<int>(i);
        }
        return -1;
    }

    uint32_t id_[max_entries]{};
    uint8_t bit_[max_entries]{};
    size_t count_{0};
};


/*
 * Groups bursts into presses and votes. Bursts less than gap_ms apart belong to the same press;
 * the answer is the most common (id, button, toggle) of the group, `agree` of `total` bursts.
 * Keeps a few candidates only (a corrupted burst makes a one-off candidate); when the table is
 * full the weakest one is replaced, which is exact as long as the real answer is the majority.
 */
class PressVoter {
   public:
    static constexpr uint32_t gap_ms = 500;

    struct Press {
        Frame frame;
        uint16_t agree;
        uint16_t total;
        uint32_t start_ms;
        uint32_t duration_ms;  // first burst to last burst (a held button repeats until released)
    };

    /* Adds a burst. A quiet gap, or a release after the press it ends, finishes the previous
     * group and returns it in `done`. A bad burst of the same press stays in the group and is voted down. */
    bool add(const Frame& f, uint32_t now_ms, Press& done) {
        bool closed = false;
        const bool gap = total_ && (now_ms - last_ms_) >= gap_ms;
        const Frame& cur = result().frame;
        /* A release of this button ends the press even inside the gap. A corrupted burst
         * (different id, button, or toggle) stays in the group and is outvoted. */
        const bool release_edge = total_ && f.release != cur.release && f.id == cur.id && f.button == cur.button && f.toggle == cur.toggle;
        if (gap || release_edge) {
            done = result();
            closed = true;
            reset();
        }
        if (!total_) start_ms_ = now_ms;
        last_ms_ = now_ms;
        total_++;

        size_t weakest = 0;
        bool found = false;
        for (size_t i = 0; i < used_; i++) {
            if (cand_[i].frame == f) {
                cand_[i].count++;
                found = true;
                break;
            }
            if (cand_[i].count < cand_[weakest].count) weakest = i;
        }
        if (!found) {
            if (used_ < max_candidates) {
                cand_[used_++] = {f, 1};
            } else {
                cand_[weakest] = {f, 1};
            }
        }
        return closed;
    }

    /* Call regularly: closes the press once no burst has arrived for gap_ms. */
    bool idle(uint32_t now_ms, Press& done) {
        if (total_ && (now_ms - last_ms_) >= gap_ms) {
            done = result();
            reset();
            return true;
        }
        return false;
    }

    bool active() const { return total_ != 0; }

    /* The running answer of the press in progress (valid while active()). */
    Press current() const { return result(); }

   private:
    static constexpr size_t max_candidates = 4;

    struct Candidate {
        Frame frame;
        uint16_t count;
    };

    Candidate cand_[max_candidates]{};
    size_t used_{0};
    uint16_t total_{0};
    uint32_t start_ms_{0};
    uint32_t last_ms_{0};

    Press result() const {
        size_t best = 0;
        for (size_t i = 1; i < used_; i++) {
            if (cand_[i].count > cand_[best].count) best = i;
        }
        return {cand_[best].frame, cand_[best].count, total_, start_ms_, last_ms_ - start_ms_};
    }

    void reset() {
        used_ = 0;
        total_ = 0;
    }
};

}  // namespace lutron

#endif /*__LUTRON_PICO_H__*/

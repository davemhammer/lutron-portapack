/*
 * Lutron Pico transmitter timing. One sample at a time, at 1 MHz (16 samples per symbol).
 *
 * A burst is the air format of lutron_pico.py: 80 bits of 1010..., the UART bytes FF FA DE,
 * then the 24-byte body. The carrier is off for the rest of the 75 ms slot. The next frame
 * in the list starts the next slot. +1 is the high tone, -1 the low tone, 0 is carrier off.
 * The processor turns that into 2-FSK at ±37 kHz. Header only, no firmware dependencies.
 */

#ifndef __LUTRON_MOD_H__
#define __LUTRON_MOD_H__

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "lutron_pico.hpp"

namespace lutron {

class Mod {
   public:
    static constexpr uint32_t sample_rate = 1000000;
    static constexpr uint32_t slot_samples = 75000;  // 75 ms at 1 MHz
    static constexpr uint32_t preamble_bits = 80;
    static constexpr uint32_t prefix_bits = 30;  // FF FA DE, UART 8N1
    static constexpr int sps = 16;
    static constexpr uint32_t burst_bits = preamble_bits + prefix_bits + frame_bits;
    static constexpr uint32_t burst_samples = burst_bits * sps;

    /* `frames` is count bodies of frame_bytes, at most tx_repeat_cap. */
    void start(const uint8_t* frames, uint8_t count) {
        if (count > tx_repeat_cap) count = tx_repeat_cap;
        count_ = count;
        for (uint8_t i = 0; i < count_; i++) frame_to_bits(frames + i * frame_bytes, bits_[i]);
        frame_ = 0;
        sample_ = 0;
        running_ = count_ > 0;
    }

    void stop() { running_ = false; }

    bool running() const { return running_; }
    uint8_t frame_index() const { return frame_; }

    /* The next 1 MHz sample: +1 high tone, -1 low tone, 0 carrier off. */
    int8_t next() {
        if (!running_) return 0;
        const uint32_t in_slot = sample_ % slot_samples;
        int8_t tone = 0;
        if (in_slot < burst_samples) {
            const uint32_t bit = in_slot / sps;
            tone = air_bit(bit) ? static_cast<int8_t>(1) : static_cast<int8_t>(-1);
        }
        sample_++;
        if ((sample_ % slot_samples) == 0) {
            frame_++;
            if (frame_ >= count_) running_ = false;
        }
        return tone;
    }

   private:
    /* MSB of this word is the first air bit of FF FA DE. */
    static constexpr uint32_t prefix = 0b011111111100101111110011110111;

    bool air_bit(uint32_t bit) const {
        if (bit < preamble_bits) return (bit & 1u) == 0;
        bit -= preamble_bits;
        if (bit < prefix_bits) return ((prefix >> (prefix_bits - 1 - bit)) & 1u) != 0;
        bit -= prefix_bits;
        return ((bits_[frame_][bit >> 3] >> (7 - (bit & 7))) & 1) != 0;
    }

    uint8_t bits_[tx_repeat_cap][payload_bytes]{};
    uint8_t count_{0};
    uint8_t frame_{0};
    uint32_t sample_{0};
    bool running_{false};
};

}  // namespace lutron

#endif /*__LUTRON_MOD_H__*/

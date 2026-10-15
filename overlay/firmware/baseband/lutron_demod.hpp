/*
 * Lutron Pico receiver DSP: raw 8-bit I/Q at 1 MHz in, payload bytes of each burst out.
 *
 * 62.5 kbaud is exactly 16 samples per symbol at 1 MHz. Chain per sample:
 *   DC block -> 4-tap box filter on I and Q (channel filter) -> delayed-conjugate FM
 *   discriminator (lag 4) -> 6-tap box filter -> slicer around the midpoint of the tracked
 *   high/low levels -> symbol clock (nudged by every edge) -> sync word hunt -> payload.
 * Tuning: a Pico's three bursts per 75 ms slot share one carrier near 433.594 MHz with tones about
 * +-39 kHz from it. The discriminator output stops rising at a tone offset of about 62 kHz, so
 * the tuned frequency should be within about 20 kHz of the carrier. On a real recording tuning
 * to 433.61 MHz decoded the most bursts (83 of 84), 433.595 MHz gave 72, 433.64 MHz only 33.
 * The input must not clip: a Pico close to the antenna at full gain saturates the 8-bit
 * samples and the phase is lost, lower the LNA/VGA gain if bursts at high RSSI do not decode.
 * Bits are hunted in both polarities, so it does not matter which way the I/Q spectrum is
 * mirrored. Header only (no firmware dependencies) so it can run on the host in the tests.
 */

#ifndef __LUTRON_DEMOD_H__
#define __LUTRON_DEMOD_H__

#include <cstddef>
#include <cstdint>

#include "lutron_pico.hpp"

namespace lutron {

class Demod {
   public:
    static constexpr int sps = 16;  // samples per symbol at 1 MHz

    struct Result {
        uint8_t bytes[payload_bytes];  // payload bits after the sync word, in the sent polarity, left aligned
        uint16_t bits;                 // how many of them were sent: the carrier dropped after this many
        bool long_frame;               // the carrier did not drop within max_payload_bits: not a Pico (the bridge)
        uint8_t level;                 // signal level (peak-to-peak of the discriminator, coarse)
    };

    /* Feed one sample; true when a burst was received completely (`out` is filled). */
    bool process(int8_t i_in, int8_t q_in, Result& out) {
        /* DC block (the LO leakage of the radio sits at the tuned frequency) */
        i_dc_ += i_in - (i_dc_ >> 10);
        q_dc_ += q_in - (q_dc_ >> 10);
        const int32_t i_clean = i_in - (i_dc_ >> 10);
        const int32_t q_clean = q_in - (q_dc_ >> 10);

        /* channel filter: box filter on I and Q */
        i_sum_ += i_clean - i_box_[box_pos_];
        q_sum_ += q_clean - q_box_[box_pos_];
        i_box_[box_pos_] = i_clean;
        q_box_[box_pos_] = q_clean;
        box_pos_ = (box_pos_ + 1) % iq_box;

        /* signal power (smoothed over ~8 us): tells when the carrier stops */
        pwr_ += (((i_sum_ * i_sum_ + q_sum_ * q_sum_) >> 6) - pwr_) >> 3;
        bool dropped = false;
        if (collecting_) {
            since_sync_++;
            dropped = since_sync_ > 64 && pwr_ < (sync_pwr_ >> 2);
        }

        /* FM discriminator on the sample lag us back: Im(z[n] * conj(z[n-lag])) > 0 for a
         * positive frequency offset (the +-30..50 kHz tones turn 0.2 rad per us) */
        const int32_t i_old = i_hist_[hist_pos_];
        const int32_t q_old = q_hist_[hist_pos_];
        i_hist_[hist_pos_] = i_sum_;
        q_hist_[hist_pos_] = q_sum_;
        hist_pos_ = (hist_pos_ + 1) % lag;
        const int32_t disc = (q_sum_ * i_old - i_sum_ * q_old) >> 4;

        /* smooth over a fraction of a symbol */
        disc_sum_ += disc - disc_box_[disc_pos_];
        disc_box_[disc_pos_] = disc;
        disc_pos_ = (disc_pos_ + 1) % disc_box;
        const int32_t x = disc_sum_;

        /* slicer level: midpoint of the high and low peaks, both leaking towards each other */
        const int32_t span = hi_ - lo_;
        hi_ -= span >> level_leak;
        lo_ += span >> level_leak;
        if (x > hi_) hi_ = x;
        if (x < lo_) lo_ = x;
        const bool bit = x > ((hi_ + lo_) >> 1);

        /* symbol clock: an edge should land on phase 0, pull the clock half way towards that */
        if (bit != last_bit_) {
            last_bit_ = bit;
            const int err = (phase_ < sps / 2) ? phase_ : phase_ - sps;
            phase_ = (phase_ - err / 2) & (sps - 1);
        }
        phase_ = (phase_ + 1) & (sps - 1);
        if (dropped) {
            /* the symbols start 8 samples after the sync word's last sample point; the power takes
             * end_delay samples to fall below the threshold once the carrier stops */
            int32_t bits = (static_cast<int32_t>(since_sync_) - end_delay) / sps;
            if (bits < 0) bits = 0;
            if (static_cast<size_t>(bits) < count_) count_ = static_cast<size_t>(bits);
            return finish(false, out);
        }
        if (phase_ != sps / 2) return false;

        return on_symbol(bit, out);
    }

    /* Forgets everything (new frequency, new run). */
    void reset() { *this = Demod(); }

   private:
    /* filter lengths in samples (= us), tuned on a real recording of two Picos */
    static constexpr uint32_t iq_box = 4;    // channel filter: first null at 250 kHz, -4 dB at 130 kHz
    static constexpr uint32_t lag = 4;       // discriminator spacing
    static constexpr uint32_t disc_box = 6;  // smoothing of the discriminator output
    static constexpr int level_leak = 9;
    static constexpr uint32_t max_sync_errors = 2;      // bit errors tolerated in the 28 sync bits ...
    static constexpr uint32_t min_preamble_edges = 26;  // ... if 26 of the 31 bit pairs before it alternate

    static uint32_t popcount(uint32_t v) {
        v = v - ((v >> 1) & 0x55555555);
        v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
        return (((v + (v >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
    }

    int32_t i_dc_{0}, q_dc_{0};
    int32_t i_box_[8]{}, q_box_[8]{};
    int32_t i_sum_{0}, q_sum_{0};
    int32_t i_hist_[8]{}, q_hist_[8]{};
    int32_t disc_box_[16]{};
    int32_t disc_sum_{0};
    int32_t hi_{0}, lo_{0};
    uint32_t box_pos_{0}, hist_pos_{0}, disc_pos_{0};
    int phase_{0};
    bool last_bit_{false};

    static constexpr int32_t end_delay = -2;  // calibrated on bursts of known length: every value from -12 to +8 gives exact lengths, so this is the middle

    int32_t pwr_{0};
    uint32_t since_sync_{0};
    int32_t sync_pwr_{0};  // power when the sync word was found: the carrier is "gone" below a quarter of it
    uint64_t reg_{0};
    bool collecting_{false};
    bool inverted_{false};
    size_t count_{0};
    Result frame_{};

    bool finish(bool long_frame, Result& out) {
        collecting_ = false;
        if (count_ & 7) frame_.bytes[count_ >> 3] = static_cast<uint8_t>(frame_.bytes[count_ >> 3] << (8 - (count_ & 7)));
        frame_.bits = static_cast<uint16_t>(count_);
        frame_.long_frame = long_frame;
        const int32_t pp = (hi_ - lo_) >> 12;
        frame_.level = static_cast<uint8_t>(pp > 255 ? 255 : (pp < 0 ? 0 : pp));
        out = frame_;
        return true;
    }

    bool on_symbol(bool bit, Result& out) {
        if (collecting_) {
            const uint8_t b = (bit != inverted_) ? 1 : 0;
            frame_.bytes[count_ >> 3] = static_cast<uint8_t>((frame_.bytes[count_ >> 3] << 1) | b);
            if (++count_ < max_payload_bits) return false;
            return finish(true, out);  // still transmitting after a Pico's whole frame
        }

        reg_ = (reg_ << 1) | (bit ? 1 : 0);
        const uint32_t tail = static_cast<uint32_t>(reg_) & sync_mask;
        const uint32_t errors = popcount(tail ^ sync_word);
        const bool normal = errors <= max_sync_errors;
        const bool flipped = (sync_bits - errors) <= max_sync_errors;
        if (!normal && !flipped) return false;

        /* the sync word is preceded by the 1010... preamble: a long alternating run before it
         * is what makes a sync word with a bit error or two believable */
        const uint32_t pre = static_cast<uint32_t>(reg_ >> sync_bits);
        if (popcount((pre ^ (pre >> 1)) & 0x7FFFFFFF) < min_preamble_edges) return false;

        sync_pwr_ = pwr_;
        since_sync_ = 0;
        collecting_ = true;
        inverted_ = flipped;
        count_ = 0;
        for (auto& b : frame_.bytes) b = 0;
        return false;
    }
};

}  // namespace lutron

#endif /*__LUTRON_DEMOD_H__*/

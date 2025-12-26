/**
 * @file bpsk_demod.cpp
 * @brief BPSK/PSK demodulator implementation for Cospas-Sarsat
 */

#include "bpsk_demod.h"
#include "sarsat_decoder.h"
#include <cstring>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace sarsat {

BPSKDemodulator::BPSKDemodulator(float sample_rate, float symbol_rate)
    : sample_rate(sample_rate), symbol_rate(symbol_rate) {
    
    samples_per_symbol = sample_rate / symbol_rate;
    clock_freq = 1.0f / samples_per_symbol;
    
    // Initialize interpolation buffer (needs at least 4 samples for cubic)
    interp_buffer.resize(4, 0.0f);
    
    // Initialize symbol buffer (need enough for a complete frame + sync)
    // 15 bit sync + 9 frame sync + 120 message = 144 bits
    // With Manchester: 288 symbols
    soft_symbols.reserve(512);
    
    // Initialize bit buffer
    bit_buffer.reserve(256);
    
    // Initialize frame buffer (144 bits)
    frame_buffer.resize(FRAME_BITS, 0);
    
    // Calculate PLL loop filter coefficients
    // Using a 2nd order loop with damping factor ~0.707
    float wn = 2.0f * M_PI * pll_bandwidth / sample_rate;
    float damping = 0.707f;
    pll_alpha = 2.0f * damping * wn;
    pll_beta = wn * wn;
    
    // Calculate TED loop filter coefficients
    float ted_wn = 2.0f * M_PI * clock_bandwidth;
    ted_alpha = 2.0f * damping * ted_wn;
    ted_beta = ted_wn * ted_wn;
    
    initFilters();
    reset();
}

void BPSKDemodulator::initFilters() {
    // Design low-pass filter for symbol filtering
    // Cutoff at ~1.5x symbol rate
    float cutoff = 1.5f * symbol_rate / sample_rate;
    int taps = 31;
    
    lpf_coeffs.resize(taps);
    lpf_buffer.resize(taps, 0.0f);
    
    // Sinc-based FIR filter with Hamming window
    int M = taps - 1;
    for (int i = 0; i < taps; i++) {
        float n = i - M / 2.0f;
        if (std::abs(n) < 1e-6f) {
            lpf_coeffs[i] = 2.0f * cutoff;
        } else {
            lpf_coeffs[i] = std::sin(2.0f * M_PI * cutoff * n) / (M_PI * n);
        }
        // Hamming window
        lpf_coeffs[i] *= 0.54f - 0.46f * std::cos(2.0f * M_PI * i / M);
    }
    
    // Normalize
    float sum = 0.0f;
    for (auto& c : lpf_coeffs) sum += c;
    for (auto& c : lpf_coeffs) c /= sum;
}

void BPSKDemodulator::reset() {
    agc_gain = 1.0f;
    carrier_phase = 0.0f;
    carrier_freq = 0.0f;
    clock_phase = 0.0f;
    clock_freq = 1.0f / samples_per_symbol;
    
    std::fill(interp_buffer.begin(), interp_buffer.end(), 0.0f);
    interp_idx = 0;
    
    soft_symbols.clear();
    symbol_count = 0;
    
    bit_buffer.clear();
    bit_count = 0;
    last_symbol = false;
    
    sync_state = SyncState::SEARCHING;
    sync_bit_count = 0;
    sync_ones_count = 0;
    sync_shift_reg = 0;
    
    std::fill(frame_buffer.begin(), frame_buffer.end(), 0);
    frame_bit_count = 0;
    is_test_frame = false;
    
    signal_level = 0.0f;
    snr_estimate = 0.0f;
    freq_offset = 0.0f;
    is_locked = false;
    
    std::fill(lpf_buffer.begin(), lpf_buffer.end(), 0.0f);
    lpf_idx = 0;
}

void BPSKDemodulator::setSampleRate(float rate) {
    sample_rate = rate;
    samples_per_symbol = sample_rate / symbol_rate;
    clock_freq = 1.0f / samples_per_symbol;
    
    // Recalculate PLL coefficients
    float wn = 2.0f * M_PI * pll_bandwidth / sample_rate;
    float damping = 0.707f;
    pll_alpha = 2.0f * damping * wn;
    pll_beta = wn * wn;
    
    initFilters();
    reset();
}

void BPSKDemodulator::setSymbolRate(float rate) {
    symbol_rate = rate;
    samples_per_symbol = sample_rate / symbol_rate;
    clock_freq = 1.0f / samples_per_symbol;
    reset();
}

float BPSKDemodulator::lowPassFilter(float sample) {
    // Ring buffer FIR filter
    lpf_buffer[lpf_idx] = sample;
    
    float output = 0.0f;
    int idx = lpf_idx;
    for (size_t i = 0; i < lpf_coeffs.size(); i++) {
        output += lpf_coeffs[i] * lpf_buffer[idx];
        idx--;
        if (idx < 0) idx = lpf_coeffs.size() - 1;
    }
    
    lpf_idx = (lpf_idx + 1) % lpf_coeffs.size();
    return output;
}

void BPSKDemodulator::updateAGC(float sample) {
    float level = std::abs(sample);
    
    if (level > agc_target) {
        agc_gain *= (1.0f - agc_attack);
    } else {
        agc_gain *= (1.0f + agc_decay);
    }
    
    // Clamp gain
    agc_gain = std::max(0.01f, std::min(agc_gain, 100.0f));
    
    // Update signal level (smoothed)
    signal_level = 0.99f * signal_level + 0.01f * (level * agc_gain);
}

std::complex<float> BPSKDemodulator::carrierRecovery(std::complex<float> sample) {
    // Costas loop for carrier recovery
    // Rotate input by current carrier estimate
    std::complex<float> rotated = sample * std::polar(1.0f, -carrier_phase);
    
    // Phase error detector (I*Q for BPSK)
    float error = rotated.real() * rotated.imag();
    
    // Loop filter
    carrier_freq += pll_beta * error;
    carrier_phase += carrier_freq + pll_alpha * error;
    
    // Clamp frequency
    float max_freq_rad = 2.0f * M_PI * carrier_freq_max / sample_rate;
    carrier_freq = std::max(-max_freq_rad, std::min(carrier_freq, max_freq_rad));
    
    // Wrap phase
    carrier_phase = wrap_phase(carrier_phase);
    
    // Update frequency offset estimate
    freq_offset = carrier_freq * sample_rate / (2.0f * M_PI);
    
    return rotated;
}

float BPSKDemodulator::interpolate(float mu) {
    // Cubic interpolation
    // mu is the fractional sample position (0 to 1)
    float y0 = interp_buffer[0];
    float y1 = interp_buffer[1];
    float y2 = interp_buffer[2];
    float y3 = interp_buffer[3];
    
    // Catmull-Rom spline
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    
    return ((c3 * mu + c2) * mu + c1) * mu + c0;
}

float BPSKDemodulator::clockRecovery(float sample) {
    // Shift interpolation buffer
    interp_buffer[0] = interp_buffer[1];
    interp_buffer[1] = interp_buffer[2];
    interp_buffer[2] = interp_buffer[3];
    interp_buffer[3] = sample;
    interp_idx++;
    
    // Check if we have a symbol
    clock_phase += clock_freq;
    
    if (clock_phase >= 1.0f) {
        clock_phase -= 1.0f;
        
        // Interpolate at optimal point
        float symbol = interpolate(clock_phase);
        
        // Gardner timing error detector
        // Uses previous, current and mid-point samples
        static float prev_symbol = 0.0f;
        static float mid_symbol = 0.0f;
        
        // Store mid-point for next calculation
        float current_mid = interpolate(clock_phase + 0.5f);
        
        // TED calculation: (current - prev) * mid
        float ted = (symbol - prev_symbol) * mid_symbol;
        
        // Loop filter
        clock_freq += ted_beta * ted;
        clock_phase += ted_alpha * ted;
        
        // Clamp frequency to reasonable range (±10%)
        float nominal = 1.0f / samples_per_symbol;
        clock_freq = std::max(nominal * 0.9f, std::min(clock_freq, nominal * 1.1f));
        
        // Update for next iteration
        mid_symbol = current_mid;
        prev_symbol = symbol;
        
        // Check if locked
        is_locked = (std::abs(clock_freq - nominal) < nominal * 0.02f);
        
        return symbol;
    }
    
    return NAN;  // No symbol this sample
}

void BPSKDemodulator::process(const std::complex<float>* samples, int count) {
    for (int i = 0; i < count; i++) {
        // AGC
        std::complex<float> sample = samples[i] * agc_gain;
        updateAGC(std::abs(sample));
        
        // Carrier recovery
        std::complex<float> baseband = carrierRecovery(sample);
        
        // Take real part (BPSK)
        float real_sample = baseband.real();
        
        // Low-pass filter
        float filtered = lowPassFilter(real_sample);
        
        // Clock recovery
        float symbol = clockRecovery(filtered);
        
        if (!std::isnan(symbol)) {
            processSymbol(symbol);
        }
    }
}

void BPSKDemodulator::processAudio(const float* samples, int count) {
    // For audio input (from FM discriminator), we skip carrier recovery
    for (int i = 0; i < count; i++) {
        float sample = samples[i] * agc_gain;
        updateAGC(std::abs(sample));
        
        float filtered = lowPassFilter(sample);
        float symbol = clockRecovery(filtered);
        
        if (!std::isnan(symbol)) {
            processSymbol(symbol);
        }
    }
}

void BPSKDemodulator::processSymbol(float symbol) {
    // Update SNR estimate
    updateSNR(symbol);
    
    // Store soft symbol
    soft_symbols.push_back(symbol);
    symbol_count++;
    
    // Manchester decode
    manchesterDecode(symbol);
}

void BPSKDemodulator::manchesterDecode(float symbol) {
    // Hard decision
    bool current = (symbol > 0);
    
    // Manchester: bit 1 = high->low transition, bit 0 = low->high transition
    // We look at transitions between half-symbols
    
    static bool half_symbol = false;
    static int half_count = 0;
    
    half_count++;
    
    if (half_count == 2) {
        // Full symbol period - decode bit
        if (half_symbol && !current) {
            // High to low = bit 1
            processBit(1);
        } else if (!half_symbol && current) {
            // Low to high = bit 0
            processBit(0);
        } else {
            // Invalid transition - could be bit boundary
            // In case of sync issues, just output based on level
            processBit(current ? 0 : 1);
        }
        half_count = 0;
    }
    
    half_symbol = current;
    last_symbol = current;
}

void BPSKDemodulator::processBit(uint8_t bit) {
    bit_buffer.push_back(bit);
    bit_count++;
    
    switch (sync_state) {
        case SyncState::SEARCHING:
            searchBitSync(bit);
            break;
            
        case SyncState::BIT_SYNC:
            searchFrameSync(bit);
            break;
            
        case SyncState::FRAME_SYNC:
        case SyncState::RECEIVING:
            receiveFrame(bit);
            break;
    }
}

void BPSKDemodulator::searchBitSync(uint8_t bit) {
    // Look for 15 consecutive 1s (bit sync pattern)
    if (bit == 1) {
        sync_ones_count++;
        if (sync_ones_count >= 12) {  // Allow some margin
            sync_state = SyncState::BIT_SYNC;
            sync_bit_count = sync_ones_count;
            sync_shift_reg = 0;
        }
    } else {
        // Check if we had enough 1s before this 0
        if (sync_ones_count >= 10) {
            // Could be start of frame sync - include this 0
            sync_state = SyncState::BIT_SYNC;
            sync_bit_count = 0;
            sync_shift_reg = 0;
            // Process this bit as part of frame sync
            searchFrameSync(bit);
        }
        sync_ones_count = 0;
    }
}

void BPSKDemodulator::searchFrameSync(uint8_t bit) {
    // Shift in bit to sync register
    sync_shift_reg = ((sync_shift_reg << 1) | bit) & 0x1FF;  // 9 bits
    sync_bit_count++;
    
    // Check for frame sync patterns
    if (sync_shift_reg == FRAME_SYNC_NORMAL) {
        // Normal message
        sync_state = SyncState::RECEIVING;
        frame_bit_count = 0;
        is_test_frame = false;
        return;
    }
    
    if (sync_shift_reg == FRAME_SYNC_TEST) {
        // Self-test message
        sync_state = SyncState::RECEIVING;
        frame_bit_count = 0;
        is_test_frame = true;
        return;
    }
    
    // Timeout - go back to searching
    if (sync_bit_count > 30) {
        sync_state = SyncState::SEARCHING;
        sync_ones_count = 0;
    }
}

void BPSKDemodulator::receiveFrame(uint8_t bit) {
    if (frame_bit_count < FRAME_BITS) {
        // Actually only 120 bits after sync (PDF-1 + BCH-1 + PDF-2 + BCH-2)
        // But frame_buffer is sized for full 144
        frame_buffer[frame_bit_count++] = bit;
    }
    
    // Check if frame complete (120 bits of data)
    if (frame_bit_count >= 120) {
        frameComplete();
        
        // Reset for next frame
        sync_state = SyncState::SEARCHING;
        sync_ones_count = 0;
        frame_bit_count = 0;
    }
}

void BPSKDemodulator::frameComplete() {
    if (frame_callback) {
        frame_callback(frame_buffer.data(), frame_bit_count, snr_estimate);
    }
}

void BPSKDemodulator::updateSNR(float symbol) {
    // Estimate signal and noise power
    float sym_sq = symbol * symbol;
    
    // Symbol should be ±1 for BPSK
    float expected = 1.0f;
    float error = sym_sq - expected;
    
    // Update running estimates
    signal_power = 0.99f * signal_power + 0.01f * sym_sq;
    noise_power = 0.99f * noise_power + 0.01f * (error * error);
    
    // Calculate SNR in dB
    if (noise_power > 1e-10f) {
        float snr_linear = signal_power / noise_power;
        snr_estimate = 10.0f * std::log10(snr_linear);
    }
}

// SignalDetector implementation

SignalDetector::SignalDetector(float sample_rate) 
    : sample_rate(sample_rate) {
    fft_buffer.resize(fft_size);
}

bool SignalDetector::detect(const std::complex<float>* samples, int count) {
    // Simple energy detection
    float energy = 0.0f;
    
    for (int i = 0; i < count; i++) {
        energy += std::norm(samples[i]);
    }
    energy /= count;
    
    strength = 0.9f * strength + 0.1f * energy;
    
    if (strength > energy_threshold) {
        detection_count++;
        if (detection_count >= detection_threshold) {
            return true;
        }
    } else {
        detection_count = 0;
    }
    
    return false;
}

} // namespace sarsat

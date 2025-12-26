/**
 * @file bpsk_demod.h
 * @brief BPSK/PSK demodulator with clock recovery for Cospas-Sarsat 406 MHz
 * 
 * Signal characteristics:
 * - 400 bps symbol rate
 * - BPSK modulation (±1.1 rad phase deviation)
 * - Manchester/Biphase-L encoding on top
 * 
 * Processing chain:
 * - Low-pass filter
 * - AGC
 * - Carrier recovery (Costas loop or FM discriminator)
 * - Clock recovery (Mueller-Muller or Gardner)
 * - Manchester decoder
 * - Frame synchronization
 */

#pragma once

#include <cstdint>
#include <complex>
#include <vector>
#include <functional>
#include <cmath>

namespace sarsat {

// Forward declaration
struct BeaconMessage;

/**
 * @class BPSKDemodulator
 * @brief Demodulates BPSK signals from Cospas-Sarsat beacons
 */
class BPSKDemodulator {
public:
    using FrameCallback = std::function<void(const uint8_t* bits, int len, float snr)>;
    
    /**
     * @brief Constructor
     * @param sample_rate Input sample rate (Hz)
     * @param symbol_rate Symbol rate (400 bps for Cospas-Sarsat)
     */
    BPSKDemodulator(float sample_rate, float symbol_rate = 400.0f);
    ~BPSKDemodulator() = default;
    
    /**
     * @brief Process complex IQ samples
     * @param samples Input samples
     * @param count Number of samples
     */
    void process(const std::complex<float>* samples, int count);
    
    /**
     * @brief Process real audio samples (from FM discriminator output)
     * @param samples Input samples
     * @param count Number of samples
     */
    void processAudio(const float* samples, int count);
    
    /**
     * @brief Set callback for decoded frames
     * @param callback Function called when a frame is detected
     */
    void setFrameCallback(FrameCallback callback) { frame_callback = callback; }
    
    /**
     * @brief Reset demodulator state
     */
    void reset();
    
    /**
     * @brief Get current signal level (for display)
     */
    float getSignalLevel() const { return signal_level; }
    
    /**
     * @brief Get current SNR estimate
     */
    float getSNR() const { return snr_estimate; }
    
    /**
     * @brief Get carrier frequency offset estimate (Hz)
     */
    float getFrequencyOffset() const { return freq_offset; }
    
    /**
     * @brief Check if currently locked to a signal
     */
    bool isLocked() const { return is_locked; }
    
    /**
     * @brief Get number of symbols in sync buffer
     */
    int getSymbolCount() const { return symbol_count; }
    
    // Configuration
    void setSampleRate(float rate);
    void setSymbolRate(float rate);
    
private:
    // Sample rates
    float sample_rate;
    float symbol_rate;
    float samples_per_symbol;
    
    // AGC
    float agc_gain = 1.0f;
    float agc_target = 0.5f;
    float agc_attack = 0.01f;
    float agc_decay = 0.0001f;
    
    // Carrier recovery (Costas loop)
    float carrier_phase = 0.0f;
    float carrier_freq = 0.0f;
    float carrier_freq_max = 100.0f;  // Max frequency offset (Hz)
    float pll_bandwidth = 50.0f;      // PLL bandwidth (Hz)
    float pll_alpha, pll_beta;        // Loop filter coefficients
    
    // Clock recovery (Gardner timing error detector)
    float clock_phase = 0.0f;
    float clock_freq;
    float clock_bandwidth = 0.02f;    // Timing loop bandwidth (relative)
    float ted_alpha, ted_beta;        // TED loop filter coefficients
    
    // Interpolation buffer
    std::vector<float> interp_buffer;
    int interp_idx = 0;
    
    // Symbol buffer
    std::vector<float> soft_symbols;
    int symbol_count = 0;
    
    // Manchester decoding
    std::vector<uint8_t> bit_buffer;
    int bit_count = 0;
    bool last_symbol = false;
    
    // Frame synchronization
    enum class SyncState {
        SEARCHING,      // Looking for bit sync pattern
        BIT_SYNC,       // Found bit sync, looking for frame sync
        FRAME_SYNC,     // Found frame sync, collecting data
        RECEIVING       // Receiving message data
    };
    SyncState sync_state = SyncState::SEARCHING;
    
    int sync_bit_count = 0;
    int sync_ones_count = 0;
    uint16_t sync_shift_reg = 0;
    
    // Frame buffer
    std::vector<uint8_t> frame_buffer;
    int frame_bit_count = 0;
    bool is_test_frame = false;
    
    // Status
    float signal_level = 0.0f;
    float snr_estimate = 0.0f;
    float freq_offset = 0.0f;
    bool is_locked = false;
    
    // Noise estimation for SNR
    float noise_power = 0.001f;
    float signal_power = 0.0f;
    
    // Callback
    FrameCallback frame_callback;
    
    // Low-pass filter
    std::vector<float> lpf_coeffs;
    std::vector<float> lpf_buffer;
    int lpf_idx = 0;
    
    // Processing functions
    void initFilters();
    float lowPassFilter(float sample);
    void updateAGC(float sample);
    std::complex<float> carrierRecovery(std::complex<float> sample);
    float clockRecovery(float sample);
    void manchesterDecode(float symbol);
    void processSymbol(float symbol);
    void processBit(uint8_t bit);
    void searchBitSync(uint8_t bit);
    void searchFrameSync(uint8_t bit);
    void receiveFrame(uint8_t bit);
    void frameComplete();
    void updateSNR(float symbol);
    
    // Interpolation
    float interpolate(float mu);
    
    // Utility
    static inline float wrap_phase(float phase) {
        while (phase > M_PI) phase -= 2.0f * M_PI;
        while (phase < -M_PI) phase += 2.0f * M_PI;
        return phase;
    }
};

/**
 * @class SignalDetector
 * @brief Detects presence of Cospas-Sarsat signal
 */
class SignalDetector {
public:
    SignalDetector(float sample_rate);
    
    /**
     * @brief Process samples and check for signal
     * @param samples Input samples
     * @param count Number of samples
     * @return true if signal detected
     */
    bool detect(const std::complex<float>* samples, int count);
    
    /**
     * @brief Get signal strength
     */
    float getStrength() const { return strength; }
    
    /**
     * @brief Get estimated frequency offset
     */
    float getFrequencyOffset() const { return freq_offset; }
    
private:
    float sample_rate;
    float strength = 0.0f;
    float freq_offset = 0.0f;
    
    // FFT for frequency detection
    int fft_size = 1024;
    std::vector<std::complex<float>> fft_buffer;
    
    // Energy detection
    float energy_threshold = 0.1f;
    int detection_count = 0;
    int detection_threshold = 10;
};

} // namespace sarsat

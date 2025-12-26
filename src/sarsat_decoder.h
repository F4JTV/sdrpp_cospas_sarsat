/**
 * @file sarsat_decoder.h
 * @brief Cospas-Sarsat 406 MHz beacon frame decoder
 * 
 * Based on dec406 by F4EHY (http://jgsenlis.free.fr/)
 * Adapted for SDR++ by F4JTV for ADRASEC 06
 * 
 * Protocol: Cospas-Sarsat T.001 Issue 3 Rev.14
 */

#pragma once

#include <cstdint>
#include <string>
#include <cmath>

namespace sarsat {

// Constants
constexpr int SYMBOL_RATE = 400;           // 400 bps
constexpr int FRAME_BITS = 144;            // Total frame bits (after sync)
constexpr int BIT_SYNC_LEN = 15;           // Bit sync pattern length
constexpr int FRAME_SYNC_LEN = 9;          // Frame sync pattern length
constexpr int PDF1_LEN = 61;               // PDF-1 field length
constexpr int BCH1_LEN = 21;               // BCH-1 parity bits
constexpr int PDF2_LEN = 26;               // PDF-2 field length  
constexpr int BCH2_LEN = 12;               // BCH-2 parity bits

// Frame sync patterns
constexpr uint16_t FRAME_SYNC_NORMAL = 0b000101111;  // Normal message
constexpr uint16_t FRAME_SYNC_TEST   = 0b011010000;  // Self-test message

// BCH Generator polynomials (from T.001 Annex B)
// BCH(82,61) shortened from BCH(127,106), t=3 errors
constexpr uint32_t BCH1_POLY = 0b1001101101100111100011;  // degree 21

// BCH(38,26) shortened from BCH(63,51), t=2 errors  
constexpr uint16_t BCH2_POLY = 0b1010100111001;           // degree 12

// Protocol codes (bits 36-39)
enum class ProtocolCode {
    ORBITOGRAPHY = 0b0010,
    ELT_SERIAL   = 0b0011,       // ELT serial user
    ELT_AIRCRAFT = 0b0101,       // ELT with aircraft 24-bit address
    ELT_OPERATOR = 0b0100,       // ELT with aircraft operator designator
    EPIRB_MMSI   = 0b0110,       // EPIRB with MMSI
    EPIRB_RADIO  = 0b1000,       // EPIRB with radio call sign
    SHIP_MMSI    = 0b1001,       // Ship security with MMSI
    PLB_SERIAL   = 0b1011,       // PLB serial identification
    NAT_LOC      = 0b1100,       // National location
    SPARE        = 0b1101,
    STD_TEST     = 0b1110,       // Standard test
    NAT_TEST     = 0b1111,       // National test
    ELT_DT       = 0b0001,       // ELT(DT) - Data Transmission
    UNKNOWN      = 0xFFFF
};

// Beacon types
enum class BeaconType {
    ELT,        // Emergency Locator Transmitter (aircraft)
    EPIRB,      // Emergency Position Indicating Radio Beacon (maritime)
    PLB,        // Personal Locator Beacon
    SSAS,       // Ship Security Alert System
    ELT_DT,     // ELT with Data Transmission
    UNKNOWN
};

// Position source (bit 110 in PDF-2)
enum class PositionSource {
    INTERNAL = 0,   // Internal navigation device
    EXTERNAL = 1    // External navigation device (GPS)
};

/**
 * @struct Position
 * @brief Decoded GPS position from beacon
 */
struct Position {
    bool valid = false;
    
    // Latitude
    bool north = true;              // true=N, false=S
    int lat_degrees = 0;            // 0-90
    int lat_minutes = 0;            // 0-59
    int lat_seconds = 0;            // 0-59 (resolution 4s)
    
    // Longitude
    bool east = true;               // true=E, false=W
    int lon_degrees = 0;            // 0-180
    int lon_minutes = 0;            // 0-59
    int lon_seconds = 0;            // 0-59 (resolution 4s)
    
    PositionSource source = PositionSource::INTERNAL;
    bool homing_121_5 = false;      // 121.5 MHz homing device present
    
    // Convert to decimal degrees
    double getLatitudeDecimal() const {
        double lat = lat_degrees + lat_minutes / 60.0 + lat_seconds / 3600.0;
        return north ? lat : -lat;
    }
    
    double getLongitudeDecimal() const {
        double lon = lon_degrees + lon_minutes / 60.0 + lon_seconds / 3600.0;
        return east ? lon : -lon;
    }
    
    // Format as DMS string
    std::string getLatitudeString() const {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d°%02d'%02d\"%c", 
                lat_degrees, lat_minutes, lat_seconds, north ? 'N' : 'S');
        return buf;
    }
    
    std::string getLongitudeString() const {
        char buf[32];
        snprintf(buf, sizeof(buf), "%03d°%02d'%02d\"%c",
                lon_degrees, lon_minutes, lon_seconds, east ? 'E' : 'W');
        return buf;
    }
};

/**
 * @struct BeaconMessage  
 * @brief Complete decoded beacon message
 */
struct BeaconMessage {
    bool valid = false;
    bool is_test = false;           // Self-test message flag
    
    // Identification
    bool long_message = false;      // true=144 bits, false=112 bits
    int country_code = 0;           // MID (Maritime ID Digit) or country code
    std::string country_name;
    ProtocolCode protocol = ProtocolCode::UNKNOWN;
    BeaconType beacon_type = BeaconType::UNKNOWN;
    
    // Beacon ID
    std::string hex_id;             // 15 Hex ID (bits 26-85)
    uint32_t serial_number = 0;     // Serial or certificate number
    uint32_t cert_number = 0;       // Type approval certificate
    
    // MMSI/Call sign (for maritime beacons)
    std::string mmsi;
    std::string call_sign;
    
    // Aircraft ID (for ELT)
    std::string aircraft_address;   // 24-bit ICAO address
    std::string aircraft_operator;  // Operator designator
    
    // Position
    Position position;
    
    // BCH status
    bool bch1_valid = false;
    bool bch2_valid = false;
    int bch1_errors = 0;            // Errors corrected
    int bch2_errors = 0;
    
    // Raw frame data
    uint8_t raw_bits[FRAME_BITS];
    
    // Timing info
    double timestamp = 0.0;         // Reception time
    double frequency_offset = 0.0;  // Measured frequency offset
    float snr = 0.0f;               // Signal to noise ratio
};

/**
 * @class SarsatDecoder
 * @brief Decodes Cospas-Sarsat 406 MHz beacon frames
 */
class SarsatDecoder {
public:
    SarsatDecoder();
    ~SarsatDecoder() = default;
    
    /**
     * @brief Decode a complete frame
     * @param bits Array of 144 bits (after sync removal)
     * @param msg Output decoded message
     * @return true if frame is valid
     */
    bool decode(const uint8_t* bits, BeaconMessage& msg);
    
    /**
     * @brief Verify and correct BCH-1 code
     * @param bits 82 bits (61 data + 21 parity)
     * @param errors_out Number of errors corrected
     * @return true if valid or correctable
     */
    bool verifyBCH1(uint8_t* bits, int& errors_out);
    
    /**
     * @brief Verify and correct BCH-2 code  
     * @param bits 38 bits (26 data + 12 parity)
     * @param errors_out Number of errors corrected
     * @return true if valid or correctable
     */
    bool verifyBCH2(uint8_t* bits, int& errors_out);
    
    /**
     * @brief Get country name from MID code
     * @param mid Maritime ID Digit (0-999)
     * @return Country name or "Unknown"
     */
    static std::string getCountryName(int mid);
    
    /**
     * @brief Get protocol description
     * @param code Protocol code
     * @return Protocol description string
     */
    static std::string getProtocolName(ProtocolCode code);
    
    /**
     * @brief Get beacon type string
     * @param type Beacon type enum
     * @return Beacon type string
     */
    static std::string getBeaconTypeName(BeaconType type);

private:
    // Decode specific fields
    void decodeIdentification(const uint8_t* pdf1, BeaconMessage& msg);
    void decodePosition(const uint8_t* pdf1, const uint8_t* pdf2, BeaconMessage& msg);
    void decodeCoarsePosition(const uint8_t* bits, Position& pos);
    void decodeFinePosition(const uint8_t* bits, Position& pos);
    
    // BCH support functions
    uint32_t calculateSyndrome(const uint8_t* bits, int n, uint32_t poly, int deg);
    bool tryCorrectErrors(uint8_t* bits, int n, uint32_t syndrome, uint32_t poly, int deg, int max_errors, int& corrected);
    
    // Bit manipulation helpers
    static uint32_t bitsToInt(const uint8_t* bits, int start, int len);
    static void extractBits(const uint8_t* src, uint8_t* dst, int start, int len);
    static std::string bitsToHex(const uint8_t* bits, int len);
    
    // Country code lookup table
    static const char* country_names[];
};

} // namespace sarsat

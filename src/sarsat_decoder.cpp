/**
 * @file sarsat_decoder.cpp
 * @brief Cospas-Sarsat 406 MHz beacon frame decoder implementation
 * 
 * Based on dec406 by F4EHY (http://jgsenlis.free.fr/)
 * Adapted for SDR++ by F4JTV for ADRASEC 06
 */

#include "sarsat_decoder.h"
#include <cstring>
#include <algorithm>

namespace sarsat {

// Country/MID code lookup table (main entries)
// Full list available at: https://www.itu.int/en/ITU-R/terrestrial/fmd/Pages/mid.aspx
const char* SarsatDecoder::country_names[] = {
    // Index by MID code - sparse array with common codes
    // 201-299: Europe
    // 201 Albania, 202 Andorra, 203 Austria, etc.
    nullptr  // Placeholder - actual implementation uses map
};

// Country code to name mapping based on ITU MID table
// https://www.itu.int/en/ITU-R/terrestrial/fmd/Pages/mid.aspx
static const struct { int code; const char* name; } country_table[] = {
    // Europe 201-279
    {201, "Albania"}, {202, "Andorra"}, {203, "Austria"}, 
    {204, "Azores"}, {205, "Belgium"}, {206, "Belarus"},
    {207, "Bulgaria"}, {208, "Vatican"}, {209, "Cyprus"},
    {210, "Cyprus"}, {211, "Germany"}, {212, "Cyprus"},
    {213, "Georgia"}, {214, "Moldova"}, {215, "Malta"},
    {216, "Armenia"}, {218, "Germany"}, {219, "Denmark"},
    {220, "Denmark"}, {224, "Spain"}, {225, "Spain"},
    {226, "France"}, {227, "France"}, {228, "France"},
    {229, "Malta"}, {230, "Finland"}, {231, "Faroe Islands"},
    {232, "United Kingdom"}, {233, "United Kingdom"}, {234, "United Kingdom"},
    {235, "United Kingdom"}, {236, "Gibraltar"}, 
    {237, "Greece"}, {238, "Croatia"}, {239, "Greece"},
    {240, "Greece"}, {241, "Greece"}, {242, "Morocco"},
    {243, "Hungary"}, {244, "Netherlands"}, {245, "Netherlands"},
    {246, "Netherlands"}, {247, "Italy"}, {248, "Malta"},
    {249, "Malta"}, {250, "Ireland"}, {251, "Iceland"},
    {252, "Liechtenstein"}, {253, "Luxembourg"}, {254, "Monaco"},
    {255, "Madeira"}, {256, "Malta"}, {257, "Norway"},
    {258, "Norway"}, {259, "Norway"}, {261, "Poland"},
    {262, "Montenegro"}, {263, "Portugal"}, {264, "Romania"},
    {265, "Sweden"}, {266, "Sweden"}, {267, "Slovakia"},
    {268, "San Marino"}, {269, "Switzerland"}, {270, "Czech Republic"},
    {271, "Turkey"}, {272, "Ukraine"}, {273, "Russia"},
    {274, "North Macedonia"}, {275, "Latvia"}, {276, "Estonia"},
    {277, "Lithuania"}, {278, "Slovenia"}, {279, "Serbia"},
    
    // Americas 301-379
    {301, "Anguilla"}, {303, "Alaska"}, {304, "Antigua and Barbuda"},
    {305, "Antigua and Barbuda"}, {306, "Bonaire/Sint Eustatius/Saba"}, {307, "Aruba"},
    {308, "Bahamas"}, {309, "Bahamas"}, {310, "Bermuda"},
    {311, "Bahamas"}, {312, "Belize"}, {314, "Barbados"},
    {316, "Canada"}, {319, "Cayman Islands"}, {321, "Costa Rica"},
    {323, "Cuba"}, {325, "Dominica"}, {327, "Dominican Republic"},
    {329, "Guadeloupe"}, {330, "Grenada"}, {331, "Greenland"},
    {332, "Guatemala"}, {334, "Honduras"}, {336, "Haiti"},
    {338, "USA"}, {339, "Jamaica"}, {341, "Saint Kitts and Nevis"},
    {343, "Saint Lucia"}, {345, "Mexico"}, {347, "Martinique"},
    {348, "Montserrat"}, {350, "Nicaragua"}, {351, "Panama"},
    {352, "Panama"}, {353, "Panama"}, {354, "Panama"},
    {355, "Panama"}, {356, "Panama"}, {357, "Panama"},
    {358, "Puerto Rico"}, {359, "El Salvador"}, {361, "Saint Pierre and Miquelon"},
    {362, "Trinidad and Tobago"}, {364, "Turks and Caicos Islands"}, {366, "USA"},
    {367, "USA"}, {368, "USA"}, {369, "USA"},
    {370, "Panama"}, {371, "Panama"}, {372, "Panama"},
    {373, "Panama"}, {374, "Panama"}, {375, "Saint Vincent and the Grenadines"},
    {376, "Saint Vincent and the Grenadines"}, {377, "Saint Vincent and the Grenadines"},
    {378, "British Virgin Islands"}, {379, "US Virgin Islands"},
    
    // Asia 401-499
    {401, "Afghanistan"}, {403, "Saudi Arabia"}, {405, "Bangladesh"},
    {408, "Bahrain"}, {410, "Bhutan"}, {412, "China"},
    {413, "China"}, {414, "China"}, {416, "Taiwan"},
    {417, "Sri Lanka"}, {419, "India"}, {422, "Iran"},
    {423, "Azerbaijan"}, {425, "Iraq"}, {428, "Israel"},
    {431, "Japan"}, {432, "Japan"}, {434, "Turkmenistan"},
    {436, "Kazakhstan"}, {437, "Uzbekistan"}, {438, "Jordan"},
    {440, "South Korea"}, {441, "South Korea"}, {443, "Palestine"},
    {445, "North Korea"}, {447, "Kuwait"}, {450, "Lebanon"},
    {451, "Kyrgyzstan"}, {453, "Macao"}, {455, "Maldives"},
    {457, "Mongolia"}, {459, "Nepal"}, {461, "Oman"},
    {463, "Pakistan"}, {466, "Qatar"}, {468, "Syria"},
    {470, "United Arab Emirates"}, {471, "United Arab Emirates"},
    {472, "Tajikistan"}, {473, "Yemen"}, {475, "Yemen"},
    {477, "Hong Kong"}, {478, "Bosnia and Herzegovina"},
    
    // Pacific 501-579
    {501, "Adelie Land"}, {503, "Australia"}, {506, "Myanmar"},
    {508, "Brunei"}, {510, "Micronesia"}, {511, "Palau"},
    {512, "New Zealand"}, {514, "Cambodia"}, {515, "Cambodia"},
    {516, "Christmas Island"}, {518, "Cook Islands"}, {520, "Fiji"},
    {523, "Cocos Islands"}, {525, "Indonesia"}, {529, "Kiribati"},
    {531, "Laos"}, {533, "Malaysia"}, {536, "Northern Mariana Islands"},
    {538, "Marshall Islands"}, {540, "New Caledonia"},
    {542, "Niue"}, {544, "Nauru"}, {546, "French Polynesia"},
    {548, "Philippines"}, {550, "Timor-Leste"}, {553, "Papua New Guinea"},
    {555, "Pitcairn Island"}, {557, "Solomon Islands"}, {559, "American Samoa"},
    {561, "Samoa"}, {563, "Singapore"}, {564, "Singapore"},
    {565, "Singapore"}, {566, "Singapore"}, {567, "Thailand"},
    {570, "Tonga"}, {572, "Tuvalu"}, {574, "Vietnam"},
    {576, "Vanuatu"}, {577, "Vanuatu"}, {578, "Wallis and Futuna"},
    
    // Africa 601-679
    {601, "South Africa"}, {603, "Angola"}, {605, "Algeria"},
    {607, "Saint Paul and Amsterdam Islands"}, {608, "Ascension Island"}, {609, "Burundi"},
    {610, "Benin"}, {611, "Botswana"}, {612, "Central African Republic"},
    {613, "Cameroon"}, {615, "Congo"}, {616, "Comoros"},
    {617, "Cape Verde"}, {618, "Crozet Archipelago"}, {619, "Ivory Coast"},
    {620, "Comoros"}, {621, "Djibouti"}, {622, "Egypt"},
    {624, "Ethiopia"}, {625, "Eritrea"}, {626, "Gabon"},
    {627, "Ghana"}, {629, "Gambia"}, {630, "Guinea-Bissau"},
    {631, "Equatorial Guinea"}, {632, "Guinea"}, {633, "Burkina Faso"},
    {634, "Kenya"}, {635, "Kerguelen Islands"}, {636, "Liberia"},
    {637, "Liberia"}, {638, "South Sudan"}, {642, "Libya"},
    {644, "Lesotho"}, {645, "Mauritius"}, {647, "Madagascar"},
    {649, "Mali"}, {650, "Mozambique"}, {654, "Mauritania"},
    {655, "Malawi"}, {656, "Niger"}, {657, "Nigeria"},
    {659, "Namibia"}, {660, "Reunion"}, {661, "Rwanda"},
    {662, "Sudan"}, {663, "Senegal"}, {664, "Seychelles"},
    {665, "Saint Helena"}, {666, "Somalia"}, {667, "Sierra Leone"},
    {668, "Sao Tome and Principe"}, {669, "Eswatini"}, {670, "Chad"},
    {671, "Togo"}, {672, "Tunisia"}, {674, "Tanzania"},
    {675, "Uganda"}, {676, "DR Congo"}, {677, "Tanzania"},
    {678, "Zambia"}, {679, "Zimbabwe"},
    
    // South America 701-775
    {701, "Argentina"}, {710, "Brazil"}, {720, "Bolivia"},
    {725, "Chile"}, {730, "Colombia"}, {735, "Ecuador"},
    {740, "Falkland Islands"}, {745, "French Guiana"},
    {750, "Guyana"}, {755, "Paraguay"}, {760, "Peru"},
    {765, "Suriname"}, {770, "Uruguay"}, {775, "Venezuela"},
    
    {0, nullptr}  // End marker
};


SarsatDecoder::SarsatDecoder() {
}

std::string SarsatDecoder::getCountryName(int mid) {
    for (int i = 0; country_table[i].name != nullptr; i++) {
        if (country_table[i].code == mid) {
            return country_table[i].name;
        }
    }
    
    // Check ranges for common countries
    if (mid >= 211 && mid <= 218) return "Germany";
    if (mid >= 224 && mid <= 225) return "Spain";
    if (mid >= 226 && mid <= 228) return "France";
    if (mid >= 232 && mid <= 235) return "United Kingdom";
    if (mid >= 237 && mid <= 241) return "Greece";
    if (mid >= 244 && mid <= 246) return "Netherlands";
    if (mid >= 257 && mid <= 259) return "Norway";
    if (mid >= 265 && mid <= 266) return "Sweden";
    if (mid >= 338 && mid <= 339) return "USA";
    if (mid >= 366 && mid <= 369) return "USA";
    if (mid >= 351 && mid <= 357) return "Panama";
    if (mid >= 370 && mid <= 374) return "Panama";
    if (mid >= 412 && mid <= 414) return "China";
    if (mid >= 431 && mid <= 432) return "Japan";
    if (mid >= 440 && mid <= 441) return "South Korea";
    if (mid >= 563 && mid <= 566) return "Singapore";
    if (mid >= 636 && mid <= 637) return "Liberia";
    
    return "Unknown";
}

std::string SarsatDecoder::getProtocolName(ProtocolCode code) {
    switch (code) {
        case ProtocolCode::ORBITOGRAPHY:  return "Orbitography";
        case ProtocolCode::ELT_SERIAL:    return "ELT with Serial Number";
        case ProtocolCode::ELT_AIRCRAFT:  return "ELT with 24-bit Aircraft Address";
        case ProtocolCode::ELT_OPERATOR:  return "ELT with Aircraft Operator";
        case ProtocolCode::EPIRB_MMSI:    return "EPIRB with MMSI";
        case ProtocolCode::EPIRB_RADIO:   return "EPIRB with Radio Call Sign";
        case ProtocolCode::SHIP_MMSI:     return "Ship Security with MMSI";
        case ProtocolCode::PLB_SERIAL:    return "PLB with Serial Number";
        case ProtocolCode::NAT_LOC:       return "National Location";
        case ProtocolCode::SPARE:         return "Spare";
        case ProtocolCode::STD_TEST:      return "Standard Test";
        case ProtocolCode::NAT_TEST:      return "National Test";
        case ProtocolCode::ELT_DT:        return "ELT(DT) - Data Transmission";
        default:                          return "Unknown Protocol";
    }
}

std::string SarsatDecoder::getBeaconTypeName(BeaconType type) {
    switch (type) {
        case BeaconType::ELT:     return "ELT (Aviation)";
        case BeaconType::EPIRB:   return "EPIRB (Maritime)";
        case BeaconType::PLB:     return "PLB (Personal)";
        case BeaconType::SSAS:    return "SSAS (Ship Security)";
        case BeaconType::ELT_DT:  return "ELT(DT) (Data)";
        default:                  return "Unknown Type";
    }
}

bool SarsatDecoder::decode(const uint8_t* bits, BeaconMessage& msg) {
    // Clear message
    msg = BeaconMessage();
    
    // Copy raw bits
    memcpy(msg.raw_bits, bits, FRAME_BITS);
    
    // Extract fields
    // Bit 25 (index 0): Format flag - 1=long, 0=short
    msg.long_message = (bits[0] == 1);
    
    if (!msg.long_message) {
        // Short message format not fully supported yet
        msg.valid = false;
        return false;
    }
    
    // Extract PDF-1 (61 bits: bits 25-85, indices 0-60)
    uint8_t pdf1[PDF1_LEN];
    extractBits(bits, pdf1, 0, PDF1_LEN);
    
    // Extract BCH-1 (21 bits: bits 86-106, indices 61-81)
    uint8_t bch1_data[PDF1_LEN + BCH1_LEN];
    extractBits(bits, bch1_data, 0, PDF1_LEN + BCH1_LEN);
    
    // Verify BCH-1
    msg.bch1_valid = verifyBCH1(bch1_data, msg.bch1_errors);
    
    // Extract PDF-2 (26 bits: bits 107-132, indices 82-107)
    uint8_t pdf2[PDF2_LEN];
    extractBits(bits, pdf2, PDF1_LEN + BCH1_LEN, PDF2_LEN);
    
    // Extract BCH-2 (12 bits: bits 133-144, indices 108-119)
    uint8_t bch2_data[PDF2_LEN + BCH2_LEN];
    extractBits(bits, bch2_data, PDF1_LEN + BCH1_LEN, PDF2_LEN + BCH2_LEN);
    
    // Verify BCH-2
    msg.bch2_valid = verifyBCH2(bch2_data, msg.bch2_errors);
    
    // Decode identification from PDF-1
    decodeIdentification(pdf1, msg);
    
    // Decode position from PDF-1 and PDF-2
    if (msg.long_message) {
        decodePosition(pdf1, pdf2, msg);
    }
    
    // Generate 15 Hex ID (bits 26-85 = indices 1-60 in PDF1 + some more)
    // Actually bits 26-85 means 60 bits starting from bit index 1 of our frame
    uint8_t hex_bits[60];
    extractBits(bits, hex_bits, 1, 60);
    msg.hex_id = bitsToHex(hex_bits, 60);
    
    // Frame is valid if at least BCH-1 is valid
    msg.valid = msg.bch1_valid;
    
    return msg.valid;
}

void SarsatDecoder::decodeIdentification(const uint8_t* pdf1, BeaconMessage& msg) {
    // Bit 26 (index 1): Protocol flag - 0=standard location, 1=user
    bool protocol_flag = (pdf1[1] == 1);
    
    // Bits 27-36 (indices 2-11): Country code (10 bits)
    msg.country_code = bitsToInt(pdf1, 2, 10);
    msg.country_name = getCountryName(msg.country_code);
    
    // Bits 37-40 (indices 12-15): Protocol code (4 bits)
    uint8_t proto = bitsToInt(pdf1, 12, 4);
    
    // Determine protocol based on protocol flag
    if (!protocol_flag) {
        // Standard location protocol
        switch (proto) {
            case 0b0010: msg.protocol = ProtocolCode::ORBITOGRAPHY; break;
            case 0b0011: msg.protocol = ProtocolCode::ELT_SERIAL; break;
            case 0b0101: msg.protocol = ProtocolCode::ELT_AIRCRAFT; break;
            case 0b0100: msg.protocol = ProtocolCode::ELT_OPERATOR; break;
            case 0b0110: msg.protocol = ProtocolCode::EPIRB_MMSI; break;
            case 0b1110: msg.protocol = ProtocolCode::STD_TEST; break;
            case 0b1111: msg.protocol = ProtocolCode::NAT_TEST; break;
            case 0b0001: msg.protocol = ProtocolCode::ELT_DT; break;
            default:     msg.protocol = ProtocolCode::UNKNOWN; break;
        }
    } else {
        // User protocol (different interpretation)
        switch (proto) {
            case 0b1000: msg.protocol = ProtocolCode::EPIRB_RADIO; break;
            case 0b1001: msg.protocol = ProtocolCode::SHIP_MMSI; break;
            case 0b1011: msg.protocol = ProtocolCode::PLB_SERIAL; break;
            case 0b1100: msg.protocol = ProtocolCode::NAT_LOC; break;
            case 0b1110: msg.protocol = ProtocolCode::STD_TEST; break;
            case 0b1111: msg.protocol = ProtocolCode::NAT_TEST; break;
            default:     msg.protocol = ProtocolCode::UNKNOWN; break;
        }
    }
    
    // Determine beacon type from protocol
    switch (msg.protocol) {
        case ProtocolCode::ELT_SERIAL:
        case ProtocolCode::ELT_AIRCRAFT:
        case ProtocolCode::ELT_OPERATOR:
            msg.beacon_type = BeaconType::ELT;
            break;
        case ProtocolCode::EPIRB_MMSI:
        case ProtocolCode::EPIRB_RADIO:
            msg.beacon_type = BeaconType::EPIRB;
            break;
        case ProtocolCode::PLB_SERIAL:
            msg.beacon_type = BeaconType::PLB;
            break;
        case ProtocolCode::SHIP_MMSI:
            msg.beacon_type = BeaconType::SSAS;
            break;
        case ProtocolCode::ELT_DT:
            msg.beacon_type = BeaconType::ELT_DT;
            break;
        default:
            msg.beacon_type = BeaconType::UNKNOWN;
            break;
    }
    
    // Decode serial/identification based on protocol
    // Bits 41-64 (indices 16-39): Identification data (24 bits for serial)
    
    switch (msg.protocol) {
        case ProtocolCode::ELT_SERIAL:
        case ProtocolCode::PLB_SERIAL:
            // Certificate number (bits 41-50) + Serial (bits 51-64)
            msg.cert_number = bitsToInt(pdf1, 16, 10);
            msg.serial_number = bitsToInt(pdf1, 26, 14);
            break;
            
        case ProtocolCode::ELT_AIRCRAFT:
            // 24-bit aircraft address (bits 41-64)
            {
                uint32_t addr = bitsToInt(pdf1, 16, 24);
                char buf[16];
                snprintf(buf, sizeof(buf), "%06X", addr);
                msg.aircraft_address = buf;
            }
            break;
            
        case ProtocolCode::EPIRB_MMSI:
        case ProtocolCode::SHIP_MMSI:
        case ProtocolCode::ORBITOGRAPHY:
            // MMSI (bits 41-60, 20 bits)
            // This is a truncated MMSI - the full 9-digit MMSI can be reconstructed
            // by prepending the country MID
            {
                uint32_t mmsi_part = bitsToInt(pdf1, 16, 20);
                char buf[16];
                snprintf(buf, sizeof(buf), "%u", mmsi_part);
                msg.mmsi = buf;
                
                // Also extract beacon number (bits 61-64, 4 bits)
                uint8_t beacon_num = bitsToInt(pdf1, 36, 4);
                msg.serial_number = beacon_num;  // Store beacon number as serial
            }
            break;
            
        default:
            // Generic serial number extraction
            msg.serial_number = bitsToInt(pdf1, 16, 24);
            break;
    }
}

void SarsatDecoder::decodePosition(const uint8_t* pdf1, const uint8_t* pdf2, BeaconMessage& msg) {
    // Decode coarse position from PDF-1 (bits 65-85 = indices 40-60)
    decodeCoarsePosition(pdf1 + 40, msg.position);
    
    // Check if PDF-2 contains valid position data
    // PDF-2 bits 107-110 (indices 0-3) should be 1101 for position data
    uint8_t pdf2_type = bitsToInt(pdf2, 0, 4);
    if (pdf2_type != 0b1101) {
        // No fine position data
        return;
    }
    
    // Decode fine position from PDF-2
    decodeFinePosition(pdf2, msg.position);
    
    msg.position.valid = true;
}

void SarsatDecoder::decodeCoarsePosition(const uint8_t* bits, Position& pos) {
    // Coarse position: 21 bits total
    // Bit 0: Latitude N/S (0=N, 1=S)
    // Bits 1-7: Latitude degrees (7 bits, 0-90)
    // Bits 8-9: Latitude minutes / 15 (2 bits, 0-3)
    // Bit 10: Longitude E/W (0=E, 1=W)
    // Bits 11-18: Longitude degrees (8 bits, 0-180)
    // Bits 19-20: Longitude minutes / 15 (2 bits, 0-3)
    
    pos.north = (bits[0] == 0);
    pos.lat_degrees = bitsToInt(bits, 1, 7);
    int lat_min_coarse = bitsToInt(bits, 8, 2) * 15;
    
    pos.east = (bits[10] == 0);
    pos.lon_degrees = bitsToInt(bits, 11, 8);
    int lon_min_coarse = bitsToInt(bits, 19, 2) * 15;
    
    // Store coarse values (will be refined by PDF-2)
    pos.lat_minutes = lat_min_coarse;
    pos.lat_seconds = 0;
    pos.lon_minutes = lon_min_coarse;
    pos.lon_seconds = 0;
}

void SarsatDecoder::decodeFinePosition(const uint8_t* bits, Position& pos) {
    // Fine position from PDF-2:
    // Bit 4: Position source (0=internal, 1=external GPS)
    // Bit 5: 121.5 MHz homing (0=no, 1=yes)
    // Bit 6: Latitude offset sign (0=negative, 1=positive)
    // Bits 7-11: Latitude minutes offset (5 bits, 0-14)
    // Bits 12-15: Latitude seconds / 4 (4 bits, 0-15 -> 0-60)
    // Bit 16: Longitude offset sign (0=negative, 1=positive)
    // Bits 17-21: Longitude minutes offset (5 bits, 0-14)
    // Bits 22-25: Longitude seconds / 4 (4 bits, 0-15 -> 0-60)
    
    pos.source = (bits[4] == 0) ? PositionSource::INTERNAL : PositionSource::EXTERNAL;
    pos.homing_121_5 = (bits[5] == 1);
    
    // Latitude refinement
    // Sign bit: 1 = positive offset, 0 = negative offset
    bool lat_offset_pos = (bits[6] == 1);
    int lat_min_offset = bitsToInt(bits, 7, 5);
    int lat_sec_4 = bitsToInt(bits, 12, 4);
    
    // Apply offset to coarse position
    int total_lat_min = pos.lat_minutes + (lat_offset_pos ? lat_min_offset : -lat_min_offset);
    if (total_lat_min < 0) {
        total_lat_min += 60;
        pos.lat_degrees--;
    } else if (total_lat_min >= 60) {
        total_lat_min -= 60;
        pos.lat_degrees++;
    }
    pos.lat_minutes = total_lat_min;
    pos.lat_seconds = lat_sec_4 * 4;
    
    // Longitude refinement
    // Sign bit: 1 = positive offset, 0 = negative offset
    bool lon_offset_pos = (bits[16] == 1);
    int lon_min_offset = bitsToInt(bits, 17, 5);
    int lon_sec_4 = bitsToInt(bits, 22, 4);
    
    int total_lon_min = pos.lon_minutes + (lon_offset_pos ? lon_min_offset : -lon_min_offset);
    if (total_lon_min < 0) {
        total_lon_min += 60;
        pos.lon_degrees--;
    } else if (total_lon_min >= 60) {
        total_lon_min -= 60;
        pos.lon_degrees++;
    }
    pos.lon_minutes = total_lon_min;
    pos.lon_seconds = lon_sec_4 * 4;
    
    pos.valid = true;
}

bool SarsatDecoder::verifyBCH1(uint8_t* bits, int& errors_out) {
    // BCH(82,61) - 61 data bits + 21 parity bits
    // Calculate syndrome
    uint32_t syndrome = calculateSyndrome(bits, 82, BCH1_POLY, 21);
    
    errors_out = 0;
    
    if (syndrome == 0) {
        return true;  // No errors
    }
    
    // Try to correct up to 3 errors
    return tryCorrectErrors(bits, 82, syndrome, BCH1_POLY, 21, 3, errors_out);
}

bool SarsatDecoder::verifyBCH2(uint8_t* bits, int& errors_out) {
    // BCH(38,26) - 26 data bits + 12 parity bits
    uint32_t syndrome = calculateSyndrome(bits, 38, BCH2_POLY, 12);
    
    errors_out = 0;
    
    if (syndrome == 0) {
        return true;
    }
    
    // Try to correct up to 2 errors
    return tryCorrectErrors(bits, 38, syndrome, BCH2_POLY, 12, 2, errors_out);
}

uint32_t SarsatDecoder::calculateSyndrome(const uint8_t* bits, int n, uint32_t poly, int deg) {
    // Calculate syndrome by dividing received polynomial by generator
    uint32_t remainder = 0;
    
    for (int i = 0; i < n; i++) {
        // Shift in next bit
        remainder = (remainder << 1) | bits[i];
        
        // If MSB is set, XOR with generator polynomial
        if (remainder & (1 << deg)) {
            remainder ^= poly;
        }
    }
    
    return remainder;
}

bool SarsatDecoder::tryCorrectErrors(uint8_t* bits, int n, uint32_t syndrome, 
                                      uint32_t poly, int deg, int max_errors, int& corrected) {
    // Simple error correction by trying single bit flips
    // For a more robust implementation, use syndrome decoding tables
    
    if (max_errors >= 1) {
        // Try single bit errors
        for (int i = 0; i < n; i++) {
            bits[i] ^= 1;
            uint32_t new_syndrome = calculateSyndrome(bits, n, poly, deg);
            if (new_syndrome == 0) {
                corrected = 1;
                return true;
            }
            bits[i] ^= 1;  // Restore
        }
    }
    
    if (max_errors >= 2) {
        // Try double bit errors
        for (int i = 0; i < n - 1; i++) {
            bits[i] ^= 1;
            for (int j = i + 1; j < n; j++) {
                bits[j] ^= 1;
                uint32_t new_syndrome = calculateSyndrome(bits, n, poly, deg);
                if (new_syndrome == 0) {
                    corrected = 2;
                    return true;
                }
                bits[j] ^= 1;
            }
            bits[i] ^= 1;
        }
    }
    
    if (max_errors >= 3) {
        // Try triple bit errors (expensive!)
        for (int i = 0; i < n - 2; i++) {
            bits[i] ^= 1;
            for (int j = i + 1; j < n - 1; j++) {
                bits[j] ^= 1;
                for (int k = j + 1; k < n; k++) {
                    bits[k] ^= 1;
                    uint32_t new_syndrome = calculateSyndrome(bits, n, poly, deg);
                    if (new_syndrome == 0) {
                        corrected = 3;
                        return true;
                    }
                    bits[k] ^= 1;
                }
                bits[j] ^= 1;
            }
            bits[i] ^= 1;
        }
    }
    
    corrected = 0;
    return false;  // Could not correct
}

uint32_t SarsatDecoder::bitsToInt(const uint8_t* bits, int start, int len) {
    uint32_t result = 0;
    for (int i = 0; i < len; i++) {
        result = (result << 1) | bits[start + i];
    }
    return result;
}

void SarsatDecoder::extractBits(const uint8_t* src, uint8_t* dst, int start, int len) {
    memcpy(dst, src + start, len);
}

std::string SarsatDecoder::bitsToHex(const uint8_t* bits, int len) {
    std::string result;
    result.reserve((len + 3) / 4);
    
    for (int i = 0; i < len; i += 4) {
        uint8_t nibble = 0;
        for (int j = 0; j < 4 && (i + j) < len; j++) {
            nibble = (nibble << 1) | bits[i + j];
        }
        result += "0123456789ABCDEF"[nibble];
    }
    
    return result;
}

} // namespace sarsat

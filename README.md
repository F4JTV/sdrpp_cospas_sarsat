# SDR++ Cospas-Sarsat 406 MHz Beacon Decoder

A real-time decoder module for SDR++ that receives and decodes Cospas-Sarsat 406 MHz emergency distress beacons (ELT, EPIRB, PLB).

## Features

- **Real-time decoding** of 406 MHz emergency beacons
- **Supported beacon types:**
  - ELT (Emergency Locator Transmitter) - Aviation
  - EPIRB (Emergency Position Indicating Radio Beacon) - Maritime
  - PLB (Personal Locator Beacon) - Personal
  - SSAS (Ship Security Alert System)
- **Decoded information:**
  - 15-character Hex ID
  - Country of registration (MID code)
  - Beacon type and protocol
  - MMSI (for maritime beacons)
  - GPS position (latitude/longitude)
  - BCH error correction status
- **Quick frequency buttons** for all Cospas-Sarsat channels
- **Log to file** with automatic timestamped filenames
- **OpenStreetMap integration** - click to view beacon position on map

## Supported Frequencies

| Frequency | Usage |
|-----------|-------|
| 406.025 MHz | Primary channel |
| 406.028 MHz | Secondary channel |
| 406.037 MHz | Additional channel |
| 406.040 MHz | Additional channel |

---

# Technical Details

## Signal Processing

1. **FM Demodulation** - 6 kHz bandwidth, ±3.5 kHz deviation
2. **AGC** - Automatic gain control for level normalization
3. **Biphase-L Decoding** - 400 baud symbol rate
4. **Frame Sync** - 15-bit preamble (all 1s) + 9-bit sync word
5. **BCH Correction** - BCH(82,61) t=3 and BCH(38,26) t=2

## Frame Structure (144 bits)

```
| Preamble | Sync  | PDF-1  | BCH-1  | PDF-2  | BCH-2  |
| 15 bits  | 9 bits| 61 bits| 21 bits| 26 bits| 12 bits|
```

## References

- Cospas-Sarsat T.001 Issue 3 Rev.14
- ITU-R M.633 - EPIRB transmission characteristics
- ITU MID Table - Country codes

---

# License

BSD 3-Clause License. See [LICENSE](LICENSE) file.

# Credits

- **F4EHY** - Original dec406 decoder (http://jgsenlis.free.fr/)
- **Alexandre Rouma** - SDR++ software

---

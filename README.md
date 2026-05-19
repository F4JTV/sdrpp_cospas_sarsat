# SDR++ Cospas-Sarsat 406 MHz Beacon Decoder

A real-time decoder module for [SDR++](https://github.com/AlexandreRouma/SDRPlusPlus)
that receives and decodes Cospas-Sarsat 406 MHz emergency distress beacons
(ELT, EPIRB, PLB, SSAS).

Developed by **F4JTV** for **ADRASEC 06**, based on the original `dec406`
decoder by **F4EHY**.

---

## Features

- Real-time decoding of 406 MHz emergency beacons
- Beacon types: **ELT** (aviation), **EPIRB** (maritime),
  **PLB** (personal), **SSAS** (ship security)
- Decoded information: 15-character Hex ID, country (MID code), beacon type,
  protocol, MMSI, GPS position, BCH error-correction status
- Quick frequency buttons for all Cospas-Sarsat channels
- Logging to timestamped file
- OpenStreetMap integration (click to view beacon position)

### Supported frequencies

| Frequency   | Usage             |
|-------------|-------------------|
| 406.025 MHz | Primary channel   |
| 406.028 MHz | Secondary channel |
| 406.037 MHz | Additional        |
| 406.040 MHz | Additional        |

---

## Build & Install

The module is built **out-of-tree**: it links against an already-compiled
SDR++ and is loaded as a plugin. You therefore need the SDR++ source tree
(and its build artifacts) available locally.

### Ubuntu 24.04

#### 1. Install dependencies and build SDR++

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
    libfftw3-dev libglfw3-dev libglew-dev libvolk-dev libzstd-dev \
    libsoapysdr-dev libairspy-dev librtlsdr-dev libhackrf-dev \
    libairspyhf-dev libiio-dev libad9361-dev libbladerf-dev \
    libcodec2-dev portaudio19-dev

# Clone and build SDR++
cd ~
git clone https://github.com/AlexandreRouma/SDRPlusPlus.git
cd SDRPlusPlus
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

#### 2. Build the module

```bash
cd ~
git clone https://github.com/F4JTV/sdrpp_cospas_sarsat.git
cd sdrpp_cospas_sarsat
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The CMakeLists auto-detects SDR++ in common locations (`~/SDRPlusPlus`,
`../SDRPlusPlus`, …). If it cannot find it, pass the path explicitly:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DSDRPP_SOURCE_DIR=/path/to/SDRPlusPlus
```

#### 3. Install the plugin

```bash
sudo cp cospas_sarsat_decoder.so /usr/lib/sdrpp/plugins/
```

---

### Windows 11

#### 1. Prerequisites

- **Visual Studio 2022** with the *"Desktop development with C++"* workload
- **Git for Windows**
- **CMake** (bundled with VS, otherwise install from https://cmake.org)
- **vcpkg** for SDR++ dependencies

```powershell
# Install vcpkg
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd C:\vcpkg
.\bootstrap-vcpkg.bat
```

#### 2. Build SDR++

Open **Developer PowerShell for VS 2022** and run:

```powershell
# SDR++ dependencies via vcpkg
C:\vcpkg\vcpkg.exe install fftw3:x64-windows glfw3:x64-windows `
    zstd:x64-windows volk:x64-windows portaudio:x64-windows

# Clone and build SDR++
cd C:\Dev
git clone https://github.com/AlexandreRouma/SDRPlusPlus.git
cd SDRPlusPlus
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

#### 3. Build the module

```powershell
cd C:\Dev
git clone https://github.com/F4JTV/sdrpp_cospas_sarsat.git
cd sdrpp_cospas_sarsat
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

The CMakeLists auto-detects SDR++ at `C:\Dev\SDRPlusPlus`. To override:

```powershell
cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DSDRPP_SOURCE_DIR=C:/path/to/SDRPlusPlus
```

#### 4. Install the plugin

Copy `build\Release\cospas_sarsat_decoder.dll` into your SDR++ installation:

```powershell
copy build\Release\cospas_sarsat_decoder.dll "C:\Program Files\SDR++\plugins\"
```

---

## Enabling the module in SDR++

1. Launch SDR++
2. Open the **Module Manager** (left-side menu)
3. In the dropdown at the bottom, select `cospas_sarsat_decoder`
4. Enter an instance name (e.g. `SARSAT`) and click **+**
5. Tune to a Cospas-Sarsat frequency (406.025 MHz is a good start)

---

## Technical details

**Signal processing chain**

1. FM demodulation — 6 kHz bandwidth, ±3.5 kHz deviation
2. AGC for level normalization
3. Biphase-L decoding at 400 baud
4. Frame sync: 15-bit preamble (all 1s) + 9-bit sync word
5. BCH error correction: BCH(82,61) t=3 and BCH(38,26) t=2

**Frame structure (144 bits)**

```
| Preamble | Sync   | PDF-1   | BCH-1   | PDF-2   | BCH-2   |
| 15 bits  | 9 bits | 61 bits | 21 bits | 26 bits | 12 bits |
```

**References**

- Cospas-Sarsat T.001 Issue 3 Rev.14
- ITU-R M.633 — EPIRB transmission characteristics
- ITU MID Table — Country codes

---

## License

BSD 3-Clause License — see [LICENSE](LICENSE).

## Credits

- **F4EHY** — original `dec406` decoder (http://jgsenlis.free.fr/)
- **Alexandre Rouma** — SDR++ software
- **F4JTV / ADRASEC 06** — SDR++ module port

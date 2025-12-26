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

# Building on Ubuntu 24.04 LTS

## 1. Install Dependencies

```bash
sudo apt update
sudo apt install -y build-essential cmake git libfftw3-dev libglfw3-dev libvolk2-dev \
    libzstd-dev libairspyhf-dev libairspy-dev librtlsdr-dev libhackrf-dev \
    librtaudio-dev libsoapysdr-dev libiio-dev libad9361-dev libglew-dev
```

## 2. Clone and Build SDR++

```bash
git clone https://github.com/AlexandreRouma/SDRPlusPlus.git
cd SDRPlusPlus
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
sudo make install
```

## 3. Build and Install the Cospas-Sarsat Module

```bash
# Extract the module source
cd sdrpp_cospas_sarsat

# Create build directory
mkdir build && cd build

# Configure - adjust SDRPP_MODULE_DIR if needed
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Install the module
sudo cp cospas_sarsat_decoder.so /usr/lib/sdrpp/plugins/
```

## 4. Enable the Module

1. Launch SDR++ : `sdrpp`
2. Go to **Module Manager** (in the menu bar)
3. In the dropdown, select `cospas_sarsat_decoder`
4. Click **+** to add the module
5. The "Cospas" module should appear in the left panel

## 5. Troubleshooting (Ubuntu)

```bash
# Check if the module is installed
ls -la /usr/lib/sdrpp/plugins/ | grep cospas

# Reset module configuration if needed
rm ~/.config/sdrpp/cospas_sarsat_config.json

# Run SDR++ from terminal to see errors
sdrpp
```

---

# Building on Windows 11

## 1. Install Prerequisites

### Visual Studio 2022

1. Download from https://visualstudio.microsoft.com/downloads/
2. Run the installer and select **"Desktop development with C++"**
3. Make sure these components are checked:
   - MSVC v143 - VS 2022 C++ x64/x86 build tools
   - Windows 11 SDK (or Windows 10 SDK)
   - C++ CMake tools for Windows

### Git for Windows

Download and install from https://git-scm.com/download/win

### vcpkg (Package Manager)

Open **PowerShell as Administrator**:

```powershell
cd C:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
```

### Install Dependencies

```powershell
cd C:\vcpkg
.\vcpkg install fftw3:x64-windows glfw3:x64-windows glew:x64-windows zstd:x64-windows rtaudio:x64-windows
```

This will take several minutes.

## 2. Clone and Build SDR++

Open **"Developer PowerShell for VS 2022"** (search in Start menu):

```powershell
cd C:\Dev
git clone https://github.com/AlexandreRouma/SDRPlusPlus.git
cd SDRPlusPlus
mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build . --config Release -j
```

## 3. Build the Cospas-Sarsat Module

Extract `sdrpp_cospas_sarsat.tar.gz` to `C:\Dev\sdrpp_cospas_sarsat`

In **Developer PowerShell for VS 2022**:

```powershell
cd C:\Dev\sdrpp_cospas_sarsat
mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
    -DCMAKE_BUILD_TYPE=Release

cmake --build . --config Release
```

## 4. Install the Module

```powershell
# Copy to SDR++ plugins folder
copy .\Release\cospas_sarsat_decoder.dll "C:\Dev\SDRPlusPlus\build\Release\plugins\"
```

Or if SDR++ is installed in Program Files:
```powershell
copy .\Release\cospas_sarsat_decoder.dll "C:\Program Files\SDR++\plugins\"
```

## 5. Run SDR++

```powershell
cd C:\Dev\SDRPlusPlus\build\Release
.\sdrpp.exe
```

## 6. Enable the Module

1. In SDR++, go to **Module Manager**
2. Select `cospas_sarsat_decoder` from the dropdown
3. Click **+** to add
4. The "Cospas" module appears in the left panel

## 7. Troubleshooting (Windows)

### Module doesn't load

Check for missing DLLs using [Dependencies](https://github.com/lucasg/Dependencies):

```powershell
# Copy vcpkg runtime DLLs if needed
copy C:\vcpkg\installed\x64-windows\bin\*.dll "C:\Dev\SDRPlusPlus\build\Release\"
```

### Reset configuration

```powershell
del "$env:APPDATA\sdrpp\cospas_sarsat_config.json"
```

### Build errors

Make sure you're using **Developer PowerShell for VS 2022**, not regular PowerShell.

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

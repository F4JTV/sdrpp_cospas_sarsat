/**
 * @file main.cpp
 * @brief SDR++ Cospas-Sarsat 406 MHz Beacon Decoder Module
 * 
 * Decodes emergency distress beacons (ELT, EPIRB, PLB) on 406 MHz
 * Based on dec406 by F4EHY - Adapted for SDR++ by F4JTV for ADRASEC 06
 */

#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/demod/fm.h>
#include <core.h>
#include <config.h>
#include <utils/flog.h>

#include <complex>
#include <vector>
#include <mutex>
#include <atomic>
#include <deque>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#endif

#include "sarsat_decoder.h"

SDRPP_MOD_INFO{
    /* Name:            */ "cospas_sarsat_decoder",
    /* Description:     */ "Cospas-Sarsat 406 MHz Beacon Decoder",
    /* Author:          */ "F4JTV (ADRASEC 06)",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ -1
};

ConfigManager config;

const double SARSAT_FREQ_1 = 406025000.0;
const double SARSAT_FREQ_2 = 406028000.0;
const double SARSAT_FREQ_3 = 406037000.0;
const double SARSAT_FREQ_4 = 406040000.0;

const int MAX_HISTORY = 50;
const float SYMBOL_RATE = 400.0f;

class CospasSarsatDecoderModule : public ModuleManager::Instance {
public:
    CospasSarsatDecoderModule(std::string name) {
        this->name = name;
        
        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name] = json({});
        }
        if (!config.conf[name].contains("vfoBandwidth")) {
            config.conf[name]["vfoBandwidth"] = 6000.0;
        }
        if (!config.conf[name].contains("logEnabled")) {
            config.conf[name]["logEnabled"] = false;
        }
        if (!config.conf[name].contains("logFolder")) {
            #ifdef _WIN32
            const char* userprofile = getenv("USERPROFILE");
            config.conf[name]["logFolder"] = userprofile ? std::string(userprofile) + "\\Documents\\SARSAT_Logs" : "C:\\SARSAT_Logs";
            #else
            const char* home = getenv("HOME");
            if (!home) {
                struct passwd* pw = getpwuid(getuid());
                home = pw ? pw->pw_dir : "/tmp";
            }
            config.conf[name]["logFolder"] = std::string(home) + "/SARSAT_Logs";
            #endif
        }
        
        vfoBandwidth = config.conf[name]["vfoBandwidth"];
        logEnabled = config.conf[name]["logEnabled"];
        logFolder = config.conf[name]["logFolder"];
        strncpy(folderInputBuf, logFolder.c_str(), sizeof(folderInputBuf) - 1);
        config.release(true);
        
        decoder = std::make_unique<sarsat::SarsatDecoder>();
        samplesPerHalfSymbol = vfoBandwidth / SYMBOL_RATE / 2.0f;
        
        gui::menu.registerEntry(name, menuHandler, this, this);
    }
    
    ~CospasSarsatDecoderModule() {
        stop();
        gui::menu.removeEntry(name);
    }
    
    void postInit() override { 
        if (enabled) start();
    }
    
    void enable() override { 
        enabled = true; 
        start(); 
    }
    
    void disable() override { 
        enabled = false; 
        stop(); 
    }
    
    bool isEnabled() override { return enabled; }
    
private:
    void start() {
        std::lock_guard<std::mutex> lock(startStopMutex);
        
        if (running.load()) return;
        
        // Create VFO
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 
                                            0, vfoBandwidth, vfoBandwidth, 
                                            vfoBandwidth, vfoBandwidth, true);
        if (vfo == nullptr) {
            flog::error("Failed to create VFO for Cospas-Sarsat module");
            return;
        }
        
        // Random color with reduced opacity
        srand((unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)this);
        float hue = (float)(rand() % 360) / 360.0f;
        float r, g, b;
        int hi = (int)(hue * 6.0f) % 6;
        float f = hue * 6.0f - hi;
        float q = 1.0f - 0.8f * f;
        float t = 1.0f - 0.8f * (1.0f - f);
        switch (hi) {
            case 0: r = 1.0f; g = t; b = 0.2f; break;
            case 1: r = q; g = 1.0f; b = 0.2f; break;
            case 2: r = 0.2f; g = 1.0f; b = t; break;
            case 3: r = 0.2f; g = q; b = 1.0f; break;
            case 4: r = t; g = 0.2f; b = 1.0f; break;
            default: r = 1.0f; g = 0.2f; b = q; break;
        }
        vfo->setColor(IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 100));
        
        // Set snap interval to 1000 Hz
        vfo->setSnapInterval(1000.0);
        
        // Create and initialize DSP chain
        demod = new dsp::demod::FM<float>();
        demod->init(vfo->output, vfoBandwidth, 3000.0, true, false);
        
        sink = new dsp::sink::Handler<float>();
        sink->init(&demod->out, audioHandler, this);
        
        resetDecoder();
        
        demod->start();
        sink->start();
        running.store(true);
    }
    
    void stop() {
        std::lock_guard<std::mutex> lock(startStopMutex);
        
        if (!running.load()) return;
        running.store(false);
        
        // Stop and delete DSP chain
        if (sink != nullptr) {
            sink->stop();
            delete sink;
            sink = nullptr;
        }
        
        if (demod != nullptr) {
            demod->stop();
            delete demod;
            demod = nullptr;
        }
        
        // Delete VFO
        if (vfo != nullptr) {
            sigpath::vfoManager.deleteVFO(vfo);
            vfo = nullptr;
        }
    }
    
    void resetDecoder() {
        dcOffset = 0.0f;
        signalLevel = 0.0f;
        agcGain = 1.0f;
        clockPhase = 0.0f;
        halfSymbols.clear();
        totalHalfSymbols = 0;
    }
    
    static void audioHandler(float* data, int count, void* ctx) {
        CospasSarsatDecoderModule* _this = (CospasSarsatDecoderModule*)ctx;
        if (_this == nullptr) return;
        if (!_this->running.load()) return;
        _this->processAudio(data, count);
    }
    
    void processAudio(float* data, int count) {
        for (int i = 0; i < count; i++) {
            float sample = data[i];
            
            dcOffset = 0.995f * dcOffset + 0.005f * sample;
            sample -= dcOffset;
            
            float absSample = std::abs(sample);
            if (absSample > signalLevel) {
                signalLevel = 0.9f * signalLevel + 0.1f * absSample;
            } else {
                signalLevel = 0.9999f * signalLevel + 0.0001f * absSample;
            }
            
            if (signalLevel > 0.001f) {
                float target = 0.5f / signalLevel;
                agcGain = 0.99f * agcGain + 0.01f * target;
                agcGain = std::min(100.0f, std::max(0.01f, agcGain));
            }
            
            float normalizedSample = sample * agcGain;
            normalizedSample = std::min(1.0f, std::max(-1.0f, normalizedSample));
            
            clockPhase += 1.0f;
            
            if (clockPhase >= samplesPerHalfSymbol) {
                clockPhase -= samplesPerHalfSymbol;
                int level = (normalizedSample > 0) ? 1 : 0;
                processHalfSymbol(level);
            }
        }
    }
    
    void processHalfSymbol(int level) {
        totalHalfSymbols++;
        halfSymbols.push_back(level);
        
        if (halfSymbols.size() > 400) {
            halfSymbols.erase(halfSymbols.begin());
        }
        
        if (halfSymbols.size() < 288) return;
        tryDecode();
    }
    
    void tryDecode() {
        std::vector<int> bits;
        size_t n = halfSymbols.size();
        
        for (size_t i = 0; i + 1 < n; i += 2) {
            int first = halfSymbols[i];
            int second = halfSymbols[i + 1];
            
            if (first == 1 && second == 0) {
                bits.push_back(1);
            } else if (first == 0 && second == 1) {
                bits.push_back(0);
            } else {
                bits.push_back(-1);
            }
        }
        
        if (bits.size() < 144) return;
        
        for (size_t startPos = 0; startPos + 144 <= bits.size(); startPos++) {
            int ones = 0;
            for (size_t j = startPos; j < startPos + 15 && j < bits.size(); j++) {
                if (bits[j] == 1) ones++;
            }
            
            if (ones < 10) continue;
            if (startPos + 24 > bits.size()) continue;
            
            uint16_t sync = 0;
            for (size_t j = startPos + 15; j < startPos + 24; j++) {
                sync = (sync << 1) | (bits[j] == 1 ? 1 : 0);
            }
            
            bool isTest = false;
            if (sync == 0x02F) {
            } else if (sync == 0x1A0) {
                isTest = true;
            } else {
                continue;
            }
            
            if (startPos + 144 > bits.size()) continue;
            
            uint8_t frameBits[120];
            for (int j = 0; j < 120; j++) {
                int b = bits[startPos + 24 + j];
                frameBits[j] = (b < 0) ? 0 : b;
            }
            
            sarsat::BeaconMessage msg;
            msg.is_test = isTest;
            msg.timestamp = static_cast<double>(std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now()));
            
            if (decoder->decode(frameBits, msg)) {
                // Restore timestamp and test flag (also reset by decode())
                msg.is_test = isTest;
                msg.timestamp = static_cast<double>(std::chrono::system_clock::to_time_t(
                    std::chrono::system_clock::now()));
                
                std::lock_guard<std::mutex> lock(historyMutex);
                
                bool isDuplicate = false;
                for (const auto& prev : messageHistory) {
                    if (prev.hex_id == msg.hex_id && 
                        std::abs(msg.timestamp - prev.timestamp) < 2.0) {
                        isDuplicate = true;
                        break;
                    }
                }
                
                if (!isDuplicate) {
                    messageHistory.push_front(msg);
                    while (messageHistory.size() > MAX_HISTORY) {
                        messageHistory.pop_back();
                    }
                    totalDecoded++;
                    
                    if (logEnabled) {
                        logMessage(msg);
                    }
                }
                
                halfSymbols.clear();
                return;
            } else {
                failedDecodes++;
            }
        }
    }
    
    void createLogFolder() {
        #ifdef _WIN32
        CreateDirectoryA(logFolder.c_str(), NULL);
        #else
        mkdir(logFolder.c_str(), 0755);
        #endif
    }
    
    std::string generateLogFilename() {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm* ti = localtime(&t);
        
        char buf[64];
        strftime(buf, sizeof(buf), "sarsat_%Y%m%d_%H%M%S.log", ti);
        
        #ifdef _WIN32
        return logFolder + "\\" + buf;
        #else
        return logFolder + "/" + buf;
        #endif
    }
    
    void logMessage(const sarsat::BeaconMessage& msg) {
        createLogFolder();
        std::string filename = generateLogFilename();
        
        std::ofstream logFile(filename, std::ios::app);
        if (!logFile.is_open()) return;
        
        time_t ts = static_cast<time_t>(msg.timestamp);
        struct tm* ti = localtime(&ts);
        char timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", ti);
        
        logFile << "========================================" << std::endl;
        logFile << "COSPAS-SARSAT 406 MHz BEACON RECEIVED" << std::endl;
        logFile << "========================================" << std::endl;
        logFile << "Time: " << timebuf << std::endl;
        logFile << std::endl;
        
        logFile << "--- IDENTIFICATION ---" << std::endl;
        
        // Convert hex ID to decimal
        uint64_t decimalId = 0;
        for (char c : msg.hex_id) {
            decimalId <<= 4;
            if (c >= '0' && c <= '9') decimalId |= (c - '0');
            else if (c >= 'A' && c <= 'F') decimalId |= (c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') decimalId |= (c - 'a' + 10);
        }
        logFile << "Beacon ID: " << decimalId << std::endl;
        logFile << "Hex ID: " << msg.hex_id << std::endl;
        logFile << "Country: " << msg.country_name << " (MID: " << msg.country_code << ")" << std::endl;
        logFile << "Protocol: " << sarsat::SarsatDecoder::getProtocolName(msg.protocol) << std::endl;
        logFile << "Beacon Type: " << sarsat::SarsatDecoder::getBeaconTypeName(msg.beacon_type) << std::endl;
        logFile << "Message Type: " << (msg.is_test ? "SELF-TEST" : "DISTRESS ALERT") << std::endl;
        logFile << "Format: " << (msg.long_message ? "Long (144 bits)" : "Short (112 bits)") << std::endl;
        
        if (!msg.mmsi.empty()) {
            logFile << "MMSI: " << msg.mmsi << std::endl;
            if (msg.serial_number > 0 && msg.serial_number < 16) {
                logFile << "Beacon Number: " << msg.serial_number << std::endl;
            }
        }
        if (!msg.aircraft_address.empty()) {
            logFile << "Aircraft Address: " << msg.aircraft_address << std::endl;
        }
        if (msg.mmsi.empty() && msg.serial_number > 0) {
            logFile << "Serial Number: " << msg.serial_number << std::endl;
        }
        if (msg.cert_number > 0) {
            logFile << "Certificate Number: " << msg.cert_number << std::endl;
        }
        logFile << std::endl;
        
        logFile << "--- POSITION ---" << std::endl;
        if (msg.position.valid) {
            logFile << "Latitude: " << msg.position.getLatitudeString() 
                   << " (" << std::fixed << std::setprecision(6) << msg.position.getLatitudeDecimal() << " deg)" << std::endl;
            logFile << "Longitude: " << msg.position.getLongitudeString()
                   << " (" << std::fixed << std::setprecision(6) << msg.position.getLongitudeDecimal() << " deg)" << std::endl;
            logFile << "Position Source: " << (msg.position.source == sarsat::PositionSource::INTERNAL ? "Internal Navigation" : "External GPS") << std::endl;
            logFile << "121.5 MHz Homing: " << (msg.position.homing_121_5 ? "Enabled" : "Disabled") << std::endl;
            logFile << "OpenStreetMap: https://www.openstreetmap.org/?mlat=" 
                   << msg.position.getLatitudeDecimal() << "&mlon=" << msg.position.getLongitudeDecimal() 
                   << "#map=14/" << msg.position.getLatitudeDecimal() << "/" << msg.position.getLongitudeDecimal() << std::endl;
        } else {
            logFile << "Position: Not encoded in message" << std::endl;
        }
        logFile << std::endl;
        
        logFile << "--- ERROR CORRECTION ---" << std::endl;
        logFile << "BCH-1: " << (msg.bch1_valid ? "Valid" : "INVALID");
        if (msg.bch1_errors > 0) logFile << " (" << msg.bch1_errors << " bit(s) corrected)";
        logFile << std::endl;
        logFile << "BCH-2: " << (msg.bch2_valid ? "Valid" : "INVALID");
        if (msg.bch2_errors > 0) logFile << " (" << msg.bch2_errors << " bit(s) corrected)";
        logFile << std::endl;
        logFile << std::endl;
        
        logFile.close();
    }
    
    static void menuHandler(void* ctx) {
        CospasSarsatDecoderModule* _this = (CospasSarsatDecoderModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;
        
        if (!_this->enabled) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Module disabled");
            return;
        }
        
        // === Signal level and SNR ===
        // === Stats ===
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%d", _this->totalDecoded);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "decoded |");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%d", _this->failedDecodes);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "failed");
        
        // === Frequencies ===
        float btnW = (menuWidth - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
        if (ImGui::Button("406.025", ImVec2(btnW, 0))) {
            tuner::tune(tuner::TUNER_MODE_NORMAL, "", SARSAT_FREQ_1);
        }
        ImGui::SameLine();
        if (ImGui::Button("406.028", ImVec2(btnW, 0))) {
            tuner::tune(tuner::TUNER_MODE_NORMAL, "", SARSAT_FREQ_2);
        }
        ImGui::SameLine();
        if (ImGui::Button("406.037", ImVec2(btnW, 0))) {
            tuner::tune(tuner::TUNER_MODE_NORMAL, "", SARSAT_FREQ_3);
        }
        ImGui::SameLine();
        if (ImGui::Button("406.040", ImVec2(btnW, 0))) {
            tuner::tune(tuner::TUNER_MODE_NORMAL, "", SARSAT_FREQ_4);
        }
        
        // === Logging ===
        if (ImGui::Checkbox("Log to file", &_this->logEnabled)) {
            config.acquire();
            config.conf[_this->name]["logEnabled"] = _this->logEnabled;
            config.release(true);
        }
        
        if (_this->logEnabled) {
            ImGui::SetNextItemWidth(menuWidth - 60);
            if (ImGui::InputText("##logfolder", _this->folderInputBuf, sizeof(_this->folderInputBuf), 
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                _this->logFolder = _this->folderInputBuf;
                config.acquire();
                config.conf[_this->name]["logFolder"] = _this->logFolder;
                config.release(true);
            }
            // Update on focus lost as well
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                _this->logFolder = _this->folderInputBuf;
                config.acquire();
                config.conf[_this->name]["logFolder"] = _this->logFolder;
                config.release(true);
            }
        }
        
        ImGui::Spacing();
        
        // === Messages ===
        {
            std::lock_guard<std::mutex> lock(_this->historyMutex);
            
            if (_this->messageHistory.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting for beacon signals...");
            } else {
                ImGui::BeginChild("MsgList", ImVec2(0, 320), true);
                
                for (size_t idx = 0; idx < _this->messageHistory.size(); idx++) {
                    const auto& msg = _this->messageHistory[idx];
                    ImGui::PushID((int)idx);
                    
                    time_t ts = static_cast<time_t>(msg.timestamp);
                    struct tm* ti = localtime(&ts);
                    char timeBuf[16];
                    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", ti);
                    
                    bool bchOk = msg.bch1_valid && msg.bch2_valid;
                    
                    // Header
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", timeBuf);
                    ImGui::SameLine();
                    ImGui::TextColored(bchOk ? ImVec4(0,1,0,1) : ImVec4(1,0.3f,0,1), bchOk ? "OK" : "ERR");
                    
                    if (msg.is_test) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1,1,0,1), "TEST");
                    } else {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1,0.2f,0.2f,1), "ALERT");
                    }
                    
                    // Show MMSI prominently if available (like dec406)
                    if (!msg.mmsi.empty()) {
                        ImGui::TextColored(ImVec4(1,1,1,1), "MMSI: %s", msg.mmsi.c_str());
                    } else {
                        // Beacon ID - convert hex to decimal
                        uint64_t decimalId = 0;
                        for (char c : msg.hex_id) {
                            decimalId <<= 4;
                            if (c >= '0' && c <= '9') decimalId |= (c - '0');
                            else if (c >= 'A' && c <= 'F') decimalId |= (c - 'A' + 10);
                            else if (c >= 'a' && c <= 'f') decimalId |= (c - 'a' + 10);
                        }
                        ImGui::TextColored(ImVec4(1,1,1,1), "ID: %llu", (unsigned long long)decimalId);
                    }
                    
                    // Country + Type
                    ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "%s (%d) | %s", 
                        msg.country_name.c_str(), msg.country_code,
                        sarsat::SarsatDecoder::getBeaconTypeName(msg.beacon_type).c_str());
                    
                    // Protocol
                    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "%s",
                        sarsat::SarsatDecoder::getProtocolName(msg.protocol).c_str());
                    
                    // Serial number (only if no MMSI)
                    if (msg.mmsi.empty() && msg.serial_number > 0) {
                        ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "S/N: %u", msg.serial_number);
                    }
                    
                    // Aircraft address
                    if (!msg.aircraft_address.empty()) {
                        ImGui::TextColored(ImVec4(0.8f,0.8f,0.5f,1), "Aircraft: %s", msg.aircraft_address.c_str());
                    }
                    
                    // Position
                    if (msg.position.valid) {
                        ImGui::TextColored(ImVec4(0,1,1,1), "%s  %s", 
                            msg.position.getLatitudeString().c_str(),
                            msg.position.getLongitudeString().c_str());
                        
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Map")) {
                            std::stringstream ss;
                            #ifdef _WIN32
                            ss << "start \"\" \"https://www.openstreetmap.org/?mlat=";
                            #else
                            ss << "xdg-open 'https://www.openstreetmap.org/?mlat=";
                            #endif
                            ss << std::fixed << std::setprecision(6)
                               << msg.position.getLatitudeDecimal() << "&mlon="
                               << msg.position.getLongitudeDecimal() 
                               << "#map=14/" << msg.position.getLatitudeDecimal()
                               << "/" << msg.position.getLongitudeDecimal();
                            #ifdef _WIN32
                            ss << "\"";
                            #else
                            ss << "' &";
                            #endif
                            int r = system(ss.str().c_str()); (void)r;
                        }
                        
                        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Src:%s | 121.5:%s",
                            msg.position.source == sarsat::PositionSource::INTERNAL ? "Int" : "GPS",
                            msg.position.homing_121_5 ? "Yes" : "No");
                    } else {
                        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Position: N/A");
                    }
                    
                    // BCH
                    ImGui::TextColored(ImVec4(0.4f,0.4f,0.4f,1), "BCH1:%s%s BCH2:%s%s",
                        msg.bch1_valid ? "OK" : "Err",
                        msg.bch1_errors > 0 ? ("+" + std::to_string(msg.bch1_errors)).c_str() : "",
                        msg.bch2_valid ? "OK" : "Err",
                        msg.bch2_errors > 0 ? ("+" + std::to_string(msg.bch2_errors)).c_str() : "");
                    
                    if (idx < _this->messageHistory.size() - 1) {
                        ImGui::Separator();
                    }
                    
                    ImGui::PopID();
                }
                
                ImGui::EndChild();
                
                if (ImGui::Button("Clear", ImVec2(menuWidth, 0))) {
                    _this->messageHistory.clear();
                    _this->totalDecoded = 0;
                    _this->failedDecodes = 0;
                }
            }
        }
    }
    
    std::string name;
    bool enabled = true;
    std::atomic<bool> running{false};
    
    VFOManager::VFO* vfo = nullptr;
    float vfoBandwidth = 6000.0f;
    dsp::demod::FM<float>* demod = nullptr;
    dsp::sink::Handler<float>* sink = nullptr;
    
    float dcOffset = 0.0f;
    float signalLevel = 0.0f;
    float agcGain = 1.0f;
    
    float samplesPerHalfSymbol = 7.5f;
    float clockPhase = 0.0f;
    std::vector<int> halfSymbols;
    int totalHalfSymbols = 0;
    
    bool logEnabled = false;
    std::string logFolder;
    char folderInputBuf[512] = {0};
    
    std::unique_ptr<sarsat::SarsatDecoder> decoder;
    
    int totalDecoded = 0;
    int failedDecodes = 0;
    
    std::mutex startStopMutex;
    std::mutex historyMutex;
    std::deque<sarsat::BeaconMessage> messageHistory;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/cospas_sarsat_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new CospasSarsatDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (CospasSarsatDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}

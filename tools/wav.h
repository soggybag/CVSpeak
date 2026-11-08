#pragma once
// Minimal 16-bit mono WAV reader/writer for desktop tools and tests (not part of dsp/).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cvspeak::tools {

inline bool writeWav16(const char* path, const std::vector<float>& samples, int sampleRate) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate * 2));
    u16(2);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);

    for (float s : samples) {
        if (s > 1.f) s = 1.f;
        if (s < -1.f) s = -1.f;
        const int16_t v = static_cast<int16_t>(s * 32767.f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
    return true;
}

// Reads a 16-bit PCM mono WAV, skipping any chunks other than fmt/data
// (macOS `say` adds an FLLR padding chunk). Returns false on anything else.
inline bool readWav16(const char* path, std::vector<float>& samples, int& sampleRate) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char riff[12];
    if (std::fread(riff, 1, 12, f) != 12 || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(riff + 8, "WAVE", 4) != 0) {
        std::fclose(f);
        return false;
    }
    bool haveFmt = false, ok = false;
    char id[4];
    uint32_t size;
    while (std::fread(id, 1, 4, f) == 4 && std::fread(&size, 4, 1, f) == 1) {
        if (std::memcmp(id, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (size < 16 || std::fread(fmt, 1, 16, f) != 16) break;
            uint16_t format, channels, bits;
            uint32_t rate;
            std::memcpy(&format, fmt, 2);
            std::memcpy(&channels, fmt + 2, 2);
            std::memcpy(&rate, fmt + 4, 4);
            std::memcpy(&bits, fmt + 14, 2);
            if (format != 1 || channels != 1 || bits != 16) break;
            sampleRate = static_cast<int>(rate);
            haveFmt = true;
            std::fseek(f, static_cast<long>(size - 16 + (size & 1)), SEEK_CUR);
        } else if (std::memcmp(id, "data", 4) == 0 && haveFmt) {
            std::vector<int16_t> raw(size / 2);
            ok = std::fread(raw.data(), 2, raw.size(), f) == raw.size();
            samples.resize(raw.size());
            for (size_t i = 0; i < raw.size(); ++i) samples[i] = static_cast<float>(raw[i]) / 32768.f;
            break;
        } else {
            std::fseek(f, static_cast<long>(size + (size & 1)), SEEK_CUR);
        }
    }
    std::fclose(f);
    return ok;
}

}  // namespace cvspeak::tools

#pragma once
// Minimal 16-bit mono WAV writer for desktop tools and tests (not part of dsp/).

#include <cstdint>
#include <cstdio>
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

}  // namespace cvspeak::tools

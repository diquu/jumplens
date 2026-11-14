#pragma once

#include <cstdint>
#include <cstring>

namespace jl {

inline uint16_t rd_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline int16_t rd_i16(const uint8_t* p) {
    return static_cast<int16_t>(rd_u16(p));
}

inline uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t rd_i32(const uint8_t* p) {
    return static_cast<int32_t>(rd_u32(p));
}

inline uint64_t rd_u64(const uint8_t* p) {
    return static_cast<uint64_t>(rd_u32(p)) | (static_cast<uint64_t>(rd_u32(p + 4)) << 32);
}

inline float rd_f32(const uint8_t* p) {
    float value = 0.0f;
    memcpy(&value, p, sizeof(float));
    return value;
}

inline double filetime_to_unix(uint64_t value) {
    if (value == 0) {
        return 0.0;
    }
    double seconds = static_cast<double>(value) / 10000000.0 - 11644473600.0;
    if (seconds < -11644473600.0 || seconds > 32503680000.0) {
        return 0.0;
    }
    return seconds;
}

}

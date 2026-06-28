#pragma once

#include <cctype>
#include <cmath>
#include <string>

namespace ship_guidance {

inline std::string normalize_navigation_mode(std::string mode)
{
    for (char& ch : mode) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ch == '-' || ch == ' ') {
            ch = '_';
        }
    }
    return mode;
}

inline bool is_dp_navigation_mode(const std::string& mode)
{
    const std::string normalized = normalize_navigation_mode(mode);
    return normalized == "dp_hold" ||
           normalized == "dp" ||
           normalized == "station_keeping";
}

inline bool is_emergency_avoidance_mode(const std::string& mode)
{
    const std::string normalized = normalize_navigation_mode(mode);
    return normalized == "emergency_avoidance" ||
           normalized == "emergency_avoid" ||
           normalized == "collision_avoidance" ||
           normalized == "avoidance";
}

inline bool is_colregs_overtake_mode(const std::string& mode)
{
    const std::string normalized = normalize_navigation_mode(mode);
    return normalized == "colregs_overtake" ||
           normalized == "overtake_avoidance";
}

inline bool is_colregs_protected_mode(const std::string& mode)
{
    return is_emergency_avoidance_mode(mode) || is_colregs_overtake_mode(mode);
}

inline int navigation_mode_code(const std::string& raw_mode)
{
    const std::string mode = normalize_navigation_mode(raw_mode);
    if (mode.empty()) return 0;
    if (mode == "cruise" || mode == "open_water_cruise" || mode == "post_turn_cruise") return 1;
    if (mode == "narrow_channel") return 2;
    if (mode == "harbor") return 3;
    if (mode == "approach") return 4;
    if (is_dp_navigation_mode(mode)) return 5;
    if (is_emergency_avoidance_mode(mode)) return 6;
    if (is_colregs_overtake_mode(mode)) return 7;
    return 0;
}

inline std::string navigation_mode_from_code(double code_value)
{
    const int code = static_cast<int>(std::llround(code_value));
    switch (code) {
        case 1: return "cruise";
        case 2: return "narrow_channel";
        case 3: return "harbor";
        case 4: return "approach";
        case 5: return "dp_hold";
        case 6: return "emergency_avoidance";
        case 7: return "colregs_overtake";
        default: return "";
    }
}

inline bool navigation_mode_code_is_colregs_protected(int code)
{
    return code == 6 || code == 7;
}

}  // namespace ship_guidance

#pragma once
#include <Arduino.h>

struct SemVer { uint8_t major, minor, patch; };

inline SemVer parseSemVer(const String& s) {
    String v = s;
    if (v.length() && (v[0] == 'v' || v[0] == 'V')) v = v.substring(1);
    SemVer sv = {0, 0, 0};
    int a = v.indexOf('.');
    if (a < 0) { sv.major = (uint8_t)v.toInt(); return sv; }
    int b = v.indexOf('.', a + 1);
    sv.major = (uint8_t)v.substring(0, a).toInt();
    if (b < 0) { sv.minor = (uint8_t)v.substring(a + 1).toInt(); return sv; }
    sv.minor = (uint8_t)v.substring(a + 1, b).toInt();
    sv.patch = (uint8_t)v.substring(b + 1).toInt();
    return sv;
}

inline bool semverNewerThan(const SemVer& latest, const SemVer& current) {
    if (latest.major != current.major) return latest.major > current.major;
    if (latest.minor != current.minor) return latest.minor > current.minor;
    return latest.patch > current.patch;
}

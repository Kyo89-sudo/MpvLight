#pragma once
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <limits>
#include <optional>
#include <string>

inline std::wstring clockText(double seconds) {
    auto t = static_cast<long long>(std::max(0.0, std::isfinite(seconds) ? seconds : 0.0));
    wchar_t result[64];
    if (t >= 3600)
        swprintf_s(result, L"%02lld:%02lld:%02lld", t / 3600, t / 60 % 60, t % 60);
    else
        swprintf_s(result, L"%02lld:%02lld", t / 60, t % 60);
    return result;
}

// One field is seconds; two fields are minutes:seconds; three are
// hours:minutes:seconds.
inline std::optional<double> parseTime(const std::wstring &text) {
    if (text.empty() || text.size() > 30)
        return {};
    double fields[3]{};
    int count = 0;
    size_t start = 0;
    for (;;) {
        size_t end = text.find(L':', start);
        std::wstring token = text.substr(start, end == std::wstring::npos ? end : end - start);
        if (count == 3 || token.empty())
            return {};
        bool decimal = false, digit = false;
        for (wchar_t c : token) {
            if (c >= L'0' && c <= L'9') {
                digit = true;
                continue;
            }
            if ((c == L'.' || c == L',') && !decimal && end == std::wstring::npos) {
                decimal = true;
                continue;
            }
            return {};
        }
        if (!digit)
            return {};
        std::replace(token.begin(), token.end(), L',', L'.');
        fields[count++] = std::wcstod(token.c_str(), nullptr);
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    for (int i = 1; i < count; ++i)
        if (fields[i] >= 60)
            return {};
    double result = 0;
    for (int i = 0; i < count; ++i)
        result = result * 60 + fields[i];
    if (!std::isfinite(result) || result > 1e9)
        return {};
    return result;
}

inline double positionAt(int x, int left, int right, double duration) {
    return right > left && duration > 0
               ? std::clamp(static_cast<double>(x - left) / (right - left), 0.0, 1.0) * duration
               : 0;
}

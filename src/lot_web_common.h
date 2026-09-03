#pragma once

#include <webgpu/webgpu.h>
#include <cstring>
#include <string>

// webgpu.h 는 문자열을 WGPUStringView(포인터 + 길이)로 주고받는다.
// length 가 WGPU_STRLEN 이면 널 종료 문자열이라는 뜻이다.

inline WGPUStringView lotStringView(const char* str) {
    WGPUStringView view{};
    view.data = str;
    view.length = (str == nullptr) ? 0 : WGPU_STRLEN;
    return view;
}

inline WGPUStringView lotStringView(const std::string& str) {
    WGPUStringView view{};
    view.data = str.data();
    view.length = str.size();
    return view;
}

inline std::string lotToString(WGPUStringView view) {
    if (view.data == nullptr) {
        return {};
    }
    if (view.length == WGPU_STRLEN) {
        return std::string(view.data);
    }
    return std::string(view.data, view.length);
}

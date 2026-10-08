// 스케치 도구의 명령행 좌표 입력 - 네이티브 first_app/command.cpp 의 도구별 규칙과 같다.
//   첫 점 = 절대 (x,y[,z]), 다음 점 = 상대 (dx,dy[,dz]) - 기준은 직전 점, 사각형은 첫 모서리, 원 · 다각형은 중심.
//   '=x,y' / '#x,y' = 절대, '@dx,dy' = 상대 (적지 않아도 상대).
//   값 하나 = 거리: 선 · 폴리선 · 호 · 치수는 커서 쪽으로 (AutoCAD 의 직접 거리 입력), 원 · 다각형은 반지름.
//   폴리선: 'c' / 'close' = 닫기, 'x' / 'end' / 'done' = 열린 채로 끝.
// 좌표는 스케치 평면의 축 (origin + right * x + up * y + normal * z) - 위에서 본 바닥이면 월드 x, y 그대로.
#include "lot_sketch_tool.h"

#include "lot_log.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace {

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// "1, 2.5,-3" -> {1, 2.5, -3}. 숫자가 아닌 것이 섞이면 false.
bool parseNumbers(const std::string& s, std::vector<float>& out) {
    out.clear();
    const char* p = s.c_str();
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p || !std::isfinite(v)) return false;
        out.push_back(v);
        p = end;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ',') ++p;
        else if (*p) return false;
    }
    return !out.empty() && out.size() <= 3;
}

}  // namespace

bool SketchController::typed(const std::string& text, const Context& ctx) {
    if (!active_) return false;
    const std::string t = trimmed(text);
    if (t.empty()) return false;
    std::string lower = t;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // 폴리선 옵션
    if (active_ == polyline_.get()) {
        if (lower == "c" || lower == "close") {
            if (polyline_->closeLoop(ctx.objects)) {
                lastCommitted_ = polyline_->consumeCommittedId();
                active_ = nullptr;
                LOT_LOG("sketch: polyline closed");
            } else {
                LOT_LOG("sketch: polyline needs 3 points to close");
            }
            return true;
        }
        if (lower == "x" || lower == "end" || lower == "done") {
            finish(ctx.objects);
            return true;
        }
    }
    if (active_ == textTool_.get()) return false;   // 문자는 입력창이 받는다

    bool absolute = false;
    std::string body = t;
    if (body[0] == '=' || body[0] == '#') { absolute = true; body = body.substr(1); }
    else if (body[0] == '@') body = body.substr(1);
    std::vector<float> v;
    if (!parseNumbers(body, v)) return false;   // 좌표가 아니다 - 명령 이름일 수 있다

    const bool first = !active_->hasPoints();
    const bool centred = active_ == circle_.get() || active_ == polygon_.get();
    vec3 p;
    if (v.size() == 1) {
        if (first) {
            LOT_LOG("sketch: " << active_->name() << " - type the first point as x,y");
            return true;
        }
        const vec3& last = active_->points().back();
        if (centred) {
            p = active_->points().front() + plane_.right * v[0];   // 반지름 (오른쪽 축 방향의 점)
        } else if (active_ == rectangle_.get()) {
            LOT_LOG("sketch: rectangle - type width,height for the opposite corner");
            return true;
        } else {
            // 직접 거리: 직전 점에서 커서 쪽으로 (평면 위 방향)
            vec3 cursor;
            if (!cursorPoint(ctx, cursor)) return true;
            vec3 d = cursor - last;
            d = d - plane_.normal * dot(d, plane_.normal);
            if (dot(d, d) < 1e-20f) {
                LOT_LOG("sketch: move the cursor towards the direction, then type the distance");
                return true;
            }
            p = last + normalize(d) * v[0];
        }
    } else {
        const vec3 off = plane_.right * v[0] + plane_.up * v[1] + plane_.normal * (v.size() == 3 ? v[2] : 0.0f);
        if (absolute || first) {
            p = plane_.origin + off;
        } else {
            const bool fromFirst = centred || active_ == rectangle_.get();
            p = (fromFirst ? active_->points().front() : active_->points().back()) + off;
        }
    }
    LOT_LOG("sketch: typed point (" << p.x << ", " << p.y << ", " << p.z << ")");
    feedPoint(p, ctx.objects);
    return true;
}

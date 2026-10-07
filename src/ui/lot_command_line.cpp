#include "lot_command_line.h"
#include "lot_main_menu.h"
#include "lot_ribbon.h"

#include <algorithm>
#include <cctype>

namespace lot_ui {
namespace {

std::string lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) out += static_cast<char>(std::tolower(c));
    return out;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// 명령 id -> 칠 수 있는 이름들. Vulkan 쪽 builtinCommandTable 과 같은 규약:
// 영문 풀이름 · 짧은 별칭 · 한글. 메뉴의 한국어 이름은 build() 가 자동으로 더한다.
struct Alias {
    const char* id;
    const char* names;  // 공백으로 나눔
};

const Alias kAliases[] = {
    {"draw.line",     "line l"},
    {"draw.rect",     "rect rec rectangle"},
    {"draw.polyline", "polyline pl"},
    {"draw.circle",   "circle c"},
    {"draw.arc",      "arc a"},
    {"draw.polygon",  "polygon pol"},
    {"draw.dim",      "dim dimension 치수선"},
    {"draw.text",     "text t 글자"},
    {"draw.cube",     "cube n box 상자"},
    {"draw.finish",   "finish done 확정"},
    {"modify.move",   "move m"},
    {"modify.copy",   "copy co cp"},
    {"modify.rotate", "rotate ro"},
    {"modify.scale",  "scale sc"},
    {"modify.mirror", "mirror mi"},
    {"modify.offset", "offset o 간격"},
    {"modify.trim",   "trim tr 자르기"},
    {"modify.fillet", "fillet fil 필렛"},
    {"modify.chamfer","chamfer cha 모따기"},
    {"modify.extend", "extend ex 연장"},
    {"gizmo.move",    "gmove g1"},
    {"gizmo.rotate",  "grotate g2"},
    {"gizmo.scale",   "gscale g3"},
    {"edit.undo",     "undo u 취소"},
    {"edit.redo",     "redo re 복구"},
    {"edit.dup",      "duplicate dup"},
    {"edit.delete",   "delete del erase e 지우기"},
    {"edit.selectAll","selectall all 모두"},
    {"edit.eraseAll", "eraseall 전부지우기"},
    {"edit.cancel",   "cancel esc 취소하기"},
    {"view.front",    "front fr 앞"},
    {"view.top",      "top 위"},
    {"view.right",    "right ri 오른쪽"},
    {"view.iso",      "iso isometric"},
    {"view.fit",      "zoom z extents 줌"},
    {"view.parallel", "ortho parallel persp 투영"},
    {"view.fps",      "fps walk 걷기"},
    {"view.outline",  "outline ol 외곽선"},
    {"snap.ortho",    "orthomode f8 직교"},
    {"snap.grid",     "snap f9 그리드"},
    {"snap.osnap",    "osnap os f3 객체스냅"},
    {"snap.polar",    "polar f10 극좌표"},
    {"view.grid",     "grid f7 격자"},
    {"view.dims",     "dims 치수표시"},
    {"view.style0",   "shade shaded"},
    {"view.style1",   "shadeedges"},
    {"view.style2",   "wire wireframe 와이어프레임"},
    {"view.style3",   "wireedges"},
    {"view.style4",   "hide hidden 숨은선"},
    {"panel.layers",  "layer layers la 레이어창"},
    {"panel.props",   "properties props pr 속성"},
    {"panel.tree",    "tree nodetree 노드트리"},
    {"file.newDoc",   "new newdoc 새문서"},
    {"file.closeDoc", "close closedoc 문서닫기"},
    {"file.nextDoc",  "nexttab 다음탭"},
    {"file.prevDoc",  "prevtab 이전탭"},
    {"file.open",     "open 열기 dxf dxfopen dxfin obj objopen"},
    {"file.saveLot",  "save 저장"},
    {"file.saveDxf",  "dxfout export 내보내기"},
};

}  // namespace

void LotCommandLine::addCommand(const Command& c) {
    // 메뉴에 적힌 한국어 이름 (공백 없는 것만 - "기즈모: 이동" 같은 것은 별칭으로 받는다)
    auto put = [&](const std::string& name) {
        const std::string key = lower(trim(name));
        if (key.empty() || key.find(' ') != std::string::npos) return;
        if (byName_.count(key)) return;   // 먼저 등록된 것이 이긴다
        byName_.emplace(key, c);
        names_.push_back(key);
    };
    put(c.label);
    for (const Alias& a : kAliases) {
        if (c.id != a.id) continue;
        std::string word;
        for (const char* p = a.names;; ++p) {
            if (*p == ' ' || *p == '\0') {
                put(word);
                word.clear();
                if (*p == '\0') break;
            } else {
                word += *p;
            }
        }
    }
}

void LotCommandLine::build(const LotMainMenu& menu, const LotRibbon& ribbon) {
    byName_.clear();
    names_.clear();
    for (const Group& g : menu.menus()) {
        for (const Command& c : g.commands) addCommand(c);
    }
    // 리본에만 있는 명령이 생기면 그것도 (지금은 메뉴가 전부 덮지만 표가 갈라지지 않게)
    for (const LotRibbon::Tab& t : ribbon.tabs()) {
        for (const Group& g : t.groups) {
            for (const Command& c : g.commands) addCommand(c);
        }
    }
    std::sort(names_.begin(), names_.end());
}

LotCommandLine::Resolved LotCommandLine::resolve(const std::string& typed) const {
    Resolved r;
    const std::string key = lower(trim(typed));
    if (key.empty()) return r;
    auto it = byName_.find(key);
    if (it == byName_.end()) return r;
    r.found = true;
    r.id = it->second.id;
    r.keyCode = it->second.keyCode;
    r.ctrl = it->second.ctrl;
    r.label = it->second.label;
    return r;
}

std::vector<std::string> LotCommandLine::complete(const std::string& prefix, size_t max) const {
    std::vector<std::string> out;
    const std::string key = lower(trim(prefix));
    if (key.empty()) return out;
    for (const std::string& n : names_) {
        if (n.compare(0, key.size(), key) == 0) {
            out.push_back(n);
            if (out.size() >= max) break;
        }
    }
    return out;
}

std::string LotCommandLine::namesJson() const {
    std::string j = "[";
    for (size_t i = 0; i < names_.size(); ++i) {
        if (i) j += ",";
        j += quote(names_[i]);
    }
    j += "]";
    return j;
}

}  // namespace lot_ui

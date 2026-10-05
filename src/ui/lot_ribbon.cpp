#include "lot_ribbon.h"

namespace lot_ui {
namespace {

// 아이콘은 글리프 하나로 둔다 - 아이콘 폰트를 끌어오지 않고도 모양이 구별된다.
Command icon(const char* id, const char* label, const char* glyph, const char* key,
             bool ctrl, const char* shortcut, const char* tip, const char* state = "") {
    Command c;
    c.id = id;
    c.label = label;
    c.icon = glyph;
    c.keyCode = key;
    c.ctrl = ctrl;
    c.shortcut = shortcut;
    c.tip = tip;
    c.state = state;
    return c;
}

}  // namespace

LotRibbon::LotRibbon() {
    tabs_ = {
        {"홈", {
            {"파일", {
                icon("file.newDoc",  "새 도면", "\xE2\x96\xA1", "#newDoc", false, "", "빈 도면을 새 탭에"),
                icon("file.open",    "열기",  "\xE2\x96\xA4", "@open",    false, "", "열기 (.lot .dxf .obj)"),
                icon("file.saveLot", "저장",  "\xE2\x96\xA3", "@saveLot", false, "", "씬을 .lot 으로 저장"),
                icon("file.saveDxf", "DXF 저장", "\xE2\x97\xA8", "@saveDxf", false, "", "2D 도면을 DXF 로 내보내기"),
            }},
            {"편집", {
                icon("edit.undo",   "취소", "\xE2\x86\xB6", "KeyZ",   true,  "Ctrl+Z", "실행 취소", "undo"),
                icon("edit.redo",   "복구", "\xE2\x86\xB7", "KeyY",   true,  "Ctrl+Y", "다시 실행", "redo"),
                icon("edit.dup",    "복제", "\xE2\x9A\x8A", "KeyD",   true,  "Ctrl+D", "선택을 제자리에 복제"),
                icon("edit.selectAll", "전체", "\xE2\x96\xA6", "KeyA", true, "Ctrl+A", "전체 선택"),
                icon("edit.delete", "삭제", "\xE2\x9C\x95", "Delete", false, "Del",    "선택 삭제"),
            }},
            {"뷰", {
                icon("view.front", "정면", "\xE2\x96\xAD", "KeyF", false, "F", "정면도", "view:0"),
                icon("view.top",   "평면", "\xE2\x96\xA1", "KeyT", false, "T", "평면도", "view:2"),
                icon("view.right", "우측", "\xE2\x96\xAF", "KeyR", false, "R", "우측면도", "view:4"),
                icon("view.iso",   "등각", "\xE2\x97\x87", "KeyI", false, "I", "등각도", "view:6"),
                icon("view.fit",   "맞춤", "\xE2\xA4\xA2", "KeyZ", false, "Z", "전체 보기"),
            }},
            {"기즈모", {
                icon("gizmo.move",   "이동", "\xE2\x9C\x9B", "Digit1", false, "1", "이동 기즈모", "gizmo:0"),
                icon("gizmo.rotate", "회전", "\xE2\x9F\xB3", "Digit2", false, "2", "회전 기즈모", "gizmo:1"),
                icon("gizmo.scale",  "축척", "\xE2\xA4\xA1", "Digit3", false, "3", "축척 기즈모", "gizmo:2"),
            }},
            {"보정", {
                icon("snap.ortho", "직교", "\xE2\x8A\xA5", "F8", false, "F8", "직교 트랙킹", "orthoTrack"),
                icon("snap.grid",  "스냅", "\xE2\x96\xA6", "F9", false, "F9", "그리드 스냅", "gridSnap"),
            }},
        }},
        {"2D", {
            {"그리기", {
                icon("draw.line",     "선",     "\xE2\x95\xB1", "KeyL", false, "L", "선", "sketch:0"),
                icon("draw.rect",     "사각형", "\xE2\x96\xAD", "KeyB", false, "B", "사각형", "sketch:1"),
                icon("draw.polyline", "폴리선", "\xE2\x88\xA0", "KeyN", false, "N", "폴리선", "sketch:2"),
                icon("draw.circle",   "원",     "\xE2\x97\x8B", "KeyC", false, "C", "원", "sketch:3"),
                icon("draw.arc",      "호",     "\xE2\x8C\x92", "KeyA", false, "A", "호 (3점)", "sketch:4"),
                icon("draw.polygon",  "다각형", "\xE2\xAC\xA2", "KeyG", false, "G", "정다각형", "sketch:5"),
            }},
            {"주석", {
                icon("draw.dim",  "치수", "\xE2\x86\x94", "KeyD", false, "D", "정렬 치수", "sketch:6"),
                icon("draw.text", "문자", "\xEF\xBC\xA1", "KeyW", false, "W", "문자", "sketch:7"),
            }},
            {"수정", {
                icon("modify.move",   "이동", "\xE2\x9C\x9B", "KeyM", false, "M", "기준점 이동", "xform:0"),
                icon("modify.copy",   "복사", "\xE2\x9A\x8A", "KeyU", false, "U", "기준점 복사", "xform:1"),
                icon("modify.rotate", "회전", "\xE2\x9F\xB3", "KeyK", false, "K", "기준점 회전", "xform:2"),
                icon("modify.scale",  "축척", "\xE2\xA4\xA1", "KeyX", false, "X", "기준점 축척", "xform:3"),
                icon("modify.trim",   "자르기", "\xE2\x9C\x82", "#trim", false, "", "자르기 (Shift+클릭: 연장)", "trim"),
                icon("modify.extend", "연장", "\xE2\x87\xA5", "#extend", false, "", "연장 (Shift+클릭: 자르기)", "extend"),
                icon("modify.offset", "간격", "\xE2\xAB\xBD", "#offset", false, "", "간격띄우기 (거리 -> 객체 -> 방향)", "offset"),
                icon("modify.mirror", "대칭", "\xE2\x87\x8B", "#mirror", false, "", "두 점 대칭축 (Shift+클릭: 원본 지우기)", "xform:4"),
            }},
            {"마무리", {
                icon("draw.finish", "확정", "\xE2\x9C\x93", "Enter",  false, "Enter", "그리던 것을 확정"),
                icon("edit.cancel", "취소", "\xE2\x8E\x8B", "Escape", false, "Esc",   "도구 닫기 / 선택 해제"),
            }},
        }},
        {"3D", {
            {"만들기", {
                icon("draw.cube", "큐브", "\xE2\xAC\xA1", "#cube", false, "", "보고 있는 자리에 큐브 하나"),
            }},
            {"표시", {
                icon("view.parallel", "평행",   "\xE2\x96\xB1", "KeyP", false, "P", "원근 / 평행 투영", "ortho"),
                icon("view.fps",      "1인칭",  "\xE2\x9E\x94", "KeyV", false, "V", "CAD 궤도 / 1인칭", "fps"),
                icon("view.outline",  "외곽선", "\xE2\x97\x8E", "KeyO", false, "O", "선택 외곽선", "outline"),
            }},
        }},
    };
}

std::string LotRibbon::toJson() const {
    std::string j = "[";
    for (size_t t = 0; t < tabs_.size(); ++t) {
        if (t) j += ",";
        j += "{\"tab\":" + quote(tabs_[t].name) + ",\"groups\":[";
        for (size_t g = 0; g < tabs_[t].groups.size(); ++g) {
            if (g) j += ",";
            j += "{\"caption\":" + quote(tabs_[t].groups[g].caption) + ",\"items\":[";
            for (size_t i = 0; i < tabs_[t].groups[g].commands.size(); ++i) {
                if (i) j += ",";
                j += commandJson(tabs_[t].groups[g].commands[i]);
            }
            j += "]}";
        }
        j += "]}";
    }
    j += "]";
    return j;
}

}  // namespace lot_ui

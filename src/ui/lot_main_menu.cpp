#include "lot_main_menu.h"

namespace lot_ui {
namespace {

// 메뉴 항목 하나 적기. 인자 순서가 길어 보이지만 표가 읽히는 편이 낫다.
Command item(const char* id, const char* label, const char* key, bool ctrl,
             const char* shortcut, const char* tip, const char* state = "",
             bool sep = false) {
    Command c;
    c.id = id;
    c.label = label;
    c.keyCode = key;
    c.ctrl = ctrl;
    c.shortcut = shortcut;
    c.tip = tip;
    c.state = state;
    c.separatorBefore = sep;
    return c;
}

}  // namespace

LotMainMenu::LotMainMenu() {
    menus_ = {
        {"파일", {
            item("file.openLot",  "열기 (.lot)",     "@openLot", false, "",       "저장해 둔 씬을 연다 (지금 씬을 대체)"),
            item("file.openDxf",  "DXF 열기...",     "@openDxf", false, "",       "AutoCAD DXF 도면을 연다 (지금 씬을 대체)"),
            item("file.openObj",  "OBJ 가져오기...", "@openObj", false, "",       "Wavefront OBJ 모델을 지금 씬에 얹는다"),
            item("file.saveLot",  "저장 (.lot)",     "@saveLot", false, "",       "씬을 scene.lot 으로 내려받는다", "", true),
        }},
        {"편집", {
            item("edit.undo",     "실행 취소",  "KeyZ",   true,  "Ctrl+Z", "마지막 편집을 되돌린다", "undo"),
            item("edit.redo",     "다시 실행",  "KeyY",   true,  "Ctrl+Y", "되돌린 편집을 다시 한다", "redo"),
            item("edit.dup",      "복제",       "KeyD",   true,  "Ctrl+D", "선택을 제자리에 복제한다", "", true),
            item("edit.delete",   "삭제",       "Delete", false, "Del",    "선택을 지운다"),
            item("edit.cancel",   "선택 해제",  "Escape", false, "Esc",    "도구를 닫거나 선택을 푼다"),
        }},
        {"뷰", {
            item("view.front",    "정면도",       "KeyF", false, "F", "앞(-Y)에서 본다", "view:0"),
            item("view.top",      "평면도",       "KeyT", false, "T", "위(+Z)에서 내려다본다", "view:2"),
            item("view.right",    "우측면도",     "KeyR", false, "R", "오른쪽(+X)에서 본다", "view:4"),
            item("view.iso",      "등각",         "KeyI", false, "I", "앞-왼쪽-위에서 본다", "view:6"),
            item("view.fit",      "전체 보기",    "KeyZ", false, "Z", "씬 전체가 화면에 담기게", "", true),
            item("view.parallel", "평행 투영",    "KeyP", false, "P", "원근 / 평행(직교) 전환", "ortho"),
            item("view.fps",      "1인칭",        "KeyV", false, "V", "CAD 궤도 / 1인칭 전환", "fps"),
            item("view.outline",  "선택 외곽선",  "KeyO", false, "O", "선택한 것에 외곽선", "outline", true),
        }},
        {"그리기", {
            item("draw.line",     "선",        "KeyL", false, "L", "두 점씩 이어 그린다", "sketch:0"),
            item("draw.rect",     "사각형",    "KeyB", false, "B", "마주보는 두 꼭짓점", "sketch:1"),
            item("draw.polyline", "폴리선",    "KeyN", false, "N", "점을 이어 그린다 (첫 점에서 닫힘)", "sketch:2"),
            item("draw.circle",   "원",        "KeyC", false, "C", "중심과 반지름", "sketch:3"),
            item("draw.arc",      "호",        "KeyA", false, "A", "세 점을 지나는 호", "sketch:4"),
            item("draw.polygon",  "정다각형",  "KeyG", false, "G", "중심과 꼭짓점 ([ ] 로 변 수)", "sketch:5"),
            item("draw.dim",      "치수",      "KeyD", false, "D", "두 점을 재고 치수선 위치", "sketch:6", true),
            item("draw.text",     "문자",      "KeyW", false, "W", "시작점을 찍고 입력", "sketch:7"),
            item("draw.finish",   "끝내기",    "Enter", false, "Enter", "그리던 것을 확정", "", true),
        }},
        {"수정", {
            item("modify.move",   "이동",   "KeyM", false, "M", "기준점 -> 목적점, 또는 거리 입력", "xform:0"),
            item("modify.copy",   "복사",   "KeyU", false, "U", "기준점을 잡고 여러 번 놓는다", "xform:1"),
            item("modify.rotate", "회전",   "KeyK", false, "K", "기준점 둘레로, 또는 각도 입력", "xform:2"),
            item("modify.scale",  "축척",   "KeyX", false, "X", "기준점 기준 배율, 또는 배율 입력", "xform:3"),
            item("gizmo.move",    "기즈모: 이동",  "Digit1", false, "1", "축·평면 화살표로 끌기", "gizmo:0", true),
            item("gizmo.rotate",  "기즈모: 회전",  "Digit2", false, "2", "링으로 돌리기", "gizmo:1"),
            item("gizmo.scale",   "기즈모: 축척",  "Digit3", false, "3", "핸들로 키우기", "gizmo:2"),
        }},
        {"설정", {
            item("snap.ortho", "직교 트랙킹", "F8", false, "F8", "작업평면의 한 축으로만 나가게", "orthoTrack"),
            item("snap.grid",  "그리드 스냅", "F9", false, "F9", "커서를 격자 눈금에 붙인다", "gridSnap"),
        }},
    };
}

std::string LotMainMenu::toJson() const {
    std::string j = "[";
    for (size_t m = 0; m < menus_.size(); ++m) {
        if (m) j += ",";
        j += "{\"title\":" + quote(menus_[m].caption) + ",\"items\":[";
        for (size_t i = 0; i < menus_[m].commands.size(); ++i) {
            if (i) j += ",";
            j += commandJson(menus_[m].commands[i]);
        }
        j += "]}";
    }
    j += "]";
    return j;
}

}  // namespace lot_ui

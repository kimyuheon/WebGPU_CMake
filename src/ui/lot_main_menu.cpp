#include "lot_main_menu.h"
#include "lot_osnap.h"

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
            item("file.newDoc",   "새 도면",         "#newDoc",  false, "",       "빈 도면을 새 탭에 연다"),
            item("file.closeDoc", "도면 닫기",       "#closeDoc",false, "",       "지금 탭을 닫는다 (마지막 하나면 빈 도면으로)"),
            item("file.nextDoc",  "다음 도면",       "#nextDoc", false, "",       "오른쪽 탭으로"),
            item("file.prevDoc",  "이전 도면",       "#prevDoc", false, "",       "왼쪽 탭으로"),
            item("file.open",     "열기...",         "@open",    false, "",       ".lot/.dxf 도면은 새 탭에, .obj 모델은 지금 씬에 연다", "", true),
            item("file.saveLot",  "저장 (.lot)",     "@saveLot", false, "",       "씬을 scene.lot 으로 내려받는다", "", true),
            item("file.saveDxf",  "DXF 로 내보내기", "@saveDxf", false, "",       "2D 도면을 scene.dxf 로 내려받는다 (메시는 빠진다)"),
        }},
        {"편집", {
            item("edit.undo",     "실행 취소",  "KeyZ",   true,  "Ctrl+Z", "마지막 편집을 되돌린다", "undo"),
            item("edit.redo",     "다시 실행",  "KeyY",   true,  "Ctrl+Y", "되돌린 편집을 다시 한다", "redo"),
            item("edit.dup",      "복제",       "KeyD",   true,  "Ctrl+D", "선택을 제자리에 복제한다", "", true),
            item("edit.selectAll","전체 선택",  "KeyA",   true,  "Ctrl+A", "고를 수 있는 것을 모두 선택한다 (잠긴 층 제외)", "", true),
            item("edit.delete",   "삭제",       "Delete", false, "Del",    "선택을 지운다"),
            item("edit.eraseAll", "전체 지우기","#eraseAll", false, "",    "도면을 통째로 비운다 (Ctrl+Z 로 되돌릴 수 있다)"),
            item("edit.cancel",   "선택 해제",  "Escape", false, "Esc",    "도구를 닫거나 선택을 푼다"),
        }},
        {"뷰", {
            item("view.front",    "정면도",       "KeyF", false, "F", "앞(-Y)에서 본다", "view:0"),
            item("view.top",      "평면도",       "KeyT", false, "T", "위(+Z)에서 내려다본다", "view:2"),
            item("view.right",    "우측면도",     "KeyR", false, "R", "오른쪽(+X)에서 본다", "view:4"),
            item("view.iso",      "등각",         "KeyI", false, "I", "앞-왼쪽-위에서 본다", "view:6"),
            item("view.fit",      "전체 보기",    "KeyZ", false, "Z", "씬 전체가 화면에 담기게 (휠 더블 클릭도)", "", true),
            item("view.parallel", "평행 투영",    "KeyP", false, "P", "원근 / 평행(직교) 전환", "ortho"),
            item("view.fps",      "1인칭",        "KeyV", false, "V", "CAD 궤도 / 1인칭 전환", "fps"),
            item("view.outline",  "선택 외곽선",  "KeyO", false, "O", "선택한 것에 외곽선", "outline", true),
            item("view.grid",     "격자 표시",    "F7",   false, "F7", "바닥 격자 보이기 / 숨기기", "gridShow", true),
            item("view.dims",     "치수 표시",    "#dims", false, "", "치수 보이기 / 숨기기 (숨기면 고를 수도 없다)", "dims"),
            item("view.style0",   "셰이딩",             "#style:0", false, "", "면만 칠한다", "style:0", true),
            item("view.style1",   "셰이딩 + 엣지",      "#style:1", false, "", "면 + 모서리 선", "style:1"),
            item("view.style2",   "와이어프레임 (메쉬)", "#style:2", false, "", "삼각형 변을 전부 선으로", "style:2"),
            item("view.style3",   "와이어프레임 (엣지)", "#style:3", false, "", "모서리 선만", "style:3"),
            item("view.style4",   "숨은선 제거",        "#style:4", false, "", "모서리 선, 뒤에 가려진 선은 숨긴다", "style:4"),
            item("panel.layers",  "레이어 창",          "@panel:layers", false, "", "레이어 패널 열기/닫기", "", true),
            item("panel.props",   "속성 창",            "@panel:props", false, "", "속성 패널 열기/닫기"),
            item("panel.tree",    "노드 트리 창",       "@panel:tree", false, "", "노드 트리 패널 열기/닫기"),
            item("panel.reset",   "패널 배치 초기화",   "@panel:reset", false, "", "패널을 처음 자리로"),
        }},
        {"그리기", {
            item("draw.line",     "선",        "KeyL", false, "L", "두 점씩 이어 그린다", "sketch:0"),
            item("draw.rect",     "사각형",    "KeyB", false, "B", "마주보는 두 꼭짓점", "sketch:1"),
            item("draw.polyline", "폴리선",    "KeyN", false, "N", "점을 이어 그린다 (첫 점에서 닫힘)", "sketch:2"),
            item("draw.circle",   "원",        "KeyC", false, "C", "중심과 반지름", "sketch:3"),
            item("draw.arc",      "호",        "KeyA", false, "A", "세 점을 지나는 호", "sketch:4"),
            item("draw.polygon",  "정다각형",  "KeyG", false, "G", "중심과 꼭짓점 ([ ] 로 변 수)", "sketch:5"),
            item("draw.dim",      "치수",      "KeyD", false, "D", "두 점을 재고 치수선 위치", "sketch:6", true),
            item("draw.cube",     "큐브",       "#cube",  false, "",    "보고 있는 자리에 큐브 하나 (3D)", "", true),
            item("draw.text",     "문자",      "KeyW", false, "W", "시작점을 찍고 입력", "sketch:7"),
            item("draw.finish",   "끝내기",    "Enter", false, "Enter", "그리던 것을 확정", "", true),
        }},
        {"수정", {
            item("modify.move",   "이동",   "KeyM", false, "M", "기준점 -> 목적점, 또는 거리 입력", "xform:0"),
            item("modify.copy",   "복사",   "KeyU", false, "U", "기준점을 잡고 여러 번 놓는다", "xform:1"),
            item("modify.rotate", "회전",   "KeyK", false, "K", "기준점 둘레로, 또는 각도 입력", "xform:2"),
            item("modify.scale",  "축척",   "KeyX", false, "X", "기준점 기준 배율, 또는 배율 입력", "xform:3"),
            item("modify.mirror", "대칭",   "#mirror", false, "", "대칭축 두 점 - 사본을 만든다 (Shift+클릭이면 원본 지우기)", "xform:4"),
            item("gizmo.move",    "기즈모: 이동",  "Digit1", false, "1", "축·평면 화살표로 끌기", "gizmo:0", true),
            item("gizmo.rotate",  "기즈모: 회전",  "Digit2", false, "2", "링으로 돌리기", "gizmo:1"),
            item("gizmo.scale",   "기즈모: 축척",  "Digit3", false, "3", "핸들로 키우기", "gizmo:2"),
        }},
        {"설정", {
            item("snap.osnap", "객체 스냅",   "F3", false, "F3", "끝점·중점·중심·교차·수직에 붙인다", "osnap"),
            item("snap.ortho", "직교 트랙킹", "F8", false, "F8", "작업평면의 한 축으로만 나가게", "orthoTrack"),
            item("snap.polar", "극좌표 트랙킹", "F10", false, "F10", "15° 배수 방향 근처면 그 방향에 붙인다", "polar"),
            item("snap.grid",  "그리드 스냅", "F9", false, "F9", "커서를 격자 눈금에 붙인다", "gridSnap"),
        }},
    };

    // 하단 상태바의 토글 단추들 (AutoCAD 상태 표시줄 자리). 메뉴의 명령을 짧은 이름으로
    // 다시 꺼낸 것이라 키와 켜짐 표시가 메뉴와 갈라지지 않는다.
    auto findCommand = [&](const std::string& id) -> const Command* {
        for (const Group& g : menus_) {
            for (const Command& c : g.commands) if (c.id == id) return &c;
        }
        return nullptr;
    };
    auto statusItem = [&](const char* id, const char* label) {
        const Command* c = findCommand(id);
        if (!c) return;
        Command copy = *c;
        copy.label = label;
        copy.separatorBefore = false;
        statusBar_.push_back(copy);
    };

    statusItem("view.fit", "전체 보기");
    statusItem("view.dims", "치수");
    statusItem("view.grid", "그리드");

    // 셰이딩: 누르면 비주얼 스타일 메뉴가 위로 펼쳐진다 (단추 자체는 키가 없다)
    {
        Command style;
        style.id = "status.style";
        style.label = "셰이딩";
        style.tip = "비주얼 스타일";
        style.state = "shaded";
        style.menuTitle = "비주얼 스타일";
        for (int i = 0; i < 5; ++i) {
            if (const Command* c = findCommand("view.style" + std::to_string(i))) {
                Command m = *c;
                m.separatorBefore = false;
                style.menu.push_back(m);
            }
        }
        statusBar_.push_back(style);
    }

    statusItem("view.parallel", "Ortho");
    statusItem("snap.ortho", "직교(F8)");
    statusItem("snap.polar", "극좌표(F10)");
    statusItem("snap.grid", "스냅(F9)");

    // 객체스냅: 누르면 F3 켜기/끄기, 옆의 ▴ 로 종류 설정 메뉴
    if (const Command* osnap = findCommand("snap.osnap")) {
        Command c = *osnap;
        c.label = "객체스냅";
        c.menuTitle = "객체스냅 설정";
        using lot_osnap::Kind;
        // 메뉴 순서와 구분선 (AutoCAD 객체스냅 설정과 같은 묶음)
        const struct { Kind kind; bool sep; } kOrder[] = {
            {Kind::Endpoint, false}, {Kind::Midpoint, false}, {Kind::Center, false}, {Kind::FaceCenter, false},
            {Kind::Node, true}, {Kind::Quadrant, false}, {Kind::Intersection, false},
            {Kind::Perpendicular, true}, {Kind::Tangent, false}, {Kind::Nearest, false},
        };
        for (const auto& o : kOrder) {
            const std::string n = std::to_string(static_cast<int>(o.kind));
            Command m;
            m.id = "osnap.kind" + n;
            m.label = lot_osnap::kindLabel(o.kind);
            m.keyCode = "#osnapKind:" + n;
            m.state = "osnapKind:" + n;
            m.tip = std::string(lot_osnap::kindLabel(o.kind)) + " 스냅 켜기/끄기";
            m.separatorBefore = o.sep;
            c.menu.push_back(m);
        }
        Command all;
        all.id = "osnap.all"; all.label = "전체 켜기"; all.keyCode = "#osnapAll"; all.separatorBefore = true;
        Command none;
        none.id = "osnap.none"; none.label = "전체 끄기"; none.keyCode = "#osnapNone";
        c.menu.push_back(all);
        c.menu.push_back(none);
        statusBar_.push_back(c);
    }
}

std::string LotMainMenu::statusBarJson() const {
    std::string j = "[";
    for (size_t i = 0; i < statusBar_.size(); ++i) {
        if (i) j += ",";
        j += commandJson(statusBar_[i]);
    }
    j += "]";
    return j;
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

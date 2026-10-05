#pragma once

#include "lot_command_line.h"
#include "lot_view_cube.h"
#include "lot_layer_panel.h"
#include "lot_main_menu.h"
#include "lot_node_tree.h"
#include "lot_property_panel.h"
#include "lot_ribbon.h"

#include <string>
#include <vector>

// 화면 UI 전체를 한 자리에서. Vulkan 쪽에서 FirstApp 이 메뉴/리본/패널을 차례로
// 그리는 것과 같은 자리지만, 여기서는 DOM 이 그리므로 '무엇을 보여줄지' 만 만든다.
//
// 렌더 루프는 update() 하나만 부르면 된다: 바뀐 것이 있을 때만 JS 로 넘어간다.
namespace lot_ui {

// 프레임마다 바뀌는 값들. main 이 채워 넘긴다.
struct State {
    int gizmoMode = -1;    // 0 이동 / 1 회전 / 2 축척
    int sketchTool = -1;   // SketchController::Kind
    int xformMode = -1;    // TransformTool::Mode - 1
    int view = -1;         // LotCamera::CadViewType (표준 뷰를 누른 직후에만)
    bool fps = false;
    bool ortho = false;        // 평행 투영
    bool orthoTracking = false;
    bool gridSnap = false;
    bool osnap = false;
    bool polar = false;
    bool gridShow = false;     // 바닥 격자 표시
    bool dims = false;         // 치수 표시
    int visualStyle = 0;       // main 의 DisplaySettings::Style
    unsigned osnapKinds = 0;   // lot_osnap::enabledKinds()
    bool outline = false;
    bool offset = false;       // 간격띄우기 도구가 열려 있다
    bool trim = false;         // 자르기
    bool extend = false;       // 연장
    bool canUndo = false;
    bool canRedo = false;
    std::string hint;      // 열린 도구의 다음 할 일

    bool operator==(const State& o) const;
};

// 도면 탭 하나. 탭 줄은 이름과 '고쳐졌나' 만 안다.
struct DocTab {
    std::string name;
    bool modified = false;

    bool operator==(const DocTab& o) const { return name == o.name && modified == o.modified; }
};

class LotUi {
public:
    // 메뉴/리본 뼈대를 DOM 에 한 번 보낸다 (캔버스가 생긴 뒤).
    void install();

    // 프레임마다. 바뀐 것만 보낸다.
    void update(const State& state, const LotLayers& layers, const LotGameObject::Map& objects,
                const EditController& edit, const std::vector<DocTab>& tabs, int activeTab);

    // DOM 이 아직 없어 놓쳤을 때 다시 보내게 한다.
    void invalidate();

    LotLayerPanel& layerPanel() { return layerPanel_; }
    LotNodeTree& nodeTree() { return nodeTree_; }
    LotPropertyPanel& propertyPanel() { return propertyPanel_; }
    LotCommandLine& commandLine() { return commandLine_; }

    // 뷰큐브: 자세가 바뀐 프레임에만 DOM 으로 넘어간다. 카메라가 필요해 따로 둔다.
    void updateViewCube(const LotCamera& camera);

private:
    LotMainMenu menu_;
    LotRibbon ribbon_;
    LotLayerPanel layerPanel_;
    LotNodeTree nodeTree_;
    LotPropertyPanel propertyPanel_;
    LotCommandLine commandLine_;
    LotViewCube viewCube_;
    bool viewCubeInstalled_ = false;
    State lastState_;
    bool statePushed_ = false;
    std::vector<DocTab> lastTabs_;
    int lastActiveTab_ = -1;   // -1 = 아직 안 보냈다
    bool installed_ = false;
};

}  // namespace lot_ui

#include "lot_ui.h"

extern "C" {
    // src/js/lot_ui.js. 메뉴/리본 뼈대는 한 번, 상태와 층은 바뀔 때만.
    // 셋 다 DOM 이 아직 없으면 0 을 돌려준다 - 그러면 다음 프레임에 다시 보낸다.
    extern int js_uiInstall(const char* menuJson, const char* ribbonJson, const char* commandNames,
                            const char* statusBarJson);
    extern int js_uiSetState(const char* stateJson);
    extern int js_uiSetLayers(const char* layersJson);
    extern int js_uiSetTabs(const char* tabsJson);
    // src/js/lot_dock.js. 노드 트리 요약 / 속성. DOM 이 아직이면 0.
    extern int js_uiSetTree(const char* treeJson);
    extern int js_uiSetProperties(const char* propertiesJson);
    // src/js/lot_view_cube.js. 상자는 한 번, 자세는 바뀔 때만.
    extern int js_viewCubeInstall();
    extern int js_viewCubeOrient(const char* matrixCss);
}

namespace lot_ui {

bool State::operator==(const State& o) const {
    return gizmoMode == o.gizmoMode && sketchTool == o.sketchTool && xformMode == o.xformMode
        && view == o.view && fps == o.fps && ortho == o.ortho
        && orthoTracking == o.orthoTracking && gridSnap == o.gridSnap && outline == o.outline
        && osnap == o.osnap && polar == o.polar && gridShow == o.gridShow && dims == o.dims
        && visualStyle == o.visualStyle && osnapKinds == o.osnapKinds
        && canUndo == o.canUndo && canRedo == o.canRedo && hint == o.hint;
}

void LotUi::install() {
    if (installed_) return;
    commandLine_.build(menu_, ribbon_);
    installed_ = js_uiInstall(menu_.toJson().c_str(), ribbon_.toJson().c_str(),
                              commandLine_.namesJson().c_str(), menu_.statusBarJson().c_str()) != 0;
}

void LotUi::invalidate() {
    installed_ = false;
    statePushed_ = false;
    lastActiveTab_ = -1;
    viewCubeInstalled_ = false;
    viewCube_.invalidate();
    layerPanel_.invalidate();
    nodeTree_.invalidate();
    propertyPanel_.invalidate();
}

void LotUi::updateViewCube(const LotCamera& camera) {
    if (!viewCubeInstalled_) {
        viewCubeInstalled_ = js_viewCubeInstall() != 0;
        if (!viewCubeInstalled_) return;  // 메뉴/리본이 아직 - 다음 프레임에
        viewCube_.invalidate();           // 새 DOM 에는 자세를 한 번 밀어 넣는다
    }
    if (viewCube_.poll(camera) && js_viewCubeOrient(viewCube_.matrixCss().c_str()) == 0) {
        viewCube_.invalidate();           // DOM 이 없어졌다 - 다음 프레임에 다시
    }
}

void LotUi::update(const State& state, const LotLayers& layers,
                   const LotGameObject::Map& objects, const EditController& edit,
                   const std::vector<DocTab>& tabs, int activeTab) {
    install();
    if (!installed_) return;  // DOM 이 아직 - 다음 프레임에

    if (!statePushed_ || !(state == lastState_)) {
        // 켜진 것들을 상태 키로 적는다 - 메뉴와 리본이 같은 키를 본다.
        std::string j = "{\"active\":[";
        bool first = true;
        auto add = [&](const std::string& key) {
            if (!first) j += ",";
            first = false;
            j += quote(key);
        };
        if (state.gizmoMode >= 0) add("gizmo:" + std::to_string(state.gizmoMode));
        if (state.sketchTool >= 0) add("sketch:" + std::to_string(state.sketchTool));
        if (state.xformMode >= 0) add("xform:" + std::to_string(state.xformMode));
        if (state.view >= 0) add("view:" + std::to_string(state.view));
        if (state.fps) add("fps");
        if (state.ortho) add("ortho");
        if (state.orthoTracking) add("orthoTrack");
        if (state.gridSnap) add("gridSnap");
        if (state.osnap) add("osnap");
        if (state.polar) add("polar");
        if (state.gridShow) add("gridShow");
        if (state.dims) add("dims");
        add("style:" + std::to_string(state.visualStyle));
        if (state.visualStyle != 2 && state.visualStyle != 3) add("shaded");   // 면이 칠해지는 스타일
        for (unsigned k = 1; k < 32; ++k) {
            if (state.osnapKinds & (1u << k)) add("osnapKind:" + std::to_string(k));
        }
        if (state.outline) add("outline");
        j += "],\"disabled\":[";
        first = true;
        if (!state.canUndo) add("undo");
        if (!state.canRedo) add("redo");
        j += "],\"hint\":" + quote(state.hint) + "}";

        if (js_uiSetState(j.c_str()) == 0) return;  // DOM 이 준비 안 됨 - 기억하지 않는다
        lastState_ = state;
        statePushed_ = true;
    }

    if (activeTab != lastActiveTab_ || !(tabs == lastTabs_)) {
        std::string j = "{\"active\":" + std::to_string(activeTab) + ",\"tabs\":[";
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (i) j += ",";
            j += "{\"name\":" + quote(tabs[i].name)
               + ",\"modified\":" + (tabs[i].modified ? "true" : "false") + "}";
        }
        j += "]}";
        if (js_uiSetTabs(j.c_str()) != 0) {
            lastTabs_ = tabs;
            lastActiveTab_ = activeTab;
        }
    }

    const std::string layersJson = layerPanel_.diffJson(layers, objects, edit,
                                                        nodeTree_.layerCounts(objects, edit));
    if (!layersJson.empty() && js_uiSetLayers(layersJson.c_str()) == 0) {
        layerPanel_.invalidate();  // 다음 프레임에 다시
    }
    const std::string treeJson = nodeTree_.summaryJson(layers, objects, edit);
    if (!treeJson.empty() && js_uiSetTree(treeJson.c_str()) == 0) {
        nodeTree_.invalidate();
    }
    const std::string propsJson = propertyPanel_.diffJson(layers, objects, edit);
    if (!propsJson.empty() && js_uiSetProperties(propsJson.c_str()) == 0) {
        propertyPanel_.invalidate();
    }
}

}  // namespace lot_ui

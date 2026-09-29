#include "lot_ui.h"

extern "C" {
    // src/js/lot_ui.js. 메뉴/리본 뼈대는 한 번, 상태와 층은 바뀔 때만.
    // 셋 다 DOM 이 아직 없으면 0 을 돌려준다 - 그러면 다음 프레임에 다시 보낸다.
    extern int js_uiInstall(const char* menuJson, const char* ribbonJson);
    extern int js_uiSetState(const char* stateJson);
    extern int js_uiSetLayers(const char* layersJson);
}

namespace lot_ui {

bool State::operator==(const State& o) const {
    return gizmoMode == o.gizmoMode && sketchTool == o.sketchTool && xformMode == o.xformMode
        && view == o.view && fps == o.fps && ortho == o.ortho
        && orthoTracking == o.orthoTracking && gridSnap == o.gridSnap && outline == o.outline
        && canUndo == o.canUndo && canRedo == o.canRedo && hint == o.hint;
}

void LotUi::install() {
    if (installed_) return;
    installed_ = js_uiInstall(menu_.toJson().c_str(), ribbon_.toJson().c_str()) != 0;
}

void LotUi::invalidate() {
    installed_ = false;
    statePushed_ = false;
    layerPanel_.invalidate();
}

void LotUi::update(const State& state, const LotLayers& layers,
                   const LotGameObject::Map& objects, const EditController& edit) {
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

    const std::string layersJson = layerPanel_.diffJson(layers, objects, edit);
    if (!layersJson.empty() && js_uiSetLayers(layersJson.c_str()) == 0) {
        layerPanel_.invalidate();  // 다음 프레임에 다시
    }
}

}  // namespace lot_ui

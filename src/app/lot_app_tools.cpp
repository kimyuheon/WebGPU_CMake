// 도구 묶음 - 스케치 · 변환 · 간격띄우기 · 자르기 · 필렛 · 배열 · 끊기 · 늘이기 · 길이조정 · 돌출.
// 도구를 하나 더할 때 고칠 곳이 여기 한 파일이다 (예전에는 main 곳곳의 취소 목록 · Enter/Esc 사슬 · 안내문에
// 흩어져 있었다). 도구들은 한 번에 하나만 열려 있다 - 열기 전에 cancelTools 로 나머지를 내려놓는다.
#include "app/lot_app.h"

#include "lot_feature.h"
#include "lot_log.h"

namespace {

float viewWidth() { return static_cast<float>(g_renderer->getSwapchain().getWidth()); }
float viewHeight() { return static_cast<float>(g_renderer->getSwapchain().getHeight()); }

ExtrudeTool::Context extrudeContext(float w, float h) {
    return ExtrudeTool::Context{doc().camera, g_mouse, doc().objects, doc().edit.snap(), w, h};
}
StretchTool::Context stretchContext(float w, float h) {
    return StretchTool::Context{doc().camera, g_mouse, doc().objects, doc().edit.snap(), w, h};
}
TransformTool::Context transformContext(float w, float h) {
    return TransformTool::Context{doc().camera, g_mouse, doc().objects, doc().edit.snap(), w, h};
}

// 돌출 도구가 끝났으면 만든 / 고친 솔리드를 선택
void selectExtrudeResult() {
    if (auto r = g_extrude.consumeResult(); !r.empty()) doc().edit.setSelection(r);
}

}  // namespace

void cancelTools(bool keepSketch) {
    if (!keepSketch) g_sketch.cancel();
    g_transform.cancel(doc().objects);
    g_offset.cancel();
    g_trim.cancel();
    g_fillet.cancel();
    g_array.close();
    g_break.cancel();
    g_stretch.cancel();
    g_lengthen.cancel();
    g_extrude.cancel();
    g_pendingOp.clear();
}

bool toolsTakeClicks() {
    return g_sketch.anyActive() || g_transform.isActive() || g_offset.isActive() || g_trim.isActive()
        || g_fillet.isActive() || g_array.takesClicks() || g_break.isActive() || g_stretch.takesClicks()
        || g_lengthen.isActive() || g_extrude.isActive();
}

const vec3* toolReferencePoint() {
    if (const vec3* p = g_stretch.referencePoint()) return p;
    return g_transform.isActive() ? g_transform.referencePoint() : g_sketch.referencePoint();
}

void updateTools(float width, float height) {
    // 숫자 버퍼는 키 컨트롤러가 모은 것을 넘긴다 (값을 기다리는 도구가 있을 때만 모은다)
    g_cameraController.setNumberCapture(g_transform.isPreviewing() || g_offset.wantsNumber()
                                        || g_fillet.wantsNumber() || g_stretch.wantsNumber()
                                        || g_lengthen.wantsNumber() || g_extrude.wantsNumber());
    const std::string& number = g_cameraController.numberBuffer();
    g_transform.setNumberBuffer(number);
    g_offset.setNumberBuffer(number);
    g_fillet.setNumberBuffer(number);
    g_stretch.setNumberBuffer(number);
    g_lengthen.setNumberBuffer(number);
    g_extrude.setNumberBuffer(number);

    EditHistory& history = doc().edit.history();
    const auto tctx = transformContext(width, height);
    g_transform.update(tctx, history);
    if (const auto copies = g_transform.consumeCreated(); !copies.empty()) {
        doc().edit.setSelection(copies);  // 놓은 사본을 선택 - 이어서 기즈모로 다듬을 수 있게
    }
    OffsetTool::Context octx{doc().camera, g_mouse, doc().objects, width, height};
    g_offset.update(octx, history);
    g_offset.consumeCreated();
    TrimTool::Context trctx{doc().camera, g_mouse, doc().objects, width, height};
    g_trim.update(trctx, history);
    FilletTool::Context fctx{doc().camera, g_mouse, doc().objects, width, height};
    g_fillet.update(fctx, history);
    ArrayTool::Context actx{doc().camera, g_mouse, doc().objects, doc().edit.snap(), width, height};
    g_array.update(actx);
    g_break.update(trctx, doc().edit.snap(), history);
    g_stretch.update(stretchContext(width, height), history);
    g_lengthen.update(trctx, history);
    g_extrude.update(extrudeContext(width, height), history, g_renderer->getDevice());
    selectExtrudeResult();

    // 피처 재생성: 연결된 스케치가 바뀐 솔리드를 다시 만든다 (편집 · 되돌리기마다 한 번)
    {
        static const LotDocument* lastDoc = nullptr;
        static uint64_t lastRevision = ~0ull;
        if (lastDoc != &doc() || lastRevision != history.revision()) {
            lastDoc = &doc();
            lastRevision = history.revision();
            lot_feature::regenerate(doc().objects, g_renderer->getDevice());
        }
    }

    // 스케치는 편집기가 찾아둔 스냅을 쓰므로 그 뒤에 온다. 활성이면 클릭을 가져간다.
    SketchController::Context sctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(), width, height};
    g_sketch.update(sctx);
    if (const auto id = g_sketch.consumeCommittedId(); id != LotGameObject::kInvalidId) {
        // 새로 그린 것은 현재 층에
        if (auto* obj = LotGameObject::find(doc().objects, id)) obj->layer = doc().layers.current();
        history.recordCreated("sketch", doc().objects, id);
    }
    // 문자 도구가 기준점을 찍었으면 브라우저 입력창을 연다 (글자 입력은 DOM 이 받는다)
    if (g_sketch.consumeTextInputRequest()) {
        g_editingTextId = LotGameObject::kInvalidId;
        js_showTextInput("text, Enter to place", "");
    }
    // 문자를 더블 클릭하면 그 내용을 고친다
    if (const auto id = doc().edit.consumeDoubleClicked(); id != LotGameObject::kInvalidId) {
        if (const auto* obj = LotGameObject::find(doc().objects, id); obj && obj->isText()) {
            g_editingTextId = id;
            js_showTextInput("edit text, Enter to apply", obj->text.content.c_str());
            LOT_LOG("text: editing object " << id);
        }
    }
}

void enterTools() {
    EditHistory& history = doc().edit.history();
    if (g_extrude.isActive()) {
        g_extrude.finish(extrudeContext(viewWidth(), viewHeight()), history, g_renderer->getDevice());
        selectExtrudeResult();
        g_cameraController.clearNumberBuffer();
    } else if (g_stretch.isActive()) {
        g_stretch.finish(stretchContext(viewWidth(), viewHeight()), history);
        g_cameraController.clearNumberBuffer();
    } else if (g_lengthen.isActive()) {
        g_lengthen.finish();
        g_cameraController.clearNumberBuffer();
    } else if (!g_pendingOp.empty()) {
        const std::string op = g_pendingOp;
        if (doc().edit.selection().empty()) {
            LOT_LOG(op << ": nothing selected");
        } else {
            g_pendingOp.clear();
            runPendingOp(op);
        }
    } else if (g_break.isActive()) {
        g_break.cancel();
    } else if (g_array.isActive()) {
        g_array.enter(doc().edit.selection(), doc().objects, history);
    } else if (g_fillet.isActive()) {
        g_fillet.finish();   // 숫자를 쳤으면 반지름/거리, 아니면 끝
        g_cameraController.clearNumberBuffer();
    } else if (g_trim.isActive()) {
        g_trim.cancel();
    } else if (g_offset.isActive()) {
        g_offset.finish();
        g_cameraController.clearNumberBuffer();
    } else if (g_transform.isActive()) {
        g_transform.finish(transformContext(viewWidth(), viewHeight()), history);
        g_cameraController.clearNumberBuffer();
    } else {
        g_sketch.finish(doc().objects);
    }
}

void escapeTools() {
    if (g_extrude.isActive()) { g_extrude.cancel(); g_cameraController.clearNumberBuffer(); }
    else if (g_stretch.isActive()) { g_stretch.cancel(); g_cameraController.clearNumberBuffer(); }
    else if (g_lengthen.isActive()) { g_lengthen.cancel(); g_cameraController.clearNumberBuffer(); }
    else if (!g_pendingOp.empty()) { LOT_LOG(g_pendingOp << ": cancelled"); g_pendingOp.clear(); }
    else if (g_break.isActive()) g_break.cancel();
    else if (g_array.isActive()) g_array.cancel();
    else if (g_fillet.isActive()) { g_fillet.cancel(); g_cameraController.clearNumberBuffer(); }
    else if (g_trim.isActive()) g_trim.cancel();
    else if (g_offset.isActive()) { g_offset.cancel(); g_cameraController.clearNumberBuffer(); }
    else if (g_transform.isActive()) g_transform.cancel(doc().objects);
    else if (g_sketch.anyActive()) { g_sketch.cancel(); js_hideTextInput(); }
    else doc().edit.clearSelection();
}

bool typedToolValue(const std::string& text) {
    EditHistory& history = doc().edit.history();
    // 끊기 중의 '@' (첫 점에서 나누기) / 'f' (첫 점 다시)
    if (g_break.typed(text, doc().objects, history)) return true;
    // 길이조정의 'de 0.5' / 'p 150' / 't 10' (값만도)
    if (g_lengthen.typed(text)) return true;
    // 늘이기 둘째 점의 '@dx,dy' / 거리
    if (g_stretch.isActive() && g_stretch.typed(text, stretchContext(viewWidth(), viewHeight()), history)) return true;

    // 숫자(또는 부호/소수점)로 시작하면 값이다 - 값을 기다리는 도구가 받는다.
    const char first = text[0];
    if (!(first == '-' || first == '.' || (first >= '0' && first <= '9'))) return false;
    if (g_extrude.wantsNumber()) {
        g_extrude.setNumberBuffer(text);
        g_extrude.finish(extrudeContext(viewWidth(), viewHeight()), history, g_renderer->getDevice());
        selectExtrudeResult();
    } else if (g_fillet.wantsNumber()) {
        g_fillet.setNumberBuffer(text);
        g_fillet.finish();
    } else if (g_offset.wantsNumber()) {
        g_offset.setNumberBuffer(text);
        g_offset.finish();
    } else if (g_transform.isPreviewing()) {
        g_transform.setNumberBuffer(text);
        g_transform.finish(transformContext(viewWidth(), viewHeight()), history);
    } else {
        LOT_LOG("command: " << text << " - no tool is waiting for a value");
        return true;
    }
    LOT_LOG("command: value " << text);
    return true;
}

std::string toolHint() {
    if (g_extrude.isActive()) return g_extrude.hint();
    if (g_stretch.isActive()) return g_stretch.hint();
    if (g_lengthen.isActive()) return g_lengthen.hint();
    if (!g_pendingOp.empty()) return g_pendingOp + ": select objects, then Enter  [Esc cancels]";
    if (g_break.isActive()) return g_break.hint();
    if (g_array.isActive()) return g_array.hint();
    if (g_fillet.isActive()) return g_fillet.hint();
    if (g_trim.isActive()) return g_trim.hint();
    if (g_offset.isActive()) return g_offset.hint();
    if (g_transform.isActive()) return g_transform.hint();
    return g_sketch.hint();
}

void fillToolUiState(lot_ui::State& ui) {
    ui.sketchTool = g_sketch.activeKind();
    ui.xformMode = g_transform.isActive() ? g_transform.modeIndex() - 1 : -1;
    ui.hint = toolHint();
    ui.offset = g_offset.isActive();
    ui.trim = g_trim.mode() == TrimTool::Mode::Trim;
    ui.extend = g_trim.mode() == TrimTool::Mode::Extend;
    ui.fillet = g_fillet.mode() == FilletTool::Mode::Fillet;
    ui.chamfer = g_fillet.mode() == FilletTool::Mode::Chamfer;
    ui.array = g_array.isActive();
    ui.breakTool = g_break.isActive();
    ui.stretch = g_stretch.isActive();
    ui.lengthen = g_lengthen.isActive();
    ui.solidTool = g_extrude.isActive() ? static_cast<int>(g_extrude.mode()) : -1;
    if (const std::string aj = g_array.dialogJson(); !aj.empty() && js_uiArrayDialog(aj.c_str()) == 0) {
        g_array.invalidateDialog();   // DOM 이 아직 - 다음 프레임에 다시
    }
}

void drawToolOverlays(float width, float height) {
    SketchController::Context sctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(), width, height};
    g_sketch.drawPreview(*g_polylineSystem, *g_lineSystem, *g_textSystem, sctx);
    g_transform.drawOverlay(*g_lineSystem, transformContext(width, height));
    OffsetTool::Context octx{doc().camera, g_mouse, doc().objects, width, height};
    g_offset.drawOverlay(*g_lineSystem, octx);
    TrimTool::Context trctx{doc().camera, g_mouse, doc().objects, width, height};
    g_trim.drawOverlay(*g_lineSystem, trctx);
    FilletTool::Context fctx{doc().camera, g_mouse, doc().objects, width, height};
    g_fillet.drawOverlay(*g_lineSystem, fctx);
    ArrayTool::Context actx{doc().camera, g_mouse, doc().objects, doc().edit.snap(), width, height};
    g_array.drawOverlay(*g_lineSystem, actx);
    g_break.drawOverlay(*g_lineSystem, trctx);
    g_stretch.drawOverlay(*g_lineSystem, stretchContext(width, height));
    g_extrude.drawOverlay(*g_lineSystem, extrudeContext(width, height));
}

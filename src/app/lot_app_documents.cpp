// 도면 탭 (LotDocument 목록), 전체 보기 · 객체로 줌, 읽어 들인 파일을 탭에 열기.
#include "app/lot_app.h"

#include "lot_dimension.h"
#include "lot_layers.h"
#include "lot_cursor_snap.h"
#include "lot_cursor_snap.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>
#include <cstdlib>

// ── 도면 탭 ─────────────────────────────────────────────────────
// 탭 하나가 LotDocument 하나다. 바꾸는 일은 doc() 가 가리키는 곳을 옮기는 것뿐이고,
// 그 전에 손에 든 도구를 내려놓는다 - 도구는 지금 도면의 오브젝트를 물고 있다.
// 오브젝트 id 는 전역 카운터라 도면끼리 겹치지 않고, 모델/재질은 shared_ptr 라
// 안 보이는 도면의 것도 그대로 살아 있다 (다시 올릴 것이 없다).

int g_untitledCount = 0;   // '도면N' 의 N

std::unique_ptr<LotDocument> makeDocument() {
    auto d = std::make_unique<LotDocument>();
    d->name = "도면" + std::to_string(++g_untitledCount);
    // 카메라: CAD 궤도가 기본. 앞-왼쪽-위에서 내려다보는 3/4 뷰로 시작한다.
    // 1인칭(V) 용 뷰어 오브젝트 위치도 같이 잡아둔다.
    d->camera.setViewFromDirection(normalize(vec3{-0.45f, -1.0f, 0.45f}));
    d->viewer.transform.translation = kCameraStartPosition;
    d->objPlaced = true;   // 가운데 토러스는 첫 도면에만 (main 이 풀어 준다)
    return d;
}

// 씬 크기에서 나온 값 중 도면 밖(도구 · 커서 설정)에 들어가 있는 것들을 지금 도면 것으로.
void applyDocumentScale() {
    g_sketch.setDimensionStyle(doc().dimTextHeight, doc().dimArrowSize);
    g_sketch.setTextHeight(doc().textHeight);
    if (lot_cursor::settings().gridSpacing > 0.0f) lot_cursor::settings().gridSpacing = doc().gridSpacing;
}

void putDownTools() {
    cancelTools();
    g_editingTextId = LotGameObject::kInvalidId;
    js_hideTextInput();
}

void activateDocument(size_t index) {
    if (index >= g_documents.size() || index == g_documentIndex) return;
    putDownTools();
    g_documentIndex = index;
    applyDocumentScale();
    LOT_LOG("document: switched to \"" << doc().name << "\" (" << index + 1 << "/"
            << g_documents.size() << ")");
}

void newDocument() {
    putDownTools();
    g_documents.push_back(makeDocument());
    g_documentIndex = g_documents.size() - 1;
    applyDocumentScale();
    LOT_LOG("document: new \"" << doc().name << "\" (" << g_documents.size() << " open)");
}

// 마지막 하나는 닫지 않고 새 빈 도면으로 바꾼다 - 탭은 늘 하나 이상이다 (네이티브와 같다).
void closeDocument(size_t index) {
    if (index >= g_documents.size()) return;
    if (index == g_documentIndex) putDownTools();
    const std::string name = g_documents[index]->name;
    if (g_documents.size() == 1) {
        g_documents[0] = makeDocument();
    } else {
        g_documents.erase(g_documents.begin() + static_cast<std::ptrdiff_t>(index));
        // 앞의 것이 빠졌거나 맨 끝 것을 닫았으면 한 칸 당긴다
        if (index < g_documentIndex || g_documentIndex >= g_documents.size()) --g_documentIndex;
    }
    applyDocumentScale();
    LOT_LOG("document: closed \"" << name << "\" (" << g_documents.size() << " open, showing \""
            << doc().name << "\")");
}

void stepDocument(int delta) {
    const int n = static_cast<int>(g_documents.size());
    activateDocument(static_cast<size_t>((static_cast<int>(g_documentIndex) + delta + n) % n));
}

// 탭 줄에서 온 것. index 는 탭 순서 (0 부터).
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onDocumentTab(const char* action, int index) {
    if (action == nullptr || index < 0) return;
    const std::string a(action);
    if (a == "activate") activateDocument(static_cast<size_t>(index));
    else if (a == "close") closeDocument(static_cast<size_t>(index));
}

// 전체 보기 (Zoom Extents). 모든 오브젝트의 월드 경계를 구해 카메라를 맞춘다.
// 클립 평면과 직교 줌 한계도 그 크기에 맞춘다 - 씬 단위가 m 든 mm 든 보이게.
// 객체들의 월드 경계. only 가 있으면 그 객체들만. 하나도 없으면 false.
bool objectBounds(const std::set<LotGameObject::id_t>* only, vec3& lo, vec3& hi) {
    bool any = false;
    auto grow = [&](const vec3& p) {
        if (!any) { lo = hi = p; any = true; return; }
        lo = vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    };
    for (const auto& entry : doc().objects) {
        if (only && !only->count(entry.first)) continue;
        const LotGameObject& obj = entry.second;
        if (obj.isSketch()) {
            for (const vec3& p : obj.worldPoints()) grow(p);
        } else if (obj.isDimension()) {
            for (const vec3& p : lot_dim::outlinePoints(obj)) grow(p);
        } else if (obj.isText()) {
            vec3 c[4];
            if (lot_text::quadCorners(obj, c)) for (int i = 0; i < 4; ++i) grow(c[i]);
        } else if (obj.isLight()) {
            grow(obj.transform.translation);
        } else if (obj.model) {
            // 경계 상자 여덟 꼭짓점을 변환한다 (회전한 상자도 안전하게 덮인다)
            const mat4 m = obj.transform.mat4Transform();
            const vec3& a = obj.model->boundsMin();
            const vec3& b = obj.model->boundsMax();
            for (int i = 0; i < 8; ++i) {
                grow(transformPoint(m, vec3{(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y,
                                            (i & 4) ? b.z : a.z}));
            }
        }
    }
    return any;
}

void zoomExtents() {
    vec3 lo{0.0f, 0.0f, 0.0f}, hi{0.0f, 0.0f, 0.0f};
    if (!objectBounds(nullptr, lo, hi)) return;

    const vec3 center = (lo + hi) * 0.5f;
    const vec3 half = (hi - lo) * 0.5f;
    const float radius = std::fmax(std::sqrt(dot(half, half)), 0.01f);

    doc().camera.setViewMode(LotCamera::ViewMode::Cad);
    doc().camera.frame(center, radius, kFovY);

    // 직교: 세로 절반에 반지름이 담기게. 한계도 씬 크기에 비례.
    doc().orthoHalfHeight = radius * 1.15f;
    doc().orthoMaxHalfHeight = std::fmax(20.0f, radius * 20.0f);
    doc().orthoMinHalfHeight = std::fmin(0.2f, radius * 0.001f);

    // 클립 평면: far 는 최대 궤도 거리 + 씬 반지름을 덮고, near 는 far 의 1e-5 이상.
    doc().farZ = std::fmax(100.0f, doc().camera.getMaxOrbitDistance() + radius * 2.0f);
    doc().nearZ = std::fmax(0.01f, doc().farZ * 1e-5f);

    // 치수 글자/화살표 기본 크기를 씬에 맞춘다 (mm 도면에서 0.3 짜리 글자는 안 보인다)
    doc().dimTextHeight = radius * 0.05f;
    doc().dimArrowSize = radius * 0.025f;
    doc().textHeight = radius * 0.07f;
    // 선종류 무늬도 씬 크기에 맞춘다 (mm 도면에서 0.5 짜리 파선은 실선처럼 보인다)
    doc().linetypeScale = std::fmax(0.01f, radius * 0.25f);
    // 그리드 스냅 간격도 - 화면 가로에 눈금이 20 개쯤 되게 '보기 좋은' 값으로
    doc().gridSpacing = lot_cursor::niceSpacing(radius * 0.1f);
    applyDocumentScale();

    LOT_LOG("view: zoom extents - center (" << center.x << ", " << center.y << ", " << center.z
            << ") radius " << radius << ", clip " << doc().nearZ << " .. " << doc().farZ);
}

// 객체들로 줌 (노드 트리 더블 클릭). 카메라만 옮긴다 - 클립 평면 · 치수 크기 같은
// 도면 단위 설정은 전체 보기가 정한 그대로 둔다.
void zoomToObjects(const std::set<LotGameObject::id_t>& ids) {
    vec3 lo, hi;
    if (!objectBounds(&ids, lo, hi)) return;
    const vec3 center = (lo + hi) * 0.5f;
    const vec3 half = (hi - lo) * 0.5f;
    const float radius = std::fmax(std::sqrt(dot(half, half)), doc().orthoMinHalfHeight);
    doc().camera.setViewMode(LotCamera::ViewMode::Cad);
    doc().camera.focus(center, radius, kFovY);
    doc().orthoHalfHeight = std::fmin(std::fmax(radius * 1.15f, doc().orthoMinHalfHeight), doc().orthoMaxHalfHeight);
    LOT_LOG("view: zoom to " << ids.size() << " objects - radius " << radius);
}

// 읽어 들인 파일을 탭에. 지금 탭이 손대지 않은 새 도면이면 그 자리에, 아니면 새 탭에.
// fileName 은 탭 이름과 저장할 때의 파일 이름이 된다 (테스트 도구는 안 넘긴다 - 그러면 그대로).
void openIntoDocument(LotGameObject::Map&& objects, LotLayers&& layers, const char* fileName) {
    if (doc().pristine()) putDownTools();
    else newDocument();
    doc().edit.clearSelection();
    doc().edit.history().clear();
    doc().objects = std::move(objects);
    doc().layers = std::move(layers);
    doc().objPlaced = true;  // 파일 도면에는 기본 토러스를 끼워 넣지 않는다
    if (fileName != nullptr && fileName[0] != '\0') {
        doc().path = fileName;
        const size_t dot = doc().path.find_last_of('.');
        doc().name = (dot == std::string::npos || dot == 0) ? doc().path : doc().path.substr(0, dot);
    }
    doc().markSaved();
    zoomExtents();       // 단위가 다른 파일(mm 도면)도 바로 보이게
    LOT_LOG("document: \"" << doc().name << "\" opened (" << g_documents.size() << " open)");
}

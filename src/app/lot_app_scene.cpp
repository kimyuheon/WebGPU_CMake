// 기본 씬 (큐브 · 광원 · 토러스), 층 콜백, 객체 그리기 (스케치 · 해치 · 치수 · 문자 · 광원 · 메시 모서리).
#include "app/lot_app.h"

#include "lot_dimension.h"
#include "lot_layers.h"
#include "lot_linetype.h"
#include "lot_log.h"

#include <cstdlib>

// 게임 오브젝트 생성
void createGameObjects() {
    // 큐브 하나를 여러 오브젝트가 공유한다 (모델이 shared_ptr 인 이유).
    // 정점 데이터는 GPU 에 한 번만 올라가고, 오브젝트마다 다른 것은 transform 뿐이다.
    // 가운데는 OBJ 로 불러온 모델 자리로 비워둔다.
    const vec3 spawns[] = {
        vec3(-1.5f, 0.0f, kCubeHalf),
        vec3( 1.5f, 0.0f, kCubeHalf),
    };

    for (const auto& translation : spawns) {
        auto cube = LotGameObject::createGameObject();

        // 재질 없이 면마다 다른 정점 색 (네이티브 기본 씬의 큐브와 같다).
        // 체커 텍스처는 텍스처 길을 보여 주는 토러스에만 남긴다.
        cube.model = g_cubeModel;
        cube.transform.translation = translation;
        cube.transform.scale = vec3(0.6f);
        // 세 면이 다 보이게 위 축(Z) 둘레로 살짝 돌린다 (바닥에 얹힌 채로)
        cube.transform.rotation = quat::angleAxis(0.6f, vec3(0.0f, 0.0f, 1.0f));

        const auto id = cube.getId();
        doc().objects.emplace(id, std::move(cube));
    }

    // 광원. 전에는 프레임마다 십자를 그려 넣어서 고를 수도 지울 수도 없었고
    // '전체 지우기' 뒤에도 남아 있었다. 평범한 오브젝트로 둔다.
    //
    // 같이 그리던 궤도선·나선·작업영역 상자는 없앴다. 폴리라인 파이프라인이
    // 도는지 눈으로 보려던 시연용인데, 이제 스케치마다 그 길을 지나므로
    // 도면에 남을 이유가 없다 (지울 수도 없는 선이 도면에 섞이면 더 나쁘다).
    {
        auto light = LotGameObject::createGameObject();
        light.transform.translation = vec3(kLightSwingX, kLightBaseY, kLightHeight);
        light.color = vec3(1.0f, 0.95f, 0.6f);
        light.light.valid = true;
        light.light.color = vec3(1.0f, 1.0f, 1.0f);
        light.light.intensity = 4.0f;
        light.light.orbit = true;   // 옮기면 그 자리에 선다
        doc().objects.emplace(light.getId(), std::move(light));
    }
    LOT_LOG("Game objects created: " << doc().objects.size());
}

// 받아온 OBJ 모델을 장면 가운데에 놓는다.
//
// fetch 콜백은 렌더 루프 바깥(브라우저 이벤트 루프)에서 불리므로,
// 프레임 도중에 벡터가 바뀔 걱정은 없다.
void placeObjModel() {
    auto object = LotGameObject::createGameObject();
    object.model = g_objModel;
    object.material = g_checkerMaterial;
    object.transform.translation = vec3(0.0f, 0.0f, kObjHeight);
    // OBJ 는 Y-up 관례라 Z-up 세계에서는 X 둘레 +90도 돌려 세운다. 토러스는 그러면
    // 구멍이 위를 보고 눕는다 - 살짝 기울여 구멍이 보이게.
    object.transform.rotation = normalize(quat::angleAxis(0.3f, vec3(0.0f, 0.0f, 1.0f))
                                          * quat::angleAxis(1.2f, vec3(1.0f, 0.0f, 0.0f)));
    object.transform.scale = vec3(g_objModel->fitScale(kObjTargetSize));
    doc().objObjectId = object.getId();
    doc().objects.emplace(doc().objObjectId, std::move(object));

    doc().objPlaced = true;
    LOT_LOG("OBJ model placed at scene center");
}

// 사용자가 고른 OBJ 파일이 도착했을 때 JS 가 부른다.
//
// 선종류 목록을 패널에 한 번. 표준 8종 + Continuous.
void pushLinetypes() {
    std::string j = "[";
    bool first = true;
    for (const lot_linetype::Definition& d : lot_linetype::standard()) {
        if (!first) j += ",";
        first = false;
        j += "{\"id\":" + std::to_string(d.id)
           + ",\"name\":" + lot_ui::quote(d.name)
           + ",\"sample\":" + lot_ui::quote(d.sample) + "}";
    }
    j += "]";
    js_uiSetLinetypes(j.c_str());
}

// 단색 해치 채움 메시 (네이티브 buildHatchModel 의 solid 쪽). 평면 좌표를 로컬 3D 로.
std::shared_ptr<LotModel> buildHatchFill(const lot_hatch::HatchData& h) {
    const lot_hatch::Fill fill = lot_hatch::triangulate(h);
    if (fill.indices.size() < 3 || !g_renderer) return nullptr;
    const vec3 n = normalize(cross(h.right, h.up));
    LotModel::Builder b;
    b.vertices.reserve(fill.points.size());
    for (const auto& p : fill.points) {
        const vec3 q = h.origin + h.right * p.x + h.up * p.y;
        b.vertices.push_back(Vertex::make(q.x, q.y, q.z, 1.0f, 1.0f, 1.0f, n.x, n.y, n.z, 0.0f, 0.0f));
    }
    b.indices = fill.indices;
    return std::make_shared<LotModel>(g_renderer->getDevice(), b);
}

// 문자는 문자열마다 비트맵을 구워 텍스처로 올린다. 큰 도면은 글자가 수천~수만 개라
// 다 구우면 멈추고 GPU 메모리도 모자란다. 화면 밖이거나 너무 작아 읽을 수 없는 것은
// 건너뛴다 - 줌인해서 보일 때 그때 굽는다 (한 번 구운 것은 캐시에 남는다).
bool textWorthDrawing(const LotGameObject& obj, float width, float height) {
    constexpr float kMinPx = 2.0f;   // 이보다 작으면 읽을 수 없다
    const vec3& at = obj.transform.translation;
    const float wpp = doc().camera.worldPerPixel(at, height);
    if (!(wpp > 0.0f)) return true;
    const float px = obj.text.height / wpp;
    if (px < kMinPx) return false;
    float sx = 0.0f, sy = 0.0f;
    if (!doc().camera.projectToScreen(at, width, height, sx, sy)) return false;
    // 글자 폭은 모르니 넉넉히 (UTF-8 바이트 수 x 높이 - 한글은 3 바이트라 더 넉넉하다)
    const float reach = px * (2.0f + static_cast<float>(obj.text.content.size()));
    return sx > -reach && sx < width + reach && sy > -reach && sy < height + reach;
}

// 층 표를 보는 콜백들. 렌더/피킹 시스템은 층을 모르고 이 함수만 부른다.
bool layerVisible(const LotGameObject& obj) { return !obj.hidden && doc().layers.isVisible(obj.layer); }
bool layerSelectable(const LotGameObject& obj) {
    if (obj.hidden) return false;                              // 노드 트리에서 숨긴 것
    if (!g_display.dims && obj.isDimension()) return false;  // 안 보이는 치수는 안 잡힌다
    return doc().layers.isSelectable(obj.layer);
}

// 오브젝트가 쓸 선종류. '층 따름'이면 층의 것.
uint32_t displayLinetype(const LotGameObject& obj) {
    if (obj.linetype != lot_linetype::kByLayer) return obj.linetype;
    const LotLayers::Layer* l = doc().layers.find(obj.layer);
    return l ? l->linetype : lot_linetype::kContinuous;
}

// 오브젝트가 화면에 낼 색. '층 따름'이면 층 색.
vec3 displayColor(const LotGameObject& obj) {
    if (!obj.colorByLayer) return obj.color;
    const LotLayers::Layer* l = doc().layers.find(obj.layer);
    return l ? l->color : obj.color;
}

// 스케치 오브젝트 + 치수 + 문자 + 광원 + 메시 모서리를 이번 프레임의 선 · 폴리선 · 문자 시스템에.
// (선 시스템은 부르는 쪽이 비워 두고 격자 · 선택 상자를 먼저 넣는다.)
void drawSceneObjects(float width, float height) {
    // 스케치 오브젝트 + 치수 + 그리는 중인 프리뷰
    g_polylineSystem->clear();
    g_textSystem->clear();
    for (auto& entry : doc().objects) {
        LotGameObject& obj = entry.second;
        if (!layerVisible(obj)) continue;  // 꺼진 층
        const vec3 color = displayColor(obj);
        if (obj.isHatch()) {
            // 해치 (네이티브 hatch_model): 무늬는 선분, 단색은 채움 메시 (처음 그릴 때 만든다).
            // 바깥 경계 점(points)은 피킹 · 범위용이라 따로 그리지 않는다.
            if (obj.hatch->solid) {
                if (!obj.model) obj.model = buildHatchFill(*obj.hatch);
            } else if (obj.hatchSegments) {
                const mat4 m = obj.transform.mat4Transform();
                const std::vector<vec3>& sg = *obj.hatchSegments;
                for (size_t i = 0; i + 1 < sg.size(); i += 2) {
                    g_lineSystem->addLine(transformPoint(m, sg[i]), transformPoint(m, sg[i + 1]), color);
                }
            }
        } else if (obj.isSketch()) {
            const uint32_t lt = displayLinetype(obj);
            if (lt == lot_linetype::kContinuous) {
                g_polylineSystem->addPolyline(obj.worldPoints(), color, obj.closed);
            } else {
                // 무늬가 있으면 선분으로 잘라 낸다. 화면에서 한 주기가 4px 보다
                // 짧아지면 (줌 아웃) 실선으로 떨어뜨려 뭉개지지 않게.
                const float minDash = doc().camera.worldPerPixel(obj.transform.translation,
                                                             height) * 4.0f;
                lot_linetype::emit(*g_lineSystem, obj.worldPoints(), obj.closed, color, lt,
                                   doc().linetypeScale, minDash);
            }
        } else if (obj.isDimension()) {
            if (g_display.dims) lot_dim::draw(obj, doc().camera, *g_lineSystem, *g_textSystem, color);
        } else if (obj.isText()) {
            if (textWorthDrawing(obj, width, height)) {
                lot_text::draw(obj, *g_textSystem, color);
            }
        } else if (obj.isLight()) {
            g_lineSystem->addCross(obj.transform.translation, obj.light.markerSize, color);
        } else if (obj.model && g_display.style == DisplaySettings::WireMesh) {
            // 와이어프레임: 면 대신 삼각형 변을 전부 (색은 오브젝트 색)
            const mat4 m = obj.transform.mat4Transform();
            const std::vector<vec3>& pos = obj.model->getPositions();
            const std::vector<uint32_t>& idx = obj.model->getIndices();
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                const vec3 a = transformPoint(m, pos[idx[i]]);
                const vec3 b = transformPoint(m, pos[idx[i + 1]]);
                const vec3 c = transformPoint(m, pos[idx[i + 2]]);
                g_lineSystem->addLine(a, b, color);
                g_lineSystem->addLine(b, c, color);
                g_lineSystem->addLine(c, a, color);
            }
        } else if (obj.model && g_display.featureEdges() && !obj.model->getFeatureEdges().empty()) {
            // 메시 모서리 (네이티브의 ShadedEdge). 면 위에 딱 붙은 선은 뎁스가 면과 같아
            // 깜빡이므로 카메라 쪽으로 조금 당긴다 - 거리의 0.2%, 화면에서는 안 보이는 만큼.
            const mat4 m = obj.transform.mat4Transform();
            const vec3 eye = doc().camera.getPosition();
            const std::vector<vec3>& e = obj.model->getFeatureEdges();
            for (size_t i = 0; i + 1 < e.size(); i += 2) {
                vec3 a = transformPoint(m, e[i]);
                vec3 b = transformPoint(m, e[i + 1]);
                a = a + (eye - a) * 0.002f;
                b = b + (eye - b) * 0.002f;
                // 면이 칠해진 셰이딩+엣지는 검은 테두리, 면이 없거나 배경색이면 오브젝트 색
                g_lineSystem->addLine(a, b, g_display.style == DisplaySettings::ShadedEdges ? kMeshEdgeColor : color);
            }
        }
    }
}

#pragma once

#include "lot_hatch.h"
#include "lot_math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class LotModel;
class LotMaterial;
namespace lot { class LotBRepShape; }

// 피처 기록 (네이티브 FeatureLink) - 솔리드가 어떤 스케치에서 나왔는지. 스케치가 바뀌면
// 솔리드를 다시 만들고(재생성), .lot 에 같이 저장된다. linkInv 는 연결할 때의 솔리드 변환의
// 역 (월드 -> 솔리드 로컬). last* 는 마지막으로 만든 입력 - 스케치가 그대로면 다시 만들지 않는다.
struct FeatureLink {
    static constexpr unsigned kNone = ~0u;
    unsigned sketch = kNone;
    mat4 linkInv = mat4::identity();
    std::vector<unsigned> cutSketches;
    std::vector<mat4> cutLinkInv;
    std::vector<std::vector<vec3>> lastCutProfiles;
    std::vector<unsigned> bossSketches;
    std::vector<mat4> bossLinkInv;
    std::vector<std::vector<vec3>> lastBossProfiles;
    std::vector<vec3> lastProfile;
    std::string error;   // 비어 있지 않으면 마지막 재생성이 실패 (모양은 그 전 것)
};

// 게임 오브젝트 클래스
class LotGameObject {
public:
    using id_t = unsigned int;

    // 장면의 오브젝트 목록. vector 가 아니라 맵인 이유:
    // 피킹/선택은 "무엇을 골랐는지"를 들고 있어야 하는데, vector 인덱스는
    // 중간에 하나가 지워지면 밀려서 엉뚱한 것을 가리킨다. id 는 안 바뀐다.
    using Map = std::unordered_map<id_t, LotGameObject>;

    // 없는 id 면 nullptr. 선택된 오브젝트가 지워졌을 때를 위한 안전한 조회.
    static LotGameObject* find(Map& map, id_t id) {
        auto it = map.find(id);
        return (it == map.end()) ? nullptr : &it->second;
    }
    static const LotGameObject* find(const Map& map, id_t id) {
        auto it = map.find(id);
        return (it == map.end()) ? nullptr : &it->second;
    }

    // 팩토리 메서드로 생성. id 는 0 부터 한 번씩만 나간다.
    static LotGameObject createGameObject() {
        return LotGameObject{nextId()++};
    }

    // 예전에 나갔던 id 로 다시 만든다 - 실행 취소가 지워진 오브젝트를 되살릴 때.
    // 카운터는 건드리지 않는다: 한 번 나간 id 는 다시 나가지 않으므로 충돌이 없다.
    // 다음에 나갈 id (나가지는 않는다). 파일을 읽을 때 '이 항목이 만든 객체' 를 알아내는 데 쓴다.
    static id_t peekNextId() { return nextId(); }

    static LotGameObject createWithId(id_t id) {
        if (id >= nextId()) nextId() = id + 1;  // 혹시 모를 앞선 id 도 안전하게
        return LotGameObject{id};
    }

    // "선택 없음" 을 나타내는 값. 실제 id 로는 절대 나오지 않는다.
    static constexpr id_t kInvalidId = ~static_cast<id_t>(0);

    // 복사 금지, 이동 허용
    LotGameObject(const LotGameObject&) = delete;
    LotGameObject& operator=(const LotGameObject&) = delete;
    LotGameObject(LotGameObject&&) = default;
    LotGameObject& operator=(LotGameObject&&) = default;

    id_t getId() const { return id_; }

    // 공개 멤버 (간단한 구조를 위해)
    vec3 color{1.0f, 1.0f, 1.0f};

    // 도면층 (LotLayers). 0 = 기본층. 층을 끄면 안 보이고, 잠그면 골라지지 않는다.
    uint32_t layer = 0;
    // 노드 트리에서 끈 것 (층과 별개로 이 객체만 숨긴다). 숨기면 그려지지도 잡히지도 않는다.
    bool hidden = false;
    // 선종류 (lot_linetype). 기본은 '층 따름' - 층의 선종류를 쓴다.
    uint32_t linetype = 0xFFFFFFFFu;  // kByLayer

    // 색이 '층 따름'(ByLayer)인가. 참이면 층 색으로 그린다 - 층 색을 바꾸면 따라 바뀐다.
    // 스케치/치수/문자처럼 색이 곧 의미인 것들은 자기 색을 들고 시작한다.
    bool colorByLayer = false;
    TransformComponent transform{};

    // 모델.
    // 여러 오브젝트가 같은 메시(예: 큐브 하나)를 공유하므로 shared_ptr 이다.
    // 정점 개수는 모델이 알고 있으니 여기서 다시 세지 않는다.
    std::shared_ptr<LotModel> model;

    // 재질 (텍스처). 없으면 렌더 시스템의 기본 재질(흰색)을 쓴다.
    std::shared_ptr<LotMaterial> material;

    // 스케치 오브젝트 (선, 사각형, 폴리라인). model 대신 이 점들을 가진다.
    //
    // 점은 로컬 좌표다 - 만들 때 무게중심을 translation 으로 잡고 점을 상대
    // 좌표로 저장하므로, 기즈모가 붙는 기준점이 도형 가운데가 되고 이동/회전/
    // 축척이 메시 오브젝트와 똑같이 동작한다. 그리기는 폴리라인 시스템이,
    // 피킹/스냅/박스 선택은 세그먼트 단위로 한다.
    // Vulkan 쪽은 kind=Line + LINE_LIST 모델이지만 여기서는 점 목록이 더 곧다.
    std::vector<vec3> points;
    bool closed = false;

    // 원/호의 CAD 정의. 점 목록은 이걸 잘게 쪼갠 '표시용' 사본이고, 저장할 때는
    // 이 파라미터를 쓴다 - 그래야 닫았다 열어도 원은 원이다 (Vulkan 쪽 CircleData/ArcData).
    // 점 = center + radius * (cos(t) * right + sin(t) * up), t 는 start..end (호) 또는 한 바퀴 (원).
    // 좌표는 points 와 같이 로컬이다.
    struct Curve {
        enum class Kind { None, Circle, Arc };
        Kind kind = Kind::None;
        vec3 center{0.0f, 0.0f, 0.0f};
        float radius = 1.0f;
        vec3 right{1.0f, 0.0f, 0.0f};
        vec3 up{0.0f, 1.0f, 0.0f};  // 네이티브 기본값과 같다 (XY 평면)
        float start = 0.0f;  // 라디안
        float end = 0.0f;
    };
    Curve curve;

    // 치수 (정렬 치수). 점은 로컬 - transform 으로 월드에 놓인다 (스케치와 같은 규칙,
    // 만들 때 두 측정점의 중점을 translation 으로 잡는다). Vulkan 쪽 DimensionData 와 같은 정의.
    struct Dim {
        bool valid = false;
        vec3 p1{0.0f, 0.0f, 0.0f};        // 측정점 1
        vec3 p2{0.0f, 0.0f, 0.0f};        // 측정점 2
        vec3 dimLine{0.0f, 0.0f, 0.0f};   // 치수선이 지나는 점 (오프셋 방향/거리)
        vec3 normal{0.0f, 0.0f, 1.0f};    // 작업평면 법선
        float textHeight = 0.22f;
        float arrowSize = 0.12f;
        int precision = 2;
        bool arrowsOutside = false;
        std::string prefix;               // 값 앞 글자 ("Ø" 지름 - 피처 치수)
    };
    Dim dim;

    // 문자 (Vulkan 쪽 TextData). 기준점은 transform.translation (로컬 origin 은 0),
    // right/up 은 글자가 놓이는 평면의 축 (로컬). 글리프는 TextRenderSystem 이 그린다.
    struct Text {
        bool valid = false;
        std::string content;
        float height = 0.25f;             // 글자 높이 (월드)
        vec3 right{1.0f, 0.0f, 0.0f};     // 진행 방향
        vec3 up{0.0f, 0.0f, 1.0f};        // 위 방향
        int hAlign = 0;                   // 0 왼쪽, 1 가운데, 2 오른쪽
        int vAlign = 0;                   // 0 기준선, 1 가운데, 2 위
    };
    Text text;

    // 색을 사용자가 지정했나. 메시는 보통 정점 색/텍스처로 그리므로, 흰색 그대로이고
    // 층 따름도 아니면 '지정 안 함' 으로 보아 셰이더가 정점 색을 쓰게 한다.
    bool hasOwnColor() const {
        return colorByLayer || color.x != 1.0f || color.y != 1.0f || color.z != 1.0f;
    }

    // 점 광원. 모델이 없어 메시로는 안 보이고 십자 기호로 선다 - 그래도 평범한
    // 오브젝트라 고르고 옮기고 지울 수 있다. 매 프레임 그려 넣던 때는 그게 안 됐다.
    struct Light {
        bool valid = false;
        vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float markerSize = 0.12f;   // 십자 반 길이 (월드)
        // 기본 씬의 시연용 공전. 사용자가 한 번 옮기면 꺼지고 그 자리에 선다.
        bool orbit = false;
    };
    Light light;

    // 해치 (네이티브 PrimitiveKind::Hatch). 있으면 points 는 바깥 경계(피킹 · 범위용)이고 화면에는
    // 무늬 선분(hatchSegments, 로컬 쌍) 또는 단색 채움(model, 처음 그릴 때 만든다)으로 나간다.
    std::shared_ptr<const lot_hatch::HatchData> hatch;
    std::shared_ptr<const std::vector<vec3>> hatchSegments;
    bool isHatch() const { return hatch != nullptr; }

    // 솔리드 (네이티브 LotBRepShape). 있으면 model 은 이것을 삼각형으로 나눈 캐시다 - 저장 · 편집은
    // 해석 형상으로 하고, 화면 메시는 거기서 다시 만든다. 둘 다 바꿀 때마다 새 것으로 갈아 끼운다
    // (공유되는 불변 객체라 히스토리 스냅샷이 싸다).
    std::shared_ptr<const lot::LotBRepShape> brep;
    std::shared_ptr<const FeatureLink> featureLink;
    bool isSolid() const { return brep != nullptr; }

    bool isSketch() const { return !points.empty(); }
    bool isLight() const { return light.valid; }
    bool isText() const { return text.valid; }
    bool hasCurve() const { return curve.kind != Curve::Kind::None; }
    bool isDimension() const { return dim.valid; }

    // 점들을 월드 좌표로.
    std::vector<vec3> worldPoints() const {
        std::vector<vec3> out;
        out.reserve(points.size());
        const mat4 m = transform.mat4Transform();
        for (const vec3& p : points) out.push_back(transformPoint(m, p));
        return out;
    }

private:
    explicit LotGameObject(id_t objId) : id_(objId) {}

    static id_t& nextId() {
        static id_t currentId = 0;
        return currentId;
    }

    id_t id_;
};

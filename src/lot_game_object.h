#pragma once

#include "lot_math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class LotModel;
class LotMaterial;

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

    bool isSketch() const { return !points.empty(); }
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

#pragma once

#include "lot_game_object.h"
#include "lot_math.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

class LotModel;
class LotMaterial;

// 실행 취소 / 다시 실행.
//
// 명령 객체마다 undo() 를 따로 짜는 대신, 편집 하나가 건드린 오브젝트들의
// '전 상태'와 '후 상태'를 통째로 기록한다. 되돌리기 = 전 상태를 다시 적용,
// 다시 실행 = 후 상태를 다시 적용. 이동/회전/축척/복제/삭제/스케치가 전부
// 같은 코드로 처리되고, 도구가 늘어도 여기는 안 바뀐다. 상태에 GPU 자원은
// 없다 (모델/재질은 shared_ptr 공유) 라 기록이 싸다.
//
// 생성과 삭제도 같은 틀이다: 전에 없고 후에 있으면 생성, 그 반대면 삭제.
// 되돌릴 때 같은 id 로 다시 만들어야 이후 기록들이 가리키는 id 가 맞는다
// (LotGameObject::createWithId).
class EditHistory {
public:
    using id_t = LotGameObject::id_t;

    // 오브젝트 하나의 스냅샷. LotGameObject 는 복사 금지라 필드를 따로 든다.
    struct Record {
        id_t id = LotGameObject::kInvalidId;
        TransformComponent transform;
        vec3 color;
        uint32_t layer = 0;
        uint32_t linetype = 0xFFFFFFFFu;
        bool colorByLayer = false;
        std::shared_ptr<LotModel> model;
        std::shared_ptr<LotMaterial> material;
        std::vector<vec3> points;
        bool closed = false;
        LotGameObject::Curve curve;
        LotGameObject::Dim dim;
        LotGameObject::Text text;
        LotGameObject::Light light;

        static Record capture(const LotGameObject& obj);
        void apply(LotGameObject& obj) const;
    };

    struct Edit {
        std::string label;             // 로그용 ("move", "delete", "sketch line" ...)
        std::vector<Record> before;    // 편집 전에 있던 것들의 상태
        std::vector<Record> after;     // 편집 후에 있는 것들의 상태
    };

    // 편집을 기록한다. 새 편집이 들어오면 redo 스택은 버린다 (분기는 지원하지 않는다).
    // before 와 after 가 완전히 같으면 (클릭만 하고 안 움직인 드래그) 기록하지 않는다.
    void record(Edit edit);

    // 자주 쓰는 모양들
    static Record snapshot(const LotGameObject::Map& objects, id_t id);
    static std::vector<Record> snapshot(const LotGameObject::Map& objects, const std::set<id_t>& ids);
    void recordCreated(const char* label, const LotGameObject::Map& objects, const std::set<id_t>& ids);
    void recordCreated(const char* label, const LotGameObject::Map& objects, id_t id);

    // 되돌리기 / 다시 실행. 영향을 받은 오브젝트 id 를 돌려준다 (선택 갱신용).
    // 할 게 없으면 빈 집합.
    std::set<id_t> undo(LotGameObject::Map& objects);
    std::set<id_t> redo(LotGameObject::Map& objects);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }
    void clear() { undo_.clear(); redo_.clear(); }

    size_t maxEntries = 100;

private:
    // 'from' 상태의 오브젝트들을 지우고 'to' 상태를 적용한다.
    static std::set<id_t> apply(LotGameObject::Map& objects, const std::vector<Record>& from,
                                const std::vector<Record>& to);

    std::vector<Edit> undo_;
    std::vector<Edit> redo_;
};

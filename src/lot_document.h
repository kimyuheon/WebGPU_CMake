#pragma once

#include "lot_camera.h"
#include "lot_edit_controller.h"
#include "lot_game_object.h"
#include "lot_layers.h"
#include "lot_math.h"

#include <string>

// 도면 하나. 탭 하나가 이것을 하나씩 들고, 탭을 바꾸면 통째로 갈아 끼운다.
//
// 무엇이 들어오나: 그 도면에만 속하는 것. 오브젝트 · 도면층 · 선택과 히스토리 ·
// 보고 있던 시점 · 씬 크기에서 나온 값들(그리드 간격, LTSCALE, 클립 평면).
// 무엇이 안 들어오나: 장치에 매인 것(모델 · 재질 · 렌더 시스템)과 지금 손에 든
// 도구(스케치/변환) - 앞의 것은 도면끼리 나눠 쓰고, 뒤의 것은 탭을 바꿀 때 접는다.
//
// 저장은 여전히 .lot 한 장이 도면 하나다. 탭은 그걸 여러 장 띄워 두는 것뿐이라
// 파일 형식은 건드리지 않는다.
struct LotDocument {
    std::string name = "도면1";
    std::string path;            // 열거나 저장한 파일 이름 (없으면 빈 칸)
    bool modified = false;       // 탭 이름 옆에 점을 찍을지

    LotGameObject::Map objects;
    LotLayers layers;
    EditController edit;         // 선택 + 기즈모 드래그 + 히스토리
    LotCamera camera;

    // 카메라의 위치/회전을 담는 오브젝트 (1인칭 이동이 이걸 움직인다). 모델이 없어
    // 그려지지는 않는다 - 카메라를 게임 오브젝트처럼 다루면 나중에 붙이기 쉽다.
    LotGameObject viewer = LotGameObject::createGameObject();

    // 씬 크기에서 따라오는 값들. zoomExtents 가 한꺼번에 잡는다.
    float gridSpacing = 0.5f;        // F9 스냅 간격
    float linetypeScale = 1.0f;      // AutoCAD 의 LTSCALE
    float nearZ = 0.1f;
    float farZ = 100.0f;

    // 투영 (P 키). 직교의 halfHeight 는 화면 세로 절반에 담기는 월드 길이 = 줌.
    bool orthographic = false;
    float orthoHalfHeight = 1.6f;
    float orthoMinHalfHeight = 0.2f;
    float orthoMaxHalfHeight = 20.0f;

    // 가운데에 끼워 넣는 OBJ 모델 자리. 인덱스가 아니라 id 라서 다른 것이 지워져도
    // 어긋나지 않는다.
    bool objPlaced = false;
    LotGameObject::id_t objObjectId = LotGameObject::kInvalidId;
};

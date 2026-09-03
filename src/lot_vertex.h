#pragma once

// 정점 레이아웃의 유일한 정의처.
//
// 예전에는 같은 레이아웃이 세 군데에 흩어져 있었다:
//   - main.cpp 의 72 바이트 하드코딩
//   - webgpu_bindings.js 의 arrayStride: 24
//   - triangle.wgsl 의 VertexInput
// 이제 C++ 쪽은 이 구조체 하나만 보면 되고, 파이프라인은 offsetof/sizeof 로
// 레이아웃을 만든다. WGSL 의 @location 번호만 아래 주석과 맞춰주면 된다.
struct Vertex {
    float position[3];  // @location(0)
    float color[3];     // @location(1)
};

static_assert(sizeof(Vertex) == 24, "Vertex layout must stay tightly packed");

#pragma once

// 정점 레이아웃의 유일한 정의처.
//
// 예전에는 같은 레이아웃이 세 군데에 흩어져 있었다:
//   - main.cpp 의 72 바이트 하드코딩
//   - webgpu_bindings.js 의 arrayStride: 24
//   - triangle.wgsl 의 VertexInput
// 이제 C++ 쪽은 이 구조체 하나만 보면 되고, 파이프라인은 offsetof/sizeof 로
// 레이아웃을 만든다. WGSL 의 @location 번호만 아래 주석과 맞춰주면 된다.
//
// color 에 알파가 있는 이유: 기즈모 평면 핸들, 고스트 미리보기처럼 CPU 에서
// 매 프레임 만드는 '도구' 지오메트리는 정점 단위 알파가 자연스럽다.
// 메시의 재질 알파는 나중에 재질 유니폼으로 따로 간다.
struct Vertex {
    float position[3];  // @location(0)
    float color[4];     // @location(1)  rgba
    float normal[3];    // @location(2)
    float uv[2];        // @location(3)  텍스처 좌표

    // 불투명한 정점. 대부분의 호출부가 이걸 쓴다.
    static Vertex make(float px, float py, float pz,
                       float r, float g, float b,
                       float nx, float ny, float nz,
                       float u = 0.0f, float v = 0.0f) {
        return Vertex{{px, py, pz}, {r, g, b, 1.0f}, {nx, ny, nz}, {u, v}};
    }
};

static_assert(sizeof(Vertex) == 48, "Vertex layout must stay tightly packed");

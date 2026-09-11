// 바닥 격자. 조명 없이 정점 색을 그대로 낸다.
//
// GlobalUniforms 는 triangle.wgsl 과 같은 레이아웃이어야 한다 - 같은 버퍼를
// 같은 바인드 그룹으로 공유하기 때문이다. 조명 필드는 여기서 안 쓰지만
// 구조체는 그대로 맞춰둔다.
struct GlobalUniforms {
    projection: mat4x4<f32>,
    view: mat4x4<f32>,
    ambientLightColor: vec4<f32>,
    lightPosition: vec4<f32>,
    lightColor: vec4<f32>,
};

@group(0) @binding(0) var<uniform> global: GlobalUniforms;

// 정점 버퍼 레이아웃은 메시와 같다 (position / color / normal).
// 셰이더는 쓰는 것만 선언하면 된다 - normal 은 안 받는다.
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    // 격자는 월드에 고정이라 모델 행렬이 없다
    output.position = global.projection * global.view * vec4<f32>(input.position, 1.0);
    output.color = input.color;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}

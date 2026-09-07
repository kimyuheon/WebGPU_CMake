// Uniform 데이터 (오브젝트마다 dynamic offset 으로 다른 슬롯을 본다)
struct Uniforms {
    // projection * view * model 이 이미 접혀 있는 행렬.
    transform: mat4x4<f32>,
    // 노멀 전용 행렬 = transpose(inverse(mat3(model))).
    // 상단 3x3 만 쓰지만, mat3x3 은 열마다 16바이트로 패딩되어 C++ 구조체와
    // 어긋나기 쉬우므로 mat4x4 로 받는다.
    normalMatrix: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// Vertex Input (버퍼에서 받음)
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) normal: vec3<f32>,
};

// Vertex Output
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // 스케일 -> 회전 -> 이동 -> 카메라 -> 투영이 행렬 하나에 다 들어 있다.
    output.position = uniforms.transform * vec4<f32>(input.position, 1.0);

    // 방향 광원. 위치가 아니라 방향만 있으므로 무한히 먼 광원(태양)에 해당한다.
    // +Y 가 아래인 좌표계라 y 가 음수인 쪽이 '위에서 비추는' 방향이다.
    let directionToLight = normalize(vec3<f32>(1.0, -3.0, -1.0));
    let ambient = 0.05;

    // w = 0 을 곱해 이동 성분을 죽인다 (노멀은 위치가 아니라 방향이므로).
    let normalWorld = normalize((uniforms.normalMatrix * vec4<f32>(input.normal, 0.0)).xyz);

    // 램버트 확산광: 면이 광원을 정면으로 볼수록 밝다.
    // 등지는 면은 dot 이 음수가 되므로 0 으로 잘라낸다.
    let diffuse = max(dot(normalWorld, directionToLight), 0.0);

    // 한 면의 네 꼭짓점이 같은 법선을 쓰므로 면 전체가 균일하게 칠해진다.
    output.color = input.color * (ambient + diffuse);
    return output;
}

// Fragment Shader
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}

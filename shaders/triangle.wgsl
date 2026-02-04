// Uniform 데이터 (매 프레임 업데이트)
struct Uniforms {
    offset: vec2<f32>,  // x, y 오프셋
    rotation: f32,      // 회전 각도 (라디안)
    scale: f32,         // 크기
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// Vertex Input (버퍼에서 받음)
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
};

// Vertex Output
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // 정삼각형 중심점 (centroid)
    // x = (0.0 + (-0.5) + 0.5) / 3 = 0.0
    // y = (0.577 + (-0.289) + (-0.289)) / 3 = -0.001 / 3 ≈ 0.0
    let center = vec2<f32>(0.0, 0.0);

    // 회전 행렬 계산
    let cosTheta = cos(uniforms.rotation);
    let sinTheta = sin(uniforms.rotation);

    // 1. 중심점으로 이동 (pivot to origin)
    var pos = input.position.xy - center;

    // 2. 회전 적용 (rotate around origin)
    var rotatedPos: vec2<f32>;
    rotatedPos.x = pos.x * cosTheta - pos.y * sinTheta;
    rotatedPos.y = pos.x * sinTheta + pos.y * cosTheta;

    // 3. 다시 원래 위치로 + 오프셋 (translate back + offset)
    rotatedPos = rotatedPos + center /** uniforms.scale*/ + uniforms.offset;

    output.position = vec4<f32>(rotatedPos, input.position.z, 1.0);
    output.color = input.color;
    return output;
}

// Fragment Shader
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}

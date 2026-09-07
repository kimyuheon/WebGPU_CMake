// Uniform 데이터 (오브젝트마다 dynamic offset 으로 다른 슬롯을 본다)
struct Uniforms {
    // translate * Ry * Rx * Rz * scale 이 이미 접혀 있는 모델 행렬.
    // C++ 쪽 TransformComponent::mat4Transform() 이 만든다.
    transform: mat4x4<f32>,
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

    // 스케일 -> 회전 -> 이동이 행렬 하나에 다 들어 있으므로 곱셈 한 번이면 된다.
    output.position = uniforms.transform * vec4<f32>(input.position, 1.0);
    output.color = input.color;
    return output;
}

// Fragment Shader
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}

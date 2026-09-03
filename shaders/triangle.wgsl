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

    let cosTheta = cos(uniforms.rotation);
    let sinTheta = sin(uniforms.rotation);

    // 1. 스케일 (원점 기준)
    let scaled = input.position.xy * uniforms.scale;

    // 2. 회전 (원점 기준)
    var rotated: vec2<f32>;
    rotated.x = scaled.x * cosTheta - scaled.y * sinTheta;
    rotated.y = scaled.x * sinTheta + scaled.y * cosTheta;

    // 3. 이동
    let worldPos = rotated + uniforms.offset;

    output.position = vec4<f32>(worldPos, input.position.z, 1.0);
    output.color = input.color;
    return output;
}

// Fragment Shader
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}

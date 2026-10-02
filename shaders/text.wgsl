// 텍스처에 구운 글자를 사각형에 얹는 셰이더 (치수 값, 문자).
//
// 글자 비트맵은 브라우저 캔버스가 그린다 (src/js/lot_text.js). 흰 글자에 투명 배경이라
// 텍셀의 알파가 글자 덮임이고, 색은 정점 색을 곱해 낸다. 조명은 없다 - 도면 글자다.
// GlobalUniforms 는 unlit.wgsl / triangle.wgsl 과 같은 레이아웃이어야 한다 (같은 버퍼).
struct GlobalUniforms {
    projection: mat4x4<f32>,
    view: mat4x4<f32>,
    ambientLightColor: vec4<f32>,
    lightPosition: vec4<f32>,
    lightColor: vec4<f32>,
    keyLight: vec4<f32>,           // 방향 (빛이 나아가는 쪽) + 세기
    fillLight: vec4<f32>,          // 방향 + 세기
    cameraPosition: vec4<f32>,     // xyz (하이라이트 계산용)
};

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var glyphTexture: texture_2d<f32>;
@group(1) @binding(1) var glyphSampler: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
    @location(3) uv: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = global.projection * global.view * vec4<f32>(input.position, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let texel = textureSample(glyphTexture, glyphSampler, input.uv);
    // 배경(알파 0)은 완전히 버린다 - 뎁스에도 남지 않게. 글자 가장자리는 알파로 부드럽게.
    if (texel.a < 0.02) {
        discard;
    }
    return vec4<f32>(input.color.rgb * texel.rgb, input.color.a * texel.a);
}

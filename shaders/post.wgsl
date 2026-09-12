// 후처리. 오프스크린에 그린 장면을 화면으로 옮기면서 효과를 얹는다.
//
// 정점 버퍼가 없다. vertex_index 0/1/2 로 화면을 덮는 삼각형 하나를 만든다
// (사각형 둘보다 삼각형 하나가 대각선 이음매가 없어 낫다).

struct PostUniforms {
    mode: u32,          // 0 = 그대로, 1 = 외곽선
    outlineWidth: u32,  // 픽셀
    _pad0: u32,
    _pad1: u32,
};

@group(0) @binding(0) var sceneColor: texture_2d<f32>;
@group(0) @binding(1) var sceneSampler: sampler;
@group(0) @binding(2) var sceneDepth: texture_depth_2d;
@group(0) @binding(3) var<uniform> post: PostUniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOutput {
    // (-1,-1) (3,-1) (-1,3) - 화면 밖으로 삐져나가는 큰 삼각형.
    // 클리핑되고 남는 부분이 정확히 화면 사각형이다.
    let x = f32(i32(index & 1u) * 4 - 1);
    let y = f32(i32(index >> 1u) * 4 - 1);
    var output: VertexOutput;
    output.position = vec4<f32>(x, y, 0.0, 1.0);
    // 클립 y 는 위가 +1, 텍스처 v 는 위가 0 이라 뒤집는다
    output.uv = vec2<f32>((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
    return output;
}

// 뎁스를 선형 거리처럼 비교하기 위한 값. 원근 뎁스는 비선형이라 그대로 빼면
// 멀리서는 거의 0 이 된다. 정확한 선형화에는 near/far 가 필요하지만, 외곽선
// 판정에는 '이웃과 얼마나 다른가'만 있으면 되므로 이웃 대비 상대 차이로 본다.
fn depthAt(coord: vec2<i32>, size: vec2<i32>) -> f32 {
    let c = clamp(coord, vec2<i32>(0), size - vec2<i32>(1));
    return textureLoad(sceneDepth, c, 0);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let color = textureSample(sceneColor, sceneSampler, input.uv);
    if (post.mode == 0u) {
        return color;
    }

    // 외곽선: 뎁스가 이웃과 확 달라지는 픽셀 = 물체의 실루엣.
    // 4 방향 이웃과의 차이 중 최대값이 문턱을 넘으면 어둡게 칠한다.
    let size = vec2<i32>(textureDimensions(sceneDepth));
    let p = vec2<i32>(input.position.xy);
    let w = i32(post.outlineWidth);
    let d = depthAt(p, size);
    let dl = depthAt(p - vec2<i32>(w, 0), size);
    let dr = depthAt(p + vec2<i32>(w, 0), size);
    let du = depthAt(p - vec2<i32>(0, w), size);
    let dd = depthAt(p + vec2<i32>(0, w), size);

    // 배경(뎁스 1.0)과 물체 사이는 항상 경계. 물체끼리는 상대 차이로.
    let maxDiff = max(max(abs(d - dl), abs(d - dr)), max(abs(d - du), abs(d - dd)));
    let threshold = 0.002 + d * 0.01;  // 멀수록 뎁스 정밀도가 떨어지므로 문턱을 올린다
    let edge = select(0.0, 1.0, maxDiff > threshold);

    let outlineColor = vec3<f32>(0.05, 0.05, 0.05);
    return vec4<f32>(mix(color.rgb, outlineColor, edge), 1.0);
}

// 프레임당 한 번 갱신되는 값들 (카메라 + 조명).
//
// vec3 는 WGSL 에서 16바이트로 정렬되므로 vec4 로 받고 w 를 세기로 쓴다.
// 이러면 C++ 구조체와 오프셋이 어긋날 일이 없다.
struct GlobalUniforms {
    projection: mat4x4<f32>,
    view: mat4x4<f32>,
    ambientLightColor: vec4<f32>,  // rgb + 세기
    lightPosition: vec4<f32>,      // xyz (w 는 안 씀)
    lightColor: vec4<f32>,         // rgb + 세기
};

// 오브젝트마다 dynamic offset 으로 다른 슬롯을 본다.
struct ObjectUniforms {
    // 월드 변환만. 카메라는 위쪽 group(0) 이 들고 있다.
    modelMatrix: mat4x4<f32>,
    // 노멀 전용 행렬 = transpose(inverse(mat3(model))).
    // 상단 3x3 만 쓰지만, mat3x3 은 열마다 16바이트로 패딩되어 C++ 구조체와
    // 어긋나기 쉬우므로 mat4x4 로 받는다.
    normalMatrix: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;

// Vertex Input (버퍼에서 받음)
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
    @location(2) normal: vec3<f32>,
};

// Vertex Output.
//
// 조명 계산이 프래그먼트 셰이더로 내려갔으므로 월드 좌표와 노멀을 넘긴다.
// 정점 단위로 계산하던 예전 방식은 광원이 면 한가운데 있을 때 티가 났다
// (꼭짓점 셋만 밝기를 알고 사이는 보간이라 빛이 뭉개진다).
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) positionWorld: vec3<f32>,
    @location(2) normalWorld: vec3<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    let positionWorld = object.modelMatrix * vec4<f32>(input.position, 1.0);
    output.position = global.projection * global.view * positionWorld;
    output.positionWorld = positionWorld.xyz;

    // w = 0 을 곱해 이동 성분을 죽인다 (노멀은 위치가 아니라 방향이므로).
    output.normalWorld = normalize((object.normalMatrix * vec4<f32>(input.normal, 0.0)).xyz);
    output.color = input.color.rgb;  // 메시 알파는 재질 쪽에서 다룬다
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // 방향 광원과 달리 조각마다 광원까지의 방향이 다르다.
    let toLight = global.lightPosition.xyz - input.positionWorld;

    // 거리 제곱에 반비례하는 감쇠. dot(v, v) 가 곧 거리의 제곱이라
    // sqrt 를 부르지 않아도 된다.
    let attenuation = 1.0 / dot(toLight, toLight);

    let lightColor = global.lightColor.rgb * global.lightColor.a * attenuation;
    let ambientLight = global.ambientLightColor.rgb * global.ambientLightColor.a;

    // 보간을 거치면 길이가 1 이 아니게 되므로 여기서 다시 정규화한다.
    let normal = normalize(input.normalWorld);

    // 램버트 확산광: 면이 광원을 정면으로 볼수록 밝다.
    // 등지는 면은 dot 이 음수가 되므로 0 으로 잘라낸다.
    let diffuse = lightColor * max(dot(normal, normalize(toLight)), 0.0);

    return vec4<f32>((diffuse + ambientLight) * input.color, 1.0);
}

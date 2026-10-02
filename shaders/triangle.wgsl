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
    // CAD 기본 조명 (네이티브 render_coordinator 와 같은 값). 점 광원이 없어도
    // 면이 구별되게 하는 평행광 둘 - 앞에서 비추는 주광, 반대쪽에서 약한 보조광.
    keyLight: vec4<f32>,           // 방향 (빛이 나아가는 쪽) + 세기
    fillLight: vec4<f32>,          // 방향 + 세기
    cameraPosition: vec4<f32>,     // xyz (하이라이트 계산용)
};

// 오브젝트마다 dynamic offset 으로 다른 슬롯을 본다.
struct ObjectUniforms {
    // 월드 변환만. 카메라는 위쪽 group(0) 이 들고 있다.
    modelMatrix: mat4x4<f32>,
    // 노멀 전용 행렬 = transpose(inverse(mat3(model))).
    // 상단 3x3 만 쓰지만, mat3x3 은 열마다 16바이트로 패딩되어 C++ 구조체와
    // 어긋나기 쉬우므로 mat4x4 로 받는다.
    normalMatrix: mat4x4<f32>,
    // 오브젝트 색 (rgb) + 섞는 세기 (w). w = 0 이면 정점 색 그대로, 1 이면 오브젝트 색만.
    // 메시는 보통 정점 색/텍스처를 쓰지만, 사용자가 색을 지정하면 그것이 이긴다.
    // w = 2 는 '숨은선 제거' 스타일: 조명 없이 rgb 한 색으로 칠한다 (배경색 = 뒤 선을 가린다).
    objectColor: vec4<f32>,
};

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;

// 재질. 텍스처가 없는 오브젝트에는 1x1 흰색이 묶여서 곱해도 색이 안 변한다.
@group(2) @binding(0) var materialTexture: texture_2d<f32>;
@group(2) @binding(1) var materialSampler: sampler;

// Vertex Input (버퍼에서 받음)
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) uv: vec2<f32>,
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
    @location(3) uv: vec2<f32>,
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
    output.uv = input.uv;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    if (object.objectColor.a > 1.5) {
        return vec4<f32>(object.objectColor.rgb, 1.0);
    }
    let ambientLight = global.ambientLightColor.rgb * global.ambientLightColor.a;

    // 보간을 거치면 길이가 1 이 아니게 되므로 여기서 다시 정규화한다.
    let normal = normalize(input.normalWorld);

    // 평행광 둘 (감쇠 없음). 방향은 빛이 나아가는 쪽이라 뒤집어 면과 견준다.
    var diffuse = vec3<f32>(max(dot(normal, -normalize(global.keyLight.xyz)), 0.0) * global.keyLight.w);
    diffuse += vec3<f32>(max(dot(normal, -normalize(global.fillLight.xyz)), 0.0) * global.fillLight.w);

    // 점 광원. 방향 광원과 달리 조각마다 광원까지의 방향이 다르고,
    // 거리 제곱에 반비례해 어두워진다 (dot(v, v) 가 곧 거리의 제곱).
    let toLight = global.lightPosition.xyz - input.positionWorld;
    let attenuation = 1.0 / max(dot(toLight, toLight), 1e-6);
    let lightColor = global.lightColor.rgb * global.lightColor.a * attenuation;
    let toLightN = normalize(toLight);

    // 램버트 확산광: 면이 광원을 정면으로 볼수록 밝다. 등지는 면은 0 으로 자른다.
    diffuse += lightColor * max(dot(normal, toLightN), 0.0);

    // 블린-퐁 하이라이트 (점 광원만, 네이티브와 같은 32 제곱)
    let toCamera = normalize(global.cameraPosition.xyz - input.positionWorld);
    let halfway = normalize(toLightN + toCamera);
    let specular = lightColor * pow(clamp(dot(normal, halfway), 0.0, 1.0), 32.0);

    // 텍스처 색 * 정점 색 * 조명. 정점 색이 흰색이면 텍스처 그대로,
    // 텍스처가 없으면(1x1 흰색) 정점 색 그대로다.
    let texel = textureSample(materialTexture, materialSampler, input.uv);
    let base = mix(input.color * texel.rgb, object.objectColor.rgb * texel.rgb, object.objectColor.a);
    return vec4<f32>((diffuse + ambientLight) * base + specular, 1.0);
}

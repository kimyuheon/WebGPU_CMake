#include "text_render_system.h"
#include "lot_log.h"
#include "lot_material.h"
#include "lot_texture.h"
#include "lot_web_common.h"
#include "lot_web_device.h"

#include <algorithm>
#include <cstdlib>

extern "C" {
    // src/js/lot_text.js - 캔버스로 글자를 굽는다. 반환 버퍼는 여기서 free.
    extern uint8_t* js_renderTextBitmap(const char* text, int pxHeight, int* outW, int* outH);
}

TextRenderSystem::~TextRenderSystem() {
    cache_.clear();  // 재질(바인드 그룹)이 레이아웃보다 먼저 사라져야 한다
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (materialLayout_) wgpuBindGroupLayoutRelease(materialLayout_);
}

void TextRenderSystem::create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                              WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat) {
    if (pipeline_) return;
    if (globalLayout == nullptr) {
        LOT_ERR("TextRenderSystem: global uniform layout is required!");
        return;
    }
    device_ = &device;
    buffer_ = std::make_unique<LotDynamicBuffer>(BufferType::VERTEX);

    // @group(1) = 글자 텍스처. 메시 재질과 같은 레이아웃 (텍스처 + 샘플러) 이라
    // LotMaterial 을 그대로 쓴다. WebGPU 는 레이아웃을 구조로 비교하므로 별 객체라도 호환된다.
    materialLayout_ = LotMaterial::createBindGroupLayout(device);
    if (!materialLayout_) return;

    WGPUBindGroupLayout layouts[2] = {globalLayout, materialLayout_};
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Text Render System Layout");
    layoutDesc.bindGroupLayoutCount = 2;
    layoutDesc.bindGroupLayouts = layouts;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &layoutDesc);
    if (!pipelineLayout_) {
        LOT_ERR("TextRenderSystem: Failed to create pipeline layout!");
        return;
    }

    PipelineConfig config;
    config.topology = WGPUPrimitiveTopology_TriangleList;
    config.cullMode = WGPUCullMode_None;  // 뒤에서 봐도 (거울상이지만) 보이게
    config.alphaBlend = true;             // 글자 가장자리
    config.depthTest = true;              // 메시 뒤로는 가려진다

    pipeline_ = std::make_unique<lot_web_pipeline>("shaders/text.wgsl");
    pipeline_->createPipeline(device, colorFormat, depthFormat, pipelineLayout_, config);
    LOT_LOG("TextRenderSystem: created");
}

bool TextRenderSystem::isReady() const {
    return pipeline_ && pipeline_->isReady() && buffer_ != nullptr && device_ != nullptr;
}

const TextRenderSystem::Entry* TextRenderSystem::lookup(const std::string& text) {
    auto it = cache_.find(text);
    if (it != cache_.end()) return &it->second;
    if (!device_ || !materialLayout_) return nullptr;

    int w = 0, h = 0;
    uint8_t* rgba = js_renderTextBitmap(text.c_str(), kBitmapHeightPx, &w, &h);
    if (!rgba || w <= 0 || h <= 0) {
        LOT_ERR("TextRenderSystem: could not rasterize \"" << text << "\"");
        if (rgba) std::free(rgba);
        return nullptr;
    }
    std::shared_ptr<LotTexture> texture = LotTexture::createFromRGBA(
        *device_, static_cast<uint32_t>(w), static_cast<uint32_t>(h), rgba, "Text");
    std::free(rgba);
    if (!texture) return nullptr;

    Entry entry;
    entry.material = std::make_shared<LotMaterial>(*device_, materialLayout_, std::move(texture));
    entry.aspect = static_cast<float>(w) / static_cast<float>(h);
    // 폰트 크기(em) 64px 에서 대문자/숫자 높이는 대략 0.72em. 비트맵에는 위아래 여백이 있어
    // 사각형을 글자 높이의 이 배수로 잡아야 보이는 글자가 요청한 높이가 된다.
    entry.quadPerHeight = static_cast<float>(h) / (static_cast<float>(kBitmapHeightPx) * 0.72f);
    auto inserted = cache_.emplace(text, std::move(entry));
    return &inserted.first->second;
}

float TextRenderSystem::measure(const std::string& text, float height) {
    const Entry* e = lookup(text);
    return e ? e->aspect * height * e->quadPerHeight : 0.0f;
}

void TextRenderSystem::addText(const std::string& text, const vec3& origin, const vec3& right,
                               const vec3& up, float height, const vec3& color, int align) {
    const Entry* e = lookup(text);
    if (!e || !e->material || !e->material->isReady()) return;

    // 사각형은 비트맵 전체(여백 포함)를 덮는다
    const float quadH = height * e->quadPerHeight;
    const float quadW = quadH * e->aspect;
    float x0 = 0.0f;
    if (align == 1) x0 = -quadW * 0.5f;
    else if (align == 2) x0 = -quadW;

    const vec3 bl = origin + right * x0 - up * (quadH * 0.5f);
    const vec3 br = bl + right * quadW;
    const vec3 tl = bl + up * quadH;
    const vec3 tr = br + up * quadH;

    // 텍스처는 왼쪽 위가 (0, 0). 두 삼각형.
    auto vtx = [&](const vec3& p, float u, float v) {
        return Vertex::make(p.x, p.y, p.z, color.x, color.y, color.z, 0.0f, 0.0f, 1.0f, u, v);
    };
    Quad q;
    q.entry = e;
    q.v[0] = vtx(bl, 0.0f, 1.0f);
    q.v[1] = vtx(br, 1.0f, 1.0f);
    q.v[2] = vtx(tr, 1.0f, 0.0f);
    q.v[3] = vtx(bl, 0.0f, 1.0f);
    q.v[4] = vtx(tr, 1.0f, 0.0f);
    q.v[5] = vtx(tl, 0.0f, 0.0f);
    quads_.push_back(q);
}

void TextRenderSystem::render(FrameInfo& frame) {
    if (!isReady() || frame.pass == nullptr) return;

    // 같은 텍스처끼리 모아 draw 한 번씩. 포인터 순으로 정렬하면 충분하다.
    std::sort(quads_.begin(), quads_.end(),
              [](const Quad& a, const Quad& b) { return a.entry < b.entry; });
    vertices_.clear();
    for (const Quad& q : quads_) {
        for (const Vertex& v : q.v) vertices_.push_back(v);
    }
    buffer_->upload(*device_, vertices_.data(), vertices_.size() * sizeof(Vertex));
    if (quads_.empty()) return;

    pipeline_->bind(frame.pass);
    wgpuRenderPassEncoderSetBindGroup(frame.pass, 0, frame.globalBindGroup, 0, nullptr);
    buffer_->bind(frame.pass, 0);

    size_t start = 0;
    while (start < quads_.size()) {
        size_t end = start;
        while (end < quads_.size() && quads_[end].entry == quads_[start].entry) ++end;
        wgpuRenderPassEncoderSetBindGroup(frame.pass, 1, quads_[start].entry->material->getBindGroup(),
                                          0, nullptr);
        wgpuRenderPassEncoderDraw(frame.pass, static_cast<uint32_t>((end - start) * 6), 1,
                                  static_cast<uint32_t>(start * 6), 0);
        start = end;
    }
}

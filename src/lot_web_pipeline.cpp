#include "lot_web_pipeline.h"
#include <iostream>

// JavaScript 함수 선언 (webgpu_bindings.js에서 구현)
extern "C" {
    extern void js_createPipeline(const char* shaderPath);
    extern bool js_isPipelineReady();
    extern void js_bindPipeline();
    extern void js_draw(int vertexCount);
}

// C++ 구현
lot_web_pipeline::lot_web_pipeline(const std::string& shaderPath)
    : shaderPath_(shaderPath) {
    std::cout << "lot_web_pipeline: Constructor (shader: " << shaderPath << ")" << std::endl;
}

lot_web_pipeline::~lot_web_pipeline() {
    std::cout << "lot_web_pipeline: Destructor" << std::endl;
}

void lot_web_pipeline::createPipeline() {
    std::cout << "lot_web_pipeline: Creating pipeline..." << std::endl;
    js_createPipeline(shaderPath_.c_str());
}

void lot_web_pipeline::bind() {
    if (!isReady()) return;
    js_bindPipeline();
}

void lot_web_pipeline::draw(int vertexCount) {
    if (!isReady()) return;
    js_draw(vertexCount);
}

bool lot_web_pipeline::isReady() const {
    return js_isPipelineReady();
}

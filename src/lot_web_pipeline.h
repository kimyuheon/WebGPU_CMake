#pragma once

#include <emscripten/emscripten.h>
#include <string>

class lot_web_pipeline {
public:
    lot_web_pipeline(const std::string& shaderPath);
    ~lot_web_pipeline();

    // 파이프라인 생성 (비동기)
    void createPipeline();

    // 파이프라인 바인딩
    void bind();

    // 그리기
    void draw(int vertexCount);

    // 파이프라인이 준비되었는지 확인
    bool isReady() const;

private:
    std::string shaderPath_;
};

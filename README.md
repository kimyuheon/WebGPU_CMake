# WEBGPU - RendererSystem

Tool : Visual Studio Code
- 파일 구조
  ## 📂 프로젝트 구조

<pre>
📁 개발폴더/
├── Project/                        # 메인 프로젝트 폴더
│   ├── build/                      # 빌드 시 자동 생성
│   │   └── Debug/
│   │       ├── shaders/            # 빌드 시 복사
│   │       │   ├── simple_shader.frag
│   │       │   ├── simple_shader.vert
│   │       │   └── ...
│   │       └── VulkanApp           # 실행파일
│   ├── shaders/                    # 원본 셰이더 파일들
│   │   ├── simple_shader.frag
│   │   ├── simple_shader.vert
│   │   └── ...
│   ├── src/                        # 소스코드
│   │   ├── js/
│   │   │   └── webgpu_bindings.js  # 자바 스크립트
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── webgpu_bindings.js
│   │   ├── lot_web_buffer.cpp
│   │   ├── lot_web_buffer.h
│   │   ├── lot_web_device.cpp
│   │   ├── lot_web_device.h
│   │   ├── lot_web_pipeline.cpp
│   │   ├── lot_web_pipeline.h
│   │   ├── lot_swap_chain.cpp
│   │   ├── lot_swap_chain.h
│   │   ├── simple_render_system.cpp
│   │   ├── simple_render_system.h
│   │   └── main.cpp
│   ├── clean.sh                    # Mac/Linux 용 정리
│   ├── build.sh                    # Mac/Linux 용 빌드
│   ├── run.sh                      # Mac/Linux 용 실행
│   ├── clean.bat                   # Win 용 정리
│   ├── build.bat                   # Win 용 빌드
│   ├── run.bat                     # Win 용 실행
│   └── ...
├── emsdk/                          # Emscripten SDK 라이브러리
│   ├── bazel/ 
│   ├── docker/           
│   ├── node/           
│   └── ...
└── ...
</pre>

### 📝 주요 디렉토리 설명

| 경로 | 설명 |
|------|------|
| `Project/` | 메인 프로젝트 소스 코드 |
| `Project/build/` | CMake 빌드 출력 (자동 생성) |
| `Project/shaders/` | 원본 GLSL 셰이더 파일들 |
| `Project/src/js/` | 자바스크립트 관련 파일 |
| `Project/src/lot_web*.cpp/h` | Webgpu 엔진 컴포넌트들 |
| `emsdk/` | Emscripten SDK 라이브러리 |

> **Note**: Emscripten Sdk는 프로젝트 폴더와 같은 레벨에 위치하며, CMakeLists.txt에서 `../emsdk/` 경로로 참조됩니다.


- 수정사항  
  - RendererSystem 구현

- 실행결과
  - 삼각형 객체 회전

  ---  
  - 윈도우  
      - 실행방법    
        <kbd>PS D:\vulkan\3dengine_web></kbd> .\clean.bat  
        <kbd>PS D:\vulkan\3dengine_web></kbd> .\build.bat 
        <kbd>PS D:\vulkan\3dengine_web></kbd> .\run.bat 
          
        https://github.com/user-attachments/assets/c01a49ca-fdf8-439f-9751-553695b2dba4                    
    
  - MacOS/Linux(Ubuntu)
      - 실행방법  
        <kbd>test@MacBookPro build % </kbd> .\clean.sh
        <kbd>test@MacBookPro build % </kbd> .\build.sh
        <kbd>test@MacBookPro build % </kbd> .\run.sh  

        https://github.com/user-attachments/assets/87199165-0bd9-4481-840e-6ad8b49c2362  
        


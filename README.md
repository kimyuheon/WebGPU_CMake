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


## 기능 (2026-09 기준)

네이티브 Vulkan CAD 엔진(`../3dengine`)을 WebGPU 로 옮기는 프로젝트다. 좌표계는 CAD 표준 **Z-up**
(+X 오른쪽, +Y 앞, +Z 위, 바닥 = XY) 으로 네이티브와 같고, `.lot` 씬 파일이 양쪽에서 열린다.

| 영역 | 내용 | 키 |
|---|---|---|
| 카메라 | CAD 궤도 (우클릭 궤도 · 중클릭 팬 · 휠 줌), 표준 뷰, 원근/직교, 전체 보기, 1인칭 | `F/T/R/I` `P` `Z` `V` |
| 선택 | 클릭 · Shift 토글 · 박스 선택 (window/crossing), 선/메시 모두 | 마우스 |
| 기즈모 | 이동 / 회전 / 축척 (축·평면·균등 핸들) | `1/2/3` |
| 변환 | 기준점 방식 이동 / 복사 / 회전 / 축척, 숫자 입력 | `M/U/K/X` + 값 + `Enter` |
| 스케치 | 선 · 사각형 · 폴리라인 · 원 · 호(3점) · 정다각형, 뷰에 맞는 작업평면 | `L/B/N/C/A/G`, `[ ]` |
| 스냅 | 끝점 □ · 중점 △ · 중심 ○ (메시 정점/모서리, 스케치) | 자동 |
| 편집 | 제자리 복제 · 삭제 · 실행 취소/다시 실행 (100단계) | `Ctrl+D` `Del` `Ctrl+Z/Y` |
| 파일 | `.lot` 저장/열기 (네이티브 호환 JSON), OBJ 열기 | 툴바 |
| 렌더 | 텍스처 재질, 점 광원, 오프스크린 + 외곽선 후처리 | `O` |

키 대신 캔버스 왼쪽 툴바 버튼을 눌러도 같다 (버튼은 단축키 코드를 되돌려 보낼 뿐이라 둘이 어긋나지 않는다).

### 구조 메모

- `lot_math.h` — vec/mat/quat. 회전은 항상 쿼터니언 (오일러 금지, 짐벌락).
- `lot_edit_controller` — 선택 · 기즈모 드래그 · 스냅. 도구가 열려 있으면 `toolActive` 게이트 하나로 클릭을 양보한다.
- `lot_sketch_tool` — `SketchTool` 부모 + `SketchController` 레지스트리. 도구를 추가할 때 고칠 게이트가 없다.
- `lot_transform_tool` — 기준점 변환. 숫자 입력은 `KeyboardMovementController::setNumberCapture`.
- `lot_history` — 편집 전/후 스냅샷 기반 undo/redo. 도구가 늘어도 `record()` 한 줄.
- `lot_scene_io` + `lot_json` — `.lot` 저장/열기. 외부 JSON 라이브러리 없음.
- `src/js/lot_toolbar.js` — HTML 툴바. C++ 이 상태를 밀고, 버튼은 키 코드를 되돌린다.

### 헤드리스 테스트

```
python -m http.server 8123   (build/ 에서)
chrome --headless=new --remote-debugging-port=9222 --enable-unsafe-swiftshader http://localhost:8123/WebGPUApp.html
node tools/click.mjs out.png key KeyT key KeyL click 400 300 click 600 300 key Enter
```
`tools/click.mjs` 는 클릭/드래그/키/휠/툴바 버튼/씬 저장·열기를 CDP 로 보내고 캡처한다.

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
        


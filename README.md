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
| 뷰큐브 | 오른쪽 위 상자. 면·모서리·꼭짓점 26 방향을 눌러 그 시점으로 | 마우스 |
| 선택 | 클릭 · Shift 토글 · 박스 선택 (window/crossing), 선/메시 모두 | 마우스 |
| 기즈모 | 이동 / 회전 / 축척 (축·평면·균등 핸들) | `1/2/3` |
| 변환 | 기준점 방식 이동 / 복사 / 회전 / 축척, 숫자 입력 | `M/U/K/X` + 값 + `Enter` |
| 스케치 | 선 · 사각형 · 폴리라인 · 원 · 호(3점) · 정다각형 · 치수 · 문자 | `L/B/N/C/A/G/D/W`, `[ ]` |
| 3D | 보고 있는 자리에 큐브 | 메뉴 · 리본 · `cube` |
| 주석 편집 | 문자를 더블 클릭하면 내용 고치기 (비우면 삭제) | 더블 클릭 |
| 스냅 | 끝점 □ · 중점 △ · 중심 ○ · 교차 × · 수직 ⊥ (명령이 점을 물을 때만) | 자동 |
| 커서 보정 | 직교 트랙킹, 그리드 스냅 (간격은 씬 크기에 맞춰 자동) | `F8` `F9` |
| 편집 | 제자리 복제 · 삭제 · 실행 취소/다시 실행 (100단계) | `Ctrl+D` `Del` `Ctrl+Z/Y` |
| 선택 | 전체 선택 (잠긴 층 제외) · 전체 지우기 (한 번에 되돌아간다) | `Ctrl+A` · `eraseall` |
| 레이어 | 층 만들기/삭제, 표시·잠금, 현재 층, 선택을 옮기기 | 오른쪽 패널 |
| 속성 | 색 (층 색 / 오브젝트 색 / ByLayer, 메시 포함), 선종류 (8종) | 오른쪽 패널 |
| 파일 | `.lot` 저장/열기 (네이티브 호환 JSON), DXF 열기/내보내기, OBJ 열기 | 메뉴 · 리본 |
| 도면 탭 | 여러 도면을 탭으로 (도면마다 오브젝트·층·히스토리·시점 따로), 고친 탭에 점, 파일은 새 탭에 열림 | `+` · `new` `close` `nexttab` `prevtab` |
| 명령행 | 이름으로 명령 부르기 (별칭·한글), Tab 자동완성, 지난 명령, 값 입력 | `Space` |
| 렌더 | 텍스처 재질, 점 광원 (고르고 옮기고 지울 수 있는 오브젝트), 오프스크린 + 외곽선 후처리 | `O` |

키 대신 위쪽 **메뉴바**(파일·편집·뷰·그리기·수정·설정)나 **리본**(홈 / 2D / 3D 탭)에서 눌러도 같다.
Vulkan 쪽 ImGui UI 와 같은 짜임이다: 메뉴 항목 오른쪽에 단축키, 리본은 아이콘 한 줄 + 그룹 캡션,
켜진 명령은 파란 강조, 탭 줄을 더블클릭하면 접힌다. 오른쪽에 레이어/속성 패널.

화면 맨 아래는 AutoCAD 식 **명령행**이다. `Space` 로 커서가 가고, 이름을 쳐서 부른다
(`line`/`l`/`선`, `circle`/`c`/`원`, `move`/`m`, `zoom`/`z`, `undo`/`u` …). `Tab` 자동완성,
`↑`/`↓` 로 지난 명령, 빈 `Enter` 는 직전 명령 되풀이. 변환 도구가 값을 기다릴 때 숫자를 치면
그 값으로 확정된다 (`1.5` = 1.5 단위 이동). 별칭 표는 `src/ui/lot_command_line.cpp` 에 있고
Vulkan 쪽 `builtinCommandTable` 과 같은 규약(영문 풀이름 · 짧은 별칭 · 한글)을 쓴다.

명령 표는 C++ 한 곳(`src/ui/lot_main_menu.cpp`, `lot_ribbon.cpp`)에 있고 메뉴·리본·명령행·단축키가
그걸 함께 본다. 어느 쪽으로 눌러도 그 명령의 키 코드가 키보드와 같은 경로로 들어가므로 넷이 갈라질 수 없다.
단축키가 없는 명령(큐브처럼)은 키 코드 자리에 `#이름`을 적는다 - `main.cpp` 의 `runAction` 이 받는다.
알파벳을 아껴 쓰려는 것이다: 그릴 거리마다 글쇠를 하나씩 떼어 주면 금세 바닥난다.

### 구조 메모

- `lot_math.h` — vec/mat/quat. 회전은 항상 쿼터니언 (오일러 금지, 짐벌락).
- `lot_edit_controller` — 선택 · 기즈모 드래그 · 스냅. 도구가 열려 있으면 `toolActive` 게이트 하나로 클릭을 양보한다.
- `lot_sketch_tool` — `SketchTool` 부모 + `SketchController` 레지스트리. 도구를 추가할 때 고칠 게이트가 없다.
- `lot_transform_tool` — 기준점 변환. 숫자 입력은 `KeyboardMovementController::setNumberCapture`.
- `lot_history` — 편집 전/후 스냅샷 기반 undo/redo. 도구가 늘어도 `record()` 한 줄.
- `lot_document` — 도면 하나 (탭 하나). `main.cpp` 는 `doc()` 로만 닿고, 탭을 바꾸면 그 포인터만 옮긴다.
  고쳐졌나는 히스토리 `revision()` 과 저장 때 값의 비교. 손대지 않은 새 탭에 파일을 열면 그 자리를 쓴다.
- `lot_scene_io` + `lot_json` — `.lot` 저장/열기. 외부 JSON 라이브러리 없음.
- `lot_dxf` — DXF 읽기 (LINE/CIRCLE/ARC/LWPOLYLINE/POLYLINE/TEXT/SPLINE 근사, 층·ACI 색·선종류)와
  쓰기 (R12 ASCII - 핸들도 서브클래스도 없어 어디서나 열린다). 치수는 DIMENSION 이 블록을
  달고 다녀야 해서 선과 글자로 풀어 쓰고, 메시는 빠진다. 글자 코드페이지(CP949 등)는
  JS 쪽 TextDecoder 가 풀어 UTF-8 로 넘긴다.
- `lot_cursor_snap` — 커서 보정 한 곳 (osnap > 직교 > 그리드). 도구가 각자 들면 둘이 어긋난다.
- `lot_linetype` — 선종류. 무늬를 CPU 에서 잘라 선분으로 낸다 (폴리라인 전체가 한 누적 길이).
- `lot_layers` — 도면층. 렌더/피킹은 층을 모르고 콜백(`FrameInfo::visibleFilter`, `lot_pick::setSelectableFilter`)만 본다.
- `lot_view_cube` (`src/ui/`) — 뷰큐브. 그리지 않는다: 자세를 CSS `matrix3d` 로 넘기면
  브라우저가 상자를 돌리고, 면마다 올린 3x3 칸이 26 방향 히트 테스트를 대신한다.
  뷰 공간은 +Z 가 앞인데 CSS 는 +Z 가 보는 쪽이라 셋째 행 부호만 뒤집어 넘긴다.
- `src/ui/` — UI 클래스들. `LotMainMenu`/`LotRibbon` 은 명령 표, `LotLayerPanel` 은 층/속성 편집,
  `LotCommandLine` 은 그 표에서 칠 수 있는 이름을 뽑아 둔다. `LotUi` 가 넷을 들고 바뀐 것만 DOM 으로 내보낸다. 그리는 것은 `src/js/lot_ui.js`(메뉴·리본)와
  `src/js/lot_panels.js`(패널·입력창·파일).
  ⚠️ Closure(릴리스)가 점 표기 속성명을 바꾼다. 바깥에 노출하거나 JSON 으로 주고받는 이름은
  `obj['name']` 으로, DOM 속성은 `el.setAttribute('data-cmd', ...)` 로 적는다 (`el.dataset.cmd` 도 줄여버린다).

### 헤드리스 테스트 / 회귀

```
.	ools
egress.ps1        (Windows)      서버 + 헤드리스 크롬을 띄우고 시나리오 10개를 돌린 뒤 정리
tools/regress.sh           (macOS/Linux)  같은 것. 결과 스크린샷은 build/regress/<시각>/
```
빌드 뒤, 푸시 전에 한 번 돌린다. 판정은 엔진 로그("sketch: circle committed" 같은 줄)라
렌더가 깨지는 회귀는 스크린샷을 열어 봐야 한다. 시나리오는 `tools/regress.mjs` 에 있고,
좌표는 1100x850 뷰포트의 Top 뷰 기준이다 (1 단위 ≈ 187px, 원점 = (550, 500)).

손으로 한 번씩 볼 때는 서버와 크롬을 직접 띄우고 `tools/click.mjs` 로 조작한다:
```
python -m http.server 8123   (build/ 에서)
chrome --headless=new --remote-debugging-port=9222 --enable-unsafe-swiftshader --window-size=1100,850 http://localhost:8123/WebGPUApp.html
node tools/click.mjs out.png key KeyT key KeyL click 600 700 click 900 700 key Enter
```
클릭/드래그/키/휠/툴바 버튼/문자 입력/씬 저장·열기를 CDP 로 보내고 캡처한다. 공용 도우미는 `tools/cdp.mjs`.

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
        


/**
 * DOM Helpers for the WebGPU Engine
 *
 * WebGPU 호출은 전부 C++ 쪽(webgpu.h / emdawnwebgpu)으로 옮겨졌다.
 * 여기 남은 것은 브라우저에서만 할 수 있는 DOM 구성뿐이다:
 *   - 상태바 + 캔버스 엘리먼트 생성
 *   - console 출력을 화면 상태바로 미러링
 *   - OBJ 파일 열기 버튼 (파일 선택창은 JS 로만 띄울 수 있다)
 *
 * --js-library 옵션으로 링크된다.
 */

mergeInto(LibraryManager.library, {

    // 상태바와 캔버스를 만든다. C++ 은 "#webgpu-canvas" 셀렉터로 서피스를 만든다.
    js_setupCanvas: function(statusBarHeight) {
        if (!Module.lotDom) {
            Module.lotDom = {};
        }
        var dom = Module.lotDom;

        document.body.style.margin = '0';
        document.body.style.overflow = 'hidden';
        document.body.style.fontFamily = 'monospace';
        document.body.style.backgroundColor = '#1a1a1a';

        // 상태바 (위쪽)
        if (!dom.statusBar) {
            var statusBar = document.createElement('div');
            statusBar.id = 'status-bar';
            statusBar.style.position = 'fixed';
            statusBar.style.top = '0';
            statusBar.style.left = '0';
            statusBar.style.width = '100%';
            statusBar.style.height = statusBarHeight + 'px';
            statusBar.style.backgroundColor = '#0d0d0d';
            statusBar.style.color = '#00ff00';
            statusBar.style.fontSize = '12px';
            statusBar.style.padding = '8px';
            statusBar.style.boxSizing = 'border-box';
            statusBar.style.overflowY = 'auto';
            statusBar.style.borderBottom = '2px solid #333';
            statusBar.innerHTML =
                '<div style="color:#888;">WebGPU 3D Engine - Console Output</div>' +
                '<hr style="border-color:#333;margin:4px 0;">';
            document.body.appendChild(statusBar);
            dom.statusBar = statusBar;

            // 매 프레임 에러가 나는 경우에도 DOM 이 무한히 커지지 않도록 줄 수를 제한한다.
            var MAX_LINES = 200;
            var appendLine = function(msg, color) {
                var line = document.createElement('div');
                line.textContent = msg;
                if (color) line.style.color = color;
                line.style.borderBottom = '1px solid #222';
                line.style.padding = '2px 0';
                statusBar.appendChild(line);
                while (statusBar.childElementCount > MAX_LINES) {
                    statusBar.removeChild(statusBar.firstElementChild);
                }
                statusBar.scrollTop = statusBar.scrollHeight;
            };

            var format = function(args) {
                return Array.prototype.map.call(args, function(a) {
                    return typeof a === 'object' ? JSON.stringify(a) : a;
                }).join(' ');
            };

            var originalLog = console.log;
            console.log = function() {
                originalLog.apply(console, arguments);
                appendLine(format(arguments));
            };

            var originalError = console.error;
            console.error = function() {
                originalError.apply(console, arguments);
                appendLine('[ERROR] ' + format(arguments), '#ff4444');
            };
        }

        // 캔버스 (상태바 아래)
        if (!dom.canvas) {
            var canvas = document.createElement('canvas');
            canvas.id = 'webgpu-canvas';
            canvas.style.position = 'fixed';
            canvas.style.top = statusBarHeight + 'px';
            canvas.style.left = '0';
            canvas.style.width = '100%';
            canvas.style.height = 'calc(100% - ' + statusBarHeight + 'px)';
            canvas.style.display = 'block';
            document.body.appendChild(canvas);
            dom.canvas = canvas;
        }
    },

    // 캔버스 백버퍼 크기 설정. C++ 이 서피스를 다시 configure 하기 직전에 부른다.
    js_setCanvasSize: function(width, height) {
        var dom = Module.lotDom;
        if (!dom || !dom.canvas) return;
        dom.canvas.width = width;
        dom.canvas.height = height;
    },

    js_getWindowWidth: function() {
        return window.innerWidth;
    },

    js_getWindowHeight: function(statusBarHeight) {
        return window.innerHeight - statusBarHeight;
    },

    // OBJ 파일 열기 버튼.
    //
    // 파일 선택창은 사용자 제스처로만 열 수 있어서 C++ 에서 직접 띄울 수 없다.
    // 여기서 버튼을 만들고, 고른 파일을 바이트로 읽어 wasm 힙에 복사한 뒤
    // C++ 의 lot_onObjFileLoaded 를 부른다. 버퍼 해제는 C++ 쪽 책임이다.
    js_setupObjFileInput__deps: ['lot_onObjFileLoaded', 'malloc'],
    js_setupObjFileInput: function() {
        if (!Module.lotDom) {
            Module.lotDom = {};
        }
        var dom = Module.lotDom;
        if (dom.objInput) return;

        var input = document.createElement('input');
        input.type = 'file';
        input.accept = '.obj';
        input.style.display = 'none';

        var button = document.createElement('button');
        button.textContent = 'OBJ 열기';
        button.style.position = 'fixed';
        button.style.right = '12px';
        button.style.bottom = '12px';
        button.style.zIndex = '10';
        button.style.padding = '8px 14px';
        button.style.fontFamily = 'monospace';
        button.style.fontSize = '13px';
        button.style.color = '#00ff00';
        button.style.backgroundColor = '#0d0d0d';
        button.style.border = '1px solid #00ff00';
        button.style.borderRadius = '4px';
        button.style.cursor = 'pointer';

        button.addEventListener('click', function() {
            input.click();
        });

        input.addEventListener('change', function() {
            var file = input.files && input.files[0];
            if (!file) return;

            var reader = new FileReader();
            reader.onload = function() {
                var bytes = new Uint8Array(reader.result);
                var ptr = _malloc(bytes.length);
                if (!ptr) {
                    console.error('OBJ 열기: 메모리를 잡지 못했습니다 (' +
                                  bytes.length + ' 바이트)');
                    return;
                }
                HEAPU8.set(bytes, ptr);
                _lot_onObjFileLoaded(ptr, bytes.length);
            };
            reader.onerror = function() {
                console.error('OBJ 열기: 파일을 읽지 못했습니다 - ' + file.name);
            };
            reader.readAsArrayBuffer(file);

            // 같은 파일을 다시 골라도 change 가 오도록 값을 비운다
            input.value = '';
        });

        document.body.appendChild(input);
        document.body.appendChild(button);
        dom.objInput = input;
        dom.objButton = button;
    },

});

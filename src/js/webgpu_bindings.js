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

        // 휴대폰(손가락 · 작은 화면)은 맨 위 로그 창을 숨기고 그 높이를 도면에 준다 - 가로로 돌리면
        // 세로가 400px 남짓이라 150px 로그 창이 너무 크다. 로그는 ?remotelog 로 PC 에서 본다.
        var phone = window.matchMedia && window.matchMedia('(pointer: coarse)').matches
                    && Math.min(window.screen.width, window.screen.height) < 700;
        if (phone) statusBarHeight = 0;
        dom.statusBarHeight = statusBarHeight;

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
            if (!statusBarHeight) statusBar.style.display = 'none';   // 줄은 계속 쌓인다 (원격 로그용)
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

            // ?remotelog 로 열면 콘솔 줄을 서버(/__log)로도 보낸다 - 휴대폰처럼 개발자 도구를
            // 못 여는 기기의 엔진 로그를 개발 PC 에서 읽으려는 것이다. 모아서 0.5 초마다 한 번.
            var remote = /[?&]remotelog\b/.test(window.location.search) ? [] : null;
            if (remote) {
                var flush = function() {
                    if (!remote.length) return;
                    var body = remote.join('\n');
                    remote.length = 0;
                    try { fetch('/__log', { method: 'POST', body: body }); } catch (e) {}
                };
                setInterval(flush, 500);
                window.addEventListener('error', function(e) {
                    remote.push('[window.onerror] ' + e.message + ' @' + e.filename + ':' + e.lineno);
                });
                window.addEventListener('unhandledrejection', function(e) {
                    remote.push('[unhandledrejection] ' + (e.reason && (e.reason.stack || e.reason.message) || e.reason));
                });
                remote.push('[remotelog] ' + navigator.userAgent + ' gpu=' + (!!navigator.gpu)
                            + ' dpr=' + window.devicePixelRatio + ' ' + window.innerWidth + 'x' + window.innerHeight);
            }
            var originalLog = console.log;
            console.log = function() {
                originalLog.apply(console, arguments);
                appendLine(format(arguments));
                if (remote) remote.push(format(arguments));
            };

            var originalError = console.error;
            console.error = function() {
                originalError.apply(console, arguments);
                appendLine('[ERROR] ' + format(arguments), '#ff4444');
                if (remote) remote.push('[ERROR] ' + format(arguments));
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
            // 터치는 엔진이 받는다 (lot_mouse_input.cpp) - 브라우저의 확대 · 스크롤 · 두 번 탭 확대를 끈다
            canvas.style.touchAction = 'none';
            document.body.style.touchAction = 'manipulation';
            // iOS Safari 는 touch-action 과 별도로 핀치를 페이지 확대로 쓴다
            document.addEventListener('gesturestart', function(e) { e.preventDefault(); }, { passive: false });
            document.body.appendChild(canvas);
            dom.canvas = canvas;

            // 화면 회전: iOS 는 회전 직후 resize 이벤트에서 아직 예전 크기를 알려 주기도 한다.
            // 조금 뒤에 두 번 더 resize 를 흘려 C++ 스왑체인이 자리 잡은 크기를 다시 읽게 한다.
            var settle = function() {
                [120, 450].forEach(function(ms) {
                    setTimeout(function() { window.dispatchEvent(new Event('resize')); }, ms);
                });
            };
            window.addEventListener('orientationchange', settle);
            if (window.screen && window.screen.orientation && window.screen.orientation.addEventListener) {
                window.screen.orientation.addEventListener('change', settle);
            }
        }
    },

    // 캔버스 백버퍼 크기 설정. C++ 이 서피스를 다시 configure 하기 직전에 부른다.
    js_setCanvasSize: function(width, height) {
        var dom = Module.lotDom;
        if (!dom || !dom.canvas) return;
        dom.canvas.width = width;
        dom.canvas.height = height;
    },

    // 도크(lot_dock.js)가 양옆을 차지하면 캔버스는 그 사이만 쓴다
    js_getWindowWidth: function() {
        var ins = Module.lotDom && Module.lotDom.dockInsets;
        return window.innerWidth - (ins ? ins.left + ins.right : 0);
    },

    js_getWindowHeight: function(statusBarHeight) {
        // 휴대폰은 로그 창을 숨겨 0 이다 (js_setupCanvas 가 정한 실제 높이)
        var dom = Module.lotDom;
        var bar = (dom && dom.statusBarHeight !== undefined) ? dom.statusBarHeight : statusBarHeight;
        return window.innerHeight - bar;
    },

    // OBJ 로더 등록.
    //
    // OBJ 바이트를 wasm 힙에 복사한 뒤 C++ 의 lot_onObjFileLoaded 를 부른다.
    // 버퍼 해제는 C++ 쪽 책임이다.
    js_setupObjFileInput__deps: ['lot_onObjFileLoaded', 'malloc'],
    js_setupObjFileInput: function() {
        if (!Module.lotDom) {
            Module.lotDom = {};
        }
        var dom = Module.lotDom;
        if (dom.objLoad) return;

        // 파일 선택은 lot_panels.js 의 통합 열기 대화상자가 하고, .obj 면 이걸 부른다
        dom.objLoad = function(buffer) {
            var bytes = new Uint8Array(buffer);
            var ptr = _malloc(bytes.length);
            if (!ptr) {
                console.error('OBJ 열기: 메모리를 잡지 못했습니다 (' +
                              bytes.length + ' 바이트)');
                return false;
            }
            HEAPU8.set(bytes, ptr);
            _lot_onObjFileLoaded(ptr, bytes.length);
            return true;
        };
        dom['objLoad'] = dom.objLoad;  // 테스트 도구가 부른다 (Closure 이름 고정)
    },

});

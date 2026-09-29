/**
 * 툴바 - 캔버스 왼쪽에 겹쳐 놓는 HTML 버튼 묶음.
 *
 * Vulkan 쪽은 ImGui 지만 브라우저에서는 DOM 이 더 싸고 접근성도 낫다.
 * 버튼은 로직을 갖지 않는다: 누르면 그 기능의 단축키 코드를 C++ 에 넘기고
 * (lot_onToolbarKey), C++ 은 키보드 이벤트와 같은 경로로 처리한다. 그래서
 * 키와 버튼이 어긋날 수 없다. 반대로 C++ 은 프레임마다 상태(현재 기즈모 모드,
 * 열린 스케치 도구, 뷰 모드, undo 가능 여부, 안내문)를 밀어서 버튼을 밝힌다
 * (js_setToolbarState). 바뀔 때만 부르므로 DOM 을 매 프레임 건드리지 않는다.
 *
 * --js-library 옵션으로 링크된다 (webgpu_bindings.js 와 같은 방식).
 */

mergeInto(LibraryManager.library, {

    js_setupToolbar__deps: ['lot_onToolbarKey', 'lot_saveScene', 'lot_onLotFileLoaded',
                            'lot_onTextEntered', 'lot_onTextCancelled', 'lot_onLayerCommand',
                            'lot_onDxfFileLoaded',
                            '$stringToNewUTF8', '$UTF8ToString', 'malloc', 'free'],
    js_setupToolbar: function() {
        if (!Module.lotDom) {
            Module.lotDom = {};
        }
        var dom = Module.lotDom;
        if (dom.toolbar) return;
        // 캔버스는 상태바 아래에서 시작한다. 상태바가 아직 없으면 (호출 순서가 바뀌면) 0.
        var statusBarHeight = dom.statusBar ? dom.statusBar.offsetHeight : 0;

        // .lot 파일 입력 (숨김). 파일 선택창은 사용자 제스처로만 열리므로 버튼이 click() 한다.
        var lotInput = document.createElement('input');
        lotInput.type = 'file';
        lotInput.accept = '.lot,.json';
        lotInput.style.display = 'none';
        // DXF 파일 입력. 옛 도면은 CP949 같은 코드페이지라 브라우저 TextDecoder 로 푼다
        // ($DWGCODEPAGE 를 앞부분에서 찾아 고른다). C++ 은 UTF-8 만 받는다.
        var dxfInput = document.createElement('input');
        dxfInput.type = 'file';
        dxfInput.accept = '.dxf';
        dxfInput.style.display = 'none';
        dom.dxfLoad = function(buffer) {
            var bytes = new Uint8Array(buffer);
            // 앞 4KB 만 아스키로 훑어 코드페이지를 찾는다
            var head = '';
            for (var i = 0; i < Math.min(bytes.length, 4096); ++i) head += String.fromCharCode(bytes[i]);
            var label = 'utf-8';
            var m = head.match(/\$DWGCODEPAGE\s*\r?\n\s*3\s*\r?\n\s*([A-Za-z0-9_]+)/);
            if (m) {
                var cp = m[1].toLowerCase();
                if (cp.indexOf('949') >= 0) label = 'euc-kr';
                else if (cp.indexOf('936') >= 0) label = 'gbk';
                else if (cp.indexOf('932') >= 0) label = 'shift_jis';
                else if (cp.indexOf('950') >= 0) label = 'big5';
                else if (cp.indexOf('1252') >= 0) label = 'windows-1252';
                else if (cp.indexOf('1251') >= 0) label = 'windows-1251';
            }
            var text;
            try { text = new TextDecoder(label).decode(bytes); }
            catch (e) { text = new TextDecoder('utf-8').decode(bytes); }
            var utf8 = new TextEncoder().encode(text);
            var ptr = _malloc(utf8.length);
            if (!ptr) { console.error('dxf: out of memory (' + utf8.length + ' bytes)'); return false; }
            HEAPU8.set(utf8, ptr);
            _lot_onDxfFileLoaded(ptr, utf8.length);  // 해제는 C++ 쪽
            return true;
        };
        dxfInput.addEventListener('change', function() {
            var file = dxfInput.files && dxfInput.files[0];
            if (!file) return;
            var reader = new FileReader();
            reader.onload = function() { dom.dxfLoad(reader.result); };
            reader.onerror = function() { console.error('dxf: could not read ' + file.name); };
            reader.readAsArrayBuffer(file);
            dxfInput.value = '';
        });
        document.body.appendChild(dxfInput);
        dom['dxfLoad'] = dom.dxfLoad;  // 테스트 도구가 부른다 (Closure 이름 고정)

        // 씬 텍스트 <-> C++. 파일 대화상자와 분리해 두면 테스트 도구(tools/click.mjs)가
        // Module.lotDom.sceneSave() / sceneLoad(text) 로 대화상자 없이 부를 수 있다.
        dom.sceneSave = function() {
            var ptr = _lot_saveScene();  // C++ 이 malloc 으로 잡아 준 JSON, 여기서 free
            if (!ptr) return '';
            var text = UTF8ToString(ptr);
            _free(ptr);
            return text;
        };
        dom.sceneLoad = function(text) {
            var bytes = new TextEncoder().encode(text);
            var ptr = _malloc(bytes.length);
            if (!ptr) { console.error('scene: out of memory (' + bytes.length + ' bytes)'); return false; }
            HEAPU8.set(bytes, ptr);
            _lot_onLotFileLoaded(ptr, bytes.length);  // 해제는 C++ 쪽
            return true;
        };

        // Closure 가 속성 이름을 줄이므로, 바깥(테스트 도구, 콘솔)에서 부를 이름은
        // 따옴표로 박아 둔다. 안에서는 dom.sceneSave 로 써도 같은 함수다.
        Module['lotDom'] = dom;
        dom['sceneSave'] = dom.sceneSave;
        dom['sceneLoad'] = dom.sceneLoad;

        lotInput.addEventListener('change', function() {
            var file = lotInput.files && lotInput.files[0];
            if (!file) return;
            var reader = new FileReader();
            reader.onload = function() { dom.sceneLoad(reader.result); };
            reader.onerror = function() { console.error('scene: could not read ' + file.name); };
            reader.readAsText(file);
            lotInput.value = '';
        });
        document.body.appendChild(lotInput);
        dom.lotInput = lotInput;

        // 버튼 동작 중 키가 아닌 것들. '@이름' 코드로 가리킨다.
        var actions = {
            openObj: function() { if (dom.objInput) dom.objInput.click(); },
            openLot: function() { lotInput.click(); },
            openDxf: function() { dxfInput.click(); },
            saveLot: function() {
                var text = dom.sceneSave();
                if (!text) return;
                var blob = new Blob([text], { type: 'application/json' });
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a');
                a.href = url;
                a.download = 'scene.lot';
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
            },
        };

        // 기존의 떠 있는 OBJ 버튼은 툴바로 들어왔으니 숨긴다
        if (dom.objButton) dom.objButton.style.display = 'none';

        // [그룹, 버튼들...]. 버튼 = [표시, 키 코드 또는 '@동작', ctrl 여부, 툴팁, 상태 키]
        // 상태 키는 js_setToolbarState 가 '켜짐' 표시를 할 때 대조하는 이름이다.
        var groups = [
            ['file', [
                ['Open .lot', '@openLot', 0, 'Open a .lot scene (replaces the scene)', ''],
                ['Save .lot', '@saveLot', 0, 'Download the scene as scene.lot', ''],
                ['Open DXF',  '@openDxf', 0, 'Open an AutoCAD DXF drawing (replaces the scene)', ''],
                ['Open OBJ',  '@openObj', 0, 'Add a Wavefront OBJ model', ''],
            ]],
            ['view', [
                ['Front', 'KeyF', 0, 'Front view (F)', 'view:0'],
                ['Top',   'KeyT', 0, 'Top view (T)', 'view:2'],
                ['Right', 'KeyR', 0, 'Right view (R)', 'view:4'],
                ['Iso',   'KeyI', 0, 'Isometric view (I)', 'view:6'],
                ['Fit',   'KeyZ', 0, 'Zoom extents - fit the whole scene (Z)', ''],
                ['Parallel', 'KeyP', 0, 'Perspective / parallel (orthographic) projection (P)', 'ortho'],
                ['FPS',   'KeyV', 0, 'CAD orbit / first-person (V)', 'fps'],
            ]],
            ['gizmo', [
                ['Move',   'Digit1', 0, 'Move gizmo (1)', 'gizmo:0'],
                ['Rotate', 'Digit2', 0, 'Rotate gizmo (2)', 'gizmo:1'],
                ['Scale',  'Digit3', 0, 'Scale gizmo (3)', 'gizmo:2'],
            ]],
            ['modify', [
                ['Move',   'KeyM', 0, 'Move by base point -> destination, or distance (M)', 'xform:0'],
                ['Copy',   'KeyU', 0, 'Copy by base point, repeat (U)', 'xform:1'],
                ['Rotate', 'KeyK', 0, 'Rotate about base point, or degrees (K)', 'xform:2'],
                ['Scale',  'KeyX', 0, 'Scale about base point, or factor (X)', 'xform:3'],
            ]],
            ['sketch', [
                ['Line',     'KeyL', 0, 'Line (L)', 'sketch:0'],
                ['Rect',     'KeyB', 0, 'Rectangle (B)', 'sketch:1'],
                ['Polyline', 'KeyN', 0, 'Polyline (N)', 'sketch:2'],
                ['Circle',   'KeyC', 0, 'Circle: center, radius (C)', 'sketch:3'],
                ['Arc',      'KeyA', 0, 'Arc through 3 points (A, CAD mode)', 'sketch:4'],
                ['Polygon',  'KeyG', 0, 'Regular polygon: center, vertex (G; [ ] sides)', 'sketch:5'],
                ['Dim',      'KeyD', 0, 'Aligned dimension: two points, then line position (D, CAD mode)', 'sketch:6'],
                ['Text',     'KeyW', 0, 'Text: click the start point, type, Enter (W, CAD mode)', 'sketch:7'],
                ['Finish',   'Enter', 0, 'Finish sketch (Enter)', ''],
                ['Cancel',   'Escape', 0, 'Cancel sketch / clear selection (Esc)', ''],
                ['Ortho',    'F8', 0, 'Ortho tracking: constrain to plane axes (F8)', 'orthoTrack'],
                ['Snap',     'F9', 0, 'Grid snap: snap the cursor to grid steps (F9)', 'gridSnap'],
            ]],
            ['edit', [
                ['Dup',    'KeyD', 1, 'Duplicate selection in place (Ctrl+D)', ''],
                ['Delete', 'Delete', 0, 'Delete selection (Del)', ''],
                ['Undo',   'KeyZ', 1, 'Undo (Ctrl+Z)', 'undo'],
                ['Redo',   'KeyY', 1, 'Redo (Ctrl+Y)', 'redo'],
                ['Outline','KeyO', 0, 'Selection outline (O)', 'outline'],
            ]],
        ];

        var bar = document.createElement('div');
        bar.id = 'lot-toolbar';
        bar.style.position = 'fixed';
        bar.style.top = (statusBarHeight + 8) + 'px';
        bar.style.left = '8px';
        bar.style.zIndex = '10';
        bar.style.display = 'flex';
        bar.style.flexDirection = 'column';
        // 창이 낮으면 그룹이 다음 열로 흐른다 - 버튼이 화면 밖으로 잘리지 않게
        bar.style.flexWrap = 'wrap';
        bar.style.alignContent = 'flex-start';
        bar.style.maxHeight = 'calc(100vh - ' + (statusBarHeight + 16) + 'px)';
        bar.style.gap = '6px';
        bar.style.fontFamily = 'monospace';
        bar.style.fontSize = '12px';
        bar.style.userSelect = 'none';

        var buttons = {};  // 상태 키 -> 버튼

        var styleButton = function(b, on, enabled) {
            b.style.color = enabled ? (on ? '#0d0d0d' : '#00ff00') : '#3a5a3a';
            b.style.backgroundColor = on ? '#00ff00' : '#0d0d0d';
            b.style.borderColor = enabled ? '#00ff00' : '#2a3a2a';
            b.style.cursor = enabled ? 'pointer' : 'default';
        };

        groups.forEach(function(g) {
            var box = document.createElement('div');
            box.style.display = 'flex';
            box.style.flexDirection = 'column';
            box.style.gap = '2px';
            box.style.padding = '4px';
            box.style.border = '1px solid #333';
            box.style.borderRadius = '4px';
            box.style.backgroundColor = 'rgba(13, 13, 13, 0.85)';

            var title = document.createElement('div');
            title.textContent = g[0];
            title.style.color = '#666';
            title.style.fontSize = '10px';
            title.style.padding = '0 2px 2px';
            box.appendChild(title);

            g[1].forEach(function(spec) {
                var b = document.createElement('button');
                b.textContent = spec[0];
                b.title = spec[3];
                b.style.padding = '4px 8px';
                b.style.minWidth = '72px';
                b.style.textAlign = 'left';
                b.style.fontFamily = 'monospace';
                b.style.fontSize = '12px';
                b.style.border = '1px solid #00ff00';
                b.style.borderRadius = '3px';
                styleButton(b, false, true);

                // mousedown 을 막아 포커스가 버튼으로 가지 않게 한다 - 그래야 이후
                // Enter/Space 가 버튼을 다시 누르지 않고 캔버스 단축키로 간다.
                b.addEventListener('mousedown', function(e) { e.preventDefault(); });
                b.addEventListener('click', function() {
                    if (spec[1].charAt(0) === '@') {
                        var fn = actions[spec[1].substring(1)];
                        if (fn) fn();
                        return;
                    }
                    var ptr = stringToNewUTF8(spec[1]);
                    _lot_onToolbarKey(ptr, spec[2]);
                    _free(ptr);
                });
                if (spec[4]) buttons[spec[4]] = b;
                box.appendChild(b);
            });
            bar.appendChild(box);
        });

        // 레이어 패널 (오른쪽 위). C++ 이 js_setLayers 로 목록을 밀어 넣는다.
        // 행: [색] 이름 (개수)  [눈] [자물쇠] [x].  이름을 누르면 현재 층이 된다 (새 객체가 여기로).
        var layerPanel = document.createElement('div');
        layerPanel.id = 'lot-layers';
        layerPanel.style.position = 'fixed';
        layerPanel.style.right = '12px';
        layerPanel.style.top = (statusBarHeight + 8) + 'px';
        layerPanel.style.zIndex = '10';
        layerPanel.style.minWidth = '210px';
        layerPanel.style.maxHeight = 'calc(100vh - ' + (statusBarHeight + 80) + 'px)';
        layerPanel.style.overflowY = 'auto';
        layerPanel.style.padding = '4px';
        layerPanel.style.border = '1px solid #333';
        layerPanel.style.borderRadius = '4px';
        layerPanel.style.backgroundColor = 'rgba(13, 13, 13, 0.85)';
        layerPanel.style.fontFamily = 'monospace';
        layerPanel.style.fontSize = '12px';
        layerPanel.style.userSelect = 'none';
        document.body.appendChild(layerPanel);
        dom.layerPanel = layerPanel;

        // 층 명령을 C++ 로. action 은 lot_onLayerCommand 의 것.
        dom.layerCommand = function(action, id, value) {
            var ptr = stringToNewUTF8(action);
            _lot_onLayerCommand(ptr, id | 0, value | 0);
            _free(ptr);
        };

        // 안내문 (캔버스 왼쪽 아래). 열린 도구의 다음 할 일을 보여준다.
        var hint = document.createElement('div');
        hint.id = 'lot-hint';
        hint.style.position = 'fixed';
        hint.style.left = '50%';
        hint.style.transform = 'translateX(-50%)';
        hint.style.bottom = '12px';
        hint.style.whiteSpace = 'nowrap';
        hint.style.zIndex = '10';
        hint.style.padding = '6px 10px';
        hint.style.fontFamily = 'monospace';
        hint.style.fontSize = '12px';
        hint.style.color = '#ddd';
        hint.style.backgroundColor = 'rgba(13, 13, 13, 0.85)';
        hint.style.border = '1px solid #333';
        hint.style.borderRadius = '4px';
        hint.style.pointerEvents = 'none';
        hint.style.display = 'none';

        // 문자 도구 입력창. 기준점을 찍으면 C++ 이 js_showTextInput 으로 연다.
        // 여기서 잡은 키는 window 로 올라가지 않게 (stopPropagation) - 안 그러면 C++ 키 핸들러가
        // 글자를 단축키로 먹고 preventDefault 해서 입력창에 글자가 안 찍힌다.
        var textInput = document.createElement('input');
        textInput.type = 'text';
        textInput.id = 'lot-text-input';
        textInput.style.position = 'fixed';
        textInput.style.left = '50%';
        textInput.style.transform = 'translateX(-50%)';
        textInput.style.bottom = '48px';
        textInput.style.zIndex = '11';
        textInput.style.width = '360px';
        textInput.style.padding = '6px 10px';
        textInput.style.fontFamily = 'monospace';
        textInput.style.fontSize = '14px';
        textInput.style.color = '#fff';
        textInput.style.backgroundColor = '#0d0d0d';
        textInput.style.border = '1px solid #00ff00';
        textInput.style.borderRadius = '4px';
        textInput.style.display = 'none';
        textInput.addEventListener('keydown', function(e) {
            e.stopPropagation();
            if (e.key === 'Enter') {
                var ptr = stringToNewUTF8(textInput.value);
                textInput.style.display = 'none';
                textInput.value = '';
                _lot_onTextEntered(ptr);
                _free(ptr);
            } else if (e.key === 'Escape') {
                textInput.style.display = 'none';
                textInput.value = '';
                _lot_onTextCancelled();
            }
        });
        textInput.addEventListener('keyup', function(e) { e.stopPropagation(); });
        textInput.addEventListener('keypress', function(e) { e.stopPropagation(); });
        document.body.appendChild(textInput);
        dom.textInput = textInput;

        document.body.appendChild(bar);
        document.body.appendChild(hint);
        dom.toolbar = bar;
        dom.toolbarButtons = buttons;
        dom.toolbarStyle = styleButton;
        dom.hint = hint;
    },

    // 선종류 목록 (한 번). 드롭다운을 채우는 데 쓴다.
    js_setLinetypes__deps: ['$UTF8ToString'],
    js_setLinetypes: function(jsonPtr) {
        if (!Module.lotDom) Module.lotDom = {};
        try { Module.lotDom.linetypes = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) {}
    },

    // C++ 이 층 목록을 밀어 넣는다 (바뀔 때만). json: {current, layers:[{id,name,visible,locked,color,count}]}
    js_setLayers__deps: ['$UTF8ToString'],
    js_setLayers: function(jsonPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.layerPanel) return 0;
        // JSON.parse 결과의 속성은 아래에서 대괄호로 읽는다 - Closure 가 점 표기 이름을
        // 바꿔버리면 JSON 키와 어긋나기 때문이다 (Module['lotDom'] 과 같은 이유).
        var state;
        try { state = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) { return 0; }

        var panel = dom.layerPanel;
        panel.textContent = '';

        var head = document.createElement('div');
        head.style.display = 'flex';
        head.style.justifyContent = 'space-between';
        head.style.alignItems = 'center';
        head.style.padding = '0 2px 4px';
        var title = document.createElement('span');
        title.textContent = 'layers';
        title.style.color = '#666';
        title.style.fontSize = '10px';
        head.appendChild(title);

        // 색 입력 (네이티브 색 고르개). value 는 '#rrggbb'.
        var mkColorInput = function(value, title, onChange) {
            var c = document.createElement('input');
            c.type = 'color';
            c.value = value;
            c.title = title;
            c.style.width = '18px';
            c.style.height = '14px';
            c.style.padding = '0';
            c.style.border = '1px solid #555';
            c.style.background = 'none';
            c.style.cursor = 'pointer';
            c.style.flex = '0 0 auto';
            c.addEventListener('mousedown', function(e) { e.stopPropagation(); });
            c.addEventListener('keydown', function(e) { e.stopPropagation(); });
            c.addEventListener('input', function() { onChange(parseInt(c.value.slice(1), 16)); });
            return c;
        };

        // 선종류 드롭다운. byLayer 를 주면 맨 위에 'ByLayer' (값 -1) 를 넣는다.
        var mkLinetypeSelect = function(value, byLayer, title, onChange) {
            var sel = document.createElement('select');
            sel.title = title;
            sel.style.fontFamily = 'monospace';
            sel.style.fontSize = '10px';
            sel.style.color = '#00ff00';
            sel.style.backgroundColor = '#0d0d0d';
            sel.style.border = '1px solid #2a3a2a';
            sel.style.borderRadius = '3px';
            sel.style.maxWidth = '92px';
            var add = function(v, text) {
                var o = document.createElement('option');
                o.value = String(v);
                o.textContent = text;
                sel.appendChild(o);
            };
            if (byLayer) add(-1, 'ByLayer');
            (dom.linetypes || []).forEach(function(lt) { add(lt['id'], lt['name']); });
            if (value === -2) { add(-2, '--'); }          // 섞여 있음
            sel.value = String(value);
            sel.addEventListener('mousedown', function(e) { e.stopPropagation(); });
            sel.addEventListener('keydown', function(e) { e.stopPropagation(); });
            sel.addEventListener('change', function() { onChange(parseInt(sel.value, 10)); });
            return sel;
        };

        var mkButton = function(label, title, onClick) {
            var b = document.createElement('button');
            b.textContent = label;
            b.title = title;
            b.style.padding = '1px 5px';
            b.style.marginLeft = '2px';
            b.style.fontFamily = 'monospace';
            b.style.fontSize = '11px';
            b.style.color = '#00ff00';
            b.style.backgroundColor = '#0d0d0d';
            b.style.border = '1px solid #2a3a2a';
            b.style.borderRadius = '3px';
            b.style.cursor = 'pointer';
            b.addEventListener('mousedown', function(e) { e.preventDefault(); });
            b.addEventListener('click', onClick);
            return b;
        };

        head.appendChild(mkButton('+', 'New layer', function() { dom.layerCommand('new', 0, 0); }));
        panel.appendChild(head);

        state['layers'].forEach(function(l) {
            var row = document.createElement('div');
            row.style.display = 'flex';
            row.style.alignItems = 'center';
            row.style.gap = '2px';
            row.style.padding = '1px 2px';
            row.style.borderRadius = '3px';
            if (l['id'] === state['current']) row.style.backgroundColor = 'rgba(0, 255, 0, 0.15)';

            row.appendChild(mkColorInput(l['color'], 'Layer colour (objects set to ByLayer follow it)',
                function(v) { dom.layerCommand('layerColor', l['id'], v); }));

            // 이름: 누르면 현재 층 (새로 그리는 것이 여기로 들어간다)
            var name = document.createElement('span');
            name.textContent = l['name'] + ' (' + l['count'] + ')';
            name.title = 'Make current (new objects go here)';
            name.style.flex = '1 1 auto';
            name.style.overflow = 'hidden';
            name.style.textOverflow = 'ellipsis';
            name.style.whiteSpace = 'nowrap';
            name.style.padding = '0 4px';
            name.style.cursor = 'pointer';
            name.style.color = l['visible'] ? (l['id'] === state['current'] ? '#00ff00' : '#ddd') : '#666';
            name.addEventListener('mousedown', function(e) { e.preventDefault(); });
            name.addEventListener('click', function() { dom.layerCommand('current', l['id'], 0); });
            row.appendChild(name);

            row.appendChild(mkLinetypeSelect(l['linetype'], false, 'Layer linetype',
                function(v) { dom.layerCommand('layerLinetype', l['id'], v); }));

            if (l['id'] !== 0) {
                row.appendChild(mkButton(l['visible'] ? '\u25c9' : '\u25cb',
                                         l['visible'] ? 'Hide layer' : 'Show layer',
                                         function() { dom.layerCommand('visible', l['id'], l['visible'] ? 0 : 1); }));
                row.appendChild(mkButton(l['locked'] ? '\u25a0' : '\u25a1',
                                         l['locked'] ? 'Unlock layer' : 'Lock layer',
                                         function() { dom.layerCommand('locked', l['id'], l['locked'] ? 0 : 1); }));
                row.appendChild(mkButton('x', 'Delete layer (objects move to layer 0)',
                                         function() { dom.layerCommand('delete', l['id'], 0); }));
            }
            // 선택된 것을 이 층으로
            row.appendChild(mkButton('\u2190', 'Move the selection to this layer',
                                     function() { dom.layerCommand('assign', l['id'], 0); }));
            panel.appendChild(row);
        });

        // 선택의 선종류 (선택이 있을 때만)
        if (state['selectionLinetype'] !== -3) {
            var selRow = document.createElement('div');
            selRow.id = 'lot-layer-selection';  // 층 행이 아니다 - 테스트 도우미가 걸러낸다
            selRow.style.display = 'flex';
            selRow.style.alignItems = 'center';
            selRow.style.gap = '4px';
            selRow.style.marginTop = '4px';
            selRow.style.paddingTop = '4px';
            selRow.style.borderTop = '1px solid #333';
            var label = document.createElement('span');
            label.textContent = 'selection';
            label.style.color = '#888';
            label.style.fontSize = '10px';
            selRow.appendChild(label);
            selRow.appendChild(mkLinetypeSelect(state['selectionLinetype'], true,
                'Linetype of the selected objects',
                function(v) { dom.layerCommand('objectLinetype', 0, v); }));

            var rgb = state['selectionColor'];
            var hex = '#' + (rgb >= 0 ? rgb : 0xcccccc).toString(16).padStart(6, '0');
            selRow.appendChild(mkColorInput(hex,
                rgb === -2 ? 'Colour of the selected objects (mixed)' : 'Colour of the selected objects',
                function(v) { dom.layerCommand('objectColor', 0, v); }));

            var byLayer = state['selectionByLayer'] === 1;
            var bl = mkButton(byLayer ? '\u25c9 ByLayer' : '\u25cb ByLayer',
                'Follow the layer colour',
                function() { dom.layerCommand('objectByLayer', 0, byLayer ? 0 : 1); });
            if (byLayer) { bl.style.color = '#0d0d0d'; bl.style.backgroundColor = '#00ff00'; }
            selRow.appendChild(bl);
            panel.appendChild(selRow);
        }
        return 1;
    },

    js_showTextInput__deps: ['$UTF8ToString'],
    js_showTextInput: function(placeholderPtr, initialPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.textInput) return;
        dom.textInput.placeholder = UTF8ToString(placeholderPtr);
        dom.textInput.value = initialPtr ? UTF8ToString(initialPtr) : '';
        dom.textInput.style.display = 'block';
        dom.textInput.focus();
        dom.textInput.select();
    },

    js_hideTextInput: function() {
        var dom = Module.lotDom;
        if (!dom || !dom.textInput) return;
        dom.textInput.style.display = 'none';
        dom.textInput.value = '';
        dom.textInput.blur();
    },

    // C++ 이 상태가 바뀔 때 부른다. 값은 키보드 컨트롤러/카메라의 enum 그대로.
    //   gizmoMode 0/1/2, sketchTool -1/0/1/2, view -1 또는 CadViewType, fps 0/1,
    //   ortho 0/1, outline 0/1, canUndo/canRedo 0/1, hint = UTF-8 문자열 (빈 문자열이면 숨김)
    js_setToolbarState__deps: ['$UTF8ToString'],
    js_setToolbarState: function(gizmoMode, sketchTool, xformMode, view, fps, ortho, orthoTrack,
                                 gridSnap, outline, canUndo, canRedo, hintPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.toolbarButtons) return 0;  // 아직 툴바가 없다 - C++ 이 다음 프레임에 다시 보낸다
        var buttons = dom.toolbarButtons;
        var style = dom.toolbarStyle;

        var setOn = function(prefix, value) {
            for (var key in buttons) {
                if (key.indexOf(prefix + ':') === 0) {
                    style(buttons[key], key === prefix + ':' + value, true);
                }
            }
        };
        setOn('gizmo', gizmoMode);
        setOn('sketch', sketchTool);
        setOn('xform', xformMode);
        setOn('view', view);
        style(buttons['fps'], fps === 1, true);
        style(buttons['ortho'], ortho === 1, true);
        style(buttons['orthoTrack'], orthoTrack === 1, true);
        style(buttons['gridSnap'], gridSnap === 1, true);
        style(buttons['outline'], outline === 1, true);
        style(buttons['undo'], false, canUndo === 1);
        style(buttons['redo'], false, canRedo === 1);

        var text = UTF8ToString(hintPtr);
        dom.hint.textContent = text;
        dom.hint.style.display = text ? 'block' : 'none';
        return 1;
    },

});

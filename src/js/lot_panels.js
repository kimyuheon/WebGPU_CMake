/**
 * 패널과 대화상자 - 레이어 패널 · 문자 입력창 · 파일 열기/저장.
 *
 * 메뉴와 리본은 src/js/lot_ui.js 가 그린다. 여기는 '창' 성격의 것들이다.
 * 생김새는 같은 ImGui 다크 톤을 따른다.
 *
 * ⚠️ Closure(릴리스)가 점 표기 속성명을 바꾸므로 JSON 으로 오간 값은 대괄호로 읽는다.
 */

mergeInto(LibraryManager.library, {

    js_setupPanels__deps: ['lot_onToolbarKey', 'lot_saveScene', 'lot_saveDxf', 'lot_onLotFileLoaded',
                            'lot_onTextEntered', 'lot_onTextCancelled', 'lot_onLayerCommand',
                            'lot_onDxfFileLoaded',
                            '$stringToNewUTF8', '$UTF8ToString', 'malloc', 'free'],
    js_setupPanels: function() {
        if (!Module.lotDom) {
            Module.lotDom = {};
        }
        var dom = Module.lotDom;
        if (dom.layerPanel) return;
        // 캔버스는 상태바 아래에서 시작한다. 상태바가 아직 없으면 (호출 순서가 바뀌면) 0.
        var statusBarHeight = dom.statusBar ? dom.statusBar.offsetHeight : 0;

        // 열기 대화상자 하나 (숨김). 파일 선택창은 사용자 제스처로만 열리므로 버튼이 click() 한다.
        // 고른 파일의 확장자로 로더를 고른다 - 아래 openFile.
        var fileInput = document.createElement('input');
        fileInput.type = 'file';
        fileInput.accept = '.lot,.json,.dxf,.obj';
        fileInput.style.display = 'none';
        // name 은 탭 이름이 된다 (테스트 도구는 안 넘긴다).
        var withName = function(name, fn) {
            var namePtr = name ? stringToNewUTF8(name) : 0;
            fn(namePtr);
            if (namePtr) _free(namePtr);
        };
        // 옛 DXF 도면은 CP949 같은 코드페이지라 브라우저 TextDecoder 로 푼다
        // ($DWGCODEPAGE 를 앞부분에서 찾아 고른다). C++ 은 UTF-8 만 받는다.
        dom.dxfLoad = function(buffer, name) {
            var bytes = new Uint8Array(buffer);
            // 앞 4KB 만 아스키로 훑어 코드페이지를 찾는다
            var head = '';
            for (var i = 0; i < Math.min(bytes.length, 4096); ++i) head += String.fromCharCode(bytes[i]);
            var label = 'utf-8';
            // AutoCAD 2007(AC1021) 이후 DXF 는 $DWGCODEPAGE 가 ANSI_949 여도 본문이 UTF-8 이다.
            // 코드페이지를 믿으면 한글 층 이름/문자가 깨진다.
            var ver = head.match(/\$ACADVER\s*\r?\n\s*1\s*\r?\n\s*AC(\d+)/);
            var unicodeDxf = ver && parseInt(ver[1], 10) >= 1021;
            var m = unicodeDxf ? null : head.match(/\$DWGCODEPAGE\s*\r?\n\s*3\s*\r?\n\s*([A-Za-z0-9_]+)/);
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
            withName(name, function(n) { _lot_onDxfFileLoaded(ptr, utf8.length, n); });  // 버퍼 해제는 C++ 쪽
            return true;
        };
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
        dom.dxfSave = function() {
            var ptr = _lot_saveDxf();   // C++ 이 malloc 으로 잡아 준 DXF, 여기서 free
            if (!ptr) return '';
            var text = UTF8ToString(ptr);
            _free(ptr);
            return text;
        };
        dom.sceneLoad = function(text, name) {
            var bytes = new TextEncoder().encode(text);
            var ptr = _malloc(bytes.length);
            if (!ptr) { console.error('scene: out of memory (' + bytes.length + ' bytes)'); return false; }
            HEAPU8.set(bytes, ptr);
            withName(name, function(n) { _lot_onLotFileLoaded(ptr, bytes.length, n); });  // 버퍼 해제는 C++ 쪽
            return true;
        };

        // Closure 가 속성 이름을 줄이므로, 바깥(테스트 도구, 콘솔)에서 부를 이름은
        // 따옴표로 박아 둔다. 안에서는 dom.sceneSave 로 써도 같은 함수다.
        Module['lotDom'] = dom;
        dom['sceneSave'] = dom.sceneSave;
        dom['dxfSave'] = dom.dxfSave;
        dom['sceneLoad'] = dom.sceneLoad;

        // 확장자 -> 로더. .lot/.dxf 는 새 탭에 열리고, .obj 는 지금 씬에 얹힌다.
        var openFile = function(file) {
            var dot = file.name.lastIndexOf('.');
            var ext = dot >= 0 ? file.name.slice(dot + 1).toLowerCase() : '';
            var reader = new FileReader();
            reader.onerror = function() { console.error('open: could not read ' + file.name); };
            if (ext === 'lot' || ext === 'json') {
                reader.onload = function() { dom.sceneLoad(reader.result, file.name); };
                reader.readAsText(file);
            } else if (ext === 'dxf') {
                reader.onload = function() { dom.dxfLoad(reader.result, file.name); };
                reader.readAsArrayBuffer(file);
            } else if (ext === 'obj') {
                reader.onload = function() { if (dom.objLoad) dom.objLoad(reader.result); };
                reader.readAsArrayBuffer(file);
            } else {
                console.error('open: unsupported file type - ' + file.name + ' (.lot .json .dxf .obj)');
            }
        };
        fileInput.addEventListener('change', function() {
            var file = fileInput.files && fileInput.files[0];
            if (file) openFile(file);
            fileInput.value = '';  // 같은 파일을 다시 골라도 change 가 오도록
        });
        document.body.appendChild(fileInput);
        dom.fileInput = fileInput;

        // 만든 텍스트를 파일로 내려준다. a[download] 는 사용자 제스처 안에서만 열린다.
        var download = function(text, name, mime) {
            if (!text) return;
            var blob = new Blob([text], { type: mime });
            var url = URL.createObjectURL(blob);
            var a = document.createElement('a');
            a.href = url;
            a.download = name;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
        };

        // 버튼 동작 중 키가 아닌 것들. '@이름' 코드로 가리킨다.
        // 메뉴/리본의 '@이름' 명령이 부른다 (src/js/lot_ui.js 의 run)
        // ⚠️ 키는 따옴표로. run 이 C++ 이 준 문자열("open")로 찾는데, 따옴표가 없으면
        //    Closure(릴리스)가 키를 줄여 버려 아무것도 안 찾아진다 - 메뉴를 눌러도 파일
        //    대화상자가 안 뜨던 원인이다.
        var actions = dom.actions = {
            'open': function() { fileInput.click(); },
            // 파일 이름은 지금 탭 이름 (js_uiSetTabs 가 dom.docName 에 둔다)
            'saveLot': function() { download(dom.sceneSave(), (dom.docName || 'scene') + '.lot', 'application/json'); },
            'saveDxf': function() { download(dom.dxfSave(), (dom.docName || 'scene') + '.dxf', 'application/dxf'); },
        };

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
        textInput.style.fontFamily = 'inherit';
        textInput.style.fontSize = '14px';
        textInput.style.color = '#e6e6e6';
        textInput.style.backgroundColor = '#1b1b1b';
        textInput.style.border = '1px solid #4296fa';
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

        // 레이어 패널 (오른쪽 위). C++ 이 js_setLayers 로 목록을 밀어 넣는다.
        // 행: [색] 이름 (개수)  [눈] [자물쇠] [x].  이름을 누르면 현재 층이 된다 (새 객체가 여기로).
        var layerPanel = document.createElement('div');
        layerPanel.id = 'lot-layers';
        layerPanel.style.position = 'fixed';
        layerPanel.style.right = '12px';
        // 메뉴바 + 리본 아래에 붙인다 (높이는 lot_ui.js 가 알려준다)
        var uiH = (dom.uiHeight ? dom.uiHeight() : 0);
        layerPanel.style.top = (statusBarHeight + uiH + 8) + 'px';
        layerPanel.style.zIndex = '10';
        layerPanel.style.minWidth = '210px';
        layerPanel.style.maxHeight = 'calc(100vh - ' + (statusBarHeight + uiH + 90) + 'px)';
        layerPanel.style.overflowY = 'auto';
        layerPanel.style.padding = '4px';
        layerPanel.style.border = '1px solid #4a4a55';
        layerPanel.style.borderRadius = '4px';
        layerPanel.style.backgroundColor = 'rgba(15, 15, 15, 0.94)';
        layerPanel.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
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


        // 안내문 (화면 아래 가운데). 열린 도구의 다음 할 일 - 메뉴바 오른쪽에도 같은 글이 뜬다.
        var hint = document.createElement('div');
        hint.id = 'lot-hint';
        hint.style.position = 'fixed';
        hint.style.left = '50%';
        hint.style.transform = 'translateX(-50%)';
        hint.style.bottom = '40px';   // 하단 상태바(30px) 위
        hint.style.zIndex = '11';
        hint.style.padding = '5px 12px';
        hint.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
        hint.style.fontSize = '12px';
        hint.style.color = '#e6e6e6';
        hint.style.backgroundColor = 'rgba(15, 15, 15, 0.92)';
        hint.style.border = '1px solid #4a4a55';
        hint.style.borderRadius = '3px';
        hint.style.whiteSpace = 'nowrap';
        hint.style.pointerEvents = 'none';
        hint.style.display = 'none';
        document.body.appendChild(hint);
        dom.hint = hint;
    },

    // 선종류 목록 (한 번). 드롭다운을 채우는 데 쓴다.
    js_uiSetLinetypes__deps: ['$UTF8ToString'],
    js_uiSetLinetypes: function(jsonPtr) {
        if (!Module.lotDom) Module.lotDom = {};
        try { Module.lotDom.linetypes = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) {}
    },

    // C++ 이 층 목록을 밀어 넣는다 (바뀔 때만). json: {current, layers:[{id,name,visible,locked,color,count}]}
    js_uiSetLayers__deps: ['$UTF8ToString'],
    js_uiSetLayers: function(jsonPtr) {
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
        title.style.color = '#9a9aa2';
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
            sel.style.fontFamily = 'inherit';
            sel.style.fontSize = '10px';
            sel.style.color = '#e6e6e6';
            sel.style.backgroundColor = '#1b1b1b';
            sel.style.border = '1px solid #4a4a55';
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
            b.style.fontFamily = 'inherit';
            b.style.fontSize = '11px';
            b.style.color = '#e6e6e6';
            b.style.backgroundColor = '#1b1b1b';
            b.style.border = '1px solid #4a4a55';
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
            if (l['id'] === state['current']) row.style.backgroundColor = '#2b5fa8';

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
            name.style.color = l['visible'] ? '#e6e6e6' : '#6f6f78';
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
            label.style.color = '#9a9aa2';
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
            if (byLayer) { bl.style.color = '#ffffff'; bl.style.backgroundColor = '#2b5fa8'; bl.style.borderColor = '#4296fa'; }
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

});

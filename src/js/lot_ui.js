/**
 * 화면 UI - 메뉴바 · 리본 · 레이어 패널 · 입력창.
 *
 * 생김새는 Vulkan 쪽(ImGui 다크 테마)을 따른다: 어두운 회색 띠, 파란 강조,
 * 메뉴 항목 오른쪽에 회색 단축키, 리본은 탭(홈/2D/3D) + 아이콘 한 줄 + 그룹 캡션.
 * 탭 줄을 더블클릭하면 본문이 접힌다.
 *
 * 무엇을 보여줄지는 C++ 이 정한다 (src/ui/lot_main_menu.cpp, lot_ribbon.cpp):
 * 여기는 그 JSON 을 그리고, 누르면 명령이 들고 있는 키 코드를 되돌려 보낼 뿐이다.
 * 그래서 메뉴·리본·단축키가 갈라지지 않는다.
 *
 * ⚠️ Closure(릴리스 빌드)가 점 표기 속성명을 바꾸므로, JSON 으로 오간 값은 전부
 *    대괄호로 읽는다 (obj['label']).
 */

mergeInto(LibraryManager.library, {

    $LotUiTheme: {
        // ImGui 다크 테마에서 가져온 값들
        windowBg: '#0f0f0fee',
        menuBg: '#242424',
        ribbonBg: '#1b1b1b',
        border: '#4a4a55',
        text: '#e6e6e6',
        textDim: '#9a9aa2',
        accent: '#4296fa',
        accentDim: '#2b5fa8',
        hover: '#2f3b4d',
    },

    // ---------------------------------------------------------------- 뼈대
    js_uiInstall__deps: ['lot_onToolbarKey', 'lot_onLayerCommand', 'lot_onTextEntered',
                         'lot_onTextCancelled', 'lot_saveScene', 'lot_onLotFileLoaded',
                         'lot_onDxfFileLoaded', 'lot_onCommandLine', '$LotUiTheme',
                         '$stringToNewUTF8', '$UTF8ToString', 'malloc', 'free'],
    js_uiInstall: function(menuPtr, ribbonPtr, namesPtr) {
        if (!Module.lotDom) Module.lotDom = {};
        var dom = Module.lotDom;
        if (!dom.statusBar) return 0;          // 캔버스/상태바가 아직
        if (dom.uiRoot) return 1;              // 이미 만들었다

        var T = LotUiTheme;
        var menus, ribbon, commandNames;
        try {
            menus = JSON.parse(UTF8ToString(menuPtr));
            ribbon = JSON.parse(UTF8ToString(ribbonPtr));
            commandNames = JSON.parse(UTF8ToString(namesPtr));
        } catch (e) { return 0; }

        var top = dom.statusBar.offsetHeight;
        dom.stateItems = {};   // 상태 키 -> [엘리먼트]
        dom.idItems = {};      // 명령 id -> [엘리먼트]

        // 명령 실행: 키 코드를 C++ 로 (키보드와 같은 경로), '@' 로 시작하면 JS 동작.
        var run = function(cmd) {
            var key = cmd['key'];
            if (key.charAt(0) === '@') {
                var fn = dom.actions[key.substring(1)];
                if (fn) fn();
                return;
            }
            var ptr = stringToNewUTF8(key);
            _lot_onToolbarKey(ptr, cmd['ctrl'] | 0);
            _free(ptr);
        };

        // 상태 표시를 위해 엘리먼트를 등록해 둔다
        var register = function(cmd, el, onStyle) {
            // ⚠️ el.dataset.cmd 로 쓰면 Closure 가 'cmd' 를 줄여버린다 (dataset 은 extern 이지만
            //    그 아래 속성명은 아니다). 속성 이름을 문자열로 박는 setAttribute 를 쓴다.
            el.setAttribute('data-cmd', cmd['id']);
            el.__lotStyle = onStyle;
            (dom.idItems[cmd['id']] = dom.idItems[cmd['id']] || []).push(el);
            if (cmd['state']) {
                (dom.stateItems[cmd['state']] = dom.stateItems[cmd['state']] || []).push(el);
            }
        };

        var root = document.createElement('div');
        root.id = 'lot-ui';
        root.style.position = 'fixed';
        root.style.left = '0';
        root.style.top = top + 'px';
        root.style.width = '100%';
        root.style.zIndex = '10';
        root.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
        root.style.fontSize = '13px';
        root.style.color = T.text;
        root.style.userSelect = 'none';

        // ── 메뉴바 ─────────────────────────────────────────────
        var bar = document.createElement('div');
        bar.id = 'lot-toolbar';   // 테스트 도구가 명령 단추를 여기서 찾는다
        bar.style.display = 'flex';
        bar.style.alignItems = 'stretch';
        bar.style.background = T.menuBg;
        bar.style.borderBottom = '1px solid ' + T.border;
        bar.style.height = '26px';

        var openMenu = null;
        var closeMenu = function() {
            if (openMenu) { openMenu.style.display = 'none'; openMenu = null; }
        };
        document.addEventListener('mousedown', function(e) {
            if (!root.contains(e.target)) closeMenu();
        });

        menus.forEach(function(m) {
            var wrap = document.createElement('div');
            wrap.style.position = 'relative';

            var title = document.createElement('div');
            title.textContent = m['title'];
            title.style.padding = '4px 10px';
            title.style.cursor = 'default';
            title.style.lineHeight = '18px';

            var drop = document.createElement('div');
            drop.style.position = 'absolute';
            drop.style.left = '0';
            drop.style.top = '26px';
            drop.style.minWidth = '210px';
            drop.style.background = T.windowBg;
            drop.style.border = '1px solid ' + T.border;
            drop.style.padding = '3px';
            drop.style.display = 'none';
            drop.style.boxShadow = '0 4px 10px rgba(0,0,0,0.5)';

            title.addEventListener('mouseenter', function() {
                title.style.background = T.hover;
                if (openMenu && openMenu !== drop) { closeMenu(); drop.style.display = 'block'; openMenu = drop; }
            });
            title.addEventListener('mouseleave', function() { title.style.background = 'transparent'; });
            title.addEventListener('mousedown', function(e) {
                e.preventDefault();
                if (openMenu === drop) { closeMenu(); return; }
                closeMenu();
                drop.style.display = 'block';
                openMenu = drop;
            });

            m['items'].forEach(function(cmd) {
                if (cmd['sep']) {
                    var hr = document.createElement('div');
                    hr.style.height = '1px';
                    hr.style.margin = '3px 2px';
                    hr.style.background = T.border;
                    drop.appendChild(hr);
                }
                var row = document.createElement('button');
                row.title = cmd['tip'];
                row.style.display = 'flex';
                row.style.justifyContent = 'space-between';
                row.style.alignItems = 'center';
                row.style.gap = '18px';
                row.style.width = '100%';
                row.style.padding = '4px 8px';
                row.style.border = 'none';
                row.style.background = 'transparent';
                row.style.color = T.text;
                row.style.font = 'inherit';
                row.style.textAlign = 'left';
                row.style.cursor = 'default';

                var name = document.createElement('span');
                name.textContent = cmd['label'];
                var sc = document.createElement('span');
                sc.textContent = cmd['shortcut'];
                sc.style.color = T.textDim;
                sc.style.fontSize = '11px';
                row.appendChild(name);
                row.appendChild(sc);

                row.addEventListener('mouseenter', function() {
                    if (!row.disabled) row.style.background = T.accentDim;
                });
                row.addEventListener('mouseleave', function() {
                    row.style.background = row.__lotOn ? T.accentDim : 'transparent';
                });
                row.addEventListener('mousedown', function(e) { e.preventDefault(); });
                row.addEventListener('click', function() { closeMenu(); run(cmd); });

                // 켜짐: 왼쪽에 체크 대신 파란 배경 (ImGui 의 selected 항목과 같은 느낌)
                register(cmd, row, function(on, enabled) {
                    row.__lotOn = on;
                    row.setAttribute('data-on', on ? '1' : '0');  // 바깥(테스트)에서 보는 값
                    row.style.background = on ? T.accentDim : 'transparent';
                    row.disabled = !enabled;
                    row.style.color = enabled ? T.text : T.textDim;
                });
                drop.appendChild(row);
            });

            wrap.appendChild(title);
            wrap.appendChild(drop);
            bar.appendChild(wrap);
        });

        // 메뉴바 오른쪽 끝: 현재 도구 안내 (ImGui 상태 문구 자리)
        var barHint = document.createElement('div');
        barHint.id = 'lot-bar-hint';
        barHint.style.marginLeft = 'auto';
        barHint.style.padding = '4px 12px';
        barHint.style.color = T.textDim;
        barHint.style.fontSize = '12px';
        barHint.style.whiteSpace = 'nowrap';
        barHint.style.overflow = 'hidden';
        barHint.style.textOverflow = 'ellipsis';
        barHint.style.maxWidth = '55%';
        bar.appendChild(barHint);
        dom.barHint = barHint;

        // ── 리본 ───────────────────────────────────────────────
        var ribbonBox = document.createElement('div');
        ribbonBox.style.background = T.ribbonBg;
        ribbonBox.style.borderBottom = '1px solid ' + T.border;

        var tabRow = document.createElement('div');
        tabRow.style.display = 'flex';
        tabRow.style.background = T.menuBg;
        tabRow.style.borderBottom = '1px solid ' + T.border;

        var body = document.createElement('div');
        body.style.display = 'flex';
        body.style.alignItems = 'flex-start';
        body.style.gap = '2px';
        body.style.padding = '3px 6px';

        var tabs = [];
        var panels = [];
        var activeTab = 0;
        var collapsed = false;

        var applyTabs = function() {
            tabs.forEach(function(t, i) {
                t.style.color = (i === activeTab) ? T.text : T.textDim;
                t.style.borderBottom = (i === activeTab && !collapsed)
                    ? '2px solid ' + T.accent : '2px solid transparent';
            });
            panels.forEach(function(p, i) { p.style.display = (i === activeTab) ? 'flex' : 'none'; });
            body.style.display = collapsed ? 'none' : 'flex';
        };

        ribbon.forEach(function(tabDef, index) {
            var tab = document.createElement('div');
            tab.textContent = tabDef['tab'];
            tab.style.padding = '3px 14px';
            tab.style.cursor = 'default';
            tab.style.fontSize = '12px';
            tab.addEventListener('mousedown', function(e) { e.preventDefault(); });
            tab.addEventListener('click', function() { activeTab = index; collapsed = false; applyTabs(); });
            tab.addEventListener('dblclick', function() { collapsed = !collapsed; applyTabs(); });
            tabRow.appendChild(tab);
            tabs.push(tab);

            var panel = document.createElement('div');
            panel.style.display = 'none';
            panel.style.gap = '2px';
            panel.style.alignItems = 'flex-start';

            tabDef['groups'].forEach(function(g, gi) {
                var group = document.createElement('div');
                group.style.display = 'flex';
                group.style.flexDirection = 'column';
                group.style.alignItems = 'center';
                group.style.padding = '0 8px';
                if (gi > 0) group.style.borderLeft = '1px solid ' + T.border;

                var icons = document.createElement('div');
                icons.style.display = 'flex';
                icons.style.gap = '2px';

                g['items'].forEach(function(cmd) {
                    var b = document.createElement('button');
                    b.title = cmd['tip'] + (cmd['shortcut'] ? '  (' + cmd['shortcut'] + ')' : '');
                    b.style.display = 'flex';
                    b.style.flexDirection = 'column';
                    b.style.alignItems = 'center';
                    b.style.gap = '1px';
                    b.style.width = '46px';
                    b.style.padding = '3px 2px';
                    b.style.border = '1px solid transparent';
                    b.style.borderRadius = '3px';
                    b.style.background = 'transparent';
                    b.style.color = T.text;
                    b.style.font = 'inherit';
                    b.style.cursor = 'default';

                    var glyph = document.createElement('span');
                    glyph.textContent = cmd['icon'] || cmd['label'].charAt(0);
                    glyph.style.fontSize = '15px';
                    glyph.style.lineHeight = '17px';
                    var cap = document.createElement('span');
                    cap.textContent = cmd['label'];
                    cap.style.fontSize = '10px';
                    cap.style.color = T.textDim;
                    b.appendChild(glyph);
                    b.appendChild(cap);

                    b.addEventListener('mouseenter', function() {
                        if (!b.disabled && !b.__lotOn) b.style.background = T.hover;
                    });
                    b.addEventListener('mouseleave', function() {
                        b.style.background = b.__lotOn ? T.accentDim : 'transparent';
                    });
                    b.addEventListener('mousedown', function(e) { e.preventDefault(); });
                    b.addEventListener('click', function() { closeMenu(); run(cmd); });

                    register(cmd, b, function(on, enabled) {
                        b.__lotOn = on;
                        b.setAttribute('data-on', on ? '1' : '0');
                        b.style.background = on ? T.accentDim : 'transparent';
                        b.style.borderColor = on ? T.accent : 'transparent';
                        b.disabled = !enabled;
                        b.style.opacity = enabled ? '1' : '0.45';
                    });
                    icons.appendChild(b);
                });

                var caption = document.createElement('div');
                caption.textContent = g['caption'];
                caption.style.fontSize = '10px';
                caption.style.color = T.textDim;
                caption.style.marginTop = '1px';

                group.appendChild(icons);
                group.appendChild(caption);
                panel.appendChild(group);
            });

            panels.push(panel);
            body.appendChild(panel);
        });

        ribbonBox.appendChild(tabRow);
        ribbonBox.appendChild(body);
        root.appendChild(bar);
        root.appendChild(ribbonBox);

        // ── 도면 탭 (리본 아래, 도면 바로 위 - AutoCAD 의 파일 탭 자리) ──
        // 내용은 js_uiSetTabs 가 채운다. 하나뿐이어도 보인다 - 높이가 바뀌면 아래 패널이 들썩인다.
        var docTabs = document.createElement('div');
        docTabs.id = 'lot-doc-tabs';
        docTabs.style.display = 'flex';
        docTabs.style.alignItems = 'stretch';
        docTabs.style.height = '24px';
        docTabs.style.background = T.menuBg;
        docTabs.style.borderBottom = '1px solid ' + T.border;
        docTabs.style.overflowX = 'auto';
        docTabs.style.overflowY = 'hidden';
        root.appendChild(docTabs);
        dom.docTabs = docTabs;
        document.body.appendChild(root);
        applyTabs();

        // ── 명령행 (화면 아래) ────────────────────────────────
        // AutoCAD 처럼 이름을 쳐서 명령을 부른다. 캔버스의 단축키와 섞이지 않도록
        // 키 이벤트를 여기서 멈춘다 (stopPropagation) - 문자 입력창과 같은 이유다.
        var cmdBox = document.createElement('div');
        cmdBox.id = 'lot-cmdline';
        cmdBox.style.position = 'fixed';
        cmdBox.style.left = '0';
        cmdBox.style.right = '0';
        cmdBox.style.bottom = '0';
        cmdBox.style.zIndex = '11';
        cmdBox.style.display = 'flex';
        cmdBox.style.alignItems = 'center';
        cmdBox.style.gap = '6px';
        cmdBox.style.padding = '3px 8px';
        cmdBox.style.background = T.menuBg;
        cmdBox.style.borderTop = '1px solid ' + T.border;
        cmdBox.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
        cmdBox.style.fontSize = '12px';

        var prompt = document.createElement('span');
        prompt.textContent = '명령:';
        prompt.style.color = T.textDim;
        cmdBox.appendChild(prompt);

        var cmdInput = document.createElement('input');
        cmdInput.type = 'text';
        cmdInput.id = 'lot-cmdline-input';
        cmdInput.title = '명령 이름을 치고 Enter (line, c, move, zoom …). Space 로 여기에 커서';
        cmdInput.style.flex = '1 1 auto';
        cmdInput.style.padding = '2px 6px';
        cmdInput.style.color = T.text;
        cmdInput.style.background = '#101010';
        cmdInput.style.border = '1px solid ' + T.border;
        cmdInput.style.borderRadius = '2px';
        cmdInput.style.font = 'inherit';
        cmdInput.style.outline = 'none';
        cmdBox.appendChild(cmdInput);

        var suggest = document.createElement('span');
        suggest.style.color = T.textDim;
        suggest.style.whiteSpace = 'nowrap';
        suggest.style.maxWidth = '40%';
        suggest.style.overflow = 'hidden';
        cmdBox.appendChild(suggest);

        var history = [];
        var histIndex = -1;

        var updateSuggest = function() {
            var v = cmdInput.value.trim().toLowerCase();
            if (!v) { suggest.textContent = ''; return; }
            var hits = commandNames.filter(function(n) { return n.indexOf(v) === 0; }).slice(0, 6);
            suggest.textContent = hits.length ? hits.join('  ') : '?';
        };

        cmdInput.addEventListener('input', updateSuggest);
        cmdInput.addEventListener('keydown', function(e) {
            e.stopPropagation();
            if (e.key === 'Enter') {
                var text = cmdInput.value.trim();
                // 빈 Enter 는 직전 명령 되풀이 (AutoCAD 관례)
                if (!text && history.length) text = history[history.length - 1];
                if (text) {
                    history.push(text);
                    histIndex = history.length;
                    var ptr = stringToNewUTF8(text);
                    _lot_onCommandLine(ptr);
                    _free(ptr);
                }
                cmdInput.value = '';
                suggest.textContent = '';
            } else if (e.key === 'Tab') {
                e.preventDefault();
                var v = cmdInput.value.trim().toLowerCase();
                var hit = commandNames.filter(function(n) { return n.indexOf(v) === 0; })[0];
                if (hit) { cmdInput.value = hit; updateSuggest(); }
            } else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
                e.preventDefault();
                if (!history.length) return;
                histIndex += (e.key === 'ArrowUp') ? -1 : 1;
                if (histIndex < 0) histIndex = 0;
                if (histIndex >= history.length) { histIndex = history.length; cmdInput.value = ''; return; }
                cmdInput.value = history[histIndex];
                updateSuggest();
            } else if (e.key === 'Escape') {
                cmdInput.value = '';
                suggest.textContent = '';
                cmdInput.blur();
            }
        });
        cmdInput.addEventListener('keyup', function(e) { e.stopPropagation(); });
        cmdInput.addEventListener('keypress', function(e) { e.stopPropagation(); });

        // Space 로 명령행에 커서 (AutoCAD 의 스페이스 = 명령 입력과 같은 자리).
        // 다른 입력창에 커서가 있을 때는 건드리지 않는다.
        window.addEventListener('keydown', function(e) {
            if (e.code !== 'Space') return;
            var t = document.activeElement;
            if (t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA')) return;
            e.preventDefault();
            cmdInput.focus();
        });

        document.body.appendChild(cmdBox);
        dom.cmdInput = cmdInput;
        dom['commandRun'] = function(text) {   // 테스트 도구가 부른다 (Closure 이름 고정)
            var ptr = stringToNewUTF8(text);
            _lot_onCommandLine(ptr);
            _free(ptr);
        };

        dom.uiRoot = root;
        dom.uiHeight = function() { return root.offsetHeight; };
        return 1;
    },

    // ---------------------------------------------------------------- 도면 탭
    // json: {active, tabs:[{name, modified}]}. 누르면 바꾸고, x 는 닫고, + 는 새 도면.
    // 고친 도면을 x 로 닫을 때만 묻는다 (명령행 close 는 묻지 않는다 - 친 사람이 안다).
    js_uiSetTabs__deps: ['lot_onDocumentTab', 'lot_onToolbarKey', '$LotUiTheme',
                         '$stringToNewUTF8', '$UTF8ToString', 'free'],
    js_uiSetTabs: function(jsonPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.docTabs) return 0;
        var s;
        try { s = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) { return 0; }
        var T = LotUiTheme;
        var active = s['active'];
        var tabs = s['tabs'];

        var send = function(action, index) {
            var ptr = stringToNewUTF8(action);
            _lot_onDocumentTab(ptr, index);
            _free(ptr);
        };

        var strip = dom.docTabs;
        strip.textContent = '';
        tabs.forEach(function(tab, i) {
            var on = (i === active);
            var el = document.createElement('div');
            el.setAttribute('data-doc-tab', String(i));
            el.setAttribute('data-on', on ? '1' : '0');
            el.title = tab['name'] + (tab['modified'] ? ' (저장 안 됨)' : '');
            el.style.display = 'flex';
            el.style.alignItems = 'center';
            el.style.gap = '6px';
            el.style.padding = '0 6px 0 12px';
            el.style.fontSize = '12px';
            el.style.cursor = 'default';
            el.style.whiteSpace = 'nowrap';
            el.style.color = on ? T.text : T.textDim;
            el.style.background = on ? T.ribbonBg : 'transparent';
            el.style.borderRight = '1px solid ' + T.border;
            el.style.borderTop = '2px solid ' + (on ? T.accent : 'transparent');

            var label = document.createElement('span');
            // 고친 도면은 이름 뒤에 점 (ImGui 의 UnsavedDocument 표시와 같다)
            label.textContent = tab['name'] + (tab['modified'] ? ' ●' : '');
            el.appendChild(label);

            var x = document.createElement('span');
            x.textContent = '×';
            x.title = '닫기';
            x.setAttribute('data-doc-close', String(i));
            x.style.padding = '0 4px';
            x.style.borderRadius = '2px';
            x.style.color = T.textDim;
            x.addEventListener('mouseenter', function() { x.style.background = T.hover; x.style.color = T.text; });
            x.addEventListener('mouseleave', function() { x.style.background = 'transparent'; x.style.color = T.textDim; });
            x.addEventListener('mousedown', function(e) { e.preventDefault(); e.stopPropagation(); });
            x.addEventListener('click', function(e) {
                e.stopPropagation();
                if (tab['modified'] && !window.confirm('"' + tab['name'] + '" 은(는) 저장하지 않았습니다. 닫을까요?')) return;
                send('close', i);
            });
            el.appendChild(x);

            el.addEventListener('mouseenter', function() { if (!on) el.style.background = T.hover; });
            el.addEventListener('mouseleave', function() { if (!on) el.style.background = 'transparent'; });
            el.addEventListener('mousedown', function(e) { e.preventDefault(); });
            el.addEventListener('click', function() { if (!on) send('activate', i); });
            strip.appendChild(el);
        });

        var plus = document.createElement('div');
        plus.textContent = '+';
        plus.title = '새 도면';
        plus.id = 'lot-doc-new';
        plus.style.padding = '0 12px';
        plus.style.display = 'flex';
        plus.style.alignItems = 'center';
        plus.style.cursor = 'default';
        plus.style.color = T.textDim;
        plus.addEventListener('mouseenter', function() { plus.style.background = T.hover; plus.style.color = T.text; });
        plus.addEventListener('mouseleave', function() { plus.style.background = 'transparent'; plus.style.color = T.textDim; });
        plus.addEventListener('mousedown', function(e) { e.preventDefault(); });
        plus.addEventListener('click', function() {
            var ptr = stringToNewUTF8('#newDoc');
            _lot_onToolbarKey(ptr, 0);
            _free(ptr);
        });
        strip.appendChild(plus);

        // 저장할 때 내려받는 파일 이름 (lot_panels.js 의 saveLot / saveDxf)
        dom.docName = (tabs[active] && tabs[active]['name']) || 'scene';
        return 1;
    },

    // ---------------------------------------------------------------- 상태
    js_uiSetState__deps: ['$UTF8ToString', '$LotUiTheme'],
    js_uiSetState: function(jsonPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.stateItems) return 0;
        var s;
        try { s = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) { return 0; }

        var on = {};
        s['active'].forEach(function(k) { on[k] = true; });
        var off = {};
        s['disabled'].forEach(function(k) { off[k] = true; });

        Object.keys(dom.stateItems).forEach(function(key) {
            dom.stateItems[key].forEach(function(el) {
                el.__lotStyle(!!on[key], !off[key]);
            });
        });
        // 상태 키가 없는 명령도 사용 불가 표시를 받을 수 있다 (실행 취소 등)
        ['undo', 'redo'].forEach(function(key) {
            (dom.idItems['edit.' + key] || []).forEach(function(el) {
                el.__lotStyle(false, !off[key]);
            });
        });

        if (dom.barHint) dom.barHint.textContent = s['hint'];
        if (dom.hint) {
            dom.hint.textContent = s['hint'];
            dom.hint.style.display = s['hint'] ? 'block' : 'none';
        }
        return 1;
    },

});

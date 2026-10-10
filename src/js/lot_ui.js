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
                         '$stringToNewUTF8', '$UTF8ToString', 'malloc', 'free', '$LotCmdLog', '$LotRibbon', 'lot_featureDimsJson'],
    js_uiInstall: function(menuPtr, ribbonPtr, namesPtr, statusPtr) {
        if (!Module.lotDom) Module.lotDom = {};
        var dom = Module.lotDom;
        if (!dom.statusBar) return 0;          // 캔버스/상태바가 아직
        if (dom.uiRoot) return 1;              // 이미 만들었다

        var T = LotUiTheme;
        var menus, ribbon, commandNames, statusCmds;
        try {
            menus = JSON.parse(UTF8ToString(menuPtr));
            ribbon = JSON.parse(UTF8ToString(ribbonPtr));
            commandNames = JSON.parse(UTF8ToString(namesPtr));
            statusCmds = JSON.parse(UTF8ToString(statusPtr));
        } catch (e) { return 0; }

        var top = dom.statusBar.offsetHeight;
        dom.stateItems = {};   // 상태 키 -> [엘리먼트]
        dom.idItems = {};      // 명령 id -> [엘리먼트]

        // 명령 실행: 키 코드를 C++ 로 (키보드와 같은 경로), '@' 로 시작하면 JS 동작.
        var run = function(cmd) {
            var key = cmd['key'];
            if (key.indexOf('@ribbon:') === 0) { setRibbonRows(parseInt(key.substring(8), 10)); return; }
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
        // 다시 만든 리본 단추를 상태 목록에서 뺀다
        var unregister = function(el) {
            var drop = function(map) {
                Object.keys(map).forEach(function(k) { map[k] = map[k].filter(function(x) { return x !== el; }); });
            };
            drop(dom.idItems);
            drop(dom.stateItems);
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

        // ── 리본 (lot_ribbon.js) ── 한 줄 / 두 줄 / 세 줄
        var rib = LotRibbon.build(ribbon, { register: register, unregister: unregister, run: run, closeMenu: closeMenu });
        // 뷰 메뉴의 '리본: N 줄' 항목에 지금 것을 표시
        var markRibbonRows = function() {
            [1, 2, 3].forEach(function(n) {
                (dom.idItems['view.ribbon' + n] || []).forEach(function(el) { el.__lotStyle(rib.rows() === n, true); });
            });
        };
        var setRibbonRows = function(n) {
            if (n === rib.rows()) return;
            rib.setRows(n);
            markRibbonRows();
            if (dom.applyUiState && dom.lastUiState) dom.applyUiState(dom.lastUiState);   // 새 단추에 켜짐 표시
        };

        root.appendChild(bar);
        root.appendChild(rib.box);

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
        markRibbonRows();

        // ── 하단 상태바: 명령행 + 토글 단추 ─────────────────────
        // [        명령: [            ][^]        [치수][그리드]...[객체스냅]]
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
        cmdBox.style.height = '30px';
        cmdBox.style.boxSizing = 'border-box';
        cmdBox.style.padding = '0 8px';
        cmdBox.style.background = T.menuBg;
        cmdBox.style.borderTop = '1px solid ' + T.border;
        cmdBox.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
        cmdBox.style.fontSize = '12px';
        cmdBox.style.userSelect = 'none';

        // 왼쪽 칸: 명령행을 가운데쯤에 (남는 폭을 차지하고 그 안에서 가운데 정렬)
        var cmdArea = document.createElement('div');
        cmdArea.style.flex = '1 1 auto';
        cmdArea.style.minWidth = '0';
        cmdArea.style.display = 'flex';
        cmdArea.style.alignItems = 'center';
        cmdArea.style.justifyContent = 'center';
        cmdArea.style.gap = '6px';
        cmdArea.style.position = 'relative';
        cmdBox.appendChild(cmdArea);

        var prompt = document.createElement('span');
        prompt.textContent = '명령:';
        prompt.style.color = T.textDim;
        prompt.style.whiteSpace = 'nowrap';
        cmdArea.appendChild(prompt);

        var cmdInput = document.createElement('input');
        cmdInput.type = 'text';
        cmdInput.id = 'lot-cmdline-input';
        cmdInput.title = '명령 이름을 치고 Enter (line, c, move, zoom …). Space 로 여기에 커서';
        cmdInput.style.flex = '0 1 420px';
        cmdInput.style.minWidth = '80px';
        cmdInput.style.padding = '2px 6px';
        cmdInput.style.color = T.text;
        cmdInput.style.background = '#101010';
        cmdInput.style.border = '1px solid ' + T.border;
        cmdInput.style.borderRadius = '2px';
        cmdInput.style.font = 'inherit';
        cmdInput.style.outline = 'none';
        cmdArea.appendChild(cmdInput);

        // 상태바 단추 공용 모양
        var mkBarButton = function(text, title) {
            var b = document.createElement('button');
            b.textContent = text;
            b.title = title;
            b.style.padding = '2px 10px';
            b.style.height = '22px';
            b.style.border = '1px solid ' + T.border;
            b.style.borderRadius = '2px';
            b.style.background = '#2a2a2a';
            b.style.color = T.textDim;
            b.style.font = 'inherit';
            b.style.whiteSpace = 'nowrap';
            b.style.cursor = 'default';
            b.addEventListener('mousedown', function(e) { e.preventDefault(); });   // 포커스를 뺏지 않게
            return b;
        };

        // ^ : 지난 명령 목록. 입력 중이면 같은 자리에 자동완성 후보가 뜬다.
        var histButton = mkBarButton('^', '지난 명령');
        histButton.id = 'lot-cmdline-history';
        histButton.style.padding = '2px 7px';
        cmdArea.appendChild(histButton);

        var popup = document.createElement('div');
        popup.id = 'lot-cmdline-popup';
        popup.style.position = 'absolute';
        popup.style.bottom = '28px';
        popup.style.minWidth = '220px';
        popup.style.maxHeight = '260px';
        popup.style.overflowY = 'auto';
        popup.style.background = T.windowBg;
        popup.style.border = '1px solid ' + T.border;
        popup.style.boxShadow = '0 -4px 10px rgba(0,0,0,0.5)';
        popup.style.padding = '3px';
        popup.style.display = 'none';
        cmdArea.appendChild(popup);

        var history = [];
        var histIndex = -1;

        var runText = function(text) {
            history.push(text);
            histIndex = history.length;
            LotCmdLog.submitted(text, !!dom.lastHint);   // 기록창 (lot_cmd_log.js) - 도구가 열려 있으면 값으로
            var ptr = stringToNewUTF8(text);
            _lot_onCommandLine(ptr);
            _free(ptr);
        };
        var hidePopup = function() { popup.style.display = 'none'; popup.__lotKind = ''; };
        // 목록을 띄운다. 항목을 누르면 pick(그 글자).
        var showPopup = function(kind, items, pick) {
            popup.textContent = '';
            if (!items.length) { hidePopup(); return; }
            items.forEach(function(text) {
                var row = document.createElement('div');
                row.textContent = text;
                row.style.padding = '3px 8px';
                row.style.color = T.text;
                row.style.whiteSpace = 'nowrap';
                row.addEventListener('mouseenter', function() { row.style.background = T.accentDim; });
                row.addEventListener('mouseleave', function() { row.style.background = 'transparent'; });
                row.addEventListener('mousedown', function(e) { e.preventDefault(); });
                row.addEventListener('click', function() { hidePopup(); pick(text); });
                popup.appendChild(row);
            });
            // 입력창 왼쪽 끝에 맞춘다
            popup.style.left = cmdInput.offsetLeft + 'px';
            popup.style.display = 'block';
            popup.__lotKind = kind;
            popup.scrollTop = popup.scrollHeight;   // 최근 것이 아래 (입력창 바로 위)
        };

        histButton.addEventListener('click', function() {
            if (popup.__lotKind === 'history') { hidePopup(); return; }
            // 같은 명령은 마지막 한 번만, 최근 것이 아래로
            var seen = {};
            var recent = [];
            for (var i = history.length - 1; i >= 0 && recent.length < 15; --i) {
                if (seen[history[i]]) continue;
                seen[history[i]] = true;
                recent.unshift(history[i]);
            }
            if (!recent.length) recent = ['(아직 친 명령이 없습니다)'];
            showPopup('history', recent, function(text) {
                if (seen[text]) runText(text);
            });
        });
        document.addEventListener('mousedown', function(e) {
            if (!cmdArea.contains(e.target)) hidePopup();
        });

        var updateSuggest = function() {
            var v = cmdInput.value.trim().toLowerCase();
            if (!v) { if (popup.__lotKind === 'suggest') hidePopup(); return; }
            var hits = commandNames.filter(function(n) { return n.indexOf(v) === 0; }).slice(0, 8);
            showPopup('suggest', hits, function(text) { cmdInput.value = text; cmdInput.focus(); });
        };

        cmdInput.addEventListener('input', updateSuggest);
        cmdInput.addEventListener('blur', function() { if (popup.__lotKind === 'suggest') hidePopup(); });
        cmdInput.addEventListener('keydown', function(e) {
            e.stopPropagation();
            if (e.key === 'Enter') {
                var text = cmdInput.value.trim();
                // 빈 Enter 는 직전 명령 되풀이 (AutoCAD 관례)
                if (!text && history.length) text = history[history.length - 1];
                if (text) runText(text);
                cmdInput.value = '';
                hidePopup();
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
                hidePopup();
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

        // 오른쪽 칸: 토글 단추들 (C++ 의 LotMainMenu::statusBarJson). 켜지면 파랗게.
        var toggles = document.createElement('div');
        toggles.id = 'lot-status-toggles';
        toggles.style.display = 'flex';
        toggles.style.gap = '3px';
        toggles.style.flex = '0 0 auto';
        // 단추 위로 펼치는 메뉴 (객체스냅 설정 / 비주얼 스타일). 항목마다 켜지면 오른쪽에 ✓.
        var openBarMenu = null;
        var closeBarMenu = function() {
            if (openBarMenu) { openBarMenu.style.display = 'none'; openBarMenu = null; }
        };
        document.addEventListener('mousedown', function(e) {
            if (openBarMenu && !openBarMenu.contains(e.target) && !(openBarMenu.__lotOwner && openBarMenu.__lotOwner.contains(e.target))) closeBarMenu();
        });
        var makeBarMenu = function(cmd, owner, keepOpen) {
            var m = document.createElement('div');
            m.setAttribute('data-menu-for', cmd['id']);
            m.style.position = 'fixed';
            m.style.bottom = '31px';
            m.style.minWidth = '170px';
            m.style.background = T.windowBg;
            m.style.border = '1px solid ' + T.border;
            m.style.boxShadow = '0 -4px 10px rgba(0,0,0,0.5)';
            m.style.padding = '3px 0';
            m.style.zIndex = '12';
            m.style.display = 'none';
            m.__lotOwner = owner;
            var title = document.createElement('div');
            title.textContent = cmd['menuTitle'];
            title.style.padding = '4px 12px 6px';
            title.style.color = T.textDim;
            title.style.borderBottom = '1px solid ' + T.border;
            title.style.marginBottom = '3px';
            m.appendChild(title);
            cmd['menu'].forEach(function(item) {
                if (item['sep']) {
                    var hr = document.createElement('div');
                    hr.style.height = '1px';
                    hr.style.margin = '3px 6px';
                    hr.style.background = T.border;
                    m.appendChild(hr);
                }
                var row = document.createElement('div');
                row.style.display = 'flex';
                row.style.justifyContent = 'space-between';
                row.style.gap = '24px';
                row.style.padding = '4px 12px';
                row.style.color = T.text;
                row.title = item['tip'] || '';
                var name = document.createElement('span');
                name.textContent = item['label'];
                var check = document.createElement('span');
                check.style.color = T.text;
                check.style.minWidth = '12px';
                row.appendChild(name);
                row.appendChild(check);
                row.addEventListener('mouseenter', function() { row.style.background = T.accentDim; });
                row.addEventListener('mouseleave', function() { row.style.background = 'transparent'; });
                row.addEventListener('mousedown', function(e) { e.preventDefault(); });
                row.addEventListener('click', function() {
                    run(item);
                    if (!keepOpen) closeBarMenu();
                });
                if (item['state']) {
                    register(item, row, function(on) {
                        row.setAttribute('data-on', on ? '1' : '0');
                        check.textContent = on ? '\u2714' : '';
                    });
                }
                m.appendChild(row);
            });
            document.body.appendChild(m);
            return m;
        };
        var toggleBarMenu = function(menu, anchor) {
            if (openBarMenu === menu) { closeBarMenu(); return; }
            closeBarMenu();
            menu.style.display = 'block';
            // 단추 오른쪽 끝에 맞추되 화면 밖으로 나가지 않게
            var r = anchor.getBoundingClientRect();
            var left = Math.min(r.left, window.innerWidth - menu.offsetWidth - 4);
            menu.style.left = Math.max(4, left) + 'px';
            openBarMenu = menu;
        };

        statusCmds.forEach(function(cmd) {
            var hasMenu = cmd['menu'] && cmd['menu'].length;
            var b = mkBarButton(cmd['label'],
                cmd['tip'] + (cmd['shortcut'] ? '  (' + cmd['shortcut'] + ')' : ''));
            register(cmd, b, function(on, enabled) {
                b.__lotOn = on;
                b.setAttribute('data-on', on ? '1' : '0');
                b.style.background = on ? T.accentDim : '#2a2a2a';
                b.style.borderColor = on ? T.accent : T.border;
                b.style.color = on ? '#ffffff' : T.textDim;
                b.disabled = !enabled;
            });
            if (!hasMenu) {
                b.addEventListener('click', function() { closeMenu(); run(cmd); });
                toggles.appendChild(b);
                return;
            }
            if (!cmd['key']) {
                // 키가 없는 단추 (셰이딩): 누르면 메뉴. 고르면 닫힌다 (하나만 고르는 메뉴).
                var menu = makeBarMenu(cmd, b, false);
                b.addEventListener('click', function() { closeMenu(); toggleBarMenu(menu, b); });
                toggles.appendChild(b);
                return;
            }
            // 키가 있는 단추 (객체스냅): 단추는 켜기/끄기, 옆 ▴ 는 설정 메뉴 (여러 개를 바꾸므로 열어 둔다)
            var group = document.createElement('div');
            group.style.display = 'flex';
            var arrow = mkBarButton('\u25b4', cmd['menuTitle']);
            arrow.setAttribute('data-menu-arrow', cmd['id']);
            arrow.style.padding = '2px 5px';
            arrow.style.marginLeft = '-1px';
            b.addEventListener('click', function() { closeMenu(); run(cmd); });
            var menu2 = makeBarMenu(cmd, group, true);
            arrow.addEventListener('click', function() { closeMenu(); toggleBarMenu(menu2, group); });
            group.appendChild(b);
            group.appendChild(arrow);
            toggles.appendChild(group);
        });
        cmdBox.appendChild(toggles);

        document.body.appendChild(cmdBox);
        dom.cmdInput = cmdInput;
        // 지금 보이는 피처 치수 [{label, value, x, y}] - 테스트 도구가 더블클릭할 자리를 찾는다
        dom['featureDims'] = function() {
            var ptr = _lot_featureDimsJson();
            if (!ptr) return [];
            var text = UTF8ToString(ptr);
            _free(ptr);
            var list;
            try { list = JSON.parse(text); } catch (e) { return []; }
            // 캔버스 픽셀 -> 페이지 좌표 (캔버스는 상태바 아래에서 시작, 화면 배율이 있을 수 있다)
            var cv = document.getElementById('webgpu-canvas');
            if (cv) {
                var r = cv.getBoundingClientRect();
                var sx = cv.width ? r.width / cv.width : 1, sy = cv.height ? r.height / cv.height : 1;
                list.forEach(function(d) { d['x'] = r.left + d['x'] * sx; d['y'] = r.top + d['y'] * sy; });
            }
            return list;
        };
        dom['commandRun'] = function(text) {   // 테스트 도구가 부른다 (Closure 이름 고정)
            var ptr = stringToNewUTF8(text);
            _lot_onCommandLine(ptr);
            _free(ptr);
        };

        dom.uiRoot = root;
        // 켜짐 / 사용 불가 표시 (js_uiSetState 가 부르고, 리본을 다시 만들 때도 다시 부른다)
        dom.applyUiState = function(s) {
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
        };
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
    js_uiSetState__deps: ['$UTF8ToString', '$LotUiTheme', '$LotCmdLog'],
    js_uiSetState: function(jsonPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.stateItems) return 0;
        var s;
        try { s = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) { return 0; }

        dom.lastUiState = s;
        if (dom.applyUiState) dom.applyUiState(s);
        dom.lastHint = s['hint'];
        LotCmdLog.hint(s['hint']);
        if (dom.barHint) dom.barHint.textContent = s['hint'];
        if (dom.hint) {
            dom.hint.textContent = s['hint'];
            dom.hint.style.display = s['hint'] ? 'block' : 'none';
        }
        return 1;
    },

});

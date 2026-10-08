/**
 * 도킹 패널 - 레이어 · 속성 · 노드 트리.
 *
 * 화면 양옆에 도크(왼쪽 / 오른쪽)가 있고, 도크마다 '묶음'이 세로로 쌓인다. 묶음 하나는
 * 탭 줄 + 본문이다 (탭이 여럿이면 하나만 보인다). 패널은 탭을 끌어서:
 *   - 다른 묶음의 탭 줄에 놓으면 그 묶음의 탭이 되고,
 *   - 도크 안 (또는 화면 가장자리) 에 놓으면 그 도크의 새 묶음이 되고,
 *   - 그 밖에 놓으면 떠 있는 창이 된다.
 * 도크 안쪽 경계를 끌면 폭이 바뀌고, x 로 닫은 패널은 뷰 메뉴에서 다시 연다.
 *
 * 캔버스는 도크를 뺀 가운데만 쓴다. 폭이 바뀌면 window resize 를 흘려 C++ 스왑체인이
 * 새 크기로 다시 잡히게 한다 (js_getWindowWidth 가 dom.dockInsets 를 뺀다).
 *
 * 배치는 localStorage 에 남는다 (실패해도 기본 배치로 돈다). ?layout=classic 이면
 * 예전 화면 그대로 - 레이어 패널만 오른쪽 위에 떠 있다 (회귀 테스트가 좌표를 그대로 쓴다).
 *
 * ⚠️ Closure(릴리스)가 점 표기 속성명을 바꾼다. C++ 이 준 JSON 과 저장하는 배치는 대괄호로.
 */

mergeInto(LibraryManager.library, {

    $LotDock__deps: ['$LotUiTheme'],
    $LotDock: {
        // 패널을 만든다. 처음 한 번 (메뉴/리본/레이어 패널이 생긴 뒤).
        ensure: function() {
            var dom = Module.lotDom;
            if (!dom || !dom.uiRoot || !dom.layerPanel || !dom.cmdInput || !dom.statusBar) return false;
            if (dom.dock) return true;
            var T = LotUiTheme;
            var D = dom.dock = {};
            var STORE = 'lot.dock.v1';
            var classic = /[?&]layout=classic\b/.test(window.location.search);
            var MIN_W = 170, MAX_W = 640, BAR_H = 30;

            // ── 패널 본문 ──────────────────────────────────────────
            var mkBody = function(id) {
                var b = document.createElement('div');
                b.setAttribute('data-panel-body', id);
                b.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
                b.style.fontSize = '12px';
                b.style.color = T.text;
                b.style.userSelect = 'none';
                return b;
            };
            // 레이어 패널은 lot_panels.js 가 이미 만들어 두었다 - 떠 있던 모양을 벗겨 본문으로
            var lp = dom.layerPanel;
            ['position', 'top', 'right', 'left', 'zIndex', 'maxHeight', 'minWidth', 'border',
             'borderRadius', 'backgroundColor', 'overflowY'].forEach(function(k) { lp.style[k] = ''; });
            lp.style.padding = '4px';
            D.panels = {
                'layers': { title: '레이어', body: lp },
                'props': { title: '속성', body: mkBody('props') },
                'tree': { title: '노드 트리', body: mkBody('tree') },
            };
            D.order = ['layers', 'props', 'tree'];

            // ── 배치 ───────────────────────────────────────────────
            // 손가락으로 쓰는 작은 화면(휴대폰)은 도면이 우선 - 패널은 닫고 시작한다 (뷰 메뉴로 연다)
            var phone = window.matchMedia && window.matchMedia('(pointer: coarse)').matches
                        && Math.min(window.screen.width, window.screen.height) < 700;
            var defaults = function() {
                if (phone) {
                    return { 'left': { 'width': 260, 'groups': [] }, 'right': { 'width': 260, 'groups': [] }, 'floats': [] };
                }
                return {
                    'left': { 'width': 260, 'groups': [] },
                    'right': { 'width': 300, 'groups': [
                        { 'panels': ['layers'], 'active': 0 },
                        { 'panels': ['props', 'tree'], 'active': 0 },
                    ] },
                    'floats': [],
                };
            };
            var classicLayout = function() {
                // 예전 화면: 레이어 패널이 뷰큐브 아래 오른쪽에 떠 있고 나머지는 닫혀 있다
                var top = dom.statusBar.offsetHeight + dom.uiHeight() + 8 + 132 + 8;
                return {
                    'left': { 'width': 260, 'groups': [] },
                    'right': { 'width': 300, 'groups': [] },
                    'floats': [{ 'panel': 'layers', 'x': window.innerWidth - 12 - 300, 'y': top, 'w': 300, 'h': 0 }],
                };
            };
            var valid = function(s) {
                if (!s || !s['left'] || !s['right'] || !s['floats']) return false;
                var seen = {};
                var ok = true;
                ['left', 'right'].forEach(function(side) {
                    (s[side]['groups'] || []).forEach(function(g) {
                        (g['panels'] || []).forEach(function(p) { if (!D.panels[p] || seen[p]) ok = false; seen[p] = true; });
                    });
                });
                s['floats'].forEach(function(f) { if (!D.panels[f['panel']] || seen[f['panel']]) ok = false; seen[f['panel']] = true; });
                return ok;
            };
            var state = null;
            if (!classic) {
                try { state = JSON.parse(window.localStorage.getItem(STORE) || 'null'); } catch (e) { state = null; }
            }
            if (!valid(state)) state = classic ? classicLayout() : defaults();
            D.state = state;
            var save = function() {
                if (classic) return;
                try { window.localStorage.setItem(STORE, JSON.stringify(state)); } catch (e) {}
            };

            // 패널이 지금 어디 있나 → {side, gi, pi} / {float: fi} / null(닫힘)
            var locate = function(id) {
                var where = null;
                ['left', 'right'].forEach(function(side) {
                    state[side]['groups'].forEach(function(g, gi) {
                        var pi = g['panels'].indexOf(id);
                        if (pi >= 0) where = { side: side, gi: gi, pi: pi };
                    });
                });
                state['floats'].forEach(function(f, fi) { if (f['panel'] === id) where = { fl: fi }; });
                return where;
            };
            // 패널을 지금 자리에서 뺀다 (빈 묶음은 지운다)
            var detach = function(id) {
                ['left', 'right'].forEach(function(side) {
                    var groups = state[side]['groups'];
                    for (var gi = groups.length - 1; gi >= 0; --gi) {
                        var g = groups[gi];
                        var pi = g['panels'].indexOf(id);
                        if (pi < 0) continue;
                        g['panels'].splice(pi, 1);
                        if (g['active'] >= g['panels'].length) g['active'] = Math.max(0, g['panels'].length - 1);
                        if (!g['panels'].length) groups.splice(gi, 1);
                    }
                });
                state['floats'] = state['floats'].filter(function(f) { return f['panel'] !== id; });
            };

            // ── 화면 틀 ────────────────────────────────────────────
            var mkDock = function(side) {
                var d = document.createElement('div');
                d.id = 'lot-dock-' + side;
                d.style.position = 'fixed';
                d.style[side] = '0';
                d.style.bottom = BAR_H + 'px';
                d.style.zIndex = '10';
                d.style.display = 'flex';
                d.style.flexDirection = 'column';
                d.style.background = T.ribbonBg;
                d.style.boxSizing = 'border-box';
                d.style['border' + (side === 'left' ? 'Right' : 'Left')] = '1px solid ' + T.border;
                // 안쪽 경계: 끌면 폭이 바뀐다
                var grip = document.createElement('div');
                grip.style.position = 'absolute';
                grip.style.top = '0';
                grip.style.bottom = '0';
                grip.style[side === 'left' ? 'right' : 'left'] = '-3px';
                grip.style.width = '6px';
                grip.style.cursor = 'col-resize';
                grip.style.zIndex = '2';
                grip.addEventListener('mousedown', function(e) {
                    e.preventDefault();
                    var startX = e.clientX, startW = state[side]['width'];
                    var move = function(ev) {
                        var dx = ev.clientX - startX;
                        var w = startW + (side === 'left' ? dx : -dx);
                        state[side]['width'] = Math.max(MIN_W, Math.min(MAX_W, w));
                        layout();
                    };
                    var up = function() {
                        window.removeEventListener('mousemove', move);
                        window.removeEventListener('mouseup', up);
                        save();
                    };
                    window.addEventListener('mousemove', move);
                    window.addEventListener('mouseup', up);
                });
                d.appendChild(grip);
                var stack = document.createElement('div');
                stack.style.flex = '1 1 auto';
                stack.style.display = 'flex';
                stack.style.flexDirection = 'column';
                stack.style.minHeight = '0';
                d.appendChild(stack);
                document.body.appendChild(d);
                return { el: d, stack: stack };
            };
            D.docks = { 'left': mkDock('left'), 'right': mkDock('right') };
            D.floatLayer = document.createElement('div');
            document.body.appendChild(D.floatLayer);

            // 탭 하나 (끌기 · 누르기 · 닫기)
            var mkTab = function(id, active, onActivate) {
                var t = document.createElement('div');
                t.setAttribute('data-panel-tab', id);
                t.setAttribute('data-on', active ? '1' : '0');
                t.style.display = 'flex';
                t.style.alignItems = 'center';
                t.style.gap = '6px';
                t.style.padding = '0 6px 0 10px';
                t.style.cursor = 'default';
                t.style.whiteSpace = 'nowrap';
                t.style.color = active ? T.text : T.textDim;
                t.style.background = active ? T.ribbonBg : 'transparent';
                t.style.borderTop = '2px solid ' + (active ? T.accent : 'transparent');
                t.style.borderRight = '1px solid ' + T.border;
                var name = document.createElement('span');
                name.textContent = D.panels[id].title;
                var x = document.createElement('span');
                x.textContent = '×';
                x.title = '닫기 (뷰 메뉴에서 다시 연다)';
                x.style.padding = '0 3px';
                x.style.color = T.textDim;
                x.addEventListener('mousedown', function(e) { e.stopPropagation(); e.preventDefault(); });
                x.addEventListener('click', function(e) { e.stopPropagation(); D.close(id); });
                t.appendChild(name);
                t.appendChild(x);
                t.addEventListener('mousedown', function(e) {
                    if (e.button !== 0) return;
                    e.preventDefault();
                    beginDrag(id, e, onActivate);
                });
                return t;
            };
            var mkTabBar = function() {
                var bar = document.createElement('div');
                bar.style.display = 'flex';
                bar.style.alignItems = 'stretch';
                bar.style.height = '24px';
                bar.style.flex = '0 0 auto';
                bar.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
                bar.style.fontSize = '12px';
                bar.style.background = T.menuBg;
                bar.style.borderBottom = '1px solid ' + T.border;
                bar.style.overflow = 'hidden';
                bar.style.userSelect = 'none';
                return bar;
            };

            // 상태 → DOM. 본문 엘리먼트는 옮기기만 한다 (내용이 날아가지 않게).
            var render = function() {
                D.dropTargets = [];
                ['left', 'right'].forEach(function(side) {
                    var stack = D.docks[side].stack;
                    while (stack.firstChild) stack.removeChild(stack.firstChild);
                    state[side]['groups'].forEach(function(g, gi) {
                        var box = document.createElement('div');
                        box.setAttribute('data-dock-group', side + ':' + gi);
                        box.style.flex = '1 1 0';
                        box.style.minHeight = '60px';
                        box.style.display = 'flex';
                        box.style.flexDirection = 'column';
                        if (gi > 0) box.style.borderTop = '1px solid ' + T.border;
                        var bar = mkTabBar();
                        g['panels'].forEach(function(id, pi) {
                            bar.appendChild(mkTab(id, pi === g['active'], function() {
                                g['active'] = pi; save(); render();
                            }));
                        });
                        var body = document.createElement('div');
                        body.style.flex = '1 1 auto';
                        body.style.overflow = 'auto';
                        body.style.minHeight = '0';
                        var activeId = g['panels'][g['active']];
                        if (activeId) body.appendChild(D.panels[activeId].body);
                        box.appendChild(bar);
                        box.appendChild(body);
                        stack.appendChild(box);
                        D.dropTargets.push({ kind: 'tabs', side: side, gi: gi, el: bar });
                    });
                });
                // 떠 있는 창
                while (D.floatLayer.firstChild) D.floatLayer.removeChild(D.floatLayer.firstChild);
                state['floats'].forEach(function(f) {
                    var id = f['panel'];
                    var w = document.createElement('div');
                    w.setAttribute('data-float-panel', id);
                    w.style.position = 'fixed';
                    w.style.left = Math.max(0, Math.min(window.innerWidth - 80, f['x'])) + 'px';
                    w.style.top = Math.max(0, Math.min(window.innerHeight - 40, f['y'])) + 'px';
                    w.style.width = f['w'] + 'px';
                    if (f['h']) w.style.height = f['h'] + 'px';
                    else w.style.maxHeight = 'calc(100vh - ' + (f['y'] + BAR_H + 30) + 'px)';
                    w.style.zIndex = '12';
                    w.style.display = 'flex';
                    w.style.flexDirection = 'column';
                    w.style.background = 'rgba(15, 15, 15, 0.94)';
                    w.style.border = '1px solid ' + T.border;
                    w.style.borderRadius = '4px';
                    w.style.boxShadow = '0 4px 14px rgba(0,0,0,0.5)';
                    w.style.overflow = 'hidden';
                    w.style.resize = 'both';
                    var bar = mkTabBar();
                    bar.appendChild(mkTab(id, true, function() {}));
                    var body = document.createElement('div');
                    body.style.flex = '1 1 auto';
                    body.style.overflow = 'auto';
                    body.style.minHeight = '0';
                    body.appendChild(D.panels[id].body);
                    w.appendChild(bar);
                    w.appendChild(body);
                    // 손으로 크기를 바꾸면 기억한다
                    w.addEventListener('mouseup', function() {
                        if (w.offsetWidth !== f['w'] || (f['h'] && w.offsetHeight !== f['h'])) {
                            f['w'] = w.offsetWidth; f['h'] = w.offsetHeight; save();
                        }
                    });
                    D.floatLayer.appendChild(w);
                });
                layout();
                syncButtons();
                if (D.onRender) D.onRender();
            };
            // 하단 바의 [레이어] [속성] [트리] 단추: 열린 패널은 파랗게 (C++ 상태가 아니라 여기 배치가 정한다)
            var syncButtons = function() {
                ['layers', 'props', 'tree'].forEach(function(id) {
                    var b = document.querySelector('#lot-status-toggles [data-cmd="panel.' + id + '"]');
                    if (!b) return;
                    var on = !!locate(id);
                    b.setAttribute('data-on', on ? '1' : '0');
                    b.style.background = on ? T.accentDim : '#2a2a2a';
                    b.style.borderColor = on ? T.accent : T.border;
                    b.style.color = on ? '#ffffff' : T.textDim;
                });
            };

            // 도크 폭 → 캔버스 · 뷰큐브 · 좌표축 · 안내문 자리
            var layout = function() {
                var top = dom.statusBar.offsetHeight + dom.uiHeight();
                var L = state['left']['groups'].length ? state['left']['width'] : 0;
                var R = state['right']['groups'].length ? state['right']['width'] : 0;
                ['left', 'right'].forEach(function(side) {
                    var d = D.docks[side].el;
                    var w = side === 'left' ? L : R;
                    d.style.display = w ? 'flex' : 'none';
                    d.style.top = top + 'px';
                    d.style.width = w + 'px';
                });
                var changed = !dom.dockInsets || dom.dockInsets.left !== L || dom.dockInsets.right !== R;
                dom.dockInsets = { left: L, right: R };
                var canvas = dom.canvas;
                if (canvas) {
                    canvas.style.left = L + 'px';
                    canvas.style.width = 'calc(100% - ' + (L + R) + 'px)';
                }
                if (dom.viewCubeWrap) dom.viewCubeWrap.style.right = (R + 12) + 'px';
                if (dom.axisGizmo && dom.axisGizmo.svg) dom.axisGizmo.svg.style.left = (L + 4) + 'px';
                var mid = L + (window.innerWidth - L - R) / 2;
                if (dom.hint) dom.hint.style.left = mid + 'px';
                if (dom.textInput) dom.textInput.style.left = mid + 'px';
                // 캔버스 백버퍼를 새 폭으로 (C++ 의 resize 콜백이 js_getWindowWidth 를 다시 읽는다).
                // 프레임 도중(C++ 이 패널을 갱신하는 중)에 바로 흘리면 이미 받아 둔 화면 텍스처와
                // 새 크기의 뎁스가 한 프레임 섞인다 - 프레임이 끝난 뒤로 미룬다.
                if (changed) setTimeout(function() { window.dispatchEvent(new Event('resize')); }, 0);
            };
            D.layout = layout;
            window.addEventListener('resize', function() {
                // 창 크기가 바뀐 것뿐이면 자리만 다시 (여기서 resize 를 또 흘리지 않는다)
                var top = dom.statusBar.offsetHeight + dom.uiHeight();
                D.docks['left'].el.style.top = top + 'px';
                D.docks['right'].el.style.top = top + 'px';
                // 화면을 돌리면 떠 있는 창이 밖으로 나갈 수 있다 - 다시 그리며 화면 안으로 (render 가 자른다)
                if (state['floats'].length) render();
            });
            // 리본을 접고 펴면 높이가 바뀐다
            if (window.ResizeObserver) new ResizeObserver(function() { layout(); }).observe(dom.uiRoot);

            // ── 끌어서 옮기기 ──────────────────────────────────────
            var ghost = document.createElement('div');
            ghost.style.position = 'fixed';
            ghost.style.zIndex = '30';
            ghost.style.pointerEvents = 'none';
            ghost.style.background = 'rgba(66, 150, 250, 0.25)';
            ghost.style.border = '2px solid ' + T.accent;
            ghost.style.display = 'none';
            document.body.appendChild(ghost);

            // 놓을 자리: 탭 줄 > 도크 안/화면 가장자리 > 떠 있는 창
            var targetAt = function(x, y) {
                for (var i = 0; i < D.dropTargets.length; ++i) {
                    var r = D.dropTargets[i].el.getBoundingClientRect();
                    if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                        var t = D.dropTargets[i];
                        return { kind: 'tabs', side: t.side, gi: t.gi, rect: r };
                    }
                }
                var top = dom.statusBar.offsetHeight + dom.uiHeight();
                var bottom = window.innerHeight - BAR_H;
                if (y < top || y > bottom) return { kind: 'float' };
                var L = dom.dockInsets.left, R = dom.dockInsets.right, W = window.innerWidth;
                var EDGE = 48;
                if (x < Math.max(L, EDGE)) {
                    var w = L || state['left']['width'];
                    return { kind: 'dock', side: 'left', rect: { left: 0, top: top, width: w, height: bottom - top } };
                }
                if (x > W - Math.max(R, EDGE)) {
                    var wr = R || state['right']['width'];
                    return { kind: 'dock', side: 'right', rect: { left: W - wr, top: top, width: wr, height: bottom - top } };
                }
                return { kind: 'float' };
            };

            var beginDrag = function(id, down, onClick) {
                var sx = down.clientX, sy = down.clientY, dragging = false, target = null;
                var move = function(e) {
                    if (!dragging && Math.abs(e.clientX - sx) + Math.abs(e.clientY - sy) < 6) return;
                    dragging = true;
                    target = targetAt(e.clientX, e.clientY);
                    var r = target.rect || { left: e.clientX - 20, top: e.clientY - 12, width: 260, height: 180 };
                    ghost.style.left = r.left + 'px';
                    ghost.style.top = r.top + 'px';
                    ghost.style.width = r.width + 'px';
                    ghost.style.height = (target.kind === 'tabs' ? r.height : r.height) + 'px';
                    ghost.style.display = 'block';
                };
                var up = function(e) {
                    window.removeEventListener('mousemove', move);
                    window.removeEventListener('mouseup', up);
                    ghost.style.display = 'none';
                    if (!dragging) { onClick(); return; }
                    D.moveTo(id, target, e.clientX, e.clientY);
                };
                window.addEventListener('mousemove', move);
                window.addEventListener('mouseup', up);
            };

            // 패널을 target 으로 (탭 줄 / 도크 / 떠 있게). 테스트 도구도 부른다.
            D.moveTo = function(id, target, x, y) {
                // 같은 묶음 탭 줄에 놓았으면 그 탭을 켜기만
                var from = locate(id);
                if (target.kind === 'tabs' && from && from.side === target.side && from.gi === target.gi) {
                    state[target.side]['groups'][target.gi]['active'] = from.pi;
                    save(); render(); return;
                }
                var groupRef = (target.kind === 'tabs') ? state[target.side]['groups'][target.gi] : null;
                detach(id);
                if (groupRef && state[target.side]['groups'].indexOf(groupRef) >= 0) {
                    groupRef['panels'].push(id);
                    groupRef['active'] = groupRef['panels'].length - 1;
                } else if (target.kind === 'dock' || target.kind === 'tabs') {
                    state[target.side]['groups'].push({ 'panels': [id], 'active': 0 });
                } else {
                    state['floats'].push({ 'panel': id, 'x': (x || 200) - 40, 'y': (y || 200) - 12, 'w': 280, 'h': 320 });
                }
                LotDock.log('dock: ' + id + ' -> ' + (target.kind === 'float' ? 'float' : target.side + (target.kind === 'tabs' ? ' tabs' : '')));
                save(); render();
            };
            D.close = function(id) {
                detach(id);
                LotDock.log('dock: ' + id + ' closed');
                save(); render();
            };
            D.open = function(id) {
                if (locate(id)) {   // 이미 열려 있으면 그 탭을 앞으로
                    var w = locate(id);
                    if (w.side) state[w.side]['groups'][w.gi]['active'] = w.pi;
                    render(); return;
                }
                var side = state['right']['groups'].length || !state['left']['groups'].length ? 'right' : 'left';
                var groups = state[side]['groups'];
                if (groups.length) { groups[groups.length - 1]['panels'].push(id); groups[groups.length - 1]['active'] = groups[groups.length - 1]['panels'].length - 1; }
                else groups.push({ 'panels': [id], 'active': 0 });
                LotDock.log('dock: ' + id + ' opened');
                save(); render();
            };
            D.reset = function() {
                state = D.state = classic ? classicLayout() : defaults();
                LotDock.log('dock: layout reset');
                save(); render();
            };
            D.isOpen = function(id) { return !!locate(id); };

            // 뷰 메뉴의 '@panel:...' 명령 (lot_ui.js 의 run 이 dom.actions 에서 찾는다)
            if (dom.actions) {
                dom.actions['panel:layers'] = function() { D.isOpen('layers') ? D.close('layers') : D.open('layers'); };
                dom.actions['panel:props'] = function() { D.isOpen('props') ? D.close('props') : D.open('props'); };
                dom.actions['panel:tree'] = function() { D.isOpen('tree') ? D.close('tree') : D.open('tree'); };
                dom.actions['panel:reset'] = function() { D.reset(); };
            }
            // 테스트 도구용 (Closure 이름 고정)
            dom['dockMove'] = function(id, kind, side, gi) { D.moveTo(id, { kind: kind, side: side, gi: gi | 0 }, 300, 300); };
            dom['dockState'] = function() { return JSON.stringify(state); };
            dom['dockOpen'] = function(id) { D.open(id); };
            dom['dockClose'] = function(id) { D.close(id); };

            LotDock.tree.init(dom, D);
            LotDock.props.init(dom, D);
            render();
            return true;
        },

        log: function(msg) { console.log(msg); },

        // ── 노드 트리 ─────────────────────────────────────────────
        tree: {
            init: function(dom, D) {
                var T = LotUiTheme;
                var body = D.panels['tree'].body;
                body.style.padding = '4px 2px';
                var self = LotDock.tree;
                self.dom = dom;
                self.body = body;
                self.summary = null;
                self.expanded = {};    // 층 id -> {items:[], total}
                self.collapsedScene = false;
                self.T = T;
            },

            fetch: function(layerId, offset, limit) {
                var ptr = _lot_treeChildren(layerId, offset, limit);
                if (!ptr) return null;
                var text = UTF8ToString(ptr);
                _free(ptr);
                try { return JSON.parse(text); } catch (e) { return null; }
            },

            send: function(action, id, value) {
                var ptr = stringToNewUTF8(action);
                _lot_onTreeCommand(ptr, id, value | 0);
                _free(ptr);
            },

            // 요약이 바뀌었다 - 펼친 층은 불러 둔 만큼 다시 읽는다 (숨김 표시가 바뀌었을 수 있다)
            update: function(summary) {
                var self = LotDock.tree;
                self.summary = summary;
                var layers = {};
                summary['layers'].forEach(function(l) { layers[l['id']] = true; });
                Object.keys(self.expanded).forEach(function(k) {
                    var id = parseInt(k, 10);
                    if (!layers[id]) { delete self.expanded[k]; return; }
                    var e = self.expanded[k];
                    var r = self.fetch(id, 0, Math.max(200, e.items.length));
                    if (r) { e.items = r['items']; e.total = r['total']; }
                });
                self.render();
            },

            render: function() {
                var self = LotDock.tree, T = self.T, s = self.summary, body = self.body;
                if (!s) return;
                var scrollTop = body.parentNode ? body.parentNode.scrollTop : 0;
                while (body.firstChild) body.removeChild(body.firstChild);
                var selected = {};
                s['selected'].forEach(function(id) { selected[id] = true; });

                var row = function(depth, opts) {
                    var r = document.createElement('div');
                    r.style.display = 'flex';
                    r.style.alignItems = 'center';
                    r.style.gap = '4px';
                    r.style.padding = '1px 4px 1px ' + (4 + depth * 14) + 'px';
                    r.style.whiteSpace = 'nowrap';
                    r.style.borderRadius = '2px';
                    if (opts.selected) r.style.background = T.accentDim;
                    var arrow = document.createElement('span');
                    arrow.style.width = '12px';
                    arrow.style.color = T.textDim;
                    arrow.style.flex = '0 0 auto';
                    arrow.textContent = opts.expandable ? (opts.open ? '▾' : '▸') : '';
                    if (opts.onToggleOpen) {
                        arrow.style.cursor = 'pointer';
                        arrow.addEventListener('click', opts.onToggleOpen);
                    }
                    r.appendChild(arrow);
                    var cb = document.createElement('input');
                    cb.type = 'checkbox';
                    cb.checked = opts.checked;
                    cb.indeterminate = !!opts.mixed;
                    cb.disabled = !!opts.disabled;
                    cb.style.margin = '0';
                    cb.style.flex = '0 0 auto';
                    cb.addEventListener('mousedown', function(e) { e.stopPropagation(); });
                    cb.addEventListener('change', function() { opts.onCheck(cb.checked); });
                    r.appendChild(cb);
                    var name = document.createElement('span');
                    name.textContent = opts.label;
                    name.style.overflow = 'hidden';
                    name.style.textOverflow = 'ellipsis';
                    name.style.color = opts.dim ? T.textDim : T.text;
                    name.style.cursor = 'default';
                    r.appendChild(name);
                    if (opts.onClick) r.addEventListener('click', function(e) {
                        if (e.target === cb || e.target === arrow) return;
                        opts.onClick(e);
                    });
                    if (opts.onDouble) r.addEventListener('dblclick', function(e) {
                        if (e.target === cb || e.target === arrow) return;
                        opts.onDouble(e);
                    });
                    if (opts.id !== undefined) r.setAttribute('data-tree-object', String(opts.id));
                    if (opts.layer !== undefined) r.setAttribute('data-tree-layer', String(opts.layer));
                    body.appendChild(r);
                    return r;
                };

                var visible = s['total'] - s['hiddenTotal'];
                var sceneRow = row(0, {
                    label: '씬 (' + visible + '/' + s['total'] + ')' + (s['selectedCount'] ? '  · 선택 ' + s['selectedCount'] : ''),
                    expandable: true, open: !self.collapsedScene,
                    checked: s['hiddenTotal'] === 0, mixed: s['hiddenTotal'] > 0 && s['hiddenTotal'] < s['total'],
                    onToggleOpen: function() { self.collapsedScene = !self.collapsedScene; self.render(); },
                    // 하나라도 숨겼으면 전부 보이기, 아니면 전부 숨기기
                    onCheck: function() { self.send('hide', -1, s['hiddenTotal'] > 0 ? 0 : 1); },
                });
                sceneRow.setAttribute('data-tree-scene', '1');
                if (self.collapsedScene) return;

                s['layers'].forEach(function(l) {
                    var id = l['id'];
                    var open = !!self.expanded[id];
                    row(1, {
                        label: l['name'] + ' (' + l['count'] + ')',
                        layer: id, expandable: l['count'] > 0, open: open,
                        dim: !l['visible'],
                        checked: l['visible'], disabled: id === 0,   // 기본층은 끌 수 없다 (레이어 패널과 같다)
                        onToggleOpen: function() {
                            if (open) delete self.expanded[id];
                            else {
                                var r = self.fetch(id, 0, 200);
                                self.expanded[id] = { items: r ? r['items'] : [], total: r ? r['total'] : 0 };
                            }
                            self.render();
                        },
                        onCheck: function(on) { self.dom.layerCommand('visible', id, on ? 1 : 0); },
                    });
                    if (!open) return;
                    var e = self.expanded[id];
                    e.items.forEach(function(it) {
                        var oid = it['id'];
                        row(2, {
                            label: it['label'], id: oid,
                            selected: !!selected[oid],
                            dim: it['hidden'] || !l['visible'],
                            checked: !it['hidden'],
                            onCheck: function(on) { self.send('hide', oid, on ? 0 : 1); },
                            onClick: function(ev) { self.send('select', oid, (ev.ctrlKey || ev.metaKey || ev.shiftKey) ? 1 : 0); },
                            onDouble: function() { self.send('zoom', oid, 0); },
                        });
                    });
                    if (e.items.length < e.total) {
                        var more = document.createElement('div');
                        more.textContent = '더 보기 (' + (e.total - e.items.length) + '개 남음)';
                        more.setAttribute('data-tree-more', String(id));
                        more.style.padding = '2px 4px 2px ' + (4 + 2 * 14 + 16) + 'px';
                        more.style.color = T.accent;
                        more.style.cursor = 'pointer';
                        more.addEventListener('click', function() {
                            var r = self.fetch(id, e.items.length, 200);
                            if (r) { e.items = e.items.concat(r['items']); e.total = r['total']; }
                            self.render();
                        });
                        body.appendChild(more);
                    }
                });
                if (body.parentNode) body.parentNode.scrollTop = scrollTop;
            },
        },

        // ── 속성 ─────────────────────────────────────────────────
        props: {
            init: function(dom, D) {
                var self = LotDock.props;
                self.dom = dom;
                self.body = D.panels['props'].body;
                self.body.style.padding = '6px 8px';
                self.T = LotUiTheme;
                self.data = null;
                self.pending = null;
                // 입력 중에는 다시 그리지 않는다 (치던 글자가 날아간다) - 손을 떼면 그때
                self.body.addEventListener('focusout', function() {
                    setTimeout(function() {
                        if (self.pending && !self.body.contains(document.activeElement)) {
                            var p = self.pending; self.pending = null; self.update(p);
                        }
                    }, 0);
                });
            },

            update: function(data) {
                var self = LotDock.props;
                if (self.body.contains(document.activeElement)) { self.pending = data; return; }
                self.data = data;
                self.render();
            },

            edit: function(key, value) {
                var k = stringToNewUTF8(key), v = stringToNewUTF8(String(value));
                _lot_onPropertyEdit(k, v);
                _free(k); _free(v);
            },

            render: function() {
                var self = LotDock.props, T = self.T, d = self.data, body = self.body, dom = self.dom;
                while (body.firstChild) body.removeChild(body.firstChild);
                if (!d) return;
                if (!d['count']) {
                    var none = document.createElement('div');
                    none.textContent = '선택 없음';
                    none.style.color = T.textDim;
                    body.appendChild(none);
                    return;
                }
                var head = document.createElement('div');
                head.textContent = d['single'] ? d['single']['label'] : (d['kind'] + '  (' + d['count'] + '개)');
                head.style.fontWeight = '600';
                head.style.marginBottom = '6px';
                head.setAttribute('data-prop-head', '1');
                body.appendChild(head);

                var grid = document.createElement('div');
                grid.style.display = 'grid';
                grid.style.gridTemplateColumns = '72px 1fr';
                grid.style.gap = '4px 6px';
                grid.style.alignItems = 'center';
                body.appendChild(grid);
                var section = function(text) {
                    var s = document.createElement('div');
                    s.textContent = text;
                    s.style.gridColumn = '1 / span 2';
                    s.style.color = T.textDim;
                    s.style.fontSize = '11px';
                    s.style.marginTop = '6px';
                    s.style.borderBottom = '1px solid ' + T.border;
                    grid.appendChild(s);
                };
                var label = function(text) {
                    var l = document.createElement('div');
                    l.textContent = text;
                    l.style.color = T.textDim;
                    grid.appendChild(l);
                };
                var styleInput = function(el) {
                    el.style.width = '100%';
                    el.style.boxSizing = 'border-box';
                    el.style.padding = '2px 4px';
                    el.style.font = 'inherit';
                    el.style.color = T.text;
                    el.style.background = '#101010';
                    el.style.border = '1px solid ' + T.border;
                    el.style.borderRadius = '2px';
                    // 캔버스 단축키로 새지 않게
                    el.addEventListener('keydown', function(e) { e.stopPropagation(); });
                    el.addEventListener('keyup', function(e) { e.stopPropagation(); });
                    el.addEventListener('keypress', function(e) { e.stopPropagation(); });
                    el.addEventListener('mousedown', function(e) { e.stopPropagation(); });
                    return el;
                };
                // 값 입력: Enter 나 포커스를 잃을 때 보낸다
                var field = function(name, key, value, numeric) {
                    label(name);
                    var inp = styleInput(document.createElement('input'));
                    inp.type = 'text';
                    inp.value = value;
                    inp.setAttribute('data-prop', key);
                    var sent = String(value);
                    var commit = function() {
                        if (inp.value === sent) return;
                        if (numeric && !isFinite(parseFloat(inp.value))) { inp.value = sent; return; }
                        sent = inp.value;
                        self.edit(key, inp.value);
                    };
                    inp.addEventListener('keydown', function(e) {
                        if (e.key === 'Enter') { commit(); inp.blur(); }
                        else if (e.key === 'Escape') { inp.value = sent; inp.blur(); }
                    });
                    inp.addEventListener('blur', commit);
                    grid.appendChild(inp);
                };
                var readonly = function(name, value) {
                    label(name);
                    var v = document.createElement('div');
                    v.textContent = value;
                    v.style.overflow = 'hidden';
                    v.style.textOverflow = 'ellipsis';
                    grid.appendChild(v);
                };

                section('일반');
                readonly('종류', d['kind']);
                // 층
                label('레이어');
                var ls = styleInput(document.createElement('select'));
                ls.setAttribute('data-prop', 'layer');
                if (d['layer'] === -2) { var o0 = document.createElement('option'); o0.value = '-2'; o0.textContent = '(여러 개)'; ls.appendChild(o0); }
                d['layers'].forEach(function(l) {
                    var o = document.createElement('option');
                    o.value = String(l['id']);
                    o.textContent = l['name'];
                    ls.appendChild(o);
                });
                ls.value = String(d['layer']);
                ls.addEventListener('change', function() {
                    var v = parseInt(ls.value, 10);
                    if (v >= 0) dom.layerCommand('assign', v, 0);
                });
                grid.appendChild(ls);
                // 색
                label('색');
                var colorRow = document.createElement('div');
                colorRow.style.display = 'flex';
                colorRow.style.alignItems = 'center';
                colorRow.style.gap = '6px';
                var ci = document.createElement('input');
                ci.type = 'color';
                ci.value = d['color'] || '#cccccc';
                ci.title = d['color'] ? '객체 색' : '객체 색 (여러 개)';
                ci.setAttribute('data-prop', 'color');
                ci.style.width = '28px';
                ci.style.height = '18px';
                ci.style.padding = '0';
                ci.style.border = '1px solid #555';
                ci.style.background = 'none';
                ci.addEventListener('mousedown', function(e) { e.stopPropagation(); });
                ci.addEventListener('input', function() { dom.layerCommand('objectColor', 0, parseInt(ci.value.slice(1), 16)); });
                colorRow.appendChild(ci);
                var bl = document.createElement('label');
                bl.style.display = 'flex';
                bl.style.alignItems = 'center';
                bl.style.gap = '3px';
                var bc = document.createElement('input');
                bc.type = 'checkbox';
                bc.checked = d['byLayer'] === 1;
                bc.indeterminate = d['byLayer'] === -2;
                bc.setAttribute('data-prop', 'byLayer');
                bc.addEventListener('change', function() { dom.layerCommand('objectByLayer', 0, bc.checked ? 1 : 0); });
                bl.appendChild(bc);
                bl.appendChild(document.createTextNode('ByLayer'));
                colorRow.appendChild(bl);
                grid.appendChild(colorRow);
                // 선종류
                label('선종류');
                var lt = styleInput(document.createElement('select'));
                lt.setAttribute('data-prop', 'linetype');
                var addOpt = function(v, text) { var o = document.createElement('option'); o.value = String(v); o.textContent = text; lt.appendChild(o); };
                if (d['linetype'] === -2) addOpt(-2, '(여러 개)');
                addOpt(-1, 'ByLayer');
                (dom.linetypes || []).forEach(function(t) { addOpt(t['id'], t['name']); });
                lt.value = String(d['linetype']);
                lt.addEventListener('change', function() {
                    var v = parseInt(lt.value, 10);
                    if (v !== -2) dom.layerCommand('objectLinetype', 0, v);
                });
                grid.appendChild(lt);

                var s = d['single'];
                if (!s) return;
                section('위치');
                field('X', 'x', s['x'], true);
                field('Y', 'y', s['y'], true);
                field('Z', 'z', s['z'], true);
                if (s['text'] !== undefined) {
                    section('문자');
                    field('내용', 'text', s['text'], false);
                    field('높이', 'textHeight', s['textHeight'], true);
                }
                if (s['solidHeight'] !== undefined) {
                    section('솔리드');
                    field('돌출 높이', 'solidHeight', s['solidHeight'], true);
                }
                if (s['info'] && s['info'].length) {
                    section('형상');
                    s['info'].forEach(function(kv) { readonly(kv[0], kv[1]); });
                }
            },
        },
    },

    // C++ 이 바뀔 때만 민다. 패널 틀이 아직이면 0 (다음 프레임에 다시).
    js_uiSetTree__deps: ['$LotDock', '$UTF8ToString', '$stringToNewUTF8', 'free',
                         'lot_treeChildren', 'lot_onTreeCommand'],
    js_uiSetTree: function(jsonPtr) {
        if (!LotDock.ensure()) return 0;
        var s;
        try { s = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) { return 0; }
        LotDock.tree.update(s);
        return 1;
    },

    js_uiSetProperties__deps: ['$LotDock', '$UTF8ToString', '$stringToNewUTF8', 'free', 'lot_onPropertyEdit'],
    js_uiSetProperties: function(jsonPtr) {
        if (!LotDock.ensure()) return 0;
        var d;
        try { d = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) { return 0; }
        LotDock.props.update(d);
        return 1;
    },
});

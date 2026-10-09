/**
 * 리본 - 탭 줄 + 그룹들. 무엇을 보여줄지는 C++ (src/ui/lot_ribbon.cpp) 이 JSON 으로 준다.
 *
 * 배치는 세 가지 (뷰 메뉴 '리본: 한 줄 / 두 줄 / 세 줄', 브라우저에 기억):
 *   한 줄  - 단추마다 아이콘 위 · 이름 아래, 옆으로 늘어선다.
 *   두/세 줄 - 네이티브 리본(3dEngine ui/lot_ribbon.cpp, kRows = 3)처럼 그룹의 첫 단추는 줄 높이를 다 쓰는
 *            큰 단추 (큰 아이콘 + 이름), 나머지는 작은 단추를 위에서 아래로 채우고 다음 열로.
 *            작은 단추는 아이콘 옆에 이름 - 웹 아이콘은 글자라 이름 없이는 알아보기 어렵다.
 * ?layout=classic (회귀 테스트) 은 늘 한 줄 - 시나리오의 화면 좌표가 그대로이게.
 *
 * ⚠️ Closure(릴리스)가 점 표기 속성명을 바꾼다. C++ 이 준 JSON 은 대괄호로.
 */
mergeInto(LibraryManager.library, {

    $LotRibbon__deps: ['$LotUiTheme'],
    $LotRibbon: {
        kStoreKey: 'lotRibbonRows',

        storedRows: function() {
            if (location.search.indexOf('layout=classic') >= 0) return 1;
            try {
                var v = parseInt(window['localStorage'].getItem(LotRibbon.kStoreKey), 10);
                if (v === 1 || v === 2 || v === 3) return v;
            } catch (e) {}
            return 1;
        },

        // ribbon: C++ 의 탭 목록. ctx: register(cmd, el, onStyle) / unregister(el) / run(cmd) / closeMenu().
        // 돌려주는 것: { box, setRows(n), rows() }
        build: function(ribbon, ctx) {
            var T = LotUiTheme;
            var box = document.createElement('div');
            box.style.background = T.ribbonBg;
            box.style.borderBottom = '1px solid ' + T.border;

            var tabRow = document.createElement('div');
            tabRow.style.display = 'flex';
            tabRow.style.background = T.menuBg;
            tabRow.style.borderBottom = '1px solid ' + T.border;

            var body = document.createElement('div');
            body.style.display = 'flex';
            body.style.alignItems = 'flex-start';
            body.style.gap = '2px';
            body.style.padding = '3px 6px';

            var tabs = [], panels = [], made = [];
            var activeTab = 0, collapsed = false, rows = LotRibbon.storedRows();

            var applyTabs = function() {
                tabs.forEach(function(t, i) {
                    t.style.color = (i === activeTab) ? T.text : T.textDim;
                    t.style.borderBottom = (i === activeTab && !collapsed) ? '2px solid ' + T.accent : '2px solid transparent';
                });
                panels.forEach(function(p, i) { p.style.display = (i === activeTab) ? 'flex' : 'none'; });
                body.style.display = collapsed ? 'none' : 'flex';
            };

            // 단추 하나. kind: 'tall' (한 줄 배치), 'big' (여러 줄의 대표), 'small' (여러 줄의 나머지)
            var makeButton = function(cmd, kind) {
                var b = document.createElement('button');
                b.title = cmd['tip'] + (cmd['shortcut'] ? '  (' + cmd['shortcut'] + ')' : '');
                b.style.display = 'flex';
                b.style.border = '1px solid transparent';
                b.style.borderRadius = '3px';
                b.style.background = 'transparent';
                b.style.color = T.text;
                b.style.font = 'inherit';
                b.style.cursor = 'default';
                var glyph = document.createElement('span');
                glyph.textContent = cmd['icon'] || cmd['label'].charAt(0);
                var cap = document.createElement('span');
                cap.textContent = cmd['label'];
                cap.style.color = T.textDim;
                if (kind === 'small') {
                    b.style.flexDirection = 'row';
                    b.style.alignItems = 'center';
                    b.style.gap = '5px';
                    b.style.height = '21px';
                    b.style.padding = '0 6px 0 3px';
                    glyph.style.fontSize = '13px';
                    glyph.style.width = '16px';
                    glyph.style.textAlign = 'center';
                    cap.style.fontSize = '11px';
                    cap.style.whiteSpace = 'nowrap';
                } else {
                    b.style.flexDirection = 'column';
                    b.style.alignItems = 'center';
                    b.style.justifyContent = 'center';
                    b.style.gap = '1px';
                    b.style.width = kind === 'big' ? 'auto' : '46px';
                    b.style.minWidth = kind === 'big' ? '46px' : '';
                    b.style.padding = '3px 4px';
                    if (kind === 'big') b.style.height = (rows * 21 + (rows - 1) * 2) + 'px';
                    glyph.style.fontSize = kind === 'big' ? '22px' : '15px';
                    glyph.style.lineHeight = kind === 'big' ? '24px' : '17px';
                    cap.style.fontSize = '10px';
                    cap.style.whiteSpace = 'nowrap';
                }
                b.appendChild(glyph);
                b.appendChild(cap);
                b.addEventListener('mouseenter', function() { if (!b.disabled && !b.__lotOn) b.style.background = T.hover; });
                b.addEventListener('mouseleave', function() { b.style.background = b.__lotOn ? T.accentDim : 'transparent'; });
                b.addEventListener('mousedown', function(e) { e.preventDefault(); });
                b.addEventListener('click', function() { ctx.closeMenu(); ctx.run(cmd); });
                ctx.register(cmd, b, function(on, enabled) {
                    b.__lotOn = on;
                    b.setAttribute('data-on', on ? '1' : '0');
                    b.style.background = on ? T.accentDim : 'transparent';
                    b.style.borderColor = on ? T.accent : 'transparent';
                    b.disabled = !enabled;
                    b.style.opacity = enabled ? '1' : '0.45';
                });
                made.push(b);
                return b;
            };

            var fillPanel = function(panel, tabDef) {
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
                    var items = g['items'];
                    if (rows === 1) {
                        items.forEach(function(cmd) { icons.appendChild(makeButton(cmd, 'tall')); });
                    } else {
                        if (items.length) icons.appendChild(makeButton(items[0], 'big'));
                        if (items.length > 1) {
                            var grid = document.createElement('div');
                            grid.style.display = 'grid';
                            grid.style.gridAutoFlow = 'column';
                            grid.style.gridTemplateRows = 'repeat(' + rows + ', 21px)';
                            grid.style.gap = '2px';
                            grid.style.alignItems = 'center';
                            grid.style.justifyItems = 'stretch';
                            items.slice(1).forEach(function(cmd) { grid.appendChild(makeButton(cmd, 'small')); });
                            icons.appendChild(grid);
                        }
                    }

                    var caption = document.createElement('div');
                    caption.textContent = g['caption'];
                    caption.style.fontSize = '10px';
                    caption.style.color = T.textDim;
                    caption.style.marginTop = '1px';
                    group.appendChild(icons);
                    group.appendChild(caption);
                    panel.appendChild(group);
                });
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
                fillPanel(panel, tabDef);
                panels.push(panel);
                body.appendChild(panel);
            });

            box.appendChild(tabRow);
            box.appendChild(body);
            applyTabs();

            return {
                box: box,
                rows: function() { return rows; },
                // 줄 수를 바꿔 단추를 다시 만든다 (지난 단추는 상태 목록에서 뺀다)
                setRows: function(n) {
                    if (n !== 1 && n !== 2 && n !== 3) return;
                    rows = n;
                    try { window['localStorage'].setItem(LotRibbon.kStoreKey, String(n)); } catch (e) {}
                    made.forEach(function(el) { ctx.unregister(el); });
                    made = [];
                    panels.forEach(function(p, i) { p.textContent = ''; fillPanel(p, ribbon[i]); });
                    applyTabs();
                },
            };
        },
    },
});

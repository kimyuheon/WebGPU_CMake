/**
 * 배열 대화상자 (네이티브 lot_array_dialog 와 같은 항목). C++ ArrayTool 이 열고 닫는다.
 *
 * 값을 바꿀 때마다 lot_onArrayParams 로 보내면 C++ 이 미리보기를 다시 그린다.
 * [생성] / [닫기] / [중심 클릭 지정] 은 lot_onArrayAction. 입력칸에서 Enter = 생성, Esc = 닫기.
 * 입력 중에는 C++ 이 보낸 값으로 덮어쓰지 않는다 (치던 글자가 날아간다).
 *
 * ⚠️ Closure(릴리스)가 점 표기 속성명을 바꾼다. C++ 이 준 JSON 은 대괄호로 읽는다.
 */

mergeInto(LibraryManager.library, {

    js_uiArrayDialog__deps: ['$LotUiTheme', '$UTF8ToString', '$stringToNewUTF8', 'free',
                             'lot_onArrayParams', 'lot_onArrayAction'],
    js_uiArrayDialog: function(jsonPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.uiRoot || !dom.statusBar) return 0;
        var s;
        try { s = JSON.parse(UTF8ToString(jsonPtr)); } catch (e) { return 0; }
        var T = LotUiTheme;
        var action = function(name) {
            var p = stringToNewUTF8(name);
            _lot_onArrayAction(p);
            _free(p);
        };

        if (!dom.arrayDialog) {
            var box = document.createElement('div');
            box.id = 'lot-array-dialog';
            box.style.position = 'fixed';
            box.style.zIndex = '13';
            box.style.width = '250px';
            box.style.background = 'rgba(15, 15, 15, 0.96)';
            box.style.border = '1px solid ' + T.border;
            box.style.borderRadius = '4px';
            box.style.boxShadow = '0 4px 14px rgba(0,0,0,0.5)';
            box.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
            box.style.fontSize = '12px';
            box.style.color = T.text;
            box.style.userSelect = 'none';
            box.style.display = 'none';
            box.addEventListener('mousedown', function(e) { e.stopPropagation(); });

            var title = document.createElement('div');
            title.textContent = '배열';
            title.style.padding = '5px 8px';
            title.style.background = T.menuBg;
            title.style.borderBottom = '1px solid ' + T.border;
            title.style.fontWeight = '600';
            box.appendChild(title);

            var body = document.createElement('div');
            body.style.padding = '8px';
            body.style.display = 'grid';
            body.style.gridTemplateColumns = '86px 1fr';
            body.style.gap = '5px 6px';
            body.style.alignItems = 'center';
            box.appendChild(body);

            var inputs = {};
            var rows = {};   // 이름 -> [라벨, 입력] (종류에 따라 감춘다)
            var styleInput = function(el) {
                el.style.width = '100%';
                el.style.boxSizing = 'border-box';
                el.style.padding = '2px 4px';
                el.style.font = 'inherit';
                el.style.color = T.text;
                el.style.background = '#101010';
                el.style.border = '1px solid ' + T.border;
                el.style.borderRadius = '2px';
                return el;
            };
            var send = function() {
                var num = function(k) { var v = parseFloat(inputs[k].value); return isFinite(v) ? v : 0; };
                _lot_onArrayParams(inputs['polar'].checked ? 1 : 0, Math.round(num('cols')), Math.round(num('rows')),
                                   num('dx'), num('dy'), Math.round(num('count')), num('angle'),
                                   inputs['rotate'].checked ? 1 : 0, inputs['centerAuto'].checked ? 1 : 0,
                                   num('cx'), num('cy'), num('cz'));
            };
            var keys = function(el) {
                el.addEventListener('keydown', function(e) {
                    e.stopPropagation();
                    if (e.key === 'Enter') { send(); action('create'); }
                    else if (e.key === 'Escape') action('close');
                });
                el.addEventListener('keyup', function(e) { e.stopPropagation(); });
                el.addEventListener('keypress', function(e) { e.stopPropagation(); });
                el.addEventListener('input', send);
                el.addEventListener('change', send);
            };
            var field = function(key, label, type) {
                var l = document.createElement('div');
                l.textContent = label;
                l.style.color = T.textDim;
                var inp = styleInput(document.createElement('input'));
                inp.type = type || 'text';
                inp.setAttribute('data-array', key);
                keys(inp);
                body.appendChild(l);
                body.appendChild(inp);
                inputs[key] = inp;
                rows[key] = [l, inp];
            };
            // 종류
            var kind = document.createElement('div');
            kind.style.gridColumn = '1 / span 2';
            kind.style.display = 'flex';
            kind.style.gap = '12px';
            var radio = function(label, polar) {
                var w = document.createElement('label');
                w.style.display = 'flex';
                w.style.alignItems = 'center';
                w.style.gap = '4px';
                var r = document.createElement('input');
                r.type = 'radio';
                r.name = 'lot-array-kind';
                r.setAttribute('data-array', polar ? 'polar' : 'rect');
                r.addEventListener('change', function() { send(); });
                w.appendChild(r);
                w.appendChild(document.createTextNode(label));
                kind.appendChild(w);
                return r;
            };
            inputs['rect'] = radio('직사각형', false);
            inputs['polar'] = radio('원형', true);
            body.appendChild(kind);
            field('cols', '열');
            field('rows', '행');
            field('dx', '열 간격');
            field('dy', '행 간격');
            field('count', '개수 (원본 포함)');
            field('angle', '채울 각도');
            // 체크박스 줄
            var check = function(key, label) {
                var w = document.createElement('label');
                w.style.gridColumn = '1 / span 2';
                w.style.display = 'flex';
                w.style.alignItems = 'center';
                w.style.gap = '4px';
                var c = document.createElement('input');
                c.type = 'checkbox';
                c.setAttribute('data-array', key);
                c.addEventListener('change', send);
                w.appendChild(c);
                w.appendChild(document.createTextNode(label));
                body.appendChild(w);
                inputs[key] = c;
                rows[key] = [w];
            };
            check('rotate', '항목 회전');
            check('centerAuto', '중심 = 선택 가운데');
            field('cx', '중심 X');
            field('cy', '중심 Y');
            field('cz', '중심 Z');
            var pick = document.createElement('button');
            pick.textContent = '중심 클릭 지정';
            pick.setAttribute('data-array', 'pickCenter');
            pick.style.gridColumn = '1 / span 2';
            pick.addEventListener('click', function() { action('pickCenter'); });
            body.appendChild(pick);
            rows['pick'] = [pick];

            var foot = document.createElement('div');
            foot.style.display = 'flex';
            foot.style.alignItems = 'center';
            foot.style.gap = '6px';
            foot.style.padding = '6px 8px';
            foot.style.borderTop = '1px solid ' + T.border;
            var info = document.createElement('span');
            info.style.flex = '1 1 auto';
            info.style.color = T.textDim;
            var mkButton = function(label, name, primary) {
                var b = document.createElement('button');
                b.textContent = label;
                b.setAttribute('data-array', name);
                b.style.padding = '3px 12px';
                b.style.font = 'inherit';
                b.style.color = primary ? '#ffffff' : T.text;
                b.style.background = primary ? T.accentDim : '#2a2a2a';
                b.style.border = '1px solid ' + (primary ? T.accent : T.border);
                b.style.borderRadius = '2px';
                b.addEventListener('click', function() { if (name === 'create') send(); action(name); });
                return b;
            };
            foot.appendChild(info);
            foot.appendChild(mkButton('생성', 'create', true));
            foot.appendChild(mkButton('닫기', 'close', false));
            box.appendChild(foot);
            [pick].forEach(function(b) {
                b.style.padding = '3px 8px';
                b.style.font = 'inherit';
                b.style.color = T.text;
                b.style.background = '#2a2a2a';
                b.style.border = '1px solid ' + T.border;
                b.style.borderRadius = '2px';
            });
            document.body.appendChild(box);
            dom.arrayDialog = { box: box, inputs: inputs, rows: rows, info: info, pick: pick };
        }

        var D = dom.arrayDialog;
        if (!s['open']) { D.box.style.display = 'none'; return 1; }
        // 자리: 도면 왼쪽 위 (왼쪽 도크 안쪽)
        var ins = dom.dockInsets || { left: 0, right: 0 };
        D.box.style.left = (ins.left + 16) + 'px';
        D.box.style.top = (dom.statusBar.offsetHeight + dom.uiHeight() + 12) + 'px';
        D.box.style.display = 'block';

        var typing = D.box.contains(document.activeElement) && document.activeElement.type !== 'checkbox'
                     && document.activeElement.type !== 'radio';
        var put = function(key, v) { if (!typing || document.activeElement !== D.inputs[key]) D.inputs[key].value = String(v); };
        D.inputs['rect'].checked = !s['polar'];
        D.inputs['polar'].checked = !!s['polar'];
        put('cols', s['cols']); put('rows', s['rows']);
        put('dx', s['dx']); put('dy', s['dy']);
        put('count', s['count']); put('angle', s['angle']);
        D.inputs['rotate'].checked = !!s['rotate'];
        D.inputs['centerAuto'].checked = !!s['centerAuto'];
        put('cx', s['center'][0]); put('cy', s['center'][1]); put('cz', s['center'][2]);
        // 종류에 맞는 줄만
        var show = function(keys, on) {
            keys.forEach(function(k) { D.rows[k].forEach(function(el) { el.style.display = on ? '' : 'none'; }); });
        };
        show(['cols', 'rows', 'dx', 'dy'], !s['polar']);
        show(['count', 'angle', 'rotate', 'centerAuto', 'cx', 'cy', 'cz', 'pick'], !!s['polar']);
        ['cx', 'cy', 'cz'].forEach(function(k) { D.inputs[k].disabled = !!s['centerAuto']; });
        D.pick.textContent = s['picking'] ? '도면에서 중심을 클릭하세요 (Esc)' : '중심 클릭 지정';
        D.info.textContent = '대상 ' + s['items'] + ' · 복사본 ' + s['copies'];
        return 1;
    },
});

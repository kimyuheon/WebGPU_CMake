/**
 * 뷰큐브 - 오른쪽 위의 작은 상자. 지금 시점을 보여주고, 누르면 그 방향 뷰로 간다.
 *
 * 그리지 않고 CSS 3D 로 세운다. 상자 바깥틀에 카메라 회전(matrix3d)을 걸면 브라우저가
 * 돌려 주고, backface-visibility 가 뒤를 보는 면을 지운다. 면마다 3x3 칸을 올려
 * 가운데는 면 · 변은 모서리 · 구석은 꼭짓점으로 - AutoCAD 뷰큐브의 26 방향이 그대로
 * DOM 히트 테스트가 된다. C++ 은 자세를 넘기고 눌린 방향을 받기만 한다.
 *
 * 축은 월드 기준(Z-up CAD: +X 오른쪽, +Y 앞, +Z 위)으로 적는다. 화면으로 옮기는 일은
 * 바깥틀의 행렬이 한다 - 면 하나하나를 화면 기준으로 적으면 뷰가 바뀔 때마다 틀어진다.
 *
 * ⚠️ Closure(릴리스)가 점 표기 속성명을 바꾼다. 바깥에서 부를 이름은 obj['name'] 으로,
 *    DOM 속성은 setAttribute('data-dir', ...) 로 적는다.
 */

mergeInto(LibraryManager.library, {

    js_viewCubeInstall__deps: ['lot_onViewCube', '$LotUiTheme',
                               '$stringToNewUTF8', '$UTF8ToString', 'malloc', 'free'],
    js_viewCubeInstall: function() {
        if (!Module.lotDom) return 0;
        var dom = Module.lotDom;
        if (!dom.uiRoot) return 0;        // 메뉴/리본이 아직 - 다음 프레임에
        if (!dom.layerPanel) return 0;    // 패널도 - 상자 자리만큼 내려 줘야 한다
        if (dom.viewCube) return 1;       // 이미 만들었다

        var T = LotUiTheme;
        var HALF = 34;                    // 상자 반 변 (px)
        var RING = 58;                    // 나침반 고리 반지름 (상자 밑면에 눕는다)
        var PAD = 8;                      // 돌 때 고리 끝이 잘리지 않을 여백
        var BOX = (RING + PAD) * 2;

        // 면: 바깥 법선 n, 면의 오른쪽 u, 면의 위 v (모두 월드 축).
        // u x v 가 n 이 되게 잡아야 글자가 뒤집히지 않는다.
        var FACES = [
            { label: 'TOP',    n: [0, 0, 1],  u: [1, 0, 0],  v: [0, 1, 0]  },
            { label: 'BOTTOM', n: [0, 0, -1], u: [1, 0, 0],  v: [0, -1, 0] },
            { label: 'FRONT',  n: [0, -1, 0], u: [1, 0, 0],  v: [0, 0, 1]  },
            { label: 'BACK',   n: [0, 1, 0],  u: [-1, 0, 0], v: [0, 0, 1]  },
            { label: 'RIGHT',  n: [1, 0, 0],  u: [0, 1, 0],  v: [0, 0, 1]  },
            { label: 'LEFT',   n: [-1, 0, 0], u: [0, -1, 0], v: [0, 0, 1]  },
        ];
        // 요소의 로컬 축을 월드 축에 꽂는 행렬: +X -> u, +Y(아래) -> -v, +Z -> n.
        var basis = function(u, v, n) {
            return 'matrix3d(' + [u[0], u[1], u[2], 0,
                                  -v[0], -v[1], -v[2], 0,
                                  n[0], n[1], n[2], 0,
                                  0, 0, 0, 1].join(',') + ')';
        };

        var wrap = document.createElement('div');
        wrap.id = 'lot-viewcube';
        wrap.style.position = 'fixed';
        wrap.style.right = '12px';
        wrap.style.top = (dom.statusBar.offsetHeight + dom.uiHeight() + 8) + 'px';
        wrap.style.width = BOX + 'px';
        wrap.style.height = BOX + 'px';
        wrap.style.zIndex = '10';
        wrap.style.userSelect = 'none';
        // 틀은 고리까지 담느라 상자보다 크다 - 빈 자리 클릭은 캔버스로 흘려보내고 칸만 받는다
        wrap.style.pointerEvents = 'none';
        // 원근을 주지 않는다 - 직교라야 줌/시점과 상관없이 늘 같은 크기로 보인다.
        wrap.style.perspective = 'none';

        var box = document.createElement('div');
        box.style.position = 'absolute';
        box.style.left = '50%';
        box.style.top = '50%';
        box.style.width = '0';
        box.style.height = '0';
        box.style.transformStyle = 'preserve-3d';
        wrap.appendChild(box);

        // 한 칸이 가리키는 방향을 "dx,dy,dz" 로 적어 둔다. 누르면 그대로 C++ 로.
        var send = function(text) {
            var parts = text.split(',');
            _lot_onViewCube(Number(parts[0]), Number(parts[1]), Number(parts[2]));
        };

        var makeFace = function(face) {
            var el = document.createElement('div');
            el.style.position = 'absolute';
            el.style.left = -HALF + 'px';
            el.style.top = -HALF + 'px';
            el.style.width = (HALF * 2) + 'px';
            el.style.height = (HALF * 2) + 'px';
            el.style.backfaceVisibility = 'hidden';
            // 밝은 회색 상자 (AutoCAD 뷰큐브 느낌) - 어두운 도면 위에서 잘 보인다
            el.style.background = 'linear-gradient(160deg, #f4f4f6 0%, #d2d3d8 100%)';
            el.style.border = '1px solid #7c7f88';
            el.style.boxSizing = 'border-box';
            el.style.display = 'grid';
            el.style.gridTemplateColumns = '1fr 1fr 1fr';
            el.style.gridTemplateRows = '1fr 1fr 1fr';

            // 면을 월드 축에 꽂고 translateZ 로 상자 반 변만큼 바깥으로 민다.
            el.style.transform = basis(face.u, face.v, face.n) + ' translateZ(' + HALF + 'px)';

            for (var j = 0; j < 3; ++j) {
                for (var i = 0; i < 3; ++i) {
                    var cell = document.createElement('div');
                    // 가운데(1,1)는 면, 변은 모서리, 구석은 꼭짓점.
                    var dx = face.n[0] + face.u[0] * (i - 1) + face.v[0] * (1 - j);
                    var dy = face.n[1] + face.u[1] * (i - 1) + face.v[1] * (1 - j);
                    var dz = face.n[2] + face.u[2] * (i - 1) + face.v[2] * (1 - j);
                    cell.setAttribute('data-dir', dx + ',' + dy + ',' + dz);
                    cell.style.display = 'flex';
                    cell.style.alignItems = 'center';
                    cell.style.justifyContent = 'center';
                    cell.style.color = '#3a3d45';
                    cell.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
                    cell.style.fontSize = face.label.length > 4 ? '10px' : '12px';
                    cell.style.fontWeight = '700';
                    cell.style.letterSpacing = '0.5px';
                    cell.style.cursor = 'pointer';
                    cell.style.pointerEvents = 'auto';
                    if (i === 1 && j === 1) cell.textContent = face.label;
                    cell.addEventListener('mouseenter', function() {
                        this.style.background = 'rgba(66, 150, 250, 0.55)';
                    });
                    cell.addEventListener('mouseleave', function() {
                        this.style.background = '';
                    });
                    // 캔버스가 아니라 여기서 받는다 - 상자를 누른 것이 오브젝트 선택이
                    // 되면 안 된다. 상자는 캔버스의 형제라 이벤트가 번지지 않는다.
                    cell.addEventListener('mousedown', function(e) { e.stopPropagation(); });
                    cell.addEventListener('click', function(e) {
                        e.stopPropagation();
                        send(this.getAttribute('data-dir'));
                    });
                    el.appendChild(cell);
                }
            }
            return el;
        };

        // 나침반 고리: 상자 밑면 높이의 월드 XY 평면에 눕힌다. 상자와 같은 틀 안이라
        // 함께 돈다 - 평면도에서는 둥근 고리, 비스듬히 보면 타원이 된다. N = +Y.
        var ring = document.createElement('div');
        ring.style.position = 'absolute';
        ring.style.left = -RING + 'px';
        ring.style.top = -RING + 'px';
        ring.style.width = (RING * 2) + 'px';
        ring.style.height = (RING * 2) + 'px';
        ring.style.boxSizing = 'border-box';
        ring.style.borderRadius = '50%';
        ring.style.border = '7px solid rgba(150, 153, 162, 0.75)';
        ring.style.pointerEvents = 'none';    // 장식 - 뒤의 캔버스 클릭을 막지 않게
        ring.style.transform = basis([1, 0, 0], [0, 1, 0], [0, 0, 1]) + ' translateZ(' + (-HALF) + 'px)';
        // 글자: 로컬 (x, y) 는 화면 아래가 +y 이므로 N(+Y 월드) 은 위쪽(-y)
        [['N', 0, -1], ['E', 1, 0], ['S', 0, 1], ['W', -1, 0]].forEach(function(c) {
            var t = document.createElement('div');
            t.textContent = c[0];
            t.style.position = 'absolute';
            t.style.left = (RING - 7 + c[1] * (RING - 3.5) - 1) + 'px';
            t.style.top = (RING - 7 + c[2] * (RING - 3.5) - 1) + 'px';
            t.style.width = '14px';
            t.style.height = '14px';
            t.style.lineHeight = '14px';
            t.style.textAlign = 'center';
            t.style.fontFamily = '"Segoe UI", sans-serif';
            t.style.fontSize = '11px';
            t.style.fontWeight = '700';
            t.style.color = '#e8e8ec';
            t.style.textShadow = '0 0 2px #000';
            ring.appendChild(t);
        });
        box.appendChild(ring);

        for (var f = 0; f < FACES.length; ++f) box.appendChild(makeFace(FACES[f]));

        document.body.appendChild(wrap);
        dom.viewCube = box;
        dom.viewCubeWrap = wrap;
        dom['viewCubeClick'] = function(dir) { send(dir); };   // 테스트 도구가 부른다

        // ── 좌표축 표시 (왼쪽 아래) ─────────────────────────────
        // 네이티브 lot_gizmo.cpp 와 같은 모양: 월드 X/Y/Z 를 빨강·초록·파랑 선 + 원뿔 화살촉 +
        // 글자로. 뷰큐브와 같은 회전을 받아 js_viewCubeOrient 에서 다시 그린다 (누를 데는 없다).
        var SVGNS = 'http://www.w3.org/2000/svg';
        var axisSvg = document.createElementNS(SVGNS, 'svg');
        axisSvg.id = 'lot-axis-gizmo';
        var AX = dom.axisGizmo = {
            size: 52, line: 3, coneLen: 16, coneR: 6.5, labelPx: 15, labelGap: 11,
            svg: axisSvg,
        };
        AX.reach = AX.size + AX.labelGap + AX.labelPx + 4;   // 중심에서 글자 끝까지
        axisSvg.setAttribute('width', String(AX.reach * 2));
        axisSvg.setAttribute('height', String(AX.reach * 2));
        axisSvg.style.position = 'fixed';
        axisSvg.style.left = '4px';
        // 명령행(맨 아래 띠) 위에 놓는다
        var cmdH = dom.cmdInput ? dom.cmdInput.parentNode.offsetHeight : 28;
        axisSvg.style.bottom = (cmdH + 4) + 'px';
        axisSvg.style.zIndex = '10';
        axisSvg.style.pointerEvents = 'none';   // 장식이다 - 캔버스 클릭을 가로채지 않게
        document.body.appendChild(axisSvg);

        // 레이어 패널은 상자 아래로. 둘 다 오른쪽 위를 노리므로 자리를 나눈다.
        var panelTop = dom.statusBar.offsetHeight + dom.uiHeight() + 8 + BOX + 8;
        dom.layerPanel.style.top = panelTop + 'px';
        dom.layerPanel.style.maxHeight = 'calc(100vh - ' + (panelTop + 60) + 'px)';
        return 1;
    },

    // 자세 갱신. C++ 이 바뀔 때만 부른다.
    js_viewCubeOrient__deps: ['$UTF8ToString', '$lotDrawAxisGizmo'],
    js_viewCubeOrient: function(matrixPtr) {
        if (!Module.lotDom || !Module.lotDom.viewCube) return 0;
        var css = UTF8ToString(matrixPtr);
        Module.lotDom.viewCube.style.transform = 'matrix3d(' + css + ')';
        if (Module.lotDom.axisGizmo) lotDrawAxisGizmo(Module.lotDom.axisGizmo, css.split(',').map(Number));
        return 1;
    },

    // 좌표축 표시를 다시 그린다. m 은 뷰큐브에 건 matrix3d 16 개 (열 우선, 셋째 행 부호가
    // 뒤집혀 있다). 월드 축 i 가 화면에서 향하는 쪽 = 열 i: x 오른쪽, y 아래, -z 깊이(앞).
    $lotDrawAxisGizmo: function(AX, m) {
        var SVGNS = 'http://www.w3.org/2000/svg';
        var svg = AX.svg;
        while (svg.firstChild) svg.removeChild(svg.firstChild);
        var cx = AX.reach, cy = AX.reach;
        var rot = function(v) {   // 월드 -> 뷰 (x, y 는 화면 픽셀 방향, z 는 클수록 멀다)
            return [m[0] * v[0] + m[4] * v[1] + m[8] * v[2],
                    m[1] * v[0] + m[5] * v[1] + m[9] * v[2],
                    -(m[2] * v[0] + m[6] * v[1] + m[10] * v[2])];
        };
        var axes = [
            { dir: [1, 0, 0], color: 'rgb(220,60,60)',  label: 'X' },
            { dir: [0, 1, 0], color: 'rgb(60,200,60)',  label: 'Y' },
            { dir: [0, 0, 1], color: 'rgb(70,110,235)', label: 'Z' },
        ];
        axes.forEach(function(a) { a.eye = rot(a.dir); });
        // 먼 축부터 그려 앞쪽 원뿔이 위에 오게 (네이티브와 같은 화가 알고리즘)
        axes.sort(function(l, r) { return r.eye[2] - l.eye[2]; });

        var add = function(tag, attrs) {
            var el = document.createElementNS(SVGNS, tag);
            for (var k in attrs) el.setAttribute(k, String(attrs[k]));
            svg.appendChild(el);
            return el;
        };
        // 점들의 볼록 껍질 (원뿔을 투영한 윤곽 = 꼭지점 + 밑면 원의 껍질)
        var hull = function(pts) {
            pts.sort(function(a, b) { return a[0] - b[0] || a[1] - b[1]; });
            var cross = function(o, a, b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); };
            var lower = [], upper = [];
            pts.forEach(function(p) {
                while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) lower.pop();
                lower.push(p);
            });
            for (var i = pts.length - 1; i >= 0; --i) {
                var p = pts[i];
                while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) upper.pop();
                upper.push(p);
            }
            return lower.slice(0, -1).concat(upper.slice(0, -1));
        };

        axes.forEach(function(a) {
            var dx = a.eye[0], dy = a.eye[1];
            // 선은 원뿔이 시작하는 데까지만 (원뿔 안으로 비치지 않게)
            var lineLen = AX.size - AX.coneLen * 0.9;
            add('line', { x1: cx, y1: cy, x2: cx + dx * lineLen, y2: cy + dy * lineLen,
                          stroke: a.color, 'stroke-width': AX.line, 'stroke-linecap': 'round' });

            // 원뿔: 축에 수직인 두 벡터로 밑면 원을 만들어 투영한다
            var d = a.dir;
            var ref = Math.abs(d[2]) < 0.9 ? [0, 0, 1] : [1, 0, 0];
            var u = [d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0]];
            var ul = Math.hypot(u[0], u[1], u[2]);
            u = [u[0] / ul, u[1] / ul, u[2] / ul];
            var v = [d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0]];
            var pts = [[cx + dx * AX.size, cy + dy * AX.size]];
            var base = AX.size - AX.coneLen;
            for (var i = 0; i < 16; ++i) {
                var t = i / 16 * Math.PI * 2;
                var c = Math.cos(t) * AX.coneR, s = Math.sin(t) * AX.coneR;
                var e = rot([d[0] * base + u[0] * c + v[0] * s,
                             d[1] * base + u[1] * c + v[1] * s,
                             d[2] * base + u[2] * c + v[2] * s]);
                pts.push([cx + e[0], cy + e[1]]);
            }
            add('polygon', { points: hull(pts).map(function(p) { return p[0].toFixed(1) + ',' + p[1].toFixed(1); }).join(' '),
                             fill: a.color });

            // 글자 - 늘 정면으로 읽히게 화면 공간에
            var t2 = add('text', { x: cx + dx * (AX.size + AX.labelGap), y: cy + dy * (AX.size + AX.labelGap),
                                   fill: a.color, 'font-size': AX.labelPx, 'font-weight': 'bold',
                                   'font-family': 'Segoe UI, sans-serif',
                                   'text-anchor': 'middle', 'dominant-baseline': 'central' });
            t2.textContent = a.label;
        });
        add('circle', { cx: cx, cy: cy, r: 4, fill: 'rgb(210,210,210)' });   // 원점
    },
});

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
        var HALF = 42;                    // 상자 반 변 (px)
        var PAD = 16;                     // 상자가 돌 때 모서리가 잘리지 않을 여백
        var BOX = (HALF + PAD) * 2;

        // 면: 바깥 법선 n, 면의 오른쪽 u, 면의 위 v (모두 월드 축).
        // u x v 가 n 이 되게 잡아야 글자가 뒤집히지 않는다.
        var FACES = [
            { label: '평면', n: [0, 0, 1],  u: [1, 0, 0],  v: [0, 1, 0]  },
            { label: '저면', n: [0, 0, -1], u: [1, 0, 0],  v: [0, -1, 0] },
            { label: '정면', n: [0, -1, 0], u: [1, 0, 0],  v: [0, 0, 1]  },
            { label: '배면', n: [0, 1, 0],  u: [-1, 0, 0], v: [0, 0, 1]  },
            { label: '우측', n: [1, 0, 0],  u: [0, 1, 0],  v: [0, 0, 1]  },
            { label: '좌측', n: [-1, 0, 0], u: [0, -1, 0], v: [0, 0, 1]  },
        ];

        var wrap = document.createElement('div');
        wrap.id = 'lot-viewcube';
        wrap.style.position = 'fixed';
        wrap.style.right = '12px';
        wrap.style.top = (dom.statusBar.offsetHeight + dom.uiHeight() + 8) + 'px';
        wrap.style.width = BOX + 'px';
        wrap.style.height = BOX + 'px';
        wrap.style.zIndex = '10';
        wrap.style.userSelect = 'none';
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
            el.style.background = 'rgba(40, 44, 52, 0.92)';
            el.style.border = '1px solid ' + T.border;
            el.style.boxSizing = 'border-box';
            el.style.display = 'grid';
            el.style.gridTemplateColumns = '1fr 1fr 1fr';
            el.style.gridTemplateRows = '1fr 1fr 1fr';

            // 요소의 로컬 축을 월드 축에 꽂는다: +X -> u, +Y(아래) -> -v, +Z -> n.
            // 그 뒤 translateZ 로 상자 반 변만큼 바깥으로 민다.
            var m = [face.u[0], face.u[1], face.u[2], 0,
                     -face.v[0], -face.v[1], -face.v[2], 0,
                     face.n[0], face.n[1], face.n[2], 0,
                     0, 0, 0, 1];
            el.style.transform = 'matrix3d(' + m.join(',') + ') translateZ(' + HALF + 'px)';

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
                    cell.style.color = T.text;
                    cell.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
                    cell.style.fontSize = '11px';
                    cell.style.cursor = 'pointer';
                    if (i === 1 && j === 1) cell.textContent = face.label;
                    cell.addEventListener('mouseenter', function() {
                        this.style.background = T.accent;
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

        for (var f = 0; f < FACES.length; ++f) box.appendChild(makeFace(FACES[f]));

        document.body.appendChild(wrap);
        dom.viewCube = box;
        dom.viewCubeWrap = wrap;
        dom['viewCubeClick'] = function(dir) { send(dir); };   // 테스트 도구가 부른다

        // 레이어 패널은 상자 아래로. 둘 다 오른쪽 위를 노리므로 자리를 나눈다.
        var panelTop = dom.statusBar.offsetHeight + dom.uiHeight() + 8 + BOX + 8;
        dom.layerPanel.style.top = panelTop + 'px';
        dom.layerPanel.style.maxHeight = 'calc(100vh - ' + (panelTop + 60) + 'px)';
        return 1;
    },

    // 자세 갱신. C++ 이 바뀔 때만 부른다.
    js_viewCubeOrient__deps: ['$UTF8ToString'],
    js_viewCubeOrient: function(matrixPtr) {
        if (!Module.lotDom || !Module.lotDom.viewCube) return 0;
        Module.lotDom.viewCube.style.transform = 'matrix3d(' + UTF8ToString(matrixPtr) + ')';
        return 1;
    },
});

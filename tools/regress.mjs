// 회귀 테스트 - 헤드리스 크롬에서 기능들을 한 번씩 돌려 엔진 로그로 판정한다.
//
//   node tools/regress.mjs            전부
//   node tools/regress.mjs sketch     이름에 'sketch' 가 든 시나리오만
//
// 전제: build/ 를 http.server 8123 으로 서빙 중, 크롬이 --remote-debugging-port=9222 로
// 떠 있음 (README '헤드리스 테스트'). 뷰포트를 1100x850 으로 고정하므로 아래 좌표는
// 창 크기와 무관하다. 시나리오마다 페이지를 새로 열어 서로 영향을 주지 않는다.
//
// 결과 스크린샷: build/regress/<시각>/<시나리오>.png. 하나라도 실패하면 종료 코드 1.
//
// 판정은 눈이 아니라 로그다 - "sketch: circle committed" 같은 줄이 있는지. 화면이 이상해
// 보이면 스크린샷을 열어 본다 (렌더가 깨지는 회귀는 로그로 못 잡는다).
import { existsSync, mkdirSync } from 'node:fs';
import { connect, sleep } from './cdp.mjs';

const filter = process.argv[2] ?? '';
const kNativeScene = process.env.LOT_NATIVE_SCENE ?? 'D:/vulkan/3dengine/tests/data/mmWall.lot';

// 1100x850 뷰포트, Top 뷰(T) 기준 좌표. 캔버스는 y = 150 부터 700px, 카메라 거리 4 에
// fov 50 도라 1 월드 단위 ≈ 187px, 월드 원점 = 화면 (550, 500). 화면 = (550 + 187x, 500 - 187y).
// 큐브: 노란 (270, 500), 파란 (830, 500), 한 변 ≈ 112px. 토러스 중심 (550, 500) 반지름 ≈ 155px.
// UI 가 가리는 곳은 쓰지 않는다: 메뉴바+리본 y < 270, 레이어 패널 x > 870 / y 270..400,
// 안내문은 아래 가운데, 명령행은 y > 820 (맨 아래 띠). 캔버스가 성한 자리 = y 400..810.
// 스케치 osnap 시험은 기본 씬의 정점에 걸리지 않게 y > 690 띠에서 그린다.
const P = {
  yellowCube: [270, 500],
  emptyFloor: [740, 640],
};

const scenarios = [
  {
    name: 'boot',
    async run(t) {
      t.expect(await t.api.evaluate(`!!document.getElementById('lot-ui')`), 'menu bar and ribbon mounted');
      t.expect(await t.api.cmdActive('gizmo.move') === true, 'move gizmo shown as active');
      t.expect(await t.api.cmdActive('edit.undo') === false, 'undo not active yet');
      t.expect(!t.api.has(/ERROR/), 'no ERROR in startup log');
    },
  },
  {
    name: 'views-and-camera',
    async run(t) {
      const a = t.api;
      for (const [code, name] of [['KeyT', 'top'], ['KeyF', 'front'], ['KeyR', 'right'], ['KeyI', 'isometric']]) {
        await a.key(code);
        t.expect(a.has(new RegExp(`view: ${name}`)), `preset ${name}`);
      }
      await a.drag(700, 400, 800, 430, 'right');
      await a.drag(700, 400, 750, 380, 'middle');
      await a.wheel(700, 400, -3);
      await a.key('KeyZ');
      t.expect(a.has(/view: zoom extents/), 'zoom extents (Z)');
      await a.key('KeyP');
      t.expect(a.has(/projection: orthographic/), 'parallel projection (P)');
      await a.key('KeyV');
      t.expect(a.has(/view: fps/), 'fps toggle (V)');
      await a.hold('KeyW', 200);
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 휠 줌은 커서 기준: 커서 아래 점이 줌 전후로 같은 화면 자리에 남는다 (원근 · 평행 둘 다)
    name: 'zoom-at-cursor',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      for (const [label, toggle] of [['perspective', null], ['parallel', 'KeyP']]) {
        // 투영을 바꾸면 배율이 달라지므로 바꾼 뒤에 그린다.
        // 화면 가운데에서 먼 곳에 짧은 세로선 - 가운데 기준 줌이면 몇 노치 만에 커서에서 멀어진다
        if (toggle) await a.key(toggle);
        await a.key('KeyL'); await a.click(950, 700); await a.click(950, 790); await a.key('Enter');
        await a.key('Escape');
        for (const notches of [3, 3, 3, -3, -3]) await a.wheel(950, 745, notches);
        const before = a.count(/pick: sketch/);
        await a.click(950, 745);
        t.expect(a.count(/pick: sketch/) > before, `${label}: the line stays under the cursor`);
        await a.key('Escape');
      }
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 폴리선을 그리는 중에 자기 꼭짓점 · 변 중점에도 스냅이 걸린다 (아직 객체가 아니어도)
    name: 'polyline-self-snap',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyN');
      await a.click(250, 720); await a.click(450, 720); await a.click(450, 800);
      await a.move(350, 722);
      t.expect(a.has(/snap: midpoint of the shape being drawn/), 'midpoint of its own segment');
      await a.move(252, 719);
      t.expect(a.has(/snap: endpoint of the shape being drawn/), 'its own first vertex');
      await a.key('Escape');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 도킹: 기본 배치 (오른쪽 도크에 레이어 / 속성+노드 트리), 옮기기 · 띄우기 · 닫기 · 다시 열기,
    // 노드 트리로 고르기 · 숨기기 · 줌, 속성으로 위치 고치기
    name: 'dock-panels',
    async run(t) {
      const a = t.api;
      await a.evaluate(`localStorage.removeItem('lot.dock.v1')`);
      await a.reload('http://localhost:8123/WebGPUApp.html');
      const st = async () => JSON.parse(await a.evaluate(`Module.lotDom['dockState']()`));
      const canvasLeftWidth = () => a.evaluate(`(() => { const r = document.getElementById('webgpu-canvas').getBoundingClientRect(); return [r.left, r.width]; })()`);
      let s = await st();
      t.expect(s.right.groups.length === 2 && s.right.groups[1].panels.join() === 'props,tree', `default layout (${JSON.stringify(s.right.groups)})`);
      let [left, width] = await canvasLeftWidth();
      t.expect(left === 0 && width === 1100 - s.right.width, `canvas leaves room for the right dock (${left}, ${width})`);

      // 하단 바 [속성] 단추: 열린 패널은 켜짐, 누르면 닫고 다시 누르면 연다
      const btn = (id) => `document.querySelector('#lot-status-toggles [data-cmd="panel.${id}"]')`;
      t.expect(await a.evaluate(`${btn('props')}.getAttribute('data-on')`) === '1', 'status bar shows the open properties panel');
      await a.evaluate(`${btn('props')}.click()`);
      t.expect(!JSON.stringify(await st()).includes('props') && await a.evaluate(`${btn('props')}.getAttribute('data-on')`) === '0', 'status bar button closes it');
      await a.evaluate(`${btn('props')}.click()`);
      t.expect(JSON.stringify(await st()).includes('props') && await a.evaluate(`${btn('props')}.getAttribute('data-on')`) === '1', 'and opens it again');

      // 노드 트리: 층 0 펼치기 → 객체 고르기 · 숨기기 · 더블 클릭 줌
      await a.evaluate(`Module.lotDom['dockOpen']('tree')`);
      await a.evaluate(`document.querySelector('[data-tree-layer="0"] span').click()`);
      await sleep(200);
      const firstId = await a.evaluate(`document.querySelector('[data-tree-object]')?.getAttribute('data-tree-object')`);
      t.expect(firstId !== undefined && firstId !== null, 'layer 0 expands into objects');
      await a.evaluate(`document.querySelector('[data-tree-object="${firstId}"]').click()`);
      await sleep(200);
      t.expect(a.has(new RegExp(`tree: select object ${firstId}`)), 'click selects');
      await a.evaluate(`document.querySelector('[data-tree-object="${firstId}"]').dispatchEvent(new MouseEvent('dblclick', {bubbles: true}))`);
      await sleep(200);
      t.expect(a.has(/view: zoom to 1 objects/), 'double click zooms to it');

      // 속성: 고른 것의 X 를 고친다 (실행 취소로 돌아간다)
      await a.evaluate(`Module.lotDom['dockOpen']('props')`);
      await sleep(200);
      const x = await a.evaluate(`document.querySelector('[data-prop="x"]')?.value`);
      t.expect(x !== undefined, 'properties show the position');
      await a.evaluate(`(() => { const i = document.querySelector('[data-prop="x"]'); i.focus(); i.value = '12.5'; i.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()`);
      await sleep(300);
      t.expect(a.has(/property: x = 12.5/), 'X edited');
      t.expect(await a.evaluate(`document.querySelector('[data-prop="x"]').value`) === '12.5', 'panel shows the new X');
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo property/), 'undo after a property edit');

      // 숨기기 (객체 체크박스)
      await a.evaluate(`Module.lotDom['dockOpen']('tree')`);
      await sleep(200);
      await a.evaluate(`document.querySelector('[data-tree-object="${firstId}"] input').click()`);
      await sleep(200);
      t.expect(a.has(/tree: hid 1 objects/), 'checkbox hides the object');
      await a.evaluate(`document.querySelector('[data-tree-scene] input').click()`);
      await sleep(200);
      t.expect(a.has(/tree: showed 1 objects/), 'scene checkbox shows everything again');

      // 옮기기: 노드 트리 → 왼쪽 도크, 속성 → 떠 있게, 레이어 닫고 뷰 메뉴로 다시
      await a.evaluate(`Module.lotDom['dockMove']('tree', 'dock', 'left')`);
      await sleep(400);
      s = await st();
      [left, width] = await canvasLeftWidth();
      t.expect(s.left.groups.length === 1 && s.left.groups[0].panels[0] === 'tree', 'tree docked left');
      t.expect(left === s.left.width && width === 1100 - s.left.width - s.right.width, `canvas between both docks (${left}, ${width})`);
      await a.evaluate(`Module.lotDom['dockMove']('props', 'float')`);
      s = await st();
      t.expect(s.floats.some(f => f.panel === 'props'), 'properties float');
      await a.evaluate(`Module.lotDom['dockClose']('layers')`);
      s = await st();
      t.expect(!JSON.stringify(s).includes('layers'), 'layers closed');
      await a.evaluate(`document.querySelector('[data-cmd="panel.layers"]').click()`);
      s = await st();
      t.expect(JSON.stringify(s).includes('layers'), 'view menu reopens layers');
      // 저장된 배치는 새로고침 뒤에도 남는다
      await a.reload('http://localhost:8123/WebGPUApp.html');
      s = await st();
      t.expect(s.left.groups.length === 1 && s.floats.some(f => f.panel === 'props'), 'layout survives a reload');
      await a.evaluate(`localStorage.removeItem('lot.dock.v1')`);
      // 전체 실행에서 가끔 ERROR 가 찍힌 적이 있다 (단독으로는 재현 안 됨) - 내용을 남긴다
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 대칭: 두 점 대칭축, 기본은 원본을 두고 사본, Shift+클릭은 원본을 지운다. 한 번에 되돌아간다.
    name: 'mirror-tool',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(600, 720); await a.click(700, 760); await a.key('Enter');
      await a.key('Escape');
      // 선 객체들의 월드 중점 x (원본 · 사본 구별용)
      const lineMids = async () => a.evaluate(`JSON.parse(Module.lotDom.sceneSave())['objects']`
        + `.filter(o => o['kind'] === 'line' || (o['kind'] === 'polyline' && o['polyline']['verts'].length === 2))`
        + `.map(o => o['transform']['t'][0]).sort((p, q) => p - q)`);
      const before = await lineMids();
      t.expect(before.length === 1, `one line drawn (${JSON.stringify(before)})`);

      await a.click(650, 740);   // 선 고르기
      await a.command('mirror');
      t.expect(a.has(/transform: mirror - click base point/), 'mirror starts with the selection');
      await a.click(750, 650); await a.click(750, 800);   // 세로 대칭축 (화면 x = 750)
      t.expect(a.has(/transform: mirror done - 1 objects, source kept/), 'mirror copy placed');
      const after = await lineMids();
      // 화면 x 750 = 월드 (750 - 550) / 187. 사본 중점 = 2 * 축 - 원본
      const axis = (750 - 550) / 187;
      t.expect(after.length === 2 && Math.abs(after[1] - (2 * axis - before[0])) < 0.02,
               `copy mirrored across the axis (${JSON.stringify(after)}, axis ${axis.toFixed(3)})`);

      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo mirror/) && (await lineMids()).length === 1, 'one undo removes the copy');

      await a.click(650, 740);
      await a.command('mirror');
      await a.click(750, 650); await a.shiftClick(750, 800);
      t.expect(a.has(/mirror done - 1 objects, source erased/), 'Shift+click erases the source');
      const moved = await lineMids();
      t.expect(moved.length === 1 && Math.abs(moved[0] - (2 * axis - before[0])) < 0.02, `only the mirrored line is left (${JSON.stringify(moved)})`);
      await a.ctrl('KeyZ');
      const back = await lineMids();
      t.expect(back.length === 1 && Math.abs(back[0] - before[0]) < 1e-4, 'undo brings the source back');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 간격띄우기: 거리 -> 객체 -> 방향 (반복). 선 · 원(안쪽은 거부) · 닫힌 사각형(꼭짓점 맞붙임)
    name: 'offset-tool',
    async run(t) {
      const a = t.api;
      const objs = async () => a.evaluate(`JSON.parse(Module.lotDom.sceneSave())['objects']`);
      const dist = (p, q) => Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(600, 720); await a.click(800, 720); await a.key('Enter');
      await a.key('KeyC'); await a.click(300, 720); await a.click(360, 720);
      await a.key('KeyB'); await a.click(420, 700); await a.click(520, 790);
      await a.key('Escape');
      const before = await objs();
      const line0 = before.find(o => o['kind'] === 'line');
      const circle0 = before.find(o => o['kind'] === 'circle');
      const rect0 = before.find(o => o['kind'] === 'polyline' && o['polyline']['closed']);
      t.expect(line0 && circle0 && rect0, 'line, circle and rectangle drawn');

      await a.command('offset');
      t.expect(a.has(/offset: type the distance/), 'offset asks for a distance');
      await a.command('0.5');
      t.expect(a.has(/offset: distance 0.5/), 'distance 0.5');
      // 선: 위쪽으로
      await a.click(700, 720); await a.click(700, 650);
      t.expect(a.has(/offset: created object/), 'line offset created');
      // 원: 안쪽은 반지름 0.32 - 0.5 < 0 이라 거부, 바깥은 r + 0.5
      await a.click(360, 720); await a.click(310, 720);
      t.expect(a.has(/offset: the circle would vanish/), 'inside offset larger than the radius is refused');
      await a.click(360, 720); await a.click(420, 640);
      // 닫힌 사각형: 바깥으로
      await a.click(420, 745); await a.click(380, 745);
      await a.key('Escape');
      t.expect(a.has(/offset: finished/), 'Esc ends the tool');

      const after = await objs();
      const ids = new Set(before.map(o => o['id'] ?? JSON.stringify(o)));
      const fresh = after.filter(o => !before.some(b => JSON.stringify(b) === JSON.stringify(o)));
      const line1 = fresh.find(o => o['kind'] === 'line');
      t.expect(line1 && Math.abs(dist(line1['transform']['t'], line0['transform']['t']) - 0.5) < 1e-3,
               `line moved by exactly 0.5 (${line1 && dist(line1['transform']['t'], line0['transform']['t'])})`);
      const circle1 = fresh.find(o => o['kind'] === 'circle');
      t.expect(circle1 && Math.abs(circle1['circle']['radius'] - circle0['circle']['radius'] - 0.5) < 1e-4,
               `circle radius + 0.5 (${circle1 && circle1['circle']['radius']} vs ${circle0['circle']['radius']})`);
      const rect1 = fresh.find(o => o['kind'] === 'polyline' && o['polyline']['closed']);
      const perim = (o) => { const v = o['polyline']['verts']; let s = 0; for (let i = 0; i < v.length; ++i) s += dist(v[i], v[(i + 1) % v.length]); return s; };
      t.expect(rect1 && rect1['polyline']['verts'].length === 4 && Math.abs(perim(rect1) - perim(rect0) - 4) < 1e-3,
               `rectangle offset outward keeps 4 corners, perimeter + 4 (${rect1 && perim(rect1)} vs ${perim(rect0)})`);
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo offset/), 'undo removes an offset copy');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 터치: 탭 = 클릭, 한 손가락 끌기 = 팬, 두 손가락 = 궤도 + 핀치 줌 (휴대폰)
    name: 'touch-gestures',
    async run(t) {
      const a = t.api;
      const touch = (type, pts) => a.send('Input.dispatchTouchEvent', {
        type, touchPoints: pts.map(([x, y], i) => ({ x, y, id: i + 1 })) });
      const drag = async (from, to, steps = 10) => {   // 손가락 여럿을 같이 옮긴다
        await touch('touchStart', from);
        for (let s = 1; s <= steps; ++s) {
          await touch('touchMove', from.map(([x, y], i) => [x + (to[i][0] - x) * s / steps, y + (to[i][1] - y) * s / steps]));
          await sleep(40);
        }
        await touch('touchEnd', []);
        await sleep(300);
      };
      const tap = async (x, y) => { await touch('touchStart', [[x, y]]); await sleep(60); await touch('touchEnd', []); await sleep(400); };

      await a.key('KeyT');
      await a.key('KeyL'); await a.click(900, 720); await a.click(900, 790); await a.key('Enter');
      await a.key('Escape');
      let picks = a.count(/pick: sketch/);
      await tap(900, 755);
      t.expect(a.count(/pick: sketch/) > picks, 'tap picks the line');
      await a.key('Escape');

      // 한 손가락 끌기 = 팬: 선이 손가락만큼 왼쪽으로 와 있다
      await drag([[700, 600]], [[580, 600]]);
      picks = a.count(/pick: sketch/);
      await tap(780, 755);
      t.expect(a.count(/pick: sketch/) > picks, 'one-finger drag pans the drawing with the finger');
      await a.key('Escape');

      // 두 손가락 벌리기 = 줌 인 (회전은 없이)
      const m0 = await a.viewCubeMatrix();
      const wpp0 = await a.evaluate(`document.getElementById('webgpu-canvas').width`);
      await drag([[500, 500], [600, 500]], [[400, 500], [700, 500]]);
      picks = a.count(/pick: sketch/);
      // 줌 인 했으면 원래 자리(780)의 선은 화면 밖으로 밀려났다 - 그 자리 탭은 비어야 한다
      await tap(780, 755);
      t.expect(a.count(/pick: sketch/) === picks, 'pinch zooms in (the line moved off that spot)');
      // 두 손가락을 같이 옮기면 궤도 - 뷰큐브 자세가 바뀐다
      await drag([[500, 500], [600, 500]], [[500, 380], [600, 380]]);
      t.expect(await a.viewCubeMatrix() !== m0, 'two-finger drag orbits');

      // 뷰큐브: 손가락 탭 = 그 칸 방향, 손가락으로 끌기 = 회전 (칸 누름이 아니다)
      await a.key('KeyT');
      const cellAt = (dir) => a.evaluate(`(() => { const r = document.querySelector('[data-dir="${dir}"]').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()`);
      const [cx, cy] = await cellAt('0,0,1');   // 평면도에서 보이는 TOP 가운데 칸
      let cubeLogs = a.count(/viewcube:/);
      await tap(cx, cy);
      t.expect(a.count(/viewcube: face \(0, 0, 1\)/) >= 1, 'tapping the TOP face on the view cube');
      cubeLogs = a.count(/viewcube:/);
      const m1 = await a.viewCubeMatrix();
      await drag([[cx, cy]], [[cx + 60, cy + 30]]);
      t.expect(await a.viewCubeMatrix() !== m1, 'dragging the view cube orbits');
      t.expect(a.count(/viewcube:/) === cubeLogs, 'a drag is not taken as a cell press');
      // 마우스로 칸 누르기도 그대로 (포인터 이벤트로 바꾼 뒤)
      await a.key('KeyT');
      const [mx, my] = await cellAt('0,0,1');
      const faces = a.count(/viewcube: face \(0, 0, 1\)/);
      await a.click(mx, my);
      t.expect(a.count(/viewcube: face \(0, 0, 1\)/) > faces, 'mouse click on a view cube cell');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 휴대폰 흉내: 로그 창 숨김, 세로 -> 가로 -> 세로로 돌려도 캔버스 백버퍼가 화면 크기를 따라간다
    name: 'phone-rotation',
    async run(t) {
      const a = t.api;
      const metrics = (w, h, landscape) => a.send('Emulation.setDeviceMetricsOverride', {
        width: w, height: h, deviceScaleFactor: 3, mobile: true, screenWidth: w, screenHeight: h,
        screenOrientation: landscape ? { type: 'landscapePrimary', angle: 90 } : { type: 'portraitPrimary', angle: 0 } });
      const fit = () => a.evaluate(`(() => { const c = document.getElementById('webgpu-canvas'); const r = c.getBoundingClientRect();`
        + ` return { back: [c.width, c.height], css: [Math.round(r.width), Math.round(r.height)], bar: document.getElementById('status-bar').offsetHeight }; })()`);
      try {
        await a.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
        await metrics(400, 860, false);
        await a.reload();
        let f = await fit();
        t.expect(f.bar === 0, `log bar hidden on a phone (${f.bar})`);
        t.expect(f.back[0] === f.css[0] && f.back[1] === f.css[1], `portrait: backbuffer matches the canvas (${JSON.stringify(f)})`);
        await metrics(860, 400, true);
        await a.evaluate(`window.dispatchEvent(new Event('orientationchange'))`);
        await sleep(1000);
        f = await fit();
        t.expect(f.back[0] === f.css[0] && f.back[1] === f.css[1] && f.css[0] > f.css[1], `landscape: backbuffer follows (${JSON.stringify(f)})`);
        await metrics(400, 860, false);
        await a.evaluate(`window.dispatchEvent(new Event('orientationchange'))`);
        await sleep(1000);
        f = await fit();
        t.expect(f.back[0] === f.css[0] && f.back[1] === f.css[1] && f.css[1] > f.css[0], `back to portrait (${JSON.stringify(f)})`);
        t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
      } finally {
        await a.send('Emulation.setTouchEmulationEnabled', { enabled: false });
        await a.send('Emulation.clearDeviceMetricsOverride');
        await a.setViewport(1100, 850);
      }
    },
  },
  {
    // 자르기 / 연장 (빠른 모드: 모든 선이 경계)
    name: 'trim-extend',
    async run(t) {
      const a = t.api;
      const U = 187;   // 평면도 1 단위 ≈ 187px
      const objs = async () => a.evaluate(`JSON.parse(Module.lotDom.sceneSave())['objects']`);
      const dist = (p, q) => Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
      const lineLens = async () => (await objs()).filter(o => o['kind'] === 'line')
        .map(o => dist(o['line']['a'], o['line']['b'])).sort((x, y) => x - y);
      const line = async (x0, y0, x1, y1) => { await a.key('KeyL'); await a.click(x0, y0); await a.click(x1, y1); await a.key('Enter'); await a.key('Escape'); };
      await a.key('KeyT');
      await a.key('F3');   // 객체스냅 끔 - 찍은 픽셀 그대로
      await line(600, 720, 900, 720);   // 가로
      await line(680, 650, 680, 790);   // 세로 1
      await line(820, 560, 820, 790);   // 세로 2 (위로 길게 - 연장 경계)
      await line(700, 600, 740, 600);   // 짧은 선 (연장 대상)
      await a.key('KeyC'); await a.click(450, 720); await a.click(510, 720); await a.key('Escape');
      await line(450, 640, 450, 800);   // 원을 지나는 세로선 (하단 바는 y 820 부터)

      await a.command('trim');
      t.expect(a.has(/trim: click the part to cut away/), 'trim starts');
      await a.click(750, 720);   // 세로 둘 사이
      t.expect(a.has(/trim: object \d+ cut into 2 pieces/), 'middle cut out');
      let lens = await lineLens();
      const piece = 80 / U;
      t.expect(lens.filter(l => Math.abs(l - piece) < 0.01).length === 2, `two pieces of ${piece.toFixed(3)} left (${lens.map(l => l.toFixed(3))})`);
      await a.click(630, 720);   // 왼쪽 조각: 끝이 세로선에 닿을 뿐 - 경계 없음 -> 지움
      t.expect(a.has(/deleted \(no cutting edge\)/), 'a piece with no cutting edge is deleted');
      await a.click(510, 720);   // 원 오른쪽 -> 왼쪽 반원(호)만 남는다
      const arc = (await objs()).find(o => o['kind'] === 'arc');
      t.expect(arc && Math.abs((arc['arc']['end'] - arc['arc']['start']) - Math.PI) < 0.01,
               `circle trimmed to a half arc (${arc && (arc['arc']['end'] - arc['arc']['start'])})`);
      // Shift+클릭 = 연장: 짧은 선의 오른쪽 끝을 세로 2 (x=820) 까지
      await a.shiftClick(735, 600);
      t.expect(a.has(/extend: object \d+ extended/), 'Shift+click extends while trimming');
      lens = await lineLens();
      t.expect(lens.some(l => Math.abs(l - 120 / U) < 0.01), `short line reaches x=820 (${lens.map(l => l.toFixed(3))})`);
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo extend/), 'undo the extension');
      await a.key('Escape');
      t.expect(a.has(/trim: finished/), 'Esc ends');

      // 연장 명령: 같은 짧은 선을 다시 연장
      await a.command('extend');
      await a.click(735, 600);
      lens = await lineLens();
      t.expect(lens.some(l => Math.abs(l - 120 / U) < 0.01), 'extend command reaches the boundary');
      await a.key('Escape');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 필렛 / 모따기 (네이티브 first_app/modify.cpp 와 같은 흐름: 값은 언제든 숫자 + Enter, 반복, Esc 끝)
    name: 'fillet-chamfer',
    async run(t) {
      const a = t.api;
      const U = 187;
      const objs = async () => a.evaluate(`JSON.parse(Module.lotDom.sceneSave())['objects']`);
      const dist = (p, q) => Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
      const lineLens = async () => (await objs()).filter(o => o['kind'] === 'line')
        .map(o => dist(o['line']['a'], o['line']['b'])).sort((x, y) => x - y);
      const near = (arr, v, e = 0.01) => arr.some(x => Math.abs(x - v) < e);
      const line = async (x0, y0, x1, y1) => { await a.key('KeyL'); await a.click(x0, y0); await a.click(x1, y1); await a.key('Enter'); await a.key('Escape'); };
      await a.key('KeyT');
      await a.key('F3');
      await line(650, 720, 850, 720);   // 가로 (오른쪽 끝이 모서리에 못 미친다)
      await line(900, 650, 900, 790);   // 세로 (모서리를 지나친다)

      await a.command('fillet');
      t.expect(a.has(/fillet R=.*click the first line/), 'fillet starts');
      await a.command('0.2');
      t.expect(a.has(/fillet: radius 0.2/), 'radius typed');
      await a.click(750, 720); await a.click(900, 690);   // 세로는 위쪽(클릭한 쪽)을 남긴다
      t.expect(a.has(/fillet: done \(R=0.2, new object\)/), 'fillet applied');
      let lens = await lineLens();
      t.expect(near(lens, (900 - 650) / U - 0.2) && near(lens, (720 - 650) / U - 0.2), `lines meet the tangent points (${lens.map(l => l.toFixed(3))})`);
      const arc = (await objs()).find(o => o['kind'] === 'arc');
      t.expect(arc && Math.abs(arc['arc']['radius'] - 0.2) < 1e-4 && Math.abs(arc['arc']['end'] - arc['arc']['start'] - Math.PI / 2) < 1e-3,
               `R0.2 quarter arc (${arc && JSON.stringify(arc['arc'])})`);
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo fillet/) && !(await objs()).some(o => o['kind'] === 'arc'), 'one undo restores both lines and removes the arc');

      // 실행 취소는 열린 도구를 닫는다 (다른 도구들과 같은 규칙) - 다시 연다
      await a.command('fillet');
      // 반지름이 너무 크면 거부
      await a.command('2');
      await a.click(750, 720); await a.click(900, 690);
      t.expect(a.has(/fillet: the radius is too large/), 'too large radius refused');
      // R0 = 맞붙인 모서리, 새 객체 없음
      await a.command('0');
      await a.click(750, 720); await a.click(900, 690);
      lens = await lineLens();
      t.expect(a.has(/fillet: done \(R=0\)/) && near(lens, (900 - 650) / U) && near(lens, (720 - 650) / U), `R0 corner (${lens.map(l => l.toFixed(3))})`);
      await a.command('fillet');
      await a.ctrl('KeyZ');   // R0 를 되돌린다 (도구도 닫힌다)
      t.expect(a.has(/fillet: finished/), 'undo closes the tool');

      // 모따기 D=0.2: 90도라 접점은 필렛과 같고 사이는 길이 0.2√2 의 선
      await a.command('chamfer');
      await a.command('0.2');
      await a.click(750, 720); await a.click(900, 690);
      lens = await lineLens();
      t.expect(near(lens, 0.2 * Math.SQRT2, 0.005), `chamfer line 0.2*sqrt2 (${lens.map(l => l.toFixed(3))})`);
      await a.key('Escape');

      // 사각형 모서리 (같은 폴리선의 이웃한 두 변): 둘레가 r(π/2 - 2) 만큼 준다
      await a.key('KeyB'); await a.click(120, 660); await a.click(320, 790); await a.key('Escape');
      const perim = (o) => { const v = o['polyline']['verts']; let s = 0; for (let i = 0; i < v.length; ++i) s += dist(v[i], v[(i + 1) % v.length]); return s; };
      const rect0 = (await objs()).find(o => o['kind'] === 'polyline' && o['polyline']['closed']);
      await a.command('fillet'); await a.command('0.2');
      await a.click(220, 660); await a.click(320, 725);
      const rect1 = (await objs()).find(o => o['kind'] === 'polyline' && o['polyline']['closed']);
      const expected = perim(rect0) + 0.2 * (Math.PI / 2 - 2);
      t.expect(rect1 && rect1['polyline']['verts'].length > 4 && Math.abs(perim(rect1) - expected) < 0.003,
               `rectangle corner rounded (${rect1 && perim(rect1).toFixed(4)} vs ${expected.toFixed(4)})`);
      await a.key('Escape');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 배열 (네이티브 lot_array_dialog 와 같은 항목): 직사각형 / 원형, 한 번에 실행 취소, 360 도 겹침 없음
    name: 'array-tool',
    async run(t) {
      const a = t.api;
      const objs = async () => a.evaluate(`JSON.parse(Module.lotDom.sceneSave())['objects']`);
      const lineTs = async () => (await objs()).filter(o => o['kind'] === 'line').map(o => o['transform']['t']);
      const setField = (key, v) => a.evaluate(`(() => { const i = document.querySelector('[data-array="${key}"]');`
        + ` if (i.type === 'checkbox' || i.type === 'radio') { i.checked = ${JSON.stringify(v)}; i.dispatchEvent(new Event('change')); }`
        + ` else { i.value = ${JSON.stringify(String(v))}; i.dispatchEvent(new Event('input')); } })()`);
      const press = (name) => a.evaluate(`document.querySelector('[data-array="${name}"]').click()`);
      const dialogOpen = () => a.evaluate(`getComputedStyle(document.getElementById('lot-array-dialog') || document.body).display !== 'none' && !!document.getElementById('lot-array-dialog')`);
      await a.key('KeyT');
      await a.key('F3');
      await a.key('KeyL'); await a.click(650, 720); await a.click(700, 720); await a.key('Enter'); await a.key('Escape');

      // 선택 없이: 고르고 Enter 를 기다린다
      await a.command('array');
      t.expect(a.has(/array: select objects, then Enter/), 'asks for a selection first');
      await a.click(675, 720);
      await a.key('Enter');
      await sleep(200);
      t.expect(await dialogOpen(), 'Enter with a selection opens the dialog');

      // 직사각형 4 x 1, 간격 0.5
      await setField('rect', true);
      await setField('cols', 4); await setField('rows', 1); await setField('dx', 0.5);
      await sleep(150);
      t.expect(/복사본 3/.test(await a.evaluate(`document.getElementById('lot-array-dialog').textContent`)), 'dialog shows 3 copies');
      await press('create');
      await sleep(200);
      t.expect(a.has(/array: created 3 copies \(rectangular\)/), 'rectangular array created');
      t.expect(!(await dialogOpen()), 'dialog closes after create');
      const xs = (await lineTs()).map(p => p[0]).sort((p, q) => p - q);
      const steps = xs.slice(1).map((x, i) => x - xs[i]);
      t.expect(xs.length === 4 && steps.every(d => Math.abs(d - 0.5) < 1e-3), `spacing 0.5 (${xs.map(x => x.toFixed(3))})`);
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo array/) && (await lineTs()).length === 1, 'one undo removes all copies');

      // 원형 4 개, 360 도, 중심 = 선 가운데에서 0.5 떨어진 곳 (직접 입력)
      await a.click(675, 720);
      await a.command('array');
      await setField('polar', true);
      await setField('count', 4); await setField('angle', 360); await setField('rotate', true);
      await setField('centerAuto', false);
      const U = 187, cx = (675 - 550) / U, cy = (500 - 720) / U + 0.5;
      await setField('cx', cx); await setField('cy', cy); await setField('cz', 0);
      await sleep(150);
      await press('create');
      await sleep(200);
      t.expect(a.has(/array: created 3 copies \(polar\)/), 'polar array created');
      const ts = await lineTs();
      const cen = [0, 1, 2].map(k => ts.reduce((s, p) => s + p[k], 0) / ts.length);
      const r = ts.map(p => Math.hypot(p[0] - cen[0], p[1] - cen[1], p[2] - cen[2]));
      let minGap = 1e9;
      for (let i = 0; i < ts.length; ++i) for (let j = i + 1; j < ts.length; ++j) minGap = Math.min(minGap, Math.hypot(ts[i][0] - ts[j][0], ts[i][1] - ts[j][1], ts[i][2] - ts[j][2]));
      // 픽셀 -> 월드 환산(187)이 근사라 반지름은 0.5 언저리 - 넷이 같고 이웃 간격이 r√2 (= 90 도) 인지 본다
      t.expect(ts.length === 4 && r.every(v => Math.abs(v - r[0]) < 1e-3) && Math.abs(r[0] - 0.5) < 0.02
               && Math.abs(minGap - r[0] * Math.SQRT2) < 2e-3,
               `4 items 90 degrees apart around the centre, none on top of another (r ${r.map(v => v.toFixed(3))}, gap ${minGap.toFixed(3)})`);
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 분해 / 결합 / 끊기 (네이티브 applyExplode / joinChains / break.cpp 와 같은 규칙)
    name: 'explode-join-break',
    async run(t) {
      const a = t.api;
      const U = 187;
      const objs = async () => a.evaluate(`JSON.parse(Module.lotDom.sceneSave())['objects']`);
      const dist = (p, q) => Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
      const lineLens = async () => (await objs()).filter(o => o['kind'] === 'line').map(o => dist(o['line']['a'], o['line']['b'])).sort((x, y) => x - y);
      const near = (arr, v, e = 0.01) => arr.some(x => Math.abs(x - v) < e);
      await a.key('KeyT');
      await a.key('F3');
      // 분해: 사각형 -> 선 4 개
      await a.key('KeyB'); await a.click(150, 650); await a.click(300, 780); await a.key('Escape');
      await a.click(150, 700);
      await a.command('explode');
      t.expect(a.has(/explode: 1 objects -> 4 lines/), 'rectangle exploded into 4 lines');
      // 결합: 4 개를 다시 -> 닫힌 폴리선
      // (Shift+클릭은 첫 선택에 뜬 기즈모 손잡이에 걸릴 수 있다 - 전체 선택. 결합은 선 · 열린 폴리선만 모은다)
      await a.ctrl('KeyA');
      await a.command('join');
      t.expect(a.has(/join: 4 pieces -> polyline \d+ \(4 points, closed\)/), 'four lines joined into a closed polyline');
      // 떨어진 두 선은 결합하지 않는다
      await a.key('KeyL'); await a.click(600, 650); await a.click(700, 650); await a.key('Enter'); await a.key('Escape');
      await a.key('KeyL'); await a.click(720, 650); await a.click(820, 650); await a.key('Enter'); await a.key('Escape');
      await a.ctrl('KeyA');   // 닫힌 폴리선은 빠지고 떨어진 두 선만 남는다
      await a.command('join');
      t.expect(a.has(/join: 1 piece\(s\) do not touch/), 'gapped lines are refused');

      // 끊기: 두 점 사이
      await a.key('Escape');
      await a.key('KeyL'); await a.click(600, 720); await a.click(900, 720); await a.key('Enter'); await a.key('Escape');
      await a.command('break');
      t.expect(a.has(/break: click the object at the first break point/), 'break starts');
      await a.click(650, 720); await a.click(750, 720);
      t.expect(a.has(/break: object \d+ -> 2 pieces/), 'two pieces after a two-point break');
      let lens = await lineLens();
      t.expect(near(lens, 50 / U) && near(lens, 150 / U), `pieces of 50px and 150px (${lens.map(l => l.toFixed(3))})`);
      // '@' = 첫 점에서 둘로만
      await a.command('break');
      await a.click(800, 720);
      await a.command('@');
      t.expect(a.has(/2 pieces \(at a point\)/), "'@' splits at the first point");
      lens = await lineLens();
      t.expect(near(lens, 50 / U) && near(lens, 100 / U), `150px piece split into 50 + 100 (${lens.map(l => l.toFixed(3))})`);
      // 원: 0 -> 90 도를 지우면 270 도 호
      await a.key('KeyC'); await a.click(450, 720); await a.click(510, 720); await a.key('Escape');
      await a.command('break');
      await a.click(510, 720);
      await a.command('@');
      t.expect(a.has(/break: a circle needs two break points/), "'@' on a circle is refused");
      await a.click(450, 660);
      const arc = (await objs()).find(o => o['kind'] === 'arc');
      t.expect(arc && Math.abs(arc['arc']['end'] - arc['arc']['start'] - 1.5 * Math.PI) < 0.02,
               `circle -> 270 degree arc (${arc && (arc['arc']['end'] - arc['arc']['start']).toFixed(3)})`);
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo break/), 'undo');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    name: 'sketch-tools',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(600, 700); await a.click(900, 700); await a.key('Enter');
      await a.key('KeyB'); await a.click(600, 730); await a.click(700, 800);
      await a.key('KeyC'); await a.click(800, 760); await a.click(850, 760);
      await a.key('KeyN'); await a.click(900, 690); await a.click(1000, 740); await a.click(950, 800); await a.click(900, 690);
      await a.key('KeyA'); await a.click(200, 700); await a.click(280, 640); await a.click(380, 700);
      await a.key('KeyG'); await a.key('BracketRight'); await a.key('BracketRight');
      await a.click(700, 330); await a.click(760, 330);
      t.expect(a.has(/sketch: line committed/), 'line');
      t.expect(a.has(/sketch: rectangle committed .*4 points, closed/), 'rectangle');
      t.expect(a.has(/sketch: circle committed .*64 points, closed/), 'circle');
      t.expect(a.has(/sketch: polyline committed .*closed/), 'polyline closed on first point');
      t.expect(a.has(/sketch: arc committed/), 'arc (3 points)');
      t.expect(a.has(/sketch: polygon committed .*8 points/), 'octagon ([ ] sides)');
    },
  },
  {
    name: 'osnap',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      // 기본 씬(큐브 둘 + 토러스)의 정점에 걸리지 않게 화면 아래 띠에서만 그린다.
      // A: (450,700)-(1000,800) [중점 (725,750)], B: (520,800)-(760,695) -> 교차 (661, 738)
      await a.key('KeyL'); await a.click(450, 700); await a.click(1000, 800); await a.key('Enter');
      await a.key('KeyL'); await a.click(520, 800); await a.click(760, 695); await a.key('Enter');
      await a.key('KeyL'); await a.move(661, 738);
      t.expect(a.has(/snap: intersection/), 'intersection snap');
      await a.key('Escape');
      // (800,690) 에서 A 에 내린 수선의 발 = (787, 761)
      await a.key('KeyL'); await a.click(800, 690); await a.move(787, 761);
      t.expect(a.has(/snap: perpendicular/), 'perpendicular snap');
      await a.key('Escape');
      await a.key('KeyL'); await a.move(450, 700);
      t.expect(a.has(/snap: endpoint/), 'endpoint snap');
      await a.move(725, 750);
      t.expect(a.has(/snap: midpoint/), 'midpoint snap');
      await a.key('Escape');
      await a.key('F8');
      t.expect(a.has(/ortho tracking: on/), 'F8 ortho tracking');
    },
  },
  {
    name: 'osnap-only-in-commands',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(450, 700); await a.click(600, 700); await a.key('Enter');
      await a.key('KeyL'); await a.click(700, 760); await a.click(900, 760); await a.key('Enter');

      // 명령이 없을 때는 끝점 위를 지나도 스냅이 잡히지 않는다 (AutoCAD 와 같다)
      const idle = a.count(/snap: /);
      await a.move(450, 700); await a.move(600, 700); await a.move(900, 760);
      t.expect(a.count(/snap: /) === idle, 'no osnap while merely selecting');

      // 도구를 열면 같은 자리에서 다시 잡힌다
      await a.key('KeyL'); await a.move(450, 700);
      t.expect(a.count(/snap: /) > idle, 'osnap comes back inside a command');
      await a.key('Escape');

      // 기즈모로 끌 때도 잡힌다 - 그때도 점을 묻고 있는 중이다
      await a.click(525, 700);
      t.expect(a.has(/pick: sketch/), 'line picked');
      const beforeDrag = a.count(/snap: /);
      await a.key('Digit1'); await a.drag(585, 700, 700, 760);
      t.expect(a.count(/snap: /) > beforeDrag, 'osnap works while dragging a gizmo');
    },
  },
  {
    name: 'select-all-and-erase',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(450, 700); await a.click(600, 700); await a.key('Enter');
      await a.key('KeyL'); await a.click(700, 760); await a.click(900, 760); await a.key('Enter');

      // Ctrl+A - 기본 씬의 메시까지 전부 (잠긴 층은 빠진다)
      await a.ctrl('KeyA');
      t.expect(a.has(/pick: select all - 6 objects/), 'Ctrl+A selects everything selectable');

      // 선택은 편집이 아니다 - Ctrl+Z 는 마지막 스케치를 되돌린다
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo sketch/), 'select all is not an edit');

      // 잠긴 층 것은 빠진다 (층 0 은 잠글 수 없어 새 층으로 옮겨 잠근다)
      await a.layerButton(-1, 'New layer');
      await a.ctrl('KeyA');
      await a.layerButton(1, 'Move the selection to this layer');
      await a.layerButton(1, 'Lock layer');
      await a.ctrl('KeyA');
      t.expect(a.has(/pick: select all - 0 objects/), 'a locked layer is left out');
      await a.layerButton(1, 'Unlock layer');

      // 전체 지우기 - 한 번의 편집이라 Ctrl+Z 로 전부 돌아온다
      await a.command('eraseall');
      t.expect(a.has(/delete: erase all - 5 objects removed/), 'erase all empties the drawing');
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo erase all/), 'erase all undoes in one step');
    },
  },
  {
    name: 'light-is-an-object',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      // 광원은 궤도를 돌므로 자리를 잡아 두고 집는다 - 궤도선을 골라 지우면
      // 그 안의 십자만 남는다. 둘 다 평범한 오브젝트다.
      await a.ctrl('KeyA');
      t.expect(a.has(/pick: select all - 4 objects/), 'the light counts (2 cubes + torus + light)');

      // 전체 지우기 뒤에는 화면에 아무 오브젝트도 남지 않는다 - 전에는 십자가 남았다
      await a.command('eraseall');
      t.expect(a.has(/delete: erase all - 4 objects removed/), 'the light goes with everything else');
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo erase all/), 'and comes back');

      // .lot 에도 실려 나간다 - 저장하고 다시 열면 그대로다
      const text = await a.sceneSave();
      t.expect(/"kind":\s*"light"/.test(text), 'the light is written to .lot');
      await a.sceneLoad(text);
      t.expect(a.has(/scene: loaded .* 1 lights/), 'and read back');
    },
  },
  {
    name: 'view-cube',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      const top = await a.viewCubeMatrix();
      t.expect(/^matrix3d\(/.test(top), 'the cube carries the camera orientation');

      // 면: 표준 뷰와 같은 자세가 된다 (평면도에서 정면도로)
      t.expect(await a.viewCube('0,-1,0'), 'the view cube is there');
      t.expect(a.has(/viewcube: face \(0, -1, 0\)/), 'face click');
      const front = await a.viewCubeMatrix();
      t.expect(front !== top, 'the cube turned with the camera');

      // 같은 자리를 F 로 가도 자세가 같아야 한다 - 뷰큐브와 표준 뷰가 갈라지면 안 된다
      await a.key('KeyT'); await a.key('KeyF');
      t.expect(await a.viewCubeMatrix() === front, 'a face click equals the standard view');

      // 모서리 · 꼭짓점
      await a.viewCube('1,0,1');
      t.expect(a.has(/viewcube: edge \(1, 0, 1\)/), 'edge click');
      await a.viewCube('-1,-1,1');
      t.expect(a.has(/viewcube: corner \(-1, -1, 1\)/), 'corner click');
      t.expect(await a.viewCubeMatrix() !== front, 'a corner is not a standard view');

      // 왼쪽 아래 좌표축 (네이티브 lot_gizmo). 앞-왼쪽-위에서 보면 X 는 오른쪽 위,
      // Y 는 왼쪽 위, Z 는 위로 선다. 끝점은 SVG 중심(reach, reach) 기준.
      const axes = await a.evaluate(
        `(() => { const s = document.getElementById('lot-axis-gizmo'); if (!s) return null;`
        + ` const c = Number(s.getAttribute('width')) / 2;`
        + ` return [...s.querySelectorAll('line')].map(l => ({ stroke: l.getAttribute('stroke'),`
        + ` dx: Number(l.getAttribute('x2')) - c, dy: Number(l.getAttribute('y2')) - c })); })()`);
      t.expect(axes && axes.length === 3, 'axis indicator with three axes');
      const ax = (rgb) => (axes || []).find(x => x.stroke === rgb) || { dx: 0, dy: 0 };
      const X = ax('rgb(220,60,60)'), Y = ax('rgb(60,200,60)'), Z = ax('rgb(70,110,235)');
      t.expect(X.dx > 0 && X.dy < 0, 'X points right and up');
      t.expect(Y.dx < 0 && Y.dy < 0, 'Y points left and up');
      t.expect(Math.abs(Z.dx) < 1 && Z.dy < 0, 'Z points straight up');
    },
  },
  {
    name: 'cube-command',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.command('cube');
      t.expect(a.has(/cube: added object \d+/), 'cube from the command line');
      await a.command('큐브');
      t.expect(a.count(/cube: added object \d+/) === 2, 'the Korean name is the same command');
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo cube/), 'adding a cube is undoable');
    },
  },
  {
    name: 'transform-tool',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.click(...P.yellowCube);
      t.expect(a.has(/pick: object/), 'pick cube');
      // 아래로 1.5 단위 = 280px -> 큐브가 (270, 780) 으로 (위쪽은 리본이 가린다)
      await a.key('KeyM'); await a.click(...P.yellowCube); await a.move(270, 620);
      await a.key('Digit1'); await a.key('Period'); await a.key('Digit5'); await a.key('Enter');
      t.expect(a.has(/transform: move done \(1\.5 units\)/), 'move by typed distance 1.5');
      await a.key('KeyK'); await a.click(270, 780); await a.move(370, 780);
      await a.key('Digit4'); await a.key('Digit5'); await a.key('Enter');
      t.expect(a.has(/transform: rotate done \(45 deg\)/), 'rotate by typed 45 deg');
      await a.key('KeyX'); await a.click(270, 780); await a.move(350, 780);
      await a.key('Digit0'); await a.key('Period'); await a.key('Digit5'); await a.key('Enter');
      t.expect(a.has(/transform: scale done \(0\.5 x\)/), 'scale by typed 0.5');
      await a.key('KeyM'); await a.click(270, 780); await a.move(500, 700); await a.key('Escape');
      t.expect(a.has(/transform: move cancelled/), 'Esc cancels');
    },
  },
  {
    name: 'copy-tool',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.click(...P.yellowCube);
      await a.key('KeyU'); await a.click(...P.yellowCube); await a.click(270, 720); await a.click(600, 720);
      await a.key('Enter');
      t.expect(a.count(/transform: copy placed 1 objects/) === 2, 'two copies placed');
      t.expect(a.has(/transform: copy finished/), 'Enter ends copy');
    },
  },
  {
    name: 'gizmo-and-undo',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(600, 700); await a.click(900, 700); await a.key('Enter');
      await a.click(...P.yellowCube);
      await a.key('Digit1'); await a.drag(330, 500, 250, 500);  // X 화살표를 잡아 왼쪽으로
      t.expect(a.has(/history: move recorded/), 'gizmo move recorded');
      await a.key('Delete');
      t.expect(a.has(/delete: 1 objects removed/), 'delete');
      await a.ctrl('KeyZ'); await a.ctrl('KeyZ'); await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo delete/), 'undo delete');
      t.expect(a.has(/history: undo move/), 'undo move');
      t.expect(a.has(/history: undo sketch/), 'undo sketch');
      await a.ctrl('KeyY');
      t.expect(a.has(/history: redo sketch/), 'redo sketch');
      await a.ctrl('KeyD');
      t.expect(a.has(/copy: 1 objects duplicated/), 'Ctrl+D duplicate');
    },
  },
  {
    name: 'dimension-and-text',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyD'); await a.click(600, 700); await a.click(900, 700); await a.click(750, 760);
      t.expect(a.has(/sketch: dimension committed .*value 1\.6\d/), 'aligned dimension ~1.60 (300px)');
      await a.key('KeyW'); await a.click(...P.emptyFloor);
      const hint = await a.hint();
      t.expect(/type the text/.test(hint), 'text input opened');
      await a.type('Regress 테스트'); await a.key('Enter');
      t.expect(a.has(/sketch: text committed .*Regress 테스트/), 'text placed');
      await a.click(750, 760);
      t.expect(a.has(/pick: sketch/), 'dimension picked by its line');

      // 문자를 더블 클릭해 내용을 고친다
      await a.doubleClick(...P.emptyFloor);
      t.expect(a.has(/text: editing object/), 'double click opens the text for editing');
      await a.type('Edited'); await a.key('Enter');
      t.expect(a.has(/text: object \d+ edited \("Edited"\)/), 'text content changed');
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo text edit/), 'undo restores the old text');
    },
  },
  {
    name: 'layers',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      t.expect((await a.layerRows()).length === 1, 'starts with layer 0 only');

      // 새 층을 만들고 현재 층으로 -> 거기에 선을 그린다
      t.expect(await a.layerButton(-1, 'New layer'), '+ creates a layer');
      t.expect(a.has(/layer: created 1/), 'layer 1 created');
      await a.key('KeyL'); await a.click(600, 700); await a.click(900, 700); await a.key('Enter');
      let rows = await a.layerRows();
      t.expect(/Layer 1 \(1\)/.test(rows[1]), 'new object goes to the current layer');

      // 층을 끄면 그 선은 못 고른다
      t.expect(await a.layerButton(1, 'Hide layer'), 'hide button');
      await a.click(750, 700);
      t.expect(!a.has(/pick: sketch/), 'hidden layer is not pickable');
      t.expect(await a.layerButton(1, 'Show layer'), 'show button');
      await a.click(750, 700);
      t.expect(a.has(/pick: sketch/), 'visible again -> pickable');

      // 잠그면 보이되 못 고른다
      await a.key('Escape');
      t.expect(await a.layerButton(1, 'Lock layer'), 'lock button');
      await a.click(750, 700);
      t.expect(a.count(/pick: sketch/) === 1, 'locked layer is not pickable');

      // 선택을 다른 층으로 옮기기
      t.expect(await a.layerButton(1, 'Unlock layer'), 'unlock button');
      await a.click(750, 700);
      t.expect(await a.layerMakeCurrent(0), 'name click makes layer 0 current');
      t.expect(await a.layerButton(0, 'Move the selection'), 'assign to layer 0');
      t.expect(a.has(/layer: moved 1 objects to layer 0/), 'object moved');

      // 층 지우기 - 오브젝트는 남고 0 층으로
      t.expect(await a.layerButton(1, 'Delete layer'), 'delete button');
      t.expect(a.has(/layer: removed 1/), 'layer removed');
      rows = await a.layerRows();
      t.expect(rows.length === 1 && /0 \(5\)/.test(rows[0]), 'objects survive on layer 0');
    },
  },
  {
    name: 'grid-snap',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyZ');  // zoom extents 가 간격을 씬 크기에 맞춘다
      await a.key('F9');
      t.expect(a.has(/grid snap: on/), 'F9 turns grid snap on');
      const hintOn = await (async () => { await a.key('KeyL'); return a.hint(); })();
      t.expect(/SNAP/.test(hintOn), 'hint shows SNAP');

      // 눈금에서 살짝 벗어난 곳을 두 번 찍고, 길이가 간격의 배수인지 본다.
      // 간격 = niceSpacing(radius * 0.1); radius ~2.2 -> 0.2. 화면 187px/단위.
      await a.click(400, 700); await a.click(777, 700); await a.key('Enter');
      const len = await a.evaluate(
        `(() => { const s = JSON.parse(Module.lotDom.sceneSave());`
        + ` const o = s['objects'].filter(o => o['kind'] === 'line').pop();`
        + ` const a = o['line']['a'], b = o['line']['b'];`
        + ` return Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); })()`);
      const step = 0.2;
      t.expect(Math.abs(len / step - Math.round(len / step)) < 0.01,
               `line length ${len.toFixed(4)} is a multiple of the grid step`);

      await a.key('F9');
      t.expect(a.has(/grid snap: off/), 'F9 turns it off again');
    },
  },
  {
    name: 'linetypes',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      // 층 0 의 선종류를 Dashed 로 -> 그 층에 그린 선이 따라간다 (ByLayer)
      t.expect(await a.setLinetype(0, 1) === '1', 'layer 0 linetype dropdown');
      t.expect(a.has(/layer: 0 linetype = Dashed/), 'layer linetype changed');
      await a.key('KeyL'); await a.click(300, 700); await a.click(900, 700); await a.key('Enter');
      // 그 선만 Center 로 (객체가 층을 덮어쓴다)
      await a.click(600, 700);
      t.expect(a.has(/pick: sketch/), 'pick the line');
      t.expect(await a.setLinetype('selection', 3) === '3', 'selection linetype dropdown');
      t.expect(a.has(/linetype: selection -> Center/), 'object linetype set');
      // 되돌리면 ByLayer 로
      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo linetype/), 'undo restores the linetype');
      // 저장/열기로 살아남나
      const text = await a.sceneSave();
      t.expect(/"linetype"/.test(text), 'linetype written to .lot');
      await a.reload();
      await a.sceneLoad(text);
      t.expect(a.has(/scene: loaded .* 1 layers/), 'scene with layers reloaded');
    },
  },
  {
    name: 'colours',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(400, 700); await a.click(900, 700); await a.key('Enter');
      await a.click(650, 700);
      t.expect(a.has(/pick: sketch/), 'pick the line');

      t.expect(await a.setColor('selection', 0xff3366) === '#ff3366', 'selection colour input');
      t.expect(a.has(/color: selection -> custom/), 'object colour set');

      t.expect(await a.toggleByLayer(), 'ByLayer button');
      t.expect(a.has(/color: selection -> ByLayer/), 'object follows the layer colour');
      t.expect(await a.setColor(0, 0x00aaff) === '#00aaff', 'layer colour input');
      t.expect(a.has(/layer: 0 color set/), 'layer colour set');

      await a.ctrl('KeyZ');
      t.expect(a.has(/history: undo color/), 'undo restores the colour');

      // 메시(큐브)도 색이 먹나 - .lot 에 적힌 색으로 확인
      await a.key('Escape');
      await a.click(270, 500);
      t.expect(a.has(/pick: object/), 'pick the cube');
      await a.setColor('selection', 0x20c040);
      const cube = await a.evaluate(
        `(() => { const s = JSON.parse(Module.lotDom.sceneSave());`
        + ` const m = s['objects'].filter(o => o['kind'] === 'mesh');`
        + ` return m.map(o => o['color'].map(v => Math.round(v * 255)).join(',')); })()`);
      t.expect(cube.includes('32,192,64'), `mesh colour stored (${cube.join(' | ')})`);

      // 저장/열기로 살아남나
      const text = await a.sceneSave();
      await a.reload();
      await a.sceneLoad(text);
      t.expect(a.has(/scene: loaded .* 1 lines/), 'scene reloaded with the coloured line');
    },
  },
  {
    name: 'scene-roundtrip',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(600, 700); await a.click(900, 700); await a.key('Enter');
      await a.key('KeyC'); await a.click(800, 760); await a.click(850, 760);
      await a.key('KeyD'); await a.click(200, 700); await a.click(450, 700); await a.click(300, 760);
      await a.key('KeyW'); await a.click(700, 330); await a.type('rt'); await a.key('Enter');
      const text = await a.sceneSave();
      t.expect(text.length > 1000 && /"format": "lot"/.test(text), 'scene saved as .lot JSON');
      await a.reload();
      await a.sceneLoad(text);
      t.expect(a.has(/scene: loaded 3 meshes, 1 lines, 0 polylines, 1 circles, 0 arcs, 1 dimensions, 1 texts, 1 lights, 1 layers/),
               'reloaded with the same object counts');
      t.expect(a.has(/view: zoom extents/), 'auto zoom extents after load');
    },
  },
  {
    name: 'document-tabs',
    async run(t) {
      const a = t.api;
      // 탭 줄은 다음 프레임에 바뀐다 - 조금 기다렸다 읽는다
      const names = async () => { await sleep(200); return (await a.docTabs()).map(x => (x.on ? '*' : '') + x.name).join(' | '); };
      t.expect(await names() === '*도면1', 'one tab at start');

      // 고치면 점이 붙는다
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(600, 700); await a.click(900, 700); await a.key('Enter');
      t.expect(await names() === '*도면1 ●', 'an edit marks the tab modified');

      // 새 도면은 빈 종이에 따로 된 히스토리
      t.expect(await a.newDocTab(), '+ button present');
      t.expect(a.has(/document: new "도면2" \(2 open\)/), 'new drawing');
      await a.ctrl('KeyA');
      t.expect(a.has(/pick: select all - 0 objects/), 'the new drawing is empty');
      t.expect(await a.cmdActive('edit.undo') === false, 'and has nothing to undo');
      await a.key('KeyT');
      await a.key('KeyL'); await a.click(600, 760); await a.click(800, 760); await a.key('Enter');
      const text = await a.sceneSave();
      t.expect(await names() === '도면1 ● | *도면2', 'saving clears the mark');

      // 돌아가면 그 도면 것 그대로
      t.expect(await a.clickDocTab(0), 'tab click');
      t.expect(a.has(/document: switched to "도면1" \(1\/2\)/), 'switched back');
      await a.ctrl('KeyA');
      t.expect(a.has(/pick: select all - 5 objects/), 'the first drawing kept its objects');

      // 고친 도면이 보이는 중에 파일을 열면 새 탭에, 파일 이름으로
      await a.sceneLoad(text, 'part.lot');
      t.expect(a.has(/document: "part" opened \(3 open\)/), 'opened into a new tab');
      t.expect(await names() === '도면1 ● | 도면2 | *part', 'tab named after the file');

      // 닫기: x 단추 · 명령행. 마지막 하나는 새 빈 도면이 된다
      t.expect(await a.closeDocTab(2), 'close button');
      t.expect(a.has(/document: closed "part" \(2 open, showing "도면2"\)/), 'closed, the neighbour shows');
      await a.command('nexttab');
      t.expect(a.has(/document: switched to "도면1" \(1\/2\)/), 'nexttab wraps around');
      await a.command('close');
      await a.command('close');
      // 이름 번호는 계속 올라간다 ('part' 탭도 하나를 썼다) - AutoCAD 의 Drawing1, 2 … 와 같다
      t.expect(a.has(/document: closed "도면2" \(1 open, showing "도면\d+"\)/), 'the last tab becomes a blank drawing');
      t.expect(/^\*도면\d+$/.test(await names()), 'one blank tab left');

      // 손대지 않은 탭에 열면 새 탭을 만들지 않는다
      await a.sceneLoad(text, 'part.lot');
      t.expect(await names() === '*part', 'a pristine tab is reused');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    // 다른 시나리오는 대화상자를 건너뛰고 sceneLoad 를 부른다. 여기서는 사용자와 같은 길로:
    // 메뉴를 누르고, 숨은 <input type=file> 에 파일을 꽂아 change 가 나게 한다.
    name: 'open-through-menu',
    async run(t) {
      const a = t.api;
      const { resolve } = await import('node:path');
      // 메뉴가 정말 파일 입력/내려받기 링크를 누르는지 엿본다. 헤드리스는 대화상자를 안 띄우므로
      // 아래 setFileInputFiles 만으로는 '메뉴가 아무것도 안 하는' 회귀를 못 잡는다 (실제로 있었다).
      // 블록으로 감싼다 - 압축된 엔진 JS 의 전역 이름(i, l ...)과 부딪치지 않게
      await a.evaluate(`window.__lotClicks = []; {`
        + ` const i = HTMLInputElement.prototype.click; HTMLInputElement.prototype.click = function() { window.__lotClicks.push('input ' + this.accept); };`
        + ` const l = HTMLAnchorElement.prototype.click; HTMLAnchorElement.prototype.click = function() { window.__lotClicks.push('download ' + this.download); }; }`);
      // 열기 대화상자는 하나 - 확장자로 로더가 갈린다
      const kAccept = '.lot,.json,.dxf,.obj';
      const pick = async (file) => {
        const clicked = await a.evaluate(
          `(() => { const b = document.querySelector('[data-cmd="file.open"]'); if (b) b.click(); return !!b; })()`);
        const doc = await a.send('DOM.getDocument', {});
        const q = await a.send('DOM.querySelector', { nodeId: doc.root.nodeId, selector: `input[type=file][accept="${kAccept}"]` });
        if (!q.nodeId) return false;
        await a.send('DOM.setFileInputFiles', { nodeId: q.nodeId, files: [resolve(file)] });
        await sleep(1200);
        return clicked;
      };
      t.expect(await pick('tests/data/sample.dxf'), 'open menu item and file input (.dxf)');
      t.expect(a.has(/dxf: /), 'the DXF was read');
      t.expect(a.has(/document: "sample" opened/), 'into a tab named after the file');
      t.expect(await pick('models/torus.obj'), 'open menu item and file input (.obj)');
      t.expect(a.has(/LotModel: loaded \(opened file\)/), 'the OBJ was read through the same dialog');
      if (existsSync(kNativeScene)) {
        t.expect(await pick(kNativeScene), 'open menu item and file input (.lot)');
        t.expect(a.has(/document: "mmWall" opened/), '.lot opened into a tab named after the file');
      }
      await a.evaluate(`document.querySelector('[data-cmd="file.saveLot"]').click()`);
      await a.evaluate(`document.querySelector('[data-cmd="file.saveDxf"]').click()`);
      const clicks = await a.evaluate(`window.__lotClicks`);
      t.expect(clicks.includes('input ' + kAccept), '열기 opens the one file dialog');
      t.expect(clicks.some(c => /^download .+\.lot$/.test(c)), '저장 downloads a .lot named after the tab');
      t.expect(clicks.some(c => /^download .+\.dxf$/.test(c)), 'DXF 내보내기 downloads a .dxf');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    name: 'command-line',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      // 이름 · 짧은 별칭 · 한국어가 모두 같은 명령으로
      t.expect(await a.command('circle'), 'command line present');
      t.expect(a.has(/command: circle -> 원/), 'full name');
      await a.key('Escape');
      await a.command('l');
      t.expect(a.has(/command: l -> 선/), 'short alias');
      await a.key('Escape');
      await a.command('평면도');
      t.expect(a.has(/command: 평면도 -> 평면도/), 'Korean name');
      await a.command('nosuchthing');
      t.expect(a.has(/command: unknown "nosuchthing"/), 'unknown command is reported');

      // 값 입력: 이동 중에 숫자를 치면 그 거리로 확정된다
      await a.click(270, 500);
      await a.command('move');
      await a.click(270, 500);
      await a.move(270, 620);
      await a.command('1.5');
      t.expect(a.has(/transform: move done \(1\.5 units\)/), 'numeric value through the command line');

      // 입력창으로 쳐도 같다 (Tab 자동완성 포함)
      await a.commandType('zoom');
      t.expect(a.has(/view: zoom extents/), 'typed in the input box');
    },
  },
  {
    name: 'dxf-import',
    async run(t) {
      const a = t.api;
      t.expect(await a.loadDxfFile('tests/data/sample.dxf'), 'sample.dxf loads');
      t.expect(a.has(/dxf: 2 lines, 1 circles, 1 arcs, 2 polylines, 1 texts/),
               'entity counts (line, circle, arc, lwpolyline+polyline, text)');
      t.expect(a.has(/dxf: .* 2 layers/), 'layers from the LAYER table');
      const rows = await a.layerRows();
      t.expect(rows.some(r => /CENTRE \(2\)/.test(r)), `CENTRE layer holds two objects (${rows.join(' | ')})`);
      t.expect(/Center/.test(rows.find(r => /CENTRE/.test(r)) ?? ''), 'CENTRE layer got the Center linetype');
      t.expect(a.has(/view: zoom extents .* radius 1\d\d/), 'framed to the drawing size');

      // bulge 가 있는 LWPOLYLINE 은 점이 늘어난다 (직선 4점보다 많아야 한다)
      const counts = await a.evaluate(
        `(() => { const s = JSON.parse(Module.lotDom.sceneSave());`
        + ` return s['objects'].filter(o => o['kind'] === 'polyline')`
        + `.map(o => o['polyline']['verts'].length).sort((x, y) => y - x); })()`);
      t.expect(counts[0] > 4, `bulge tessellated (${counts.join(',')})`);
    },
  },
  {
    // 블록(INSERT) 은 놓인 자리로 펼치고, MTEXT 는 서식을 벗겨 줄마다 문자로
    name: 'dxf-blocks-and-mtext',
    async run(t) {
      const a = t.api;
      t.expect(await a.loadDxfFile('tests/data/blocks_mtext.dxf'), 'blocks_mtext.dxf loads');
      t.expect(a.has(/dxf: 1 lines, 1 circles, 0 arcs, 0 polylines, 3 texts, .* 1 inserts, 1 blocks/),
               'block contents + MTEXT lines counted');
      const s = await a.evaluate(`JSON.parse(Module.lotDom.sceneSave())['objects']`);
      const texts = s.filter(o => o['kind'] === 'text').map(o => o['text']['content']);
      t.expect(texts.includes('구조평면도') && texts.includes('축척 1/100'), `MTEXT split, formatting stripped (${texts.join(' | ')})`);
      t.expect(texts.includes('문⌀12'), '%%c became the diameter sign');
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    name: 'status-bar',
    async run(t) {
      const a = t.api;
      const toggle = async (id) => {
        await a.evaluate(`document.querySelector('#lot-status-toggles [data-cmd="${id}"]').click()`);
        await sleep(150);
        return a.evaluate(`document.querySelector('#lot-status-toggles [data-cmd="${id}"]').getAttribute('data-on')`);
      };
      const on = id => a.evaluate(`document.querySelector('#lot-status-toggles [data-cmd="${id}"]')?.getAttribute('data-on')`);
      t.expect(await on('snap.osnap') === '1' && await on('view.grid') === '1', 'osnap and grid start on');
      t.expect(await toggle('snap.osnap') === '0' && a.has(/osnap: off/), '객체스냅 off');
      t.expect(await toggle('snap.osnap') === '1' && a.has(/osnap: on/), '객체스냅 on');
      t.expect(await toggle('snap.polar') === '1' && a.has(/polar tracking: on/), '극좌표 on');
      await toggle('snap.polar');
      t.expect(await toggle('view.grid') === '0' && a.has(/display: grid off/), '그리드 off');
      await toggle('view.grid');
      // 셰이딩 단추 = 비주얼 스타일 메뉴. 고르면 닫히고 ✓ 가 옮겨 간다.
      const menuRow = (owner, label) => `[...document.querySelectorAll('[data-menu-for="${owner}"] > div')].find(r => r.firstChild && r.firstChild.textContent === '${label}')`;
      await a.evaluate(`document.querySelector('#lot-status-toggles [data-cmd="status.style"]').click()`);
      t.expect(await a.evaluate(`document.querySelector('[data-menu-for="status.style"]').style.display`) === 'block', '셰이딩 opens the visual style menu');
      await a.evaluate(`${menuRow('status.style', '와이어프레임 (메쉬)')}.click()`);
      await sleep(200);
      t.expect(a.has(/display: style wireframe \(mesh\)/), 'wireframe (mesh) chosen');
      t.expect(await a.evaluate(`${menuRow('status.style', '와이어프레임 (메쉬)')}.getAttribute('data-on')`) === '1', '✓ on the chosen style');
      t.expect(await on('status.style') === '0', '셰이딩 button is off in a wireframe style');
      t.expect(await a.evaluate(`document.querySelector('[data-menu-for="status.style"]').style.display`) === 'none', 'menu closes after a pick');
      for (const st of ['숨은선 제거', '와이어프레임 (엣지)', '셰이딩', '셰이딩 + 엣지']) {
        await a.evaluate(`document.querySelector('#lot-status-toggles [data-cmd="status.style"]').click()`);
        await a.evaluate(`${menuRow('status.style', st)}.click()`);
        await sleep(150);
      }
      t.expect(a.has(/display: style hidden line/) && a.has(/display: style shaded \+ edges/), 'every style selectable');

      // 객체스냅 ▴ = 종류 설정. 끝점 끄기/켜기, 전체 끄기/켜기
      await a.evaluate(`document.querySelector('[data-menu-arrow="snap.osnap"]').click()`);
      t.expect(await a.evaluate(`${menuRow('snap.osnap', '끝점')}.getAttribute('data-on')`) === '1', '끝점 starts checked');
      t.expect(await a.evaluate(`${menuRow('snap.osnap', '근처점')}.getAttribute('data-on')`) === '0', '근처점 starts unchecked');
      await a.evaluate(`${menuRow('snap.osnap', '끝점')}.click()`);
      await sleep(150);
      t.expect(a.has(/osnap: endpoint off/), '끝점 off');
      t.expect(await a.evaluate(`document.querySelector('[data-menu-for="snap.osnap"]').style.display`) === 'block', 'osnap menu stays open');
      await a.evaluate(`${menuRow('snap.osnap', '전체 끄기')}.click()`);
      await sleep(150);
      t.expect(await a.evaluate(`${menuRow('snap.osnap', '교차점')}.getAttribute('data-on')`) === '0', '전체 끄기 unchecks all');
      await a.evaluate(`${menuRow('snap.osnap', '전체 켜기')}.click()`);
      await sleep(150);
      t.expect(await a.evaluate(`${menuRow('snap.osnap', '근처점')}.getAttribute('data-on')`) === '1', '전체 켜기 checks all');
      await a.evaluate(`${menuRow('snap.osnap', '근처점')}.click()`);   // 기본값으로 되돌린다
      t.expect(await toggle('view.dims') === '0' && a.has(/display: dimensions off/), '치수 off');
      await toggle('view.dims');
      // 키로도 같은 상태가 된다 (F3)
      await a.key('F3');
      t.expect(await on('snap.osnap') === '0', 'F3 flips the same button');
      await a.key('F3');
      // ^ 는 지난 명령 목록
      await a.commandType('zoom');
      await a.evaluate(`document.getElementById('lot-cmdline-history').click()`);
      const list = await a.evaluate(`document.getElementById('lot-cmdline-popup').textContent`);
      t.expect(/zoom/.test(list), `^ shows recent commands (${list})`);
      t.expect(!a.has(/ERROR/), `no ERROR (${a.last(/ERROR/) ?? ''})`);
    },
  },
  {
    name: 'dxf-export',
    async run(t) {
      const a = t.api;
      await a.key('KeyT');
      // 종류별로 하나씩. 기본 씬의 정점에 걸리지 않게 아래 띠에서 그린다.
      await a.key('KeyL'); await a.click(450, 700); await a.click(760, 700); await a.key('Enter');
      await a.key('KeyB'); await a.click(200, 730); await a.click(300, 800);
      await a.key('KeyC'); await a.click(850, 760); await a.click(890, 760);
      await a.key('KeyA'); await a.click(960, 700); await a.click(1000, 660); await a.click(1040, 700);
      await a.key('KeyW'); await a.click(...P.emptyFloor); await a.type('DXF'); await a.key('Enter');
      await a.key('KeyD'); await a.click(450, 780); await a.click(760, 780); await a.click(600, 740);

      const text = await a.dxfSave();
      t.expect(/^0\nSECTION\n2\nHEADER\n/.test(text), 'starts with a HEADER section');
      t.expect(/\n2\nLTYPE\n/.test(text) && /\n2\nLAYER\n/.test(text), 'LTYPE and LAYER tables');
      t.expect(/\n0\nEOF\n$/.test(text), 'ends with EOF');
      t.expect(a.has(/dxf: wrote 1 lines, 1 circles, 1 arcs, 1 polylines, 1 texts, 1 dimensions \(exploded\), 1 layers/),
               'one entity of each kind');
      t.expect(a.has(/dxf: wrote .*skipped \d+ meshes/), 'meshes are left out (they are not 2D entities)');

      // 그대로 다시 읽으면 같은 도면이 돌아온다. 치수만 선 7 + 글자 1 로 풀려 있다
      // (DXF 의 DIMENSION 은 그려진 모양을 담은 블록을 달고 다녀야 해서 풀어 쓴다).
      await a.dxfRoundTrip();
      t.expect(a.has(/dxf: 8 lines, 1 circles, 1 arcs, 1 polylines, 2 texts/), 'round trip keeps every entity');
    },
  },
  {
    name: 'native-scene',
    skip: !existsSync(kNativeScene),
    async run(t) {
      const a = t.api;
      await a.loadLotFile(kNativeScene);
      t.expect(a.has(/scene: loaded 2 meshes, 0 lines, 2 polylines/), 'native mmWall.lot opens');
      t.expect(a.has(/view: zoom extents .*radius 4818/), 'mm-scale scene framed');
    },
  },
];

// ---------------------------------------------------------------- runner

const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
const outDir = `build/regress/${stamp}`;
mkdirSync(outDir, { recursive: true });

const api = await connect();
await api.setViewport(1100, 850);

let failed = 0, ran = 0, skipped = 0;
const started = Date.now();
for (const sc of scenarios) {
  if (filter && !sc.name.includes(filter)) continue;
  if (sc.skip) { console.log(`SKIP  ${sc.name}`); ++skipped; continue; }
  ++ran;
  const failures = [];
  const t = {
    api,
    expect(cond, what) { if (!cond) failures.push(what); },
  };
  await api.reload();
  try {
    await sc.run(t);
  } catch (e) {
    failures.push(`exception: ${e.message}`);
  }
  await api.screenshot(`${outDir}/${sc.name}.png`);
  if (failures.length === 0) {
    console.log(`PASS  ${sc.name}`);
  } else {
    ++failed;
    console.log(`FAIL  ${sc.name}`);
    for (const f of failures) console.log(`        - ${f}`);
    for (const l of api.logs.slice(-(Number(process.env.LOG_TAIL) || 12))) console.log(`        engine: ${l}`);
  }
}

console.log(`\n${ran - failed}/${ran} passed${skipped ? `, ${skipped} skipped` : ''} in ${((Date.now() - started) / 1000).toFixed(0)}s  -> ${outDir}`);
api.close();
process.exit(failed ? 1 : 0);

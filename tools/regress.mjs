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
      t.expect(!a.has(/ERROR/), 'no ERROR');
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
      t.expect(!a.has(/ERROR/), 'no ERROR');
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
      await a.evaluate(`window.__lotClicks = [];`
        + ` const i = HTMLInputElement.prototype.click; HTMLInputElement.prototype.click = function() { window.__lotClicks.push('input ' + this.accept); };`
        + ` const l = HTMLAnchorElement.prototype.click; HTMLAnchorElement.prototype.click = function() { window.__lotClicks.push('download ' + this.download); };`);
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
      t.expect(!a.has(/ERROR/), 'no ERROR');
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
      t.expect(!a.has(/ERROR/), 'no ERROR');
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
      t.expect(!a.has(/ERROR/), 'no ERROR');
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

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
import { connect } from './cdp.mjs';

const filter = process.argv[2] ?? '';
const kNativeScene = process.env.LOT_NATIVE_SCENE ?? 'D:/vulkan/3dengine/tests/data/mmWall.lot';

// 1100x850 뷰포트, Top 뷰(T) 기준 좌표. 캔버스는 y = 150 부터 700px, 카메라 거리 4 에
// fov 50 도라 1 월드 단위 ≈ 187px, 월드 원점 = 화면 (550, 500). 화면 = (550 + 187x, 500 - 187y).
// 큐브: 노란 (270, 500), 파란 (830, 500), 한 변 ≈ 112px. 토러스 중심 (550, 500) 반지름 ≈ 155px.
// UI 가 가리는 곳은 쓰지 않는다: 메뉴바+리본 y < 270, 레이어 패널 x > 840,
// 안내문은 아래 가운데. 안전한 자리 = x 200..800, y 300..810.
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
      // A: (700,700)-(1000,820), B: (720,830)-(1050,700) -> 교차 (874, 770). 중점은 A (850,760), B (885,765).
      await a.key('KeyL'); await a.click(700, 700); await a.click(1000, 820); await a.key('Enter');
      await a.key('KeyL'); await a.click(720, 830); await a.click(1050, 700); await a.key('Enter');
      await a.key('KeyL'); await a.move(874, 770);
      t.expect(a.has(/snap: intersection/), 'intersection snap');
      await a.key('Escape');
      // (1000,620) 에서 A 에 내린 수선의 발 = (931, 792)
      await a.key('KeyL'); await a.click(1000, 620); await a.move(931, 792);
      t.expect(a.has(/snap: perpendicular/), 'perpendicular snap');
      await a.key('Escape');
      await a.key('KeyL'); await a.move(700, 700);
      t.expect(a.has(/snap: endpoint/), 'endpoint snap');
      await a.move(850, 760);
      t.expect(a.has(/snap: midpoint/), 'midpoint snap');
      await a.key('Escape');
      await a.key('F8');
      t.expect(a.has(/ortho tracking: on/), 'F8 ortho tracking');
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
      t.expect(rows.length === 1 && /0 \(4\)/.test(rows[0]), 'objects survive on layer 0');
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
      t.expect(a.has(/scene: loaded 3 meshes, 1 lines, 0 polylines, 1 circles, 0 arcs, 1 dimensions, 1 texts, 1 layers/),
               'reloaded with the same object counts');
      t.expect(a.has(/view: zoom extents/), 'auto zoom extents after load');
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
    for (const l of api.logs.slice(-12)) console.log(`        engine: ${l}`);
  }
}

console.log(`\n${ran - failed}/${ran} passed${skipped ? `, ${skipped} skipped` : ''} in ${((Date.now() - started) / 1000).toFixed(0)}s  -> ${outDir}`);
api.close();
process.exit(failed ? 1 : 0);

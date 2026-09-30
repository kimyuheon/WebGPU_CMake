// CDP 로 마우스/키를 보내 엔진을 손으로 시험한다. 명령을 이어 쓰고 마지막에 캡처한다.
//
//   node tools/click.mjs out.png  click X Y            클릭
//   node tools/click.mjs out.png  shiftclick X Y       Shift+클릭 (선택 토글)
//   node tools/click.mjs out.png  move X Y             커서만 이동 (호버 스냅)
//   node tools/click.mjs out.png  drag X1 Y1 X2 Y2     누른 채 이동 후 뗌
//   node tools/click.mjs out.png  rdrag X1 Y1 X2 Y2    오른쪽 버튼 드래그 (CAD 궤도)
//   node tools/click.mjs out.png  mdrag X1 Y1 X2 Y2    가운데 버튼 드래그 (팬)
//   node tools/click.mjs out.png  wheel X Y N          휠 N 노치 (양수 = 위 = 줌 인)
//   node tools/click.mjs out.png  key KeyT             키 한 번 (KeyboardEvent.code)
//   node tools/click.mjs out.png  ctrl KeyZ            Ctrl+키 (실행 취소)
//   node tools/click.mjs out.png  hold KeyW 300        키를 ms 동안 누르고 있기 (이동)
//   node tools/click.mjs out.png  btn Top              툴바 버튼 누르기 (글자로 찾는다)
//   node tools/click.mjs out.png  hint                 안내문 출력
//   node tools/click.mjs out.png  type 문자열           포커스된 입력창에 글자 (문자 도구)
//   node tools/click.mjs out.png  savelot a.lot        씬을 .lot 로 저장
//   node tools/click.mjs out.png  loadlot a.lot        .lot 씬 열기
//   node tools/click.mjs out.png  savedxf a.dxf        씬을 DXF 로 내보내기
//   node tools/click.mjs out.png  loaddxf a.dxf        DXF 도면 열기
//   node tools/click.mjs out.png  viewport 1100 850    창 크기 고정 (회귀와 같은 좌표로)
//   node tools/click.mjs out.png  reload               페이지 새로 열기
//   node tools/click.mjs out.png  wait                 잠깐 기다림
//
// 좌표는 페이지 기준 픽셀이다 (캔버스가 상태바 아래에서 시작하므로
// 캔버스 좌표 + 상태바 높이). 조작 뒤 엔진 로그를 'engine:' 으로 출력한다.
import { connect, sleep } from './cdp.mjs';

const out = process.argv[2];
const args = process.argv.slice(3);
const api = await connect();

await sleep(args.includes('reload') ? 0 : 8000);  // 엔진이 자리잡을 시간

let i = 0;
while (i < args.length) {
  const cmd = args[i++];
  const num = () => Number(args[i++]);
  switch (cmd) {
  case 'click':      { const x = num(), y = num(); await api.click(x, y); console.log(`click (${x}, ${y})`); break; }
  case 'shiftclick': { const x = num(), y = num(); await api.shiftClick(x, y); console.log(`shift+click (${x}, ${y})`); break; }
  case 'move':       { const x = num(), y = num(); await api.move(x, y); console.log(`move (${x}, ${y})`); break; }
  case 'drag': case 'rdrag': case 'mdrag': {
    const x1 = num(), y1 = num(), x2 = num(), y2 = num();
    const button = cmd === 'rdrag' ? 'right' : cmd === 'mdrag' ? 'middle' : 'left';
    await api.drag(x1, y1, x2, y2, button);
    console.log(`${cmd} (${x1}, ${y1}) -> (${x2}, ${y2})`);
    break;
  }
  case 'wheel':   { const x = num(), y = num(), n = num(); await api.wheel(x, y, n); console.log(`wheel (${x}, ${y}) ${n}`); break; }
  case 'key':     { const code = args[i++]; await api.key(code); console.log(`key ${code}`); break; }
  case 'ctrl':    { const code = args[i++]; await api.ctrl(code); console.log(`ctrl+${code}`); break; }
  case 'hold':    { const code = args[i++], ms = num(); await api.hold(code, ms); console.log(`hold ${code} ${ms}ms`); break; }
  case 'btn':     { const label = args[i++]; console.log(`btn ${label} -> ${await api.btn(label)}`); break; }
  case 'hint':    console.log(`hint: ${await api.hint()}`); break;
  case 'type':    { const t = args[i++]; await api.type(t); console.log(`type "${t}"`); break; }
  case 'savelot': { const p = args[i++]; await api.saveLotFile(p); console.log(`savelot -> ${p}`); break; }
  case 'loadlot': { const p = args[i++]; console.log(`loadlot ${p} -> ${await api.loadLotFile(p)}`); break; }
  case 'savedxf': { const p = args[i++]; await api.saveDxfFile(p); console.log(`savedxf -> ${p}`); break; }
  case 'loaddxf': { const p = args[i++]; console.log(`loaddxf ${p} -> ${await api.loadDxfFile(p)}`); break; }
  case 'viewport': { const x = num(), y = num(); await api.setViewport(x, y); console.log(`viewport ${x}x${y}`); break; }
  case 'reload':  await api.reload(); console.log('reload'); break;
  case 'wait':    await sleep(500); break;
  default:        console.error(`unknown command: ${cmd}`); process.exit(2);
  }
}

if (out) {
  await api.screenshot(out);
  console.log('saved', out);
}
for (const l of api.logs) console.log('  engine:', l);
api.close();
process.exit(0);

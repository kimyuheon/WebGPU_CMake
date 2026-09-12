// CDP 로 마우스를 눌러 피킹/드래그를 시험한다.
//
//   node tools/click.mjs out.png  click X Y            클릭 후 캡처
//   node tools/click.mjs out.png  drag X1 Y1 X2 Y2     누른 채 이동 후 뗌, 캡처
//   node tools/click.mjs out.png  wait                 그냥 캡처
//
// 좌표는 페이지 기준 픽셀이다 (캔버스가 상태바 아래에서 시작하므로
// 캔버스 좌표 + 상태바 높이). 여러 명령을 이어 쓸 수 있다.
import { writeFileSync } from 'node:fs';

const out = process.argv[2];
const args = process.argv.slice(3);
const cdpPort = Number(process.env.CDP_PORT ?? 9222);

const targets = await (await fetch(`http://localhost:${cdpPort}/json`)).json();
const page = targets.find(t => t.type === 'page' && t.webSocketDebuggerUrl);
if (!page) { console.error('no page target'); process.exit(1); }

const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const msgId = ++id;
  pending.set(msgId, { resolve, reject });
  ws.send(JSON.stringify({ id: msgId, method, params }));
});
ws.addEventListener('message', ev => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    const { resolve, reject } = pending.get(msg.id);
    pending.delete(msg.id);
    msg.error ? reject(new Error(JSON.stringify(msg.error))) : resolve(msg.result);
  }
});
const sleep = ms => new Promise(r => setTimeout(r, ms));

await new Promise(resolve => ws.addEventListener('open', resolve));
await send('Runtime.enable');

// 콘솔 로그를 받아 피킹 결과를 확인한다
const logs = [];
ws.addEventListener('message', ev => {
  const msg = JSON.parse(ev.data);
  if (msg.method === 'Runtime.consoleAPICalled') {
    const text = msg.params.args.map(a => a.value ?? '').join(' ');
    if (/pick:|drag:|snap:|marquee:|copy:|projection:|post:|MouseInput|RenderTarget|ERROR|error/.test(text)) logs.push(text);
  }
});

const mouse = (type, x, y, extra = {}) =>
  send('Input.dispatchMouseEvent', { type, x, y, button: 'left', ...extra });

await sleep(8000);  // 엔진이 자리잡을 시간

let i = 0;
while (i < args.length) {
  const cmd = args[i++];
  if (cmd === 'click') {
    const x = Number(args[i++]), y = Number(args[i++]);
    await mouse('mouseMoved', x, y);
    await mouse('mousePressed', x, y, { clickCount: 1 });
    await sleep(100);
    await mouse('mouseReleased', x, y, { clickCount: 1 });
    await sleep(400);
    console.log(`click (${x}, ${y})`);
  } else if (cmd === 'drag') {
    const x1 = Number(args[i++]), y1 = Number(args[i++]);
    const x2 = Number(args[i++]), y2 = Number(args[i++]);
    await mouse('mouseMoved', x1, y1);
    await mouse('mousePressed', x1, y1, { clickCount: 1 });
    await sleep(100);
    // 여러 단계로 나눠 움직여야 프레임마다 드래그가 반영된다
    const steps = 12;
    for (let s = 1; s <= steps; ++s) {
      const x = x1 + (x2 - x1) * s / steps;
      const y = y1 + (y2 - y1) * s / steps;
      await mouse('mouseMoved', x, y, { buttons: 1 });
      await sleep(50);
    }
    await mouse('mouseReleased', x2, y2, { clickCount: 1 });
    await sleep(400);
    console.log(`drag (${x1}, ${y1}) -> (${x2}, ${y2})`);
  } else if (cmd === 'shiftclick') {
    // Shift + 클릭 (선택 추가/토글). CDP modifiers: 8 = Shift
    const x = Number(args[i++]), y = Number(args[i++]);
    await mouse('mouseMoved', x, y);
    await mouse('mousePressed', x, y, { clickCount: 1, modifiers: 8 });
    await sleep(100);
    await mouse('mouseReleased', x, y, { clickCount: 1, modifiers: 8 });
    await sleep(400);
    console.log(`shift+click (${x}, ${y})`);
  } else if (cmd === 'move') {
    // 누르지 않고 커서만 옮긴다 (호버 스냅 확인용)
    const x = Number(args[i++]), y = Number(args[i++]);
    await mouse('mouseMoved', x, y);
    await sleep(400);
    console.log(`move (${x}, ${y})`);
  } else if (cmd === 'key') {
    // 키 한 번 누르기 (KeyP, ArrowLeft 같은 KeyboardEvent.code)
    const code = args[i++];
    await send('Input.dispatchKeyEvent', { type: 'keyDown', code, key: code });
    await sleep(60);
    await send('Input.dispatchKeyEvent', { type: 'keyUp', code, key: code });
    await sleep(300);
    console.log(`key ${code}`);
  } else if (cmd === 'hold') {
    // 키를 ms 동안 누르고 있기 (이동/줌)
    const code = args[i++];
    const ms = Number(args[i++]);
    await send('Input.dispatchKeyEvent', { type: 'keyDown', code, key: code });
    await sleep(ms);
    await send('Input.dispatchKeyEvent', { type: 'keyUp', code, key: code });
    await sleep(300);
    console.log(`hold ${code} ${ms}ms`);
  } else if (cmd === 'wait') {
    await sleep(500);
  }
}

const result = await send('Page.captureScreenshot', { format: 'png' });
writeFileSync(out, Buffer.from(result.data, 'base64'));
console.log('saved', out);
for (const l of logs) console.log('  engine:', l);
process.exit(0);

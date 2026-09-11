// 헤드리스 크롬으로 엔진 화면을 캡처한다.
//
// 쓰는 법:
//   1. ./build.sh && (cd build && python3 -m http.server 8123 &)
//   2. chrome --headless=new --remote-debugging-port=9222 \
//        --enable-unsafe-swiftshader --window-size=900,600 \
//        --user-data-dir=/tmp/lot-chrome http://localhost:8123/WebGPUApp.html &
//   3. node tools/screenshot.mjs out.png [waitMs]
//
// --screenshot / --virtual-time-budget 로는 안 된다. 비동기 셰이더 fetch 가
// 가상 시간 안에서 진행되지 않아 초기 화면만 찍힌다. 그래서 CDP 로 붙어서
// 실제 시간을 기다린 뒤 찍는다.
import { writeFileSync } from 'node:fs';

const out = process.argv[2] ?? 'screenshot.png';
const waitMs = Number(process.argv[3] ?? 8000);
const cdpPort = Number(process.env.CDP_PORT ?? 9222);

const targets = await (await fetch(`http://localhost:${cdpPort}/json`)).json();
const page = targets.find(t => t.type === 'page' && t.webSocketDebuggerUrl);
if (!page) {
  console.error('no page target - is chrome running with --remote-debugging-port?');
  process.exit(1);
}

const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const send = (method, params = {}) => new Promise(resolve => {
  const msgId = ++id;
  pending.set(msgId, resolve);
  ws.send(JSON.stringify({ id: msgId, method, params }));
});
ws.addEventListener('message', ev => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    pending.get(msg.id)(msg.result);
    pending.delete(msg.id);
  }
});

await new Promise(resolve => ws.addEventListener('open', resolve));

// 렌더 루프가 몇 프레임 돌 시간을 실제로 준다
await new Promise(resolve => setTimeout(resolve, waitMs));

const result = await send('Page.captureScreenshot', { format: 'png' });
if (!result?.data) {
  console.error('capture failed');
  process.exit(1);
}
writeFileSync(out, Buffer.from(result.data, 'base64'));
console.log('saved', out);
process.exit(0);

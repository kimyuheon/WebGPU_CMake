// 헤드리스 크롬(CDP)으로 엔진을 조작하는 공용 도우미.
//
// click.mjs(손으로 한 번씩) 와 regress.mjs(시나리오 묶음) 가 같은 걸 쓴다.
// 크롬은 --remote-debugging-port=9222 로 미리 떠 있어야 한다 (README 참고).
//
// 좌표는 페이지 기준 픽셀이다 (캔버스가 상태바 아래에서 시작하므로
// 캔버스 좌표 + 상태바 높이). 뷰포트를 고정하면 (setViewport) 시나리오 좌표가 안정된다.
import { readFileSync, writeFileSync } from 'node:fs';

export const sleep = ms => new Promise(r => setTimeout(r, ms));

// 상태바에 찍히는 엔진 로그 중 시나리오 판정에 쓰는 것들
export const kLogFilter =
  /pick:|drag:|snap:|marquee:|copy:|delete:|gizmo:|projection:|post:|view:|sketch:|history:|scene:|transform:|offset:|trim:|extend:|fillet:|fillet R=|chamfer:|chamfer D=|ortho tracking:|layer:|linetype:|grid snap:|color:|text:|dxf:|command:|document:|cube:|viewcube:|LotModel: loaded|osnap:|polar tracking:|display:|tree:|property:|dock:|transform:|MouseInput|RenderTarget|ERROR|error/;

export async function connect(port = Number(process.env.CDP_PORT ?? 9222)) {
  const targets = await (await fetch(`http://localhost:${port}/json`)).json();
  const page = targets.find(t => t.type === 'page' && t.webSocketDebuggerUrl);
  if (!page) throw new Error('no page target - is chrome running with --remote-debugging-port?');

  const ws = new WebSocket(page.webSocketDebuggerUrl);
  let nextId = 0;
  const pending = new Map();
  const logs = [];

  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++nextId;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
  ws.addEventListener('message', ev => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      msg.error ? reject(new Error(JSON.stringify(msg.error))) : resolve(msg.result);
    } else if (msg.method === 'Runtime.consoleAPICalled') {
      const text = msg.params.args.map(a => a.value ?? '').join(' ');
      if (kLogFilter.test(text)) logs.push(text);
    }
  });
  await new Promise(resolve => ws.addEventListener('open', resolve));
  await send('Runtime.enable');
  // 캐시를 끈다. http.server 는 Cache-Control 을 안 보내서 크롬이 옛 WebGPUApp.js 를
  // 그대로 쓴 적이 있다 - 새 빌드의 JS 수정이 시험에 안 잡히고 옛 동작이 돌았다.
  await send('Network.enable');
  await send('Network.setCacheDisabled', { cacheDisabled: true });

  const mouse = (type, x, y, extra = {}) =>
    send('Input.dispatchMouseEvent', { type, x, y, button: 'left', ...extra });
  const buttonsBit = { left: 1, right: 2, middle: 4 };

  const api = {
    send, logs, sleep,
    close() { ws.close(); },

    // 시나리오 좌표를 안정시키기 위해 뷰포트를 고정한다 (크롬 창 크기와 무관하게)
    async setViewport(width, height) {
      await send('Emulation.setDeviceMetricsOverride',
                 { width, height, deviceScaleFactor: 1, mobile: false });
    },

    // 페이지를 새로 연다. 엔진이 자리잡을 때까지 기다린다 (셰이더 fetch 등).
    async reload(url = 'http://localhost:8123/WebGPUApp.html?layout=classic', waitMs = 7000) {
      logs.length = 0;
      await send('Page.navigate', { url });
      await sleep(waitMs);
    },

    async evaluate(expression) {
      const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
      if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? 'evaluate failed');
      return r.result?.value;
    },

    async click(x, y, modifiers = 0) {
      await mouse('mouseMoved', x, y);
      await mouse('mousePressed', x, y, { clickCount: 1, modifiers });
      await sleep(100);
      await mouse('mouseReleased', x, y, { clickCount: 1, modifiers });
      await sleep(400);
    },
    async shiftClick(x, y) { await api.click(x, y, 8); },
    // 더블 클릭 (문자 편집 등). 엔진이 두 누름의 간격으로 판정하므로 (400ms)
    // api.click 을 두 번 부르면 안 된다 - 그쪽은 뗀 뒤 400ms 를 쉰다.
    async doubleClick(x, y) {
      await mouse('mouseMoved', x, y);
      for (let i = 1; i <= 2; ++i) {
        await mouse('mousePressed', x, y, { clickCount: i });
        await sleep(40);
        await mouse('mouseReleased', x, y, { clickCount: i });
        await sleep(60);
      }
      await sleep(400);
    },
    async move(x, y) { await mouse('mouseMoved', x, y); await sleep(400); },

    // 여러 단계로 나눠 움직여야 프레임마다 드래그가 반영된다
    async drag(x1, y1, x2, y2, button = 'left') {
      await mouse('mouseMoved', x1, y1);
      await mouse('mousePressed', x1, y1, { button, clickCount: 1 });
      await sleep(100);
      const steps = 12;
      for (let s = 1; s <= steps; ++s) {
        await mouse('mouseMoved', x1 + (x2 - x1) * s / steps, y1 + (y2 - y1) * s / steps,
                    { button, buttons: buttonsBit[button] });
        await sleep(50);
      }
      await mouse('mouseReleased', x2, y2, { button, clickCount: 1 });
      await sleep(400);
    },
    async wheel(x, y, notches) {
      await mouse('mouseMoved', x, y);
      await send('Input.dispatchMouseEvent', { type: 'mouseWheel', x, y, deltaX: 0, deltaY: -notches * 100 });
      await sleep(400);
    },

    // 키 한 번 (KeyboardEvent.code). modifiers: 2 Ctrl, 8 Shift
    async key(code, modifiers = 0) {
      await send('Input.dispatchKeyEvent', { type: 'keyDown', code, key: code, modifiers });
      await sleep(60);
      await send('Input.dispatchKeyEvent', { type: 'keyUp', code, key: code, modifiers });
      await sleep(300);
    },
    async ctrl(code) { await api.key(code, 2); },
    async hold(code, ms) {
      await send('Input.dispatchKeyEvent', { type: 'keyDown', code, key: code });
      await sleep(ms);
      await send('Input.dispatchKeyEvent', { type: 'keyUp', code, key: code });
      await sleep(300);
    },
    // 포커스된 입력창에 글자 (문자 도구)
    async type(text) { await send('Input.insertText', { text }); await sleep(200); },

    // 메뉴/리본의 명령 단추를 누른다. id 는 C++ 의 명령 표(src/ui/lot_main_menu.cpp)에 있는
    // "view.top" 같은 값이다 - 이름이 겹치는 명령(이동/회전…)도 구별된다.
    async cmd(id) {
      const ok = await api.evaluate(
        `(() => { const b = document.querySelector('[data-cmd=' + ${JSON.stringify(JSON.stringify(id))} + ']');`
        + ` if (b) b.click(); return !!b; })()`);
      await sleep(300);
      return ok;
    },
    // 그 명령이 지금 켜져 있나 (파란 강조)
    async cmdActive(id) {
      return api.evaluate(
        `(() => { const b = document.querySelector('[data-cmd=' + ${JSON.stringify(JSON.stringify(id))} + ']');`
        + ` return b ? b.getAttribute('data-on') === '1' : null; })()`);
    },
    // 명령행에 치기 (입력창을 거치지 않고 같은 입구로)
    async command(text) {
      const ok = await api.evaluate(
        `(() => { if (!Module.lotDom || !Module.lotDom['commandRun']) return false;`
        + ` Module.lotDom['commandRun'](${JSON.stringify(text)}); return true; })()`);
      await sleep(400);
      return ok;
    },
    // 명령행 입력창에 직접 치고 Enter (자동완성·되풀이까지 보려면)
    async commandType(text) {
      await api.evaluate(`document.getElementById('lot-cmdline-input').focus()`);
      if (text) await api.type(text);
      await api.key('Enter');
      await sleep(400);
    },
    async hint() { return api.evaluate(`document.getElementById('lot-hint')?.textContent ?? ''`); },

    // 뷰큐브: 보이는 칸을 좌표로 누른다. dir 은 "dx,dy,dz" (면 하나 · 모서리 둘 · 꼭짓점 셋).
    async viewCube(dir) {
      const ok = await api.evaluate(
        `(() => { if (!Module.lotDom || !Module.lotDom['viewCubeClick']) return false;`
        + ` Module.lotDom['viewCubeClick'](${JSON.stringify(dir)}); return true; })()`);
      await sleep(400);
      return ok;
    },
    // 상자에 걸린 CSS 행렬 (자세가 따라 도는지 보려고)
    async viewCubeMatrix() {
      return api.evaluate(`document.getElementById('lot-viewcube').firstChild.style.transform`);
    },

    // 레이어 패널: 행 번호(0 부터)의 버튼을 title 로 찾아 누른다. row 가 -1 이면 머리글(+).
    // title 예: 'New layer', 'Hide layer', 'Lock layer', 'Delete layer...', 'Move the selection...'
    async layerButton(row, titlePrefix) {
      const ok = await api.evaluate(
        `(() => { const p = document.getElementById('lot-layers'); if (!p) return false;`
        + ` const rows = [...p.children].slice(1).filter(x => x.id !== 'lot-layer-selection');`
        + ` const r = ${row} < 0 ? p.children[0] : rows[${row}]; if (!r) return false;`
        + ` const b = [...r.querySelectorAll('button')].find(x => x.title.startsWith(${JSON.stringify(titlePrefix)}));`
        + ` if (b) b.click(); return !!b; })()`);
      await sleep(300);
      return ok;
    },
    // 행의 이름을 눌러 현재 층으로
    async layerMakeCurrent(row) {
      const ok = await api.evaluate(
        `(() => { const p = document.getElementById('lot-layers'); if (!p) return false;`
        + ` const rows = [...p.children].slice(1).filter(x => x.id !== 'lot-layer-selection');`
        + ` const s = rows[${row}]?.querySelector('span[title]'); if (s) s.click(); return !!s; })()`);
      await sleep(300);
      return ok;
    },
    // 선종류 드롭다운. which: 'selection' 이면 맨 아래(선택용), 숫자면 그 행의 층 드롭다운.
    async setLinetype(which, value) {
      const sel = which === 'selection'
        ? `document.querySelector('#lot-layer-selection select')`
        : `[...document.getElementById('lot-layers').children].slice(1)`
          + `.filter(x => x.id !== 'lot-layer-selection')[${which}]?.querySelector('select')`;
      return api.evaluate(
        `(() => { const s = ${sel};`
        + ` if (!s) return null; s.value = String(${value}); s.dispatchEvent(new Event('change')); return s.value; })()`);
    },
    // 층 행만 (머리글과 'selection' 줄은 뺀다)
    // 색 입력. which: 'selection' 또는 층 행 번호. value 는 0xRRGGBB.
    async setColor(which, value) {
      const hex = '#' + value.toString(16).padStart(6, '0');
      const sel = which === 'selection'
        ? `document.querySelector('#lot-layer-selection input[type=color]')`
        : `[...document.getElementById('lot-layers').children].slice(1)`
          + `.filter(x => x.id !== 'lot-layer-selection')[${which}]?.querySelector('input[type=color]')`;
      return api.evaluate(
        `(() => { const c = ${sel}; if (!c) return null;`
        + ` c.value = ${JSON.stringify(hex)}; c.dispatchEvent(new Event('input')); return c.value; })()`);
    },
    // 선택 줄의 ByLayer 단추
    async toggleByLayer() {
      const ok = await api.evaluate(
        `(() => { const b = [...document.querySelectorAll('#lot-layer-selection button')]`
        + `.find(x => x.textContent.indexOf('ByLayer') >= 0); if (b) b.click(); return !!b; })()`);
      await sleep(300);
      return ok;
    },
    async layerRows() {
      return api.evaluate(
        `[...document.getElementById('lot-layers').children].slice(1)`
        + `.filter(r => r.id !== 'lot-layer-selection').map(r => r.textContent)`);
    },

    // 씬 저장/열기 (파일 대화상자 없이, lot_toolbar.js 의 훅)
    async sceneSave() { return api.evaluate(`Module.lotDom.sceneSave()`); },
    // name 을 주면 파일 이름으로 열린 것처럼 (탭 이름이 된다)
    async sceneLoad(text, name) {
      const ok = await api.evaluate(
        `Module.lotDom.sceneLoad(${JSON.stringify(text)}, ${JSON.stringify(name ?? '')})`);
      await sleep(500);
      return ok;
    },

    // 도면 탭 줄. [{name, on}] - name 은 보이는 글자 그대로 (고친 도면은 끝에 ' ●').
    async docTabs() {
      return api.evaluate(
        `[...document.querySelectorAll('#lot-doc-tabs [data-doc-tab]')]`
        + `.map(t => ({ name: t.firstChild.textContent, on: t.getAttribute('data-on') === '1' }))`);
    },
    async clickDocTab(i) {
      const ok = await api.evaluate(
        `(() => { const t = document.querySelector('#lot-doc-tabs [data-doc-tab="${i}"]');`
        + ` if (t) t.click(); return !!t; })()`);
      await sleep(300);
      return ok;
    },
    // x 단추. 고친 도면이면 확인창이 뜨는데, 헤드리스에서는 막히므로 '예' 로 바꿔 둔다.
    async closeDocTab(i) {
      const ok = await api.evaluate(
        `(() => { window.confirm = () => true;`
        + ` const x = document.querySelector('#lot-doc-tabs [data-doc-close="${i}"]');`
        + ` if (x) x.click(); return !!x; })()`);
      await sleep(300);
      return ok;
    },
    async newDocTab() {
      const ok = await api.evaluate(
        `(() => { const p = document.getElementById('lot-doc-new'); if (p) p.click(); return !!p; })()`);
      await sleep(300);
      return ok;
    },
    async saveLotFile(path) { writeFileSync(path, await api.sceneSave()); },

    // DXF 열기 (파일 대화상자 없이). 바이트를 그대로 넘겨 페이지가 코드페이지를 푼다.
    async loadDxfFile(path) {
      const b64 = readFileSync(path).toString('base64');
      const ok = await api.evaluate(
        `(() => { const bin = atob(${JSON.stringify(b64)});`
        + ` const u8 = new Uint8Array(bin.length);`
        + ` for (let i = 0; i < bin.length; ++i) u8[i] = bin.charCodeAt(i);`
        + ` return Module.lotDom.dxfLoad(u8.buffer); })()`);
      await sleep(800);
      return ok;
    },
    async loadLotFile(path) { return api.sceneLoad(readFileSync(path, 'utf8')); },

    // DXF 내보내기 (대화상자 없이 텍스트만)
    async dxfSave() { return api.evaluate(`Module.lotDom['dxfSave']()`); },
    async saveDxfFile(path) { writeFileSync(path, await api.dxfSave()); },
    // 내보낸 것을 그 자리에서 다시 읽는다 - 왕복 확인용. 글자 길이를 돌려준다.
    async dxfRoundTrip() {
      const n = await api.evaluate(
        `(() => { const t = Module.lotDom['dxfSave']();`
        + ` if (!t) return 0;`
        + ` Module.lotDom['dxfLoad'](new TextEncoder().encode(t).buffer);`
        + ` return t.length; })()`);
      await sleep(800);
      return n;
    },

    async screenshot(path) {
      const r = await send('Page.captureScreenshot', { format: 'png' });
      writeFileSync(path, Buffer.from(r.data, 'base64'));
    },

    // 로그 검사 도우미: 정규식에 맞는 로그가 있나 / 몇 개인가
    has(re) { return logs.some(l => re.test(l)); },
    count(re) { return logs.filter(l => re.test(l)).length; },
    last(re) { for (let i = logs.length - 1; i >= 0; --i) if (re.test(logs[i])) return logs[i]; return null; },
  };
  return api;
}

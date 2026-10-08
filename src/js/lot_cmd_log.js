/**
 * 명령 기록창 - 명령행 바로 위 왼쪽 (AutoCAD 의 명령 창 몇 줄).
 *
 *   명령: rec              친 명령
 *     > -60,-35            도구가 열려 있을 때 친 값 / 좌표
 *   rectangle: ...         그 다음에 도구가 묻는 것 (입력 직후의 안내문 한 줄)
 *
 * 안내문은 커서를 따라 매 프레임 바뀌므로 (돌출 높이 등) 전부 남기지 않고 입력 직후의 것만 남긴다.
 * 최근 몇 줄만 보이고, 전체는 ^ 단추의 지난 명령 목록이 맡는다. 클릭은 통과한다.
 */
mergeInto(LibraryManager.library, {

    $LotCmdLog__deps: ['$LotUiTheme'],
    $LotCmdLog: {
        kVisible: 4,
        lines: [],
        box: null,
        awaitingPrompt: false,

        ensure: function() {
            if (LotCmdLog.box) return true;
            if (!document.body) return false;
            var T = LotUiTheme;
            var b = document.createElement('div');
            b.id = 'lot-cmdlog';
            b.style.position = 'fixed';
            b.style.left = '8px';
            // 하단 상태바(30px)와 가운데 안내문 상자(lot-hint, 아래 40px · 높이 ~24px) 위 - 화면 폭과 상관없이
            // 겹치지 않는다. 길면 말줄임.
            b.style.bottom = '70px';
            b.style.maxWidth = 'min(480px, calc(100vw - 16px))';
            b.style.zIndex = '11';
            b.style.padding = '4px 8px';
            b.style.fontFamily = '"Segoe UI", "Malgun Gothic", sans-serif';
            b.style.fontSize = '12px';
            b.style.lineHeight = '17px';
            b.style.color = T.text;
            b.style.background = 'rgba(15, 15, 15, 0.78)';
            b.style.border = '1px solid ' + T.border;
            b.style.borderRadius = '3px';
            b.style.pointerEvents = 'none';
            b.style.display = 'none';
            document.body.appendChild(b);
            LotCmdLog.box = b;
            return true;
        },

        // kind: 'cmd' (명령) / 'input' (도구에 준 값) / 'prompt' (도구가 묻는 것)
        add: function(text, kind) {
            if (!text || !LotCmdLog.ensure()) return;
            LotCmdLog.lines.push([kind, text]);
            if (LotCmdLog.lines.length > 200) LotCmdLog.lines.shift();
            LotCmdLog.render();
        },

        render: function() {
            var T = LotUiTheme;
            var b = LotCmdLog.box;
            b.textContent = '';
            var shown = LotCmdLog.lines.slice(-LotCmdLog.kVisible);
            shown.forEach(function(line, i) {
                var row = document.createElement('div');
                row.style.whiteSpace = 'nowrap';
                row.style.overflow = 'hidden';
                row.style.textOverflow = 'ellipsis';
                var kind = line[0];
                row.textContent = kind === 'cmd' ? '명령: ' + line[1] : kind === 'input' ? '  > ' + line[1] : line[1];
                row.style.color = kind === 'prompt' ? T.textDim : T.text;
                row.style.opacity = String(0.55 + 0.45 * (i + 1) / shown.length);   // 오래된 줄은 흐리게
                b.appendChild(row);
            });
            b.style.display = shown.length ? 'block' : 'none';
        },

        // 명령행에서 Enter. toolOpen 이면 값 (도구가 받는다), 아니면 명령.
        submitted: function(text, toolOpen) {
            LotCmdLog.add(text, toolOpen ? 'input' : 'cmd');
            LotCmdLog.awaitingPrompt = true;
        },

        // C++ 이 상태를 밀 때마다 (lot_ui.js js_uiSetState). 입력 직후의 안내문만 남긴다.
        hint: function(text) {
            if (!LotCmdLog.awaitingPrompt) return;
            LotCmdLog.awaitingPrompt = false;
            if (text) LotCmdLog.add(text, 'prompt');
        },
    },
});

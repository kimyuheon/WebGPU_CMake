/**
 * 툴바 - 캔버스 왼쪽에 겹쳐 놓는 HTML 버튼 묶음.
 *
 * Vulkan 쪽은 ImGui 지만 브라우저에서는 DOM 이 더 싸고 접근성도 낫다.
 * 버튼은 로직을 갖지 않는다: 누르면 그 기능의 단축키 코드를 C++ 에 넘기고
 * (lot_onToolbarKey), C++ 은 키보드 이벤트와 같은 경로로 처리한다. 그래서
 * 키와 버튼이 어긋날 수 없다. 반대로 C++ 은 프레임마다 상태(현재 기즈모 모드,
 * 열린 스케치 도구, 뷰 모드, undo 가능 여부, 안내문)를 밀어서 버튼을 밝힌다
 * (js_setToolbarState). 바뀔 때만 부르므로 DOM 을 매 프레임 건드리지 않는다.
 *
 * --js-library 옵션으로 링크된다 (webgpu_bindings.js 와 같은 방식).
 */

mergeInto(LibraryManager.library, {

    js_setupToolbar__deps: ['lot_onToolbarKey', '$stringToNewUTF8', 'free'],
    js_setupToolbar: function() {
        if (!Module.lotDom) {
            Module.lotDom = {};
        }
        var dom = Module.lotDom;
        if (dom.toolbar) return;
        // 캔버스는 상태바 아래에서 시작한다. 상태바가 아직 없으면 (호출 순서가 바뀌면) 0.
        var statusBarHeight = dom.statusBar ? dom.statusBar.offsetHeight : 0;

        // [그룹, 버튼들...]. 버튼 = [표시, 키 코드, ctrl 여부, 툴팁, 상태 키]
        // 상태 키는 js_setToolbarState 가 '켜짐' 표시를 할 때 대조하는 이름이다.
        var groups = [
            ['view', [
                ['Front', 'KeyF', 0, 'Front view (F)', 'view:0'],
                ['Top',   'KeyT', 0, 'Top view (T)', 'view:2'],
                ['Right', 'KeyR', 0, 'Right view (R)', 'view:4'],
                ['Iso',   'KeyI', 0, 'Isometric view (I)', 'view:6'],
                ['Ortho', 'KeyP', 0, 'Perspective / orthographic (P)', 'ortho'],
                ['FPS',   'KeyV', 0, 'CAD orbit / first-person (V)', 'fps'],
            ]],
            ['gizmo', [
                ['Move',   'Digit1', 0, 'Move gizmo (1)', 'gizmo:0'],
                ['Rotate', 'Digit2', 0, 'Rotate gizmo (2)', 'gizmo:1'],
                ['Scale',  'Digit3', 0, 'Scale gizmo (3)', 'gizmo:2'],
            ]],
            ['sketch', [
                ['Line',     'KeyL', 0, 'Line (L)', 'sketch:0'],
                ['Rect',     'KeyB', 0, 'Rectangle (B)', 'sketch:1'],
                ['Polyline', 'KeyN', 0, 'Polyline (N)', 'sketch:2'],
                ['Finish',   'Enter', 0, 'Finish sketch (Enter)', ''],
                ['Cancel',   'Escape', 0, 'Cancel sketch / clear selection (Esc)', ''],
            ]],
            ['edit', [
                ['Copy',   'KeyC', 0, 'Duplicate selection (C)', ''],
                ['Delete', 'Delete', 0, 'Delete selection (Del)', ''],
                ['Undo',   'KeyZ', 1, 'Undo (Ctrl+Z)', 'undo'],
                ['Redo',   'KeyY', 1, 'Redo (Ctrl+Y)', 'redo'],
                ['Outline','KeyO', 0, 'Selection outline (O)', 'outline'],
            ]],
        ];

        var bar = document.createElement('div');
        bar.id = 'lot-toolbar';
        bar.style.position = 'fixed';
        bar.style.top = (statusBarHeight + 8) + 'px';
        bar.style.left = '8px';
        bar.style.zIndex = '10';
        bar.style.display = 'flex';
        bar.style.flexDirection = 'column';
        // 창이 낮으면 그룹이 다음 열로 흐른다 - 버튼이 화면 밖으로 잘리지 않게
        bar.style.flexWrap = 'wrap';
        bar.style.alignContent = 'flex-start';
        bar.style.maxHeight = 'calc(100vh - ' + (statusBarHeight + 16) + 'px)';
        bar.style.gap = '6px';
        bar.style.fontFamily = 'monospace';
        bar.style.fontSize = '12px';
        bar.style.userSelect = 'none';

        var buttons = {};  // 상태 키 -> 버튼

        var styleButton = function(b, on, enabled) {
            b.style.color = enabled ? (on ? '#0d0d0d' : '#00ff00') : '#3a5a3a';
            b.style.backgroundColor = on ? '#00ff00' : '#0d0d0d';
            b.style.borderColor = enabled ? '#00ff00' : '#2a3a2a';
            b.style.cursor = enabled ? 'pointer' : 'default';
        };

        groups.forEach(function(g) {
            var box = document.createElement('div');
            box.style.display = 'flex';
            box.style.flexDirection = 'column';
            box.style.gap = '2px';
            box.style.padding = '4px';
            box.style.border = '1px solid #333';
            box.style.borderRadius = '4px';
            box.style.backgroundColor = 'rgba(13, 13, 13, 0.85)';

            var title = document.createElement('div');
            title.textContent = g[0];
            title.style.color = '#666';
            title.style.fontSize = '10px';
            title.style.padding = '0 2px 2px';
            box.appendChild(title);

            g[1].forEach(function(spec) {
                var b = document.createElement('button');
                b.textContent = spec[0];
                b.title = spec[3];
                b.style.padding = '4px 8px';
                b.style.minWidth = '72px';
                b.style.textAlign = 'left';
                b.style.fontFamily = 'monospace';
                b.style.fontSize = '12px';
                b.style.border = '1px solid #00ff00';
                b.style.borderRadius = '3px';
                styleButton(b, false, true);

                // mousedown 을 막아 포커스가 버튼으로 가지 않게 한다 - 그래야 이후
                // Enter/Space 가 버튼을 다시 누르지 않고 캔버스 단축키로 간다.
                b.addEventListener('mousedown', function(e) { e.preventDefault(); });
                b.addEventListener('click', function() {
                    var ptr = stringToNewUTF8(spec[1]);
                    _lot_onToolbarKey(ptr, spec[2]);
                    _free(ptr);
                });
                if (spec[4]) buttons[spec[4]] = b;
                box.appendChild(b);
            });
            bar.appendChild(box);
        });

        // 안내문 (캔버스 왼쪽 아래). 열린 도구의 다음 할 일을 보여준다.
        var hint = document.createElement('div');
        hint.id = 'lot-hint';
        hint.style.position = 'fixed';
        hint.style.left = '50%';
        hint.style.transform = 'translateX(-50%)';
        hint.style.bottom = '12px';
        hint.style.whiteSpace = 'nowrap';
        hint.style.zIndex = '10';
        hint.style.padding = '6px 10px';
        hint.style.fontFamily = 'monospace';
        hint.style.fontSize = '12px';
        hint.style.color = '#ddd';
        hint.style.backgroundColor = 'rgba(13, 13, 13, 0.85)';
        hint.style.border = '1px solid #333';
        hint.style.borderRadius = '4px';
        hint.style.pointerEvents = 'none';
        hint.style.display = 'none';

        document.body.appendChild(bar);
        document.body.appendChild(hint);
        dom.toolbar = bar;
        dom.toolbarButtons = buttons;
        dom.toolbarStyle = styleButton;
        dom.hint = hint;
    },

    // C++ 이 상태가 바뀔 때 부른다. 값은 키보드 컨트롤러/카메라의 enum 그대로.
    //   gizmoMode 0/1/2, sketchTool -1/0/1/2, view -1 또는 CadViewType, fps 0/1,
    //   ortho 0/1, outline 0/1, canUndo/canRedo 0/1, hint = UTF-8 문자열 (빈 문자열이면 숨김)
    js_setToolbarState__deps: ['$UTF8ToString'],
    js_setToolbarState: function(gizmoMode, sketchTool, view, fps, ortho, outline,
                                 canUndo, canRedo, hintPtr) {
        var dom = Module.lotDom;
        if (!dom || !dom.toolbarButtons) return;
        var buttons = dom.toolbarButtons;
        var style = dom.toolbarStyle;

        var setOn = function(prefix, value) {
            for (var key in buttons) {
                if (key.indexOf(prefix + ':') === 0) {
                    style(buttons[key], key === prefix + ':' + value, true);
                }
            }
        };
        setOn('gizmo', gizmoMode);
        setOn('sketch', sketchTool);
        setOn('view', view);
        style(buttons['fps'], fps === 1, true);
        style(buttons['ortho'], ortho === 1, true);
        style(buttons['outline'], outline === 1, true);
        style(buttons['undo'], false, canUndo === 1);
        style(buttons['redo'], false, canRedo === 1);

        var text = UTF8ToString(hintPtr);
        dom.hint.textContent = text;
        dom.hint.style.display = text ? 'block' : 'none';
    },

});

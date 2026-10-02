/**
 * 글자 비트맵 - 브라우저 캔버스로 굽는다.
 *
 * 네이티브는 stb_truetype 으로 글리프 외곽선을 삼각분할하지만, 브라우저에는
 * 폰트 래스터라이저가 이미 있다 (한글 포함). 문자열 하나를 흰 글자 / 투명 배경
 * RGBA 비트맵으로 그려 wasm 힙에 복사해 주면, C++ 이 텍스처로 올려 사각형에 얹는다.
 * 문자열마다 한 번만 굽고 캐시하는 것은 C++ 쪽(TextRenderSystem) 책임이다.
 *
 * --js-library 옵션으로 링크된다.
 */

mergeInto(LibraryManager.library, {

    // text: UTF-8 문자열, pxHeight: 글자 높이(픽셀). 비트맵 폭/높이를 outW/outH 에 쓰고
    // malloc 으로 잡은 RGBA 버퍼를 돌려준다 (해제는 C++). 실패하면 0.
    js_renderTextBitmap__deps: ['$UTF8ToString', 'malloc'],
    js_renderTextBitmap: function(textPtr, pxHeight, outW, outH) {
        var text = UTF8ToString(textPtr);
        if (!text) return 0;

        var canvas = document.createElement('canvas');
        var ctx = canvas.getContext('2d');
        if (!ctx) return 0;

        // 도면 글자에 가까운 굵기. 폰트 크기는 em 이라 실제 글자 높이보다 조금 크다.
        var family = 'px "Segoe UI", "Malgun Gothic", "Apple SD Gothic Neo", sans-serif';
        var font = 'bold ' + pxHeight + family;
        ctx.font = font;
        var metrics = ctx.measureText(text);
        // 텍스처 폭 한도(4096)를 넘는 긴 줄은 해상도를 낮춰 굽는다 - 버리면 글자가 통째로 사라진다.
        // 화면 크기는 C++ 이 가로세로 비로 정하므로 해상도만 줄어든다.
        var kMaxW = 4096;
        var need = metrics.width + pxHeight * 0.3;
        if (need > kMaxW) {
            pxHeight = Math.max(8, Math.floor(pxHeight * kMaxW / need));
            font = 'bold ' + pxHeight + family;
            ctx.font = font;
            metrics = ctx.measureText(text);
        }
        var pad = Math.ceil(pxHeight * 0.15);
        var w = Math.ceil(metrics.width) + pad * 2;
        var h = Math.ceil(pxHeight * 1.3) + pad * 2;
        if (w <= 0 || h <= 0 || w > kMaxW) return 0;

        canvas.width = w;
        canvas.height = h;
        ctx = canvas.getContext('2d');
        ctx.clearRect(0, 0, w, h);
        ctx.font = font;
        ctx.fillStyle = '#ffffff';
        ctx.textBaseline = 'middle';
        ctx.textAlign = 'left';
        ctx.fillText(text, pad, h / 2);

        var pixels = ctx.getImageData(0, 0, w, h).data;  // RGBA, 왼쪽 위부터
        var ptr = _malloc(pixels.length);
        if (!ptr) return 0;
        HEAPU8.set(pixels, ptr);
        HEAP32[outW >> 2] = w;
        HEAP32[outH >> 2] = h;
        return ptr;
    },

});

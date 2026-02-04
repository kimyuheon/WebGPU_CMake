/**
 * WebGPU Bindings for Emscripten
 *
 * 이 파일은 C++에서 호출되는 모든 WebGPU JavaScript 함수를 포함합니다.
 * --js-library 옵션으로 링크됩니다.
 */

mergeInto(LibraryManager.library, {

    // ============================================================
    // Device Functions (lot_web_device)
    // ============================================================

    js_initWebGPU: function() {
        (async () => {
            try {
                console.log("lot_web_device: Initializing...");

                // WebGPU 지원 확인
                if (!navigator.gpu) {
                    console.error("WebGPU not supported!");
                    document.body.innerHTML = "<h1>WebGPU not supported!</h1>";
                    return;
                }

                // 어댑터와 디바이스 획득 (고성능 GPU 요청)
                const adapter = await navigator.gpu.requestAdapter({
                    powerPreference: 'high-performance'
                });
                if (!adapter) {
                    console.error("No adapter found!");
                    return;
                }

                // GPU 정보 출력
                console.log("========================================");
                console.log("GPU Information:");

                if (adapter.info) {
                    console.log("  Vendor: " + (adapter.info.vendor || "unknown"));
                    console.log("  Architecture: " + (adapter.info.architecture || "unknown"));
                    console.log("  Device: " + (adapter.info.device || "unknown"));
                    console.log("  Description: " + (adapter.info.description || "unknown"));
                } else {
                    console.log("  Status: GPU ACTIVE (detail info hidden by browser)");
                    console.log("  Note: Check chrome://gpu for full GPU details");
                }
                console.log("  Features: " + Array.from(adapter.features).join(", "));
                console.log("========================================");

                // Limits 정보
                const limits = adapter.limits;
                console.log("GPU Limits:");
                console.log("  Max Texture Size: " + limits.maxTextureDimension2D);
                console.log("  Max Buffer Size: " + (limits.maxBufferSize / 1024 / 1024).toFixed(2) + " MB");
                console.log("  Max Compute Workgroups: " + limits.maxComputeWorkgroupsPerDimension);
                console.log("========================================");

                const device = await adapter.requestDevice();
                if (!device) {
                    console.error("No device found!");
                    return;
                }

                // 전역 저장
                Module.webgpu = {
                    device: device,
                    initialized: true,
                };

                console.log("lot_web_device: Initialized successfully!");
            } catch (err) {
                console.error("lot_web_device: Init failed:", err);
            }
        })();
    },

    js_isInitialized: function() {
        return Module.webgpu && Module.webgpu.initialized === true;
    },

    js_cleanup: function() {
        if (Module.webgpu) {
            console.log("lot_web_device: Cleanup");
            Module.webgpu = null;
        }
    },

    // ============================================================
    // Swapchain Functions (lot_web_swapchain)
    // ============================================================

    js_createSwapchain: function(statusBarHeight) {
        if (!Module.webgpu || !Module.webgpu.device) {
            console.error("lot_web_swapchain: Device not initialized!");
            return;
        }

        const gpu = Module.webgpu;

        // 기본 스타일 설정
        document.body.style.margin = '0';
        document.body.style.overflow = 'hidden';
        document.body.style.fontFamily = 'monospace';
        document.body.style.backgroundColor = '#1a1a1a';

        // 상태바 생성 (위쪽)
        if (!gpu.statusBar) {
            console.log("lot_web_swapchain: Creating status bar...");
            const statusBar = document.createElement('div');
            statusBar.id = 'status-bar';
            statusBar.style.position = 'fixed';
            statusBar.style.top = '0';
            statusBar.style.left = '0';
            statusBar.style.width = '100%';
            statusBar.style.height = statusBarHeight + 'px';
            statusBar.style.backgroundColor = '#0d0d0d';
            statusBar.style.color = '#00ff00';
            statusBar.style.fontSize = '12px';
            statusBar.style.padding = '8px';
            statusBar.style.boxSizing = 'border-box';
            statusBar.style.overflowY = 'auto';
            statusBar.style.borderBottom = '2px solid #333';
            statusBar.innerHTML = '<div style="color:#888;">WebGPU 3D Engine - Console Output</div><hr style="border-color:#333;margin:4px 0;">';
            document.body.appendChild(statusBar);
            gpu.statusBar = statusBar;

            // console.log 오버라이드
            const originalLog = console.log;
            console.log = function(...args) {
                originalLog.apply(console, args);
                const msg = args.map(a => typeof a === 'object' ? JSON.stringify(a) : a).join(' ');
                const line = document.createElement('div');
                line.textContent = msg;
                line.style.borderBottom = '1px solid #222';
                line.style.padding = '2px 0';
                gpu.statusBar.appendChild(line);
                gpu.statusBar.scrollTop = gpu.statusBar.scrollHeight;
            };

            // console.error 오버라이드
            const originalError = console.error;
            console.error = function(...args) {
                originalError.apply(console, args);
                const msg = args.map(a => typeof a === 'object' ? JSON.stringify(a) : a).join(' ');
                const line = document.createElement('div');
                line.textContent = '[ERROR] ' + msg;
                line.style.color = '#ff4444';
                line.style.borderBottom = '1px solid #222';
                line.style.padding = '2px 0';
                gpu.statusBar.appendChild(line);
                gpu.statusBar.scrollTop = gpu.statusBar.scrollHeight;
            };
        }

        // Canvas 생성 (상태바 아래)
        if (!gpu.canvas) {
            console.log("lot_web_swapchain: Creating canvas...");
            const canvas = document.createElement('canvas');
            canvas.id = 'webgpu-canvas';
            canvas.style.position = 'fixed';
            canvas.style.top = statusBarHeight + 'px';
            canvas.style.left = '0';
            canvas.style.width = '100%';
            canvas.style.height = 'calc(100% - ' + statusBarHeight + 'px)';
            canvas.style.display = 'block';
            document.body.appendChild(canvas);
            gpu.canvas = canvas;
            gpu.statusBarHeight = statusBarHeight;
        }

        // 캔버스 크기를 윈도우 크기에 맞춤
        gpu.canvas.width = window.innerWidth;
        gpu.canvas.height = window.innerHeight - statusBarHeight;

        // Context 설정
        console.log("lot_web_swapchain: Configuring context...");
        const context = gpu.canvas.getContext('webgpu');
        const canvasFormat = navigator.gpu.getPreferredCanvasFormat();

        context.configure({
            device: gpu.device,
            format: canvasFormat,
        });

        gpu.context = context;
        gpu.format = canvasFormat;
        gpu.swapchainReady = true;

        console.log("Canvas size: " + gpu.canvas.width + "x" + gpu.canvas.height);
        console.log("Format: " + canvasFormat);
    },

    js_resizeSwapchain: function(width, height) {
        if (!Module.webgpu || !Module.webgpu.canvas || !Module.webgpu.context) {
            return;
        }

        const gpu = Module.webgpu;
        gpu.canvas.width = width;
        gpu.canvas.height = height;

        gpu.context.configure({
            device: gpu.device,
            format: gpu.format,
        });

        console.log("Resized: " + width + "x" + height);
    },

    js_getCanvasWidth: function() {
        return window.innerWidth;
    },

    js_getCanvasHeight: function(statusBarHeight) {
        return window.innerHeight - statusBarHeight;
    },

    js_isSwapchainReady: function() {
        return Module.webgpu && Module.webgpu.swapchainReady === true;
    },

    js_beginRenderPass: function() {
        if (!Module.webgpu || !Module.webgpu.swapchainReady) {
            return;
        }

        const gpu = Module.webgpu;
        gpu.currentEncoder = gpu.device.createCommandEncoder();
        const textureView = gpu.context.getCurrentTexture().createView();

        const renderPassDescriptor = {
            colorAttachments: [{
                view: textureView,
                clearValue: { r: 0.1, g: 0.1, b: 0.1, a: 1.0 },
                loadOp: 'clear',
                storeOp: 'store',
            }]
        };

        gpu.currentPass = gpu.currentEncoder.beginRenderPass(renderPassDescriptor);
    },

    js_endRenderPass: function() {
        if (!Module.webgpu || !Module.webgpu.currentPass) {
            return;
        }

        const gpu = Module.webgpu;
        gpu.currentPass.end();
        gpu.device.queue.submit([gpu.currentEncoder.finish()]);
        gpu.currentEncoder = null;
        gpu.currentPass = null;
    },

    js_setupResizeListener: function(statusBarHeight) {
        window.addEventListener('resize', () => {
            const width = window.innerWidth;
            const height = window.innerHeight - statusBarHeight;
            Module._onWindowResize(width, height);
        });
        console.log("Resize listener registered");
    },

    // ============================================================
    // Pipeline Functions (lot_web_pipeline)
    // ============================================================

    js_createPipeline: function(shaderPathPtr) {
        (async () => {
            if (!Module.webgpu || !Module.webgpu.device) {
                console.error("lot_web_pipeline: Device not initialized!");
                return;
            }

            const gpu = Module.webgpu;
            const path = UTF8ToString(shaderPathPtr);

            try {
                console.log("lot_web_pipeline: Loading shader from " + path);

                const shaderResponse = await fetch(path);
                const shaderCode = await shaderResponse.text();

                const shaderModule = gpu.device.createShaderModule({
                    label: 'Shader: ' + path,
                    code: shaderCode,
                });

                const vertexBufferLayout = {
                    arrayStride: 24,
                    attributes: [
                        { shaderLocation: 0, offset: 0, format: 'float32x3' },
                        { shaderLocation: 1, offset: 12, format: 'float32x3' },
                    ],
                };

                const pipeline = gpu.device.createRenderPipeline({
                    label: 'Pipeline: ' + path,
                    layout: 'auto',
                    vertex: {
                        module: shaderModule,
                        entryPoint: 'vs_main',
                        buffers: [vertexBufferLayout],
                    },
                    fragment: {
                        module: shaderModule,
                        entryPoint: 'fs_main',
                        targets: [{ format: gpu.format }],
                    },
                    primitive: {
                        topology: 'triangle-list',
                    },
                });

                gpu.pipeline = pipeline;
                gpu.pipelineReady = true;

                console.log("lot_web_pipeline: Pipeline created successfully!");
            } catch (err) {
                console.error("lot_web_pipeline: Failed to create pipeline:", err);
            }
        })();
    },

    js_isPipelineReady: function() {
        return Module.webgpu && Module.webgpu.pipelineReady === true;
    },

    js_bindPipeline: function() {
        if (!Module.webgpu || !Module.webgpu.currentPass || !Module.webgpu.pipeline) {
            return;
        }
        Module.webgpu.currentPass.setPipeline(Module.webgpu.pipeline);
    },

    js_draw: function(vertexCount) {
        if (!Module.webgpu || !Module.webgpu.currentPass) {
            return;
        }
        Module.webgpu.currentPass.draw(vertexCount);
    },

    // ============================================================
    // Buffer Functions (lot_web_buffer)
    // ============================================================

    js_createBuffer: function(bufferId, bufferType, dataPtr, size) {
        if (!Module.webgpu || !Module.webgpu.device) {
            console.error("lot_web_buffer: Device not initialized!");
            return;
        }

        const gpu = Module.webgpu;

        let usage;
        let typeName;
        if (bufferType === 0) {  // VERTEX
            usage = GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST;
            typeName = "Vertex";
        } else if (bufferType === 1) {  // INDEX
            usage = GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST;
            typeName = "Index";
        } else if (bufferType === 2) {  // UNIFORM
            usage = GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST;
            typeName = "Uniform";
        } else {
            console.error("lot_web_buffer: Unknown buffer type:", bufferType);
            return;
        }

        const buffer = gpu.device.createBuffer({
            label: typeName + " Buffer #" + bufferId,
            size: size,
            usage: usage,
        });

        if (dataPtr !== 0) {
            const dataArray = new Float32Array(Module.HEAPF32.buffer, dataPtr, size / 4);
            gpu.device.queue.writeBuffer(buffer, 0, dataArray);
        }

        if (!gpu.buffers) {
            gpu.buffers = {};
        }
        gpu.buffers[bufferId] = {
            buffer: buffer,
            type: bufferType,
            size: size,
        };

        console.log("lot_web_buffer: Created " + typeName + " buffer #" + bufferId + " (" + size + " bytes)");
    },

    js_isBufferReady: function(bufferId) {
        return Module.webgpu && Module.webgpu.buffers && Module.webgpu.buffers[bufferId] !== undefined;
    },

    js_bindVertexBuffer: function(bufferId, slot) {
        if (!Module.webgpu || !Module.webgpu.currentPass) {
            return;
        }

        const gpu = Module.webgpu;
        const bufferData = gpu.buffers[bufferId];

        if (!bufferData) {
            console.error("lot_web_buffer: Buffer #" + bufferId + " not found!");
            return;
        }

        if (bufferData.type !== 0) {
            console.error("lot_web_buffer: Buffer #" + bufferId + " is not a vertex buffer!");
            return;
        }

        gpu.currentPass.setVertexBuffer(slot, bufferData.buffer);
    },

    js_bindIndexBuffer: function(bufferId) {
        if (!Module.webgpu || !Module.webgpu.currentPass) {
            return;
        }

        const gpu = Module.webgpu;
        const bufferData = gpu.buffers[bufferId];

        if (!bufferData) {
            console.error("lot_web_buffer: Buffer #" + bufferId + " not found!");
            return;
        }

        if (bufferData.type !== 1) {
            console.error("lot_web_buffer: Buffer #" + bufferId + " is not an index buffer!");
            return;
        }

        gpu.currentPass.setIndexBuffer(bufferData.buffer, 'uint32');
    },

    js_destroyBuffer: function(bufferId) {
        if (!Module.webgpu || !Module.webgpu.buffers) {
            return;
        }

        const bufferData = Module.webgpu.buffers[bufferId];
        if (bufferData) {
            bufferData.buffer.destroy();
            delete Module.webgpu.buffers[bufferId];
            console.log("lot_web_buffer: Destroyed buffer #" + bufferId);
        }
    },

    // ============================================================
    // SimpleRenderSystem Functions
    // ============================================================

    js_srs_createUniformBuffer: function() {
        if (!Module.webgpu || !Module.webgpu.device || !Module.webgpu.pipeline) {
            return;
        }

        const gpu = Module.webgpu;

        gpu.uniformBuffer = gpu.device.createBuffer({
            label: 'Uniform Buffer',
            size: 16,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });

        gpu.bindGroup = gpu.device.createBindGroup({
            label: 'Uniform Bind Group',
            layout: gpu.pipeline.getBindGroupLayout(0),
            entries: [{
                binding: 0,
                resource: { buffer: gpu.uniformBuffer },
            }],
        });

        console.log("SimpleRenderSystem: Uniform buffer created");
    },

    js_srs_updateUniform: function(offsetX, offsetY, rotation, scale) {
        if (!Module.webgpu || !Module.webgpu.uniformBuffer) {
            return;
        }

        const gpu = Module.webgpu;
        const uniformData = new Float32Array([offsetX, offsetY, rotation, scale]);
        gpu.device.queue.writeBuffer(gpu.uniformBuffer, 0, uniformData);
    },

    js_srs_bindUniformGroup: function() {
        if (!Module.webgpu || !Module.webgpu.currentPass || !Module.webgpu.bindGroup) {
            return;
        }
        Module.webgpu.currentPass.setBindGroup(0, Module.webgpu.bindGroup);
    },

    js_srs_bindVertexBuffer: function(bufferId, slot) {
        if (!Module.webgpu || !Module.webgpu.currentPass || !Module.webgpu.buffers) {
            return;
        }

        const gpu = Module.webgpu;
        const bufferData = gpu.buffers[bufferId];

        if (bufferData && bufferData.buffer) {
            gpu.currentPass.setVertexBuffer(slot, bufferData.buffer);
        }
    },

    js_srs_draw: function(vertexCount) {
        if (!Module.webgpu || !Module.webgpu.currentPass) {
            return;
        }
        Module.webgpu.currentPass.draw(vertexCount);
    },

});

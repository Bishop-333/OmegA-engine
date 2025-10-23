# RTX Bring-Up Plan

This document captures the full end-to-end plan for getting the Vulkan-based RTX path tracer into a shippable state. The goal is to progress from configuration/cvar plumbing all the way to stable pixels on screen with optional denoiser/DLSS support and good diagnostics.

---

## Phase 0 – Establish Current Behaviour & Tooling
1. **Repro the baseline**: capture logs and GPU traces (RenderDoc) when running a map with `rtx_enabled 1` and `r_rt_enabled 1`. Note what actually renders (currently nothing ray-traced) and collect warnings.
2. **Enable verbose logging**: temporarily bump `developer` cvars and add printf breadcrumbs to trace init order, feature detection, and per-frame dispatch.
3. **Document build flags**: list which optional modules (OptiX, DLSS) are compiled in under our configuration.
4. **Set up test maps**: pick at least one simple BSP and one stress map for regression testing as fixes land.

Deliverables: baseline logs, capture files, and a short “current-state” write-up.

---

## Phase 1 – Configuration & Cvar Canonicalisation
1. **Consolidate enable switches**
   - Decide whether `r_rtx_enabled` or `rtx_enabled` is the canonical toggle (recommend `r_rtx_enabled`).
   - Remove aliases or ensure bidirectional sync so only one source of truth exists.
2. **Audit related cvars**: `r_rtx_quality`, `r_rtx_gi_bounces`, `r_rt_*` etc. Ensure defaults make sense and are stored in the base config shipped with the repo.
3. **Hook UI/debug bindings**: make sure the config binds (F-keys) reflect the final cvar names.
4. **Update documentation**: clearly describe which cvars control RTX, hybrid compositor, and debugging.

Dependencies: none. Output: cleaned config files, cvar list, README updates.

---

## Phase 2 – Vulkan Capability Probing & Feature Flags
1. **Extend `RTX_CheckVulkanRTSupport`**
   - Enumerate device extensions and set `rtx.features` bits for `RTX_FEATURE_RAY_TRACING`, `RTX_FEATURE_RAY_QUERY`, etc.
   - Store shader-group handle sizes, recursion depth, alignment requirements in the shared `rtxState_t`.
2. **Populate vendor / GPU type**
   - Read `VkPhysicalDeviceProperties.vendorID` and map to `RTX_GPU_*` enum.
   - Optionally detect architecture (e.g. NV Ampere/Turing) if different feature behaviour is needed.
3. **Query optional capabilities**
   - Check for DLSS/OptiX availability (presence of SDKs, vendor match) and set the relevant bits **before** optional init code runs.
   - Add fallbacks for unsupported combinations (disable modules gracefully).
4. **Log capability summary**: after init, dump a structured list of enabled features to the console and to a debug log.

Dependencies: Phase 1 (cvar naming). Output: `rt_rtx_impl.c` updates, `rtxState_t` properly populated, enhanced logging.

---

## Phase 3 – Core RTX Pipeline Initialisation
1. **Command and synchronization path**
   - Verify command pool/buffer lifetimes and re-init on renderer restart.
   - Ensure fences/semaphores are reset per frame (avoid the current “fence was signaled” validation error).
2. **Shader Binding Table (SBT)**
   - Confirm SBT buffer sizes use `vkrt.rtProperties.shaderGroupHandleSize/alignment` and handle dynamic shader counts.
   - Validate SBT device addresses are written to `rtx.primaryPipeline` for reuse in `RTX_RecordCommands`.
3. **Pipeline layout & descriptors**
   - Audit the descriptor set layout against the GLSL raygen/miss/hit shaders.
   - Ensure material/light buffers, TLAS, output images are bound at the correct set/binding.
4. **Acceleration structures**
   - Review BLAS/TLAS builders for proper scratch buffer alignment, usage flags, and rebuild paths.
   - Hook TLAS rebuild triggers to world-loading events (map load, dynamic entity changes).
5. **Output surfaces**
   - Create and maintain the ray-traced color image (format, usage, memory). Handle resize/RT toggle transitions.

Dependencies: Phase 2 (capabilities). Output: stable pipeline creation, no validation errors.

---

## Phase 4 – Per-Frame Execution & Composition
1. **Command recording (`RTX_RecordCommands`)**
   - Ensure begin/end command buffer calls, pipeline bind, descriptor bind, and `vkCmdTraceRaysKHR` are valid.
   - Record image layout transitions (UNDEFINED → GENERAL → TRANSFER_SRC) with proper access masks.
2. **Synchronization with raster path**
   - Define when the ray-traced image is consumed: before post-processing? integrate into `tr_postprocess` or specific composite pass.
   - Add semaphores or pipeline barriers between the RTX queue (if separate) and graphics queue.
3. **Hybrid compositor**
   - Implement or verify `RTX_CompositeHybridAdd` to blend ray-traced lighting with forward rendering (tone mapping considerations).
   - Provide cvars for blend intensity / debug overlays.
4. **Presentation**
   - Ensure the final image is copied or sampled into the swapchain-compatible surface without layout conflicts.

Dependencies: Phase 3. Output: visible ray-traced contribution on screen, deterministic across frames.

---

## Phase 5 – Optional Modules (Denoiser, DLSS, Reflex)
1. **Denoiser path**
   - Based on `rtx.features & RTX_FEATURE_DENOISER`, initialize OptiX or other denoiser.
   - Wire input/output buffers to match ray-tracing output; manage temporal history and motion vectors if available.
2. **DLSS integration**
   - Check vendor support; request render/display sizes; set up NGX handles.
   - Integrate upscaling after denoising but before post-processing.
3. **Reflex / latency markers**
   - If supported, insert appropriate markers around frame boundaries.
4. **Fallbacks**
   - Gracefully disable modules when unsupported, logging why.

Dependencies: Phases 2–4. Output: optional quality/perf features functioning when available.

---

## Phase 6 – Tooling, Debug, and Diagnostics
1. **Debug overlays**
   - Ensure `RTX_SetDebugMode` toggles the correct compute/graphics passes and that F-key bindings map to working modes.
2. **Stat counters**
   - Track BLAS/TLAS rebuild times, trace time, denoise time; expose via console or ImGui overlay.
3. **Validation**
   - Enable Vulkan validation in debug builds; fix outstanding warnings (e.g., fence state).
4. **Automated tests**
   - Add scripted timedemos that toggle RTX on/off to verify stability and performance metrics.

Dependencies: previous phases to provide meaningful data.

---

## Phase 7 – Documentation & Final QA
1. **User-facing docs**: update README/BUILD instructions to describe required hardware, cvars, and known limitations.
2. **Developer documentation**: capture the final architecture (data flow, key modules) for future maintainers.
3. **QA checklist**: visual regression runs, performance benchmarks, memory leak checks, multi-map playtest (load/unload, toggling RTX live).

Deliverables: up-to-date docs, QA sign-off, release notes snippet.

---

## Cross-Cutting Concerns
- **Restart Handling**: every module must survive `vid_restart` and map reloads without leaking resources.
- **Error Handling**: convert silent early-outs into logged failures with clear remediation steps.
- **Threading / Queues**: decide whether RTX work runs on the graphics queue or a dedicated RT queue; update synchronization accordingly.
- **Code Hygiene**: standardize naming (rtx_ vs r_rtx_), remove stale D3D placeholders, and ensure headers only expose needed symbols.

---

## Next Steps
1. Execute Phase 0 to capture baseline state.
2. Implement Phase 1–2 fixes in small PRs, verifying with validation layers enabled.
3. Iterate through remaining phases, checkpointing with in-game screenshots and logs to document progress.


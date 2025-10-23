> **Progress Tracking Instructions**  
> - Each phase in this document must be annotated with a status tag in the form `[STATUS] Phase Title` where `STATUS` is one of `PENDING`, `IN PROGRESS`, or `COMPLETED`.  
> - Update the status tag whenever work begins or finishes for that phase.  
> - For partially completed work, add bullet notes beneath the phase detailing what has been finished and what remains.  
> - Include date + agent initials for each status change (e.g., `_2025‑05‑10 JD_`).  
> - Do not remove historical notes; append updates so progress across sessions is preserved.

# Lighting System Modernization Plan

## Vision
Replace the patchwork of legacy lighting techniques with a single, physically based per‑pixel system. All pixels are lit by the same path‑traced solution, with RTX hardware acceleration acting as an optional backend. Traditional fallbacks (lightmaps, vertex lighting, fullbright, multi‑pass additive lights, stencil shadows) are removed so behaviour stays consistent across hardware tiers.

## Current Legacy Features to Retire
- **Baked path dependence**: BSP lightmap loading/merging (`src/engine/renderer/world/tr_bsp.c:166`) and light-grid sampling in `src/engine/renderer/lighting/tr_light.c:298`.
- **Compatibility CVars**: `r_fullbright`, `r_vertexLight`, `r_mergeLightmaps`, `r_lightmap`, `r_singleShader`, etc., registered in `src/engine/renderer/core/tr_init.c:1553+`. _(Removed 2025-10-21)_.
- **Redundant dynamic light flows**: `R_ProcessDynamicLights`, `R_ConvertDlights`, and the Doom 3 style additive pass (`src/engine/renderer/vulkan/vk_additive_light.c`). _(Removed 2025-10-21 AI)_
- **Shadow compatibility layers**: CPU shadow volumes (`src/engine/renderer/shadows/tr_shadow_volume.c`) and cascaded shadow fallback code.
- **Shader-stage lightmap reliance**: vertex/lightmap collapsing in `src/engine/renderer/shading/tr_shader.c:3223`.
- **Documentation/config references**: shipped cfgs and docs that still advertise the removed paths.

## Target Architecture
1. **Unified Light Description**
   - Keep `renderLight_t`/`lightSystem_t` as the canonical scene-light registry, upgraded for physically based properties (shape, softness, emissive geometry).
   - World illumination draws from real‑time GI/probes, not baked textures.

2. **Single Lighting Pipeline**
   - Path tracer (`rt_pathtracer.*`) becomes the sole lighting implementation for all pixels.
   - Geometry buffers (G-buffer or visibility pass) feed the tracer every frame.
   - RTX backend (`rt_rtx_*`) consumes identical scene data; toggling RTX swaps tracing kernels, not the lighting model.

3. **Integrated Effects**
   - Soft shadows, ambient occlusion, and emissives are generated inside the tracer.
   - Volumetrics (`tr_volumetric.c`) sample the unified light array.
   - Post-processing remains HDR-based; no separate ambient/base passes.
   - PBR materials (with override support) are mandatory inputs.

## Migration Plan

### [COMPLETED] Phase 1 – Deprecate Legacy Paths _2025-10-21 CA_
- Kickoff: begin implementing `r_modernLighting` flag and legacy toggle auditing. Remaining: config/docs update, runtime warning hook. _2025-10-21 CA_
- Introduce a transition flag (e.g. `r_modernLighting`) that forces the new pipeline and logs usage of deprecated toggles.
- Update configs (`config/*.cfg`) and docs to set the modern flag and mark `r_fullbright`, `r_vertexLight`, `r_mergeLightmaps`, etc., as obsolete.
- Emit runtime warnings whenever lightmap textures are loaded.
- Completed: Enforced read-only `r_modernLighting`, updated configs/docs, and emit BSP lightmap warning. _2025-10-21 CA_

### [COMPLETED] Phase 2 – Remove Legacy Artifacts _2025-10-22 AI_
- Kickoff: Removing BSP lightmap/vertex lighting paths; legacy additive flows next. _2025-10-21 CA_
- Strip lightmap loading/merging from `tr_bsp.c`; ensure shader setup no longer expects lightmap stages.
- Delete vertex-lighting branches in `tr_shader.c`/`tr_light.c` and remove associated CVars. _Completed 2025-10-21 CA_
- Excise `R_ProcessDynamicLights`, `R_ConvertDlights`, and the multi-pass additive backend, replacing the render-view hook with “push lights to tracer”.
- Removed `R_ProcessDynamicLights`, `R_ConvertDlights`, and the Vulkan additive backend; `R_RenderView` now updates the path tracer via `RT_UpdateDynamicLights`. _2025-10-21 AI_
- Retired legacy lighting CVars (`r_dynamicLighting`, `r_disableStaticLights`, `r_lightCullDistance`, `r_lightGridSize`, `r_showInteractions`) in favour of the unified path-traced pipeline and consolidated debug toggles under `r_showLightVolumes`. _2025-10-22 AI_
- Stripped `r_lightScissoring`, `r_scissor*`, and related scissor modules/UI hooks so the path-traced pipeline no longer exposes legacy additive tuning. _2025-10-21 AI_

### [COMPLETED] Phase 3 – Strengthen the Path Tracer _2025‑10‑23 CA_, _2025-10-23 CB_
- Consolidate light ingestion so the tracer receives all scene lights (static + dynamic/emissive) via a single API.
- Unified `rt_sceneLights` merges dynamic dlights, extracted statics, and feeds both CPU tracer and RTX upload paths. _2025-10-21 AI_
- `RT_UpdateDynamicLights` now pulls from the render-light system's visible set with legacy dlights kept as a fallback, ensuring the tracer sees a unified feed across CPU and RTX paths. _2025-10-23 CA_
- `RT_EvaluateDirectLighting` now consumes the combined `rt_sceneLights` array (with normalized directions, attenuation, and shadow state) so both CPU shading and RTX uploads operate on identical light data. _2025-10-23 CA_
- Temporal accumulation buffers now allocate per-resolution, track variance/sample counts, and auto-reset on mode/quality changes to stabilize denoising. _2025-10-23 CA_
- Added CPU-side temporal + spatial denoiser pipeline with runtime toggles (`rt_temporal`, `rt_denoise`), wiring accumulation outputs into the screen dispatcher for progressive previews. _2025-10-23 CA_
- Batched shadow queries now target the Vulkan ray-query compute path when RTX hardware isn’t present, trimming CPU fallback rays while retaining the legacy traversal as a safety net. _2025-10-23 CA_
- Move the tracer to GPU compute when RTX is absent (leveraging Vulkan ray queries) to keep performance acceptable.
  - **[COMPLETED] Sub-phase 3.1 – GPU Geometry & Acceleration Upload** _2025-10-24 CA_
    - Goal: establish GPU-friendly BVH/TLAS representation derived from the existing BSP, with streaming updates for dynamic entities (modules: `rt_bsp_loader.c`, new GPU buffers). _2025-10-24 CA_
    - Completed: World BSP batching now emits device-local BLAS clusters with per-triangle material indices using staged uploads (`src/engine/renderer/pathtracing/rt_bsp_loader.c`, `src/engine/renderer/pathtracing/rt_rtx_impl.c`). _2025-10-24 CA_
    - Completed: BLAS GPU builds allocate persistent material index buffers for shading integration alongside vertex/index resources (`src/engine/renderer/pathtracing/rt_rtx_impl.c`). _2025-10-24 CA_
    - Completed: TLAS construction is double-buffered and driven by a new dynamic refit queue (`RTX_QueueInstanceRefit`/`RTX_ProcessPendingRefits`) so movers stream updates without stalling uploads (`src/engine/renderer/pathtracing/rt_rtx.c`, `src/engine/renderer/pathtracing/rt_rtx_impl.c`). _2025-10-24 CA_
    - Follow-up: Wire triangle-material buffers into ray-query shaders and add stress tests to exercise refit queue saturation. _2025-10-24 CA_
  - **[COMPLETED] Sub-phase 3.2 – Vulkan Ray Query Shading Pipeline** _2025-10-24 CA_
    - Completed: Extended the Vulkan ray-query descriptor layout with triangle-material atlases and instance offsets so the compute path mirrors the primary path tracer inputs (`src/engine/renderer/pathtracing/rt_rtx_pipeline.c`, `.../rt_rtx_impl.c`). _2025-10-24 CA_
    - Completed: Bound per-triangle material IDs into the `shadow_queries` compute shader, skipping translucent/no-shadow surfaces to match material selection on legacy BSP content (`src/engine/renderer/shaders/compute/shadow_queries.comp`). _2025-10-24 CA_
    - Completed: Aggregated BLAS triangle material ranges into a GPU-resident atlas and propagate offsets via TLAS custom indices, ensuring ray-query shading resolves the correct surface material across mixed batches. _2025-10-24 CA_
    - Verification: Ran material flag checks against Quake III stock maps (`q3dm6`, `q3ctf1`) to confirm glass/water surfaces no longer self-shadow while opaque BSP surfaces retain occlusion. _2025-10-24 CA_
  - **[COMPLETED] Sub-phase 3.3 – Temporal/Denoise Integration & Validation** _2025-10-24 CA_
    - Completed: GPU path now stages the Vulkan ray-query output back through `RT_ProcessGpuFrame`, reusing the CPU temporal/variance buffers and denoiser so both backends share identical history management (`src/engine/renderer/pathtracing/rt_rtx_impl.c`, `.../rt_pathtracer.c`). _2025-10-24 CA_
    - Completed: Added the `rt_gpuValidate` harness that samples CPU reference traces on a configurable stride, reporting RMSE/max error each frame to catch GPU regressions early. _2025-10-24 CA_
    - Completed: Descriptor/shader plumbing updates ensure triangle-material atlases drive both primary shading and shadow queries, keeping validation parity across feature tiers (`src/engine/renderer/pathtracing/rt_rtx_pipeline.c`, `.../shaders/compute/shadow_queries.comp`). _2025-10-24 CA_
    - Follow-up: Automate nightly validation sweeps on Tier 1/2 Vulkan hardware and archive perf snapshots for regression tooling. _2025-10-24 CA_
- Completed: Removed BSP lightgrid ingestion and routed entity/path-tracer lighting through the unified probe feed (`tr_bsp.c`, `tr_light*.c`, `rt_pathtracer.c`). _2025-10-23 CB_
- Shader/material parsing now flags `$lightmap` usage as legacy, defaults offending stages to neutral textures, and removes the last `tr.mergeLightmaps` / `r_vertexLight` branches so the pipeline no longer depends on retired globals (`tr_shader.c`, `tr_backend.c`, `rt_pathtracer.*`). _2025-10-23 CB_
- Verification: `build_debug.bat` passes after the shader/path-tracer cleanup; runtime no longer references `tr.world->lightGridData` or other lightmap-specific fields. _2025-10-23 CB_
- Ensure temporal accumulation and denoising are robust enough to replace lightmaps.
- Temporal accumulation now tracks per-pixel means, variance, and sample counts to stabilize denoising without legacy lightmaps. _2025-10-21 AI_
- Prepared batched shadow query infrastructure so GPU ray-query execution can replace the CPU fallback without touching material code paths. _2025-10-21 AI_
- Implemented batched Vulkan ray-query dispatch to accelerate shadow tests when supported, with the CPU tracer retained as a fallback. _2025-10-22 AI_

### [COMPLETED] Phase 4 – Optional RTX Backend _2025-10-22 AG_
- Deliver a single lighting backend where RTX hardware acceleration is an optional drop-in over the path tracer core.
  - **[COMPLETED] Sub-phase 4.1 – Backend Lifecycle Consolidation** _2025-10-25 AI_
    - Untangle init/shutdown so `RT_InitPathTracer` owns backend selection and remove direct `RTX_Init`/`RTX_Shutdown` usage in `src/engine/renderer/core/tr_init.c:2095` and `:2149` plus `src/engine/renderer/vulkan/vk.c:4929`, routing all hardware toggles through the path tracer.
    - Replace `vk.c` direct `RTX_RecordCommands`/`RTX_ApplyDebugOverlayCompute` hooks at `src/engine/renderer/vulkan/vk.c:7809` and `:7820` with a backend-agnostic submission entry point so software and RTX share the same render path.
    - Extend `RT_ShutdownPathTracer` to release hardware state and re-run `RT_SelectBackend` when `r_rt_backend`/`rtx_enable` change, preventing double init logging loops.
    - Completed: Path tracer now brokers RTX lifecycle, exported neutral Vulkan hooks, and cleans backend state on shutdown/toggles; verified no external `RTX_*` calls remain in init/shutdown paths. _2025-10-25 AI_
  - **[COMPLETED] Sub-phase 4.2 – Shared Scene Geometry Pipeline** _2025-10-25 AI_
    - Implement `RTX_PrepareForWorld`/`RTX_PopulateWorld` (`src/engine/renderer/pathtracing/rt_rtx.h:354-356`) using the batch helpers in `rt_bsp_loader.c`, and invoke them from `RE_LoadWorldMap` (`src/engine/renderer/world/tr_bsp.c:1973-2040`) so TLAS/BLAS build alongside BSP ingestion.
    - Ensure `RT_BuildAccelerationStructure` runs on world loads so CPU fallback and RTX share geometry ownership, caching handles for later backend switches.
    - Wire dynamic entity updates through the refit queue (`rtx.refitQueue` in `rt_rtx.c`/`rt_rtx_impl.c:895+`) so instance transforms stay in sync regardless of backend.
    - Completed: World loads now reset/rebuild shared TLAS/BLAS via `RTX_PrepareForWorld`/`RTX_PopulateWorld`, CPU BSP acceleration is rebuilt during `RE_LoadWorldMap`, and backend activations queue refits so TLAS state stays current when RTX toggles. _2025-10-25 AI_
  - **[COMPLETED] Sub-phase 4.3 – Descriptor & Buffer Unification** _2025-10-25 AI_
    - Rework `RTX_UpdateDescriptorSets`/`RTX_PrepareFrameData` to consume the path-tracer light/material buffers (see `src/engine/renderer/pathtracing/rt_rtx_pipeline.c:1639` and `rt_rtx_material.c`) instead of private staging, eliminating divergent resource lifetimes.
    - Share material cache invalidation between `rt_rtx_material.c` and CPU shading by keying off `shader_t` handles, preventing duplicate conversions when the backend toggles mid-session.
    - Harden `RTX_RecordCommands` (`src/engine/renderer/pathtracing/rt_rtx_impl.c:2120+`) with TLAS/descriptor validation so dispatch fails fast if resources didn’t rebuild after a backend switch.
    - Completed: Path tracer now owns the shared light/material GPU buffers, Vulkan descriptors bind them directly, and per-frame staging copies were removed in favour of dirty-flag updates. _2025-10-25 AI_
  - **[COMPLETED] Sub-phase 4.4 – Runtime Toggle & Diagnostics** _2025-10-25 AI_
    - Add cvar watchers so changing `r_rt_backend` or `rtx_enable` triggers a controlled backend flip (update `src/engine/renderer/pathtracing/rt_pathtracer.c:120-160` and `rt_rtx.c:43-120`) and surface errors via `RTX_Status`.
    - Expand `RT_Status_f` output (`src/engine/renderer/pathtracing/rt_pathtracer.c:1802+`) and Vulkan overlays to report active backend, TLAS/BLAS counts, and last toggle reason.
    - Capture timing counters for software vs hardware frames and expose them through `rt_gpuValidate` so QA can prove parity when RTX is optional.
    - Completed: Backend toggles now log cvar changes, update shared status strings, and diagnostics commands/`rtx_status` expose TLAS/BLAS counts, timing, and buffer health for QA. _2025-10-25 AI_
  - **[COMPLETED] Sub-phase 4.5 – Validation & Tooling** _2025-10-22 AG_
    - Extend `rt_gpuValidate` harness (`src/engine/renderer/pathtracing/rt_pathtracer.c:840+`) to exercise hardware vs compute backends on representative maps and flag RMSE regressions.
    - Refresh docs (`docs/rtx/stage4/README.md`) and configs (`config/debug_rtx_enable.cfg`) to describe the new switchable backend flow and remove obsolete flags.
    - Add regression scripts (`debug_rtx_vs.bat`, `test_rtx_demo.bat`) that run both backends headless and capture frame hashes for CI comparison.
    - Completed: `rt_gpuValidate` records compute and RTX samples in one session, logs parity deltas/hashes, and surfaces results via `rt_status` (`src/engine/renderer/pathtracing/rt_pathtracer.c:179`, `src/engine/renderer/pathtracing/rt_pathtracer.h:248`). _2025-10-22 AG_
    - Completed: Added headless validation harness assets (`debug_rtx_vs.bat`, `test_rtx_demo.bat`, `config/debug_rtx_enable.cfg`) and updated Stage 4 docs with tooling workflow (`docs/rtx/stage4/README.md`). _2025-10-22 AG_
- Completed: Phase 4 now ships with automated backend validation tooling and CI-friendly defaults, closing out the optional RTX backend milestone. _2025-10-22 AG_

### [PENDING] Phase 5 – Cleanup & Polish
  - **[COMPLETED] Sub-phase 5.1 – Retire Stencil/Shadow-Volume Path** _2025-10-22 AG_
    - Remove the unused stencil shadow stack (`src/engine/renderer/lighting/tr_shadows.c`, `src/engine/renderer/shadows/tr_shadow_volume.*`, `src/engine/renderer/shadows/tr_stencil_shadow.*`) and drop their hook points in the mesh/entity paths (`src/engine/renderer/geometry/tr_mesh.c:344`, `src/engine/renderer/geometry/tr_animation.c:241`, `src/engine/renderer/models/tr_model_iqm.c:1110`).
    - Delete the parallel shadow job scaffolding (`src/engine/renderer/optimization/tr_parallel.*`) and scrub all `r_shadows`-driven code paths, including renderer backends (`src/engine/renderer/lighting/tr_light.c:362`, `src/engine/renderer/lighting/tr_light_backend.c:147`, `src/engine/renderer/vulkan/vk.c:3235`) and UI/config exposure.
    - Ensure dynamic occlusion now routes through the path tracer exclusively (extend `rt_light_backend.c` and validation hooks) so soft-shadow support remains intact after the legacy removal.
    - Completed: Removed the legacy shadow-volume modules and parallel job scaffolding from the build (deleted `tr_shadows.c`, `tr_shadow_volume.*`, `tr_stencil_shadow.*`, `tr_parallel.*`, and pruned build entries in `Makefile`/`src/project/msvc2017/quake3e.vcxproj`). _2025-10-22 AG_
    - Completed: Simplified runtime lighting to rely solely on the path tracer by dropping `r_shadows` checks and shadow-surface injection (`src/engine/renderer/geometry/tr_mesh.c:344`, `src/engine/renderer/geometry/tr_animation.c:241`, `src/engine/renderer/models/tr_model_iqm.c:1110`, `src/engine/renderer/lighting/tr_light.c:367`, `src/engine/renderer/lighting/tr_light_backend.c:147`). _2025-10-22 AG_
    - Completed: Removed the `r_shadows` cvar from engine/config surfaces and updated docs to reflect the unified pipeline (`config/ultra_settings.cfg:82`, `config/q3config.cfg:51`, `README.md:387`). _2025-10-22 AG_
- **[COMPLETED] Sub-phase 5.2 – Remove Cascaded Shadow Maps & Shadow Pool** _2025-10-22 AG_
  - Dismantle the Vulkan shadow-map system (`src/engine/renderer/vulkan/vk_shadows.c`, `src/engine/renderer/vulkan/vk_shadows_impl.c`, `src/engine/renderer/vulkan/vk_shadows.h`) and the update hooks in `src/engine/renderer/lighting/tr_light_mgmt.c:628`, migrating any remaining sun/light occlusion to tracer probes.
  - Retire the associated cvars (`r_shadowMapSize`, `r_shadowCascadeCount`, `r_shadowDistance`, `r_shadowSoftness`, `r_shadowBias`, `r_shadowFilter`) and pipeline plumbing (`src/engine/renderer/vulkan/vk_uber.c:174`, `src/engine/renderer/vulkan/vk_shader.h:206`), ensuring toggles such as `vk.shadow_volume_pipelines` and descriptor wiring are excised.
  - Audit volumetric and post-processing passes for dependencies on cascade matrices so fog/volumetric lighting sample the tracer light array instead of legacy CSM data.
  - Completed: Removed the Vulkan shadow-map stack and build entries (deleted `vk_shadows*.c/h` and pruned references from `src/project/msvc2017/quake3e.vcxproj:456`, `src/engine/renderer/vulkan/vk_uber.c:174`, `src/engine/renderer/vulkan/vk_shader.h:206`). _2025-10-22 AG_
  - Completed: Stripped shadow-map management from the light system (`src/engine/renderer/lighting/tr_light_dynamic.h:87`, `src/engine/renderer/lighting/tr_light_mgmt.c:50`, `src/engine/renderer/lighting/tr_light_debug.c:338`) so the tracer exclusively owns occlusion data. _2025-10-22 AG_
  - Completed: Updated shipped configs/docs to swap legacy `r_shadow*` toggles for `rt_*` path-tracer settings (`config/ultra_settings.cfg:87`, `config/q3config.cfg:55`, `README.md:375`). _2025-10-22 AG_
- **[PENDING] Sub-phase 5.3 – Strip Lightmap-Era Data Structures** _2025-10-22 AG_
  - Remove `LIGHTMAP_INDEX_*` usage from `textureBundle_t`/`shaderStage_t` (`src/engine/renderer/core/tr_local.h:344`) and flatten shader/material registration (`src/engine/renderer/shading/tr_shader.c:3592`, `src/engine/renderer/materials/tr_material.c:657`) to drop `lightmapIndex` plumbing.
  - Stop ingesting legacy BSP lightmap vectors in `src/engine/renderer/world/tr_bsp.c:399` and delete `tr.lightmaps` storage (`src/engine/renderer/core/tr_local.h:1247`), keeping only any metadata the exporter still requires.
  - Purge path-tracer fallbacks that read lightmap grids (`src/engine/renderer/pathtracing/rt_pathtracer.c:1367`) and migrate any remaining debug overlays or material overrides that expect lightmap slots.
- **[PENDING] Sub-phase 5.4 – Refresh Automation & Runtime Defaults** _2025-10-22 AG_
  - Update shipped configs/scripts (e.g., `config/ultra_settings.cfg:82`, `config/q3config.cfg`, `baseq3/ci/*`, `*.bat` harnesses) to remove `r_dlight*`, `r_showLightMaps`, and other retired CVars, replacing them with `rt_*` equivalents as needed.
  - Align automated tests and CI harnesses so validation relies on the tracer (`debug_rtx_vs.bat`, `test_rtx_demo.bat`, builder scripts) and ensure no fallback launches the legacy GL/Vulkan additive paths.
  - Prune any remaining asset build steps that expect lightmap atlases or shadow-map exports, updating packaging to ship only tracer resources.
  - Partial: Updated shipped configs and RTX debug docs/binds to remove `r_showLightMaps` usage and reflect the new debug modes. _2025-10-23 CB_
- **[PENDING] Sub-phase 5.5 – Documentation & Comms Refresh** _2025-10-22 AG_
  - Rewrite renderer documentation to describe the unified lighting flow and new shadow expectations (`docs/quake3e.md`, `docs/quake3e.htm`, `docs/rtx/*`), removing call-outs to legacy toggles like `r_vertexLight` or `r_shadowMapSize`.
  - Update release notes/FAQ entries (`docs/quake3e-changes.txt`) and in-game help to reflect that shadow volumes/lightmaps are gone and RTX is optional acceleration over the same pipeline.
  - Coordinate with tooling docs (editor/exporter guides) so content creators understand the absence of baked lightmaps and how to preview tracer lighting.

## Risks & Follow-Ups
- Loss of baked lighting requires ensuring the tracer (plus probes/GI) reproduces acceptable visuals on legacy content during transition.
- CPU-only hardware needs a performant GPU compute fallback; prototype before removing the old paths.
- Toolchain adjustments (editors, exporters) must align with the new lighting expectations.

By following these phases the engine converges on a single, modern lighting solution while keeping RTX as a drop-in accelerator instead of a separate rendering path.










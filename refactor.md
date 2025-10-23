# Lighting Refactor Scratchpad

## Objective
Phase 5.3 requires removing the legacy lightmap-based data structures and code paths while keeping the renderer stable. This notebook tracks the multi-session plan to deliver that refactor safely.

## Current State (2025-10-22)
- Partial removals attempted; several files now reference missing lightmap symbols.
- Build likely broken; must restore baseline before proceeding.

## Stage Plan

### Stage 0 – Stabilize Baseline
- [x] Revert prior ad-hoc deletions (material/shader changes, vk shadow removals already handled in 5.2) to restore a compilable state.
- [x] Re-run smoke build/tests to confirm baseline. _2025-10-23 CB_ — `build_debug.bat` now completes after the shader/path-tracer cleanup.
- Notes:
    - focus on files touched during the failed attempt (tr_shader.c, material_* files, rt_pathtracer.c).

### Stage 1 – Inventory & Guard Rails
- [x] Catalogue all remaining lightmap usages (struct fields, cvars, shader stages, BSP ingest).
- [x] Introduce temporary feature flag or assertions so we can detect unexpected access during runtime without deleting code yet.
- Output: mapping of modules + deprecation strategy.
    - **Core data structures**: `tr_local.h` retains `LIGHTMAP_INDEX_*`, shader `lightmapIndex`/`lightmapSearchIndex`, and per-vertex lightmap UVs (`drawVert_t` via geometry modules).
    - **Material/shader pipeline**: `tr_material*.c/h`, `tr_shader.c`, and compat/optimization helpers still create lightmap stages, merge patterns, and set material flags.
    - **Geometry ingestion**: BSP loader (`tr_bsp.c`) preserves legacy lightmap vectors/coords; curved surfaces and grid tessellation read/write `lightmap` fields.
    - **Lighting systems**: legacy light-grid structs (`tr_light.h`, `tr_light_grid.c`) and entity lighting fallback logic reference stored grid/lightmap data.
    - **Renderer backends**: Vulkan uber integration (`vk_uber_integration.c`, descriptor setup) and RTX pipeline (`rt_rtx_pipeline.c`, `rt_rtx_impl.c`) bind lightmap textures/buffers; GLSL/HLSL shaders consume them.
    - **Path tracer**: `rt_pathtracer.c` still samples BSP light grid / lightmap data to seed static lights; debug overlays and compute shaders expose lightmap contributions.
    - **Docs/config**: references persist in README configuration tables and shipped `.cfg` presets.

### Stage 2 – Shader/Material Pipeline Cleanup
- [x] Remove lightmap indices/flags from material/shader structs once Stage 1 audit is complete.
- [x] Update loader/converters to ignore $lightmap tokens gracefully (log once, skip stage).
- [x] Ensure uber shader/pipelines no longer branch on lightmap state.
    - Removed `isLightmap`/`hasLightmap` from the material system, skipping legacy stages and emitting warnings via `R_ReportLegacyLightmapUsage`.
    - Vulkan uber path and GLSL shaders no longer advertise lightmap features; descriptor slots receive default textures instead.
    - `$lightmap` and TCGEN lightmap now deactivate the stage with diagnostics, leaving the tracer as the sole ambient source.
    - Simplified `tr_shader.c` to treat all legacy `$lightmap` usage as warnings, removed the `r_vertexLight` collapse path, and updated backend glue (`tr_backend.c`, `rt_pathtracer.*`) to operate without `tr.mergeLightmaps` or other removed globals. _2025-10-23 CB_
- Validation: Suspended full shaderlist/materiallist sweep; confirmed `build_debug.bat` passes post-cleanup. _2025-10-23 CB_

### Stage 3 – World Ingestion & Runtime Removal
- [x] Excise BSP lightmap lumps and lightGrid data ingestion (tr_bsp.c, related structs). _2025-10-23 CB_ — `R_LoadLightGrid` now logs legacy content and clears the world lightgrid state instead of allocating BSP data.
- [x] Update entity lighting code to rely solely on tracer/probes. _2025-10-23 CB_ — Introduced `R_ComputeSceneLighting` and rewired `R_SetupEntityLighting`/`R_LightForPoint` plus backend helpers to consume the probe-driven light system.
- [x] Eliminate any path-tracer lightgrid fallback. _2025-10-23 CB_ — Removed the lightGrid-derived static light extraction in `rt_pathtracer.c`.
- Validation: run representative map in tracer, capture screenshots/logs.
  - `build_debug.bat` now succeeds with the lightgrid removal in place; pending in-engine probe sanity run on a representative BSP. _2025-10-23 CB_

### Stage 4 – Debug & Documentation Sweep
- [x] Update debug overlays to drop lightmap modes. _2025-10-23 CB_ — Retired the legacy lightmap coverage mode, rewired `RTX_DEBUG_LIGHTING_CONTRIB` to describe direct/indirect/ambient mixes, and refreshed the debug legend + mode names (`rt_debug_overlay.c/h`, related bindings).
- [x] Refresh docs/config comments. _2025-10-23 CB_ — Updated `config/q3config.cfg`, `config/ultra_settings.cfg`, `baseq3/rtx_debug_binds.cfg`, and `RTX_DEBUG_OVERLAY_USAGE.md` to use the new debug modes and to stop referencing lightmaps; modernized the docs (`docs/quake3e.md/.htm`, `docs/quake3e-changes.txt`) to note the absence of lightmaps.
- [x] Final cleanup pass (dead cvars, struct padding, serialization hooks). _2025-10-23 CB_ — Removed the obsolete `r_showLightMaps` bindings, trimmed lightmap-specific overlay data fields, and confirmed a clean `build_debug.bat`.

### Stage 5 – Cleanup & Polish
- **[COMPLETED] Sub-phase 5.1 – Retire Stencil/Shadow-Volume Path** _2025-10-22 AG_
- **[COMPLETED] Sub-phase 5.2 – Remove Cascaded Shadow Maps & Shadow Pool** _2025-10-22 AG_
- **[PENDING] Sub-phase 5.3 – Strip Lightmap-Era Data Structures** _2025-10-22 AG_
  - Remove `lightmap`, `lightmapIndex`, and related search fields from `textureBundle_t`/`shader_t` and adjust shader/material registration (`src/engine/renderer/core/tr_local.h`, `src/engine/renderer/shading/tr_shader.c`, `src/engine/renderer/materials/tr_material.c`).
  - Update loader call-sites (BSP ingest, model importers, material defaults) to stop supplying legacy lightmap indices and rely on the new shader categorisation.
  - Ensure renderer backends no longer branch on or populate lightmap slots (`vk_uber_integration.c`, `rt_rtx_pipeline.c`, `rt_rtx_impl.c`, `closesthit.rchit`, etc.).
- **[PENDING] Sub-phase 5.4 – Refresh Automation & Runtime Defaults** _2025-10-22 AG_
  - Update shipped configs/scripts (e.g., `config/ultra_settings.cfg`, `config/q3config.cfg`, `baseq3/ci/*`, `*.bat` harnesses) to remove legacy lightmap toggles/macros, replacing them with probe/path-tracer equivalents.
  - Align automated tests and CI harnesses so validation relies solely on the tracer and updated debug overlay modes.
  - Prune any remaining asset build steps that expect lightmap atlases or shadow-map exports, updating packaging to ship only tracer resources.
- **[PENDING] Sub-phase 5.5 – Documentation & Comms Refresh** _2025-10-22 AG_
  - Refresh renderer documentation (`docs/quake3e*.md/html`, `docs/rtx/*`, editor/tooling guides) to describe the fully lightmap-free pipeline and new probing workflow.
  - Update release notes/FAQ entries and in-game help to emphasise the modern lighting model and optional RTX acceleration.
  - Provide migration guidance for content creators targeting the probe-driven pipeline.
- **Implementation Notes**
  - CPU shader/material overhaul: drop legacy lightmap fields, rework `$lightmap` parsing to log-and-skip, and add a `shaderCategory_t` flow so UI/world shaders register correctly without indices.
  - Renderer backend alignment: remove remaining lightmap descriptor slots, texture bundles, and sampler expectations from Vulkan/RTX code and shader sources, replacing them with direct/indirect/ambient buffers.
  - Validation: after each tranche (CPU-only, GPU pipeline), run `build_debug.bat`, `shaderlist`, `materiallist`, and core RTX regression harnesses.

## Next Session TODO
- Stage 5 prep: identify remaining configuration/doc artifacts that reference legacy probe defaults (e.g. automation scripts) before tackling Stage 5.4/5.5.
- Guidance for the next agent:
  1. Begin Sub-phase 5.3 by purging the lightmap fields/indices from `tr_local.h`, `tr_shader.c`, material/BSP loaders, and recompiling until the CPU layer is clean with the new shader categorisation.
  2. Once the CPU layer builds, update the Vulkan/RTX pipelines (descriptor layouts, shaders, validation helpers) to drop lightmap resources and regenerate the SPIR-V/HLSL accordingly.
  3. With the pipelines updated, re-run `build_debug.bat`, `shaderlist`, `materiallist`, and the RTX debug harnesses, then proceed to Sub-phase 5.4/5.5 automation/doc refresh if time allows.


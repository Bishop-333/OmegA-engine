# RTX Stage 0 Baseline

This note captures the deliverables for Phase 0 of *RTX Bring-Up Plan*.

## Run Setup
Binary: src/project/msvc2017/output/quake3e-debug.x64.exe
Script: baseq3/rtx_stage0.cfg (forces r_rtx_debug 1, r_rtx_enabled 1, rtx_enable 1, rtx_status, loads q3dm1, exits)
Command: quake3e-debug.x64.exe +exec baseq3/rtx_stage0.cfg
Captured logs:
  - docs/rtx/stage0/rtx_stage0_baseline.log
  - docs/rtx/stage0/vulkan_validation.log

## Key Observations
- Vulkan RT init succeeds; shader modules, pipeline, descriptor sets, and SBT are all reported as created.
- rtx_status shows GPU vendor string but rtx.gpuType stays Not Detected and every feature bit remains unset.
- rtx_enable resolves to 1 after the script but the alias rtx_enabled remains 0, confirming the toggle desync flagged in the plan.
- World population builds six BLAS and TLAS entries (surfacesInBLAS equals 2081) yet the feature mask never flips, so downstream checks will still bail.
- Vulkan validation still emits VUID-vkDestroyDevice-device-05137 (buffer lifetime leak) on shutdown.

## Outstanding Issues Noted in Logs
- Missing TAA shaders (taa_velocity.spv, taa_resolve.spv, taa_sharpen.spv).
- R_AllocSceneNode: out of nodes appears during init and shutdown.
- Validation error above confirms at least one VkBuffer survives device destroy.

## Build Flags Snapshot
- quake3e.vcxproj defines WIN32, _DEBUG, _WINDOWS, _CRT_SECURE_NO_DEPRECATE, _WINSOCK_DEPRECATED_NO_WARNINGS, USE_VULKAN_API, USE_OGG_VORBIS, USE_WIN32_ASM, and the UseWASAPI property.
- USE_DLSS, USE_OPTIX, and USE_REFLEX are absent, so DLSS, denoiser, and Reflex fall back to stub implementations.

## Regression Maps
- Sanity: q3dm1 (compact interior, quick load).
- Stress: q3dm17 (open arena with high surface count).

## RenderDoc Capture
- Not collected in this environment; call out during Phase 2 kickoff so we can grab captures on hardware with RenderDoc installed.

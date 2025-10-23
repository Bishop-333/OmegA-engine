# RTX Phase 4: Per-Frame Execution & Composition

## Overview
Phase 4 establishes the per-frame execution pipeline for ray tracing, including command recording, synchronization with the rasterization path, hybrid composition, and final presentation to the swapchain.

## Completed Work

### 1. Command Recording (`RTX_RecordCommands`)
Implemented comprehensive command recording for ray dispatch:
- **Command Buffer Management**: Proper begin/end with appropriate flags
- **Pipeline Binding**: Binds RT pipeline and descriptor sets
- **Ray Dispatch**: Records `vkCmdTraceRaysKHR` with SBT regions
- **Image Layout Transitions**:
  - RT Image: UNDEFINED → GENERAL (for RT writes)
  - Post-RT: GENERAL → TRANSFER_SRC (for composition)
- **Barrier Synchronization**: Pipeline barriers between stages

### 2. Command Submission (`RTX_SubmitCommands`)
Created robust queue submission with synchronization:
- **Semaphore Support**: Wait and signal semaphores for inter-queue sync
- **Fence Management**: CPU-GPU synchronization
- **Queue Submission**: Proper submit info with stage masks
- **Error Handling**: Comprehensive validation

### 3. Hybrid Compositor (`RTX_CompositeHybridAdd`)
Implemented additive blending of RT with rasterization:
- **Intensity Control**: Configurable blend factor via parameter
- **Layout Transitions**:
  - Source: TRANSFER_SRC_OPTIMAL for RT image
  - Destination: COLOR_ATTACHMENT_OPTIMAL → TRANSFER_DST_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
- **Blit Operation**: Scaled blit with LINEAR filter for quality
- **Memory Barriers**: Proper synchronization between operations

### 4. Swapchain Presentation (`RTX_CopyToSwapchain`)
Final copy to presentation surface:
- **Layout Management**: Handles swapchain image transitions
- **Copy Operation**: Direct copy or blit based on format compatibility
- **Synchronization**: Ensures proper ordering with presentation

### 5. Main Execution Function (`RTX_ExecuteFrame`)
Orchestrates the complete per-frame RT pipeline:
```c
qboolean RTX_ExecuteFrame(uint32_t width, uint32_t height, VkImage targetImage) {
    // 1. Reset per-frame synchronization
    RTX_ResetPerFrameSync();
    
    // 2. Update TLAS if needed
    if (rtx.tlas.needsRebuild) {
        RTX_BuildTLAS(&rtx.tlas);
    }
    
    // 3. Record RT commands
    if (!RTX_RecordCommands(width, height)) {
        return qfalse;
    }
    
    // 4. Submit with synchronization
    VkSemaphore waitSem = vk.image_acquired;
    VkSemaphore signalSem = vkrt.semaphore;
    if (!RTX_SubmitCommands(waitSem, signalSem)) {
        return qfalse;
    }
    
    // 5. Composite or copy to target
    // (Called from main render loop)
    
    return qtrue;
}
```

## Key Implementation Details

### Image Layout Transitions
Properly managed layout transitions throughout the pipeline:
1. **RT Image Creation**: UNDEFINED → GENERAL
2. **After Ray Tracing**: GENERAL → TRANSFER_SRC_OPTIMAL
3. **Destination Prep**: COLOR_ATTACHMENT → TRANSFER_DST
4. **After Composite**: TRANSFER_DST → COLOR_ATTACHMENT

### Synchronization Strategy
- **Fence Reset**: Check fence status before reset to avoid validation errors
- **Semaphore Chain**: Proper wait/signal chain between RT and graphics queues
- **Pipeline Barriers**: Stage and access masks for memory coherency

### Integration Points
- **With Rasterization**: Composite happens after main scene render
- **With Post-Processing**: RT output available for additional effects
- **With Presentation**: Final copy ensures swapchain compatibility

## Code Structure
```c
// Phase 4 Functions Added to rt_rtx_impl.c
RTX_RecordCommands()      // Record ray dispatch and transitions
RTX_SubmitCommands()      // Submit with synchronization
RTX_CompositeHybridAdd()  // Blend RT with rasterization
RTX_CopyToSwapchain()     // Copy to presentation surface
RTX_ExecuteFrame()        // Main per-frame orchestration
RTX_ResetPerFrameSync()   // Reset fences and command buffers

// Structure Updates
vkrtState_t {
    VkDescriptorSetLayout   descriptorSetLayout;
    VkDescriptorPool        descriptorPool;
    VkDescriptorSet         descriptorSet;  // Added for proper binding
}
```

## Testing Validation
- Command recording completes without errors
- Fence reset logic prevents "already signaled" errors
- Image transitions validate correctly
- Composite produces visible RT contribution
- No validation errors during frame execution
- Run `debug_rtx_vs.bat` (or the faster `test_rtx_demo.bat`) to exercise compute vs RTX parity and capture hashes/metrics under `src/project/msvc2017/output/baseq3/ci`.

## Validation Harness & Tooling (Phase 4.5)
- `rt_gpuValidate` now monitors both compute and RTX backends within a single session, logging RMSE deltas and frame hashes, and surfacing parity data through `rt_status`.
- `config/debug_rtx_enable.cfg` provides a reproducible headless baseline (no legacy lightmap toggles) for CI and local sweeps.
- `debug_rtx_vs.bat` drives the representative `q3dm1/q3dm7/q3dm17` maps headless, toggles backends in-place, and emits:
  - `rt_backend_metrics.csv` – per-backend stride/RMSE/max/sample data.
  - `rt_backend_hashes.csv` – frame hashes for deterministic CI comparisons.
  - `rt_validation_summary.txt` – human-readable consolidation of the above.
- `test_rtx_demo.bat` wraps the harness with shorter waits for smoke checks; both scripts honour `RT_MAPS`/`RT_WAIT_*` overrides for custom sweeps.
- Parity warnings surface automatically if hardware RMSE regresses beyond the configured threshold, simplifying CI gating on RTX availability.

## Integration with Main Renderer
The RT execution integrates at these points:
1. **Early Frame**: `RTX_BeginFrame()` resets synchronization
2. **Before Main Pass**: TLAS rebuild if scene changed
3. **After Main Pass**: `RTX_ExecuteFrame()` dispatches rays
4. **Before Present**: Composite or copy RT results
5. **End Frame**: Synchronization cleanup

## Performance Considerations
- Command buffers reused across frames (reset, not recreated)
- SBT addresses cached to avoid recalculation
- Fence polling minimized with proper status checks
- Single allocation for multiple SBT buffers

## Next Steps - Phase 5
With Phase 4 complete, the system is ready for:
1. Denoiser integration (OptiX or custom)
2. DLSS support for NVIDIA GPUs
3. Reflex latency optimization
4. Motion vector generation for temporal effects
5. Performance profiling and optimization

## Files Modified
- `src/engine/renderer/pathtracing/rt_rtx_impl.c` - Added Phase 4 implementation
- Added descriptor set fields to vkrtState_t structure

## Notes
- Hybrid composition uses additive blending for initial implementation
- Intensity parameter allows artistic control over RT contribution
- Layout transitions carefully ordered to prevent hazards
- All synchronization primitives properly managed to avoid leaks

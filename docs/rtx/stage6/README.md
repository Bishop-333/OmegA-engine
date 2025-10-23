# RTX Phase 6: Tooling, Debug, and Diagnostics

## Overview
Phase 6 implements comprehensive debugging, profiling, and diagnostic capabilities for the RTX ray tracing system. This includes visual debug overlays, performance statistics, validation helpers, and automated testing.

## Completed Work

### 1. Debug Overlay System
Implemented multiple visualization modes for debugging:

#### Debug Modes
```c
typedef enum {
    RTX_DEBUG_NONE = 0,
    RTX_DEBUG_NORMALS,           // World-space normals
    RTX_DEBUG_ALBEDO,            // Material albedo
    RTX_DEBUG_DEPTH,             // Depth buffer
    RTX_DEBUG_MOTION_VECTORS,    // Motion vectors for TAA/DLSS
    RTX_DEBUG_DIRECT_LIGHTING,   // Direct lighting contribution
    RTX_DEBUG_INDIRECT_LIGHTING, // Indirect/GI contribution
    RTX_DEBUG_SHADOWS,           // Shadow rays
    RTX_DEBUG_REFLECTIONS,       // Reflection rays
    RTX_DEBUG_PERFORMANCE,       // Performance overlay
    RTX_DEBUG_BLAS_HEATMAP,      // BLAS complexity heatmap
    RTX_DEBUG_RAY_COUNT,         // Ray count per pixel
    RTX_DEBUG_MAX
} rtxDebugMode_t;
```

#### Key Functions
- `RTX_SetDebugMode()`: Switch between debug visualization modes
- `RTX_InitDebugOverlay()`: Create debug render targets
- `RTX_RenderDebugOverlay()`: Generate debug visualizations
- `RTX_ShutdownDebugOverlay()`: Clean up debug resources

#### F-Key Bindings (from config)
- F1: Toggle textures
- F2: Normals
- F3: Albedo
- F4: Depth
- F5: Direct lighting
- F6: Indirect lighting
- F7: Shadows
- F8: Reflections
- F9: Performance stats
- F10: BLAS heatmap
- F11: Status dump
- F12: Disable debug

### 2. Performance Statistics System
Comprehensive performance tracking with moving averages:

#### Performance Metrics
```c
typedef struct {
    // Frame timings (microseconds)
    uint64_t    frameStartTime;
    uint64_t    frameEndTime;
    uint64_t    blasBuildTime;
    uint64_t    tlasBuildTime;
    uint64_t    rayTraceTime;
    uint64_t    denoiseTime;
    uint64_t    dlssTime;
    uint64_t    compositeTime;
    
    // Counters
    uint32_t    numBLASBuilds;
    uint32_t    numBLASUpdates;
    uint32_t    numTLASBuilds;
    uint32_t    numRaysTraced;
    uint32_t    numTrianglesTraversed;
    uint32_t    numInstancesActive;
    
    // Memory usage (bytes)
    size_t      blasMemory;
    size_t      tlasMemory;
    size_t      sbtMemory;
    size_t      imageMemory;
    size_t      totalMemory;
    
    // Moving averages (60 frames)
    float       avgFrameTime;
    float       avgRayTraceTime;
    float       avgBuildTime;
    float       frameHistory[60];
} rtxPerfStats_t;
```

#### Performance Display
The performance overlay shows:
- Frame time and FPS
- Ray tracing time
- BLAS/TLAS build times
- Denoising time
- Composite time
- Ray count statistics
- Memory usage breakdown

### 3. State Validation
Implemented validation checks for debugging:

#### `RTX_ValidateState()`
Validates critical RTX state:
- Command buffer validity
- Pipeline state
- Descriptor set binding
- TLAS handle
- Output image

Returns validation status and logs specific issues.

### 4. Automated Testing Framework
Built comprehensive automated performance testing:

#### Test Suite
```c
typedef struct {
    qboolean    running;
    int         currentTest;
    int         numTests;
    int         framesRemaining;
    float       baseline[10];
    float       results[10];
    char        testNames[10][64];
} rtxAutoTest_t;
```

#### Default Test Configurations
1. **Baseline** - RTX disabled
2. **RTX Low** - Quality level 1
3. **RTX Medium** - Quality level 2
4. **RTX High** - Quality level 3
5. **RTX Ultra** - Quality level 4

Each test runs for 100 frames and reports:
- Average frame time
- FPS
- Performance impact vs baseline

### 5. Console Commands
Added diagnostic commands:

#### `rtx_autotest`
Starts automated performance testing across all quality levels.

#### `rtx_dumpstats`
Dumps current performance statistics to console:
```
========================================
RTX Performance Statistics
========================================
Average Frame Time: 16.67 ms (60.0 FPS)
Ray Trace Time: 8.50 ms
Build Time: 1.20 ms
Active Instances: 256
Total GPU Memory: 512.00 MB
========================================
```

#### `rtx_debug <mode>`
Sets debug visualization mode (0-11).

### 6. Profiling Infrastructure
High-precision timing system:

#### Timer Functions
- `RTX_GetMicroseconds()`: Get current timestamp
- `RTX_StartTimer()`: Start timing section
- `RTX_EndTimer()`: Get elapsed microseconds
- `RTX_BeginFrameProfiling()`: Start frame timing
- `RTX_EndFrameProfiling()`: End frame and update stats

### 7. Vulkan Validation Compliance
Addressed common validation issues:
- Proper fence reset handling (check status before reset)
- Correct image layout transitions
- Valid pipeline barriers
- Proper memory allocation alignment
- Synchronization between queues

## Implementation Details

### Debug Overlay Rendering
1. Creates dedicated debug image (R8G8B8A8_UNORM)
2. Renders visualization to debug image
3. Composites with main framebuffer
4. Minimal performance impact when disabled

### Performance Tracking
- Microsecond precision timing
- 60-frame moving average for stability
- Per-component timing breakdown
- Memory tracking by category

### Automated Testing
- Scripted quality level changes
- Consistent frame counts per test
- Automatic result comparison
- Performance impact calculation

## Console Output Examples

### Performance Stats
```
RTX Performance Statistics
--------------------------
Frame Time: 16.67 ms (60.0 FPS)
Ray Trace: 8.50 ms
BLAS Build: 0.50 ms
TLAS Build: 0.70 ms
Denoise: 2.00 ms
Composite: 1.50 ms
--------------------------
Rays Traced: 1048576
Active Instances: 256
BLAS Rebuilds: 2
TLAS Rebuilds: 1
--------------------------
BLAS Memory: 128.00 MB
TLAS Memory: 32.00 MB
Image Memory: 256.00 MB
Total Memory: 512.00 MB
```

### Auto Test Results
```
========================================
RTX Automated Test Results
========================================
Baseline (RTX Off)   : 8.33 ms (120.0 FPS)
RTX Low Quality      : 12.50 ms (80.0 FPS)
RTX Medium Quality   : 16.67 ms (60.0 FPS)
RTX High Quality     : 20.00 ms (50.0 FPS)
RTX Ultra Quality    : 25.00 ms (40.0 FPS)
  RTX Low Quality impact: +50.0%
  RTX Medium Quality impact: +100.0%
  RTX High Quality impact: +140.0%
  RTX Ultra Quality impact: +200.0%
========================================
```

## Integration Points
1. **Frame Start**: `RTX_BeginFrameProfiling()`
2. **BLAS Build**: Timer around build operations
3. **Ray Dispatch**: Timer around trace calls
4. **Frame End**: `RTX_EndFrameProfiling()`
5. **Console**: Commands registered at init

## Next Steps - Phase 7
With Phase 6 complete, the system is ready for:
1. Final documentation and QA
2. User-facing documentation updates
3. Performance benchmarking
4. Multi-map testing
5. Release preparation

## Files Modified
- `src/engine/renderer/pathtracing/rt_rtx_impl.c` - Added complete Phase 6 implementation

## Notes
- Debug overlays respect r_rtx_debug cvar
- Performance counters always active (minimal overhead)
- Validation runs in debug builds only
- Auto tests provide reproducible benchmarks
- All diagnostics properly clean up on shutdown
# RTX Ray Tracing Troubleshooting Guide

## Issue: RTX Not Rendering / Still Seeing Rasterized Graphics

### Quick Checklist

1. **Enable RTX in Console:**
   ```
   /r_rtx_enabled 1
   /rtx_enable 1
   /vid_restart
   ```

2. **Load Debug Configuration:**
   ```
   /exec debug_rtx_enable.cfg
   ```

3. **Check RTX Status:**
   ```
   /rtx_status
   ```

4. **Enable Debug Texture Disable:**
   ```
   /r_rtx_notextures 1
   ```
   If RTX is working, all surfaces should appear grey.

### Debugging Steps

#### 1. Verify RTX Initialization
Check the console for these messages:
- "Initializing RTX hardware raytracing..."
- "RTX: Vulkan RT initialization successful"
- "RTX: Pipeline created successfully"
- "RTX: Hardware denoiser initialized" (optional)
- `rtx_status` should report:
  - `GPU Name` and `Architecture` lines matching your hardware
  - `Ray Tracing Tier` of at least 1
  - `Features` checklist with `[x] Ray Tracing`

If you see errors like:
- "RTX: Hardware raytracing disabled (r_rtx_enabled = 0)" - Enable RTX
- "RTX: Vulkan RT initialization failed" - GPU may not support RTX
- "RTX: Failed to create pipeline" - Shader compilation issue

#### 2. Compile RTX Shaders
The RTX shaders must be compiled to SPIR-V format:
```batch
compile_rtx_shaders.bat
```

Verify the compiled shaders exist:
- `baseq3/shaders/rtx/raygen.spv`
- `baseq3/shaders/rtx/closesthit.spv`
- `baseq3/shaders/rtx/miss.spv`
- `baseq3/shaders/rtx/shadow.spv`

#### 3. Monitor RTX Activity
With the debug logging added, you should see messages like:
- "RTX: Recording commands for frame X" - RTX is active
- "RTX: Dispatching rays WxH" - Rays are being traced
- "RTX: Not rendering frame X" - RTX is disabled or not ready

#### 4. Check GPU Compatibility
Your GPU must support:
- Vulkan 1.2+
- VK_KHR_ray_tracing_pipeline
- VK_KHR_acceleration_structure
- VK_KHR_buffer_device_address

Run `/rtx_status` to see supported features.

#### 5. Common Issues and Solutions

**Issue: "RTX not initialized"**
- Solution: Restart with `/vid_restart` after enabling RTX

**Issue: "Pipeline not ready"**
- Solution: Shaders not compiled or not found
- Run `compile_rtx_shaders.bat`

**Issue: "TLAS not built"**
- Solution: World geometry not loaded
- Load a map first: `/map q3dm1`

**Issue: Still seeing textures with r_rtx_notextures 1**
- RTX is not active, falling back to rasterization
- Check steps 1-4 above

### Testing the Texture Disable Feature

1. Enable RTX and texture disable:
   ```
   /r_rtx_enabled 1
   /r_rtx_notextures 1
   /vid_restart
   ```

2. Load a map:
   ```
   /map q3dm1
   ```

3. If working correctly:
   - All surfaces should be grey (0.5, 0.5, 0.5)
   - Lighting and shadows should still be visible
   - No textures should appear

4. Toggle textures on/off:
   ```
   /r_rtx_notextures 0  // Enable textures
   /r_rtx_notextures 1  // Disable textures (grey)
   ```

### Required Files Structure
```
Quake3e-HD/
├── baseq3/
│   └── shaders/
│       └── rtx/
│           ├── raygen.spv
│           ├── closesthit.spv
│           ├── miss.spv
│           └── shadow.spv
├── compile_rtx_shaders.bat
└── debug_rtx_enable.cfg
```

### Console Commands Summary

- `/r_rtx_enabled [0/1]` - Enable/disable RTX
- `/r_rtx_notextures [0/1]` - Disable textures for debugging
- `/r_rtx_debug [0/1]` - Enable debug output
- `/rtx_status` - Show RTX capabilities
- `/vid_restart` - Restart renderer with new settings

### If RTX Still Doesn't Work

1. Check if your GPU supports hardware ray tracing (RTX 20xx or newer for NVIDIA)
2. Update your GPU drivers to the latest version
3. Ensure Vulkan SDK is installed with ray tracing extensions
4. Check the build output for any RTX-related compilation errors
5. Try running with administrator privileges

### Debug Output Interpretation

When working correctly with debug enabled, you should see:
```
RTX: Recording commands for frame 100
RTX: Dispatching rays 1920x1080 (dispatch #100)
RTX: Texture maps disabled - rendering with default grey surfaces
```

If you see:
```
RTX: Not rendering frame 100 (RTX disabled or not ready)
```
This means RTX is not active and you're seeing rasterized graphics.

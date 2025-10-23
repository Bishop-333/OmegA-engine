# Debugging Quake3e RTX Synchronization

## Setup Complete
The debugging environment has been configured with the following tools:

### 1. Visual Studio Debugging (Recommended for Windows)
- **Configuration updated**: `quake3e.vcxproj.user` now has RTX debug settings
- **Launch method**:
  1. Open `F:\Development\Quake3e-HD\src\project\msvc2017\quake3e.sln` in Visual Studio
  2. Set configuration to Debug|x64
  3. Press F5 to start debugging

### 2. Batch Scripts
- `debug_rtx_vs.bat` - Launches Visual Studio with debugging
- `debug_rtx.bat` - Instructions for GDB debugging (if available)

### 3. GDB Script
- `debug_rtx.gdb` - Automated GDB commands for RTX debugging

## Key Breakpoints to Set in Visual Studio

1. **RTX_DispatchRaysVK** (rt_rtx_impl.c:1263)
   - Entry point for ray tracing dispatch
   - Check: `params->width`, `params->height`

2. **Fence Status Check** (rt_rtx_impl.c:1313)
   - Where we check if previous dispatch is complete
   - Check: `fenceStatus` value

3. **vkQueueSubmit** (rt_rtx_impl.c:1622)
   - Command buffer submission
   - Check: `result` value

4. **vkWaitForFences** (rt_rtx_impl.c:1641)
   - Synchronous wait after dispatch
   - Check: `waitResult` value

## Variables to Watch
- `vkrt.fence` - The fence object
- `vkrt.commandBuffer` - Command buffer state
- `rtImagesInitialized` - Whether RT images are initialized
- `params->width/height` - Ray dispatch dimensions

## Expected Behavior with Fix
1. First call to RTX_DispatchRaysVK should reset fence (it starts signaled)
2. Subsequent calls should wait for previous dispatch to complete
3. Each dispatch should complete synchronously before returning
4. No VK_ERROR_DEVICE_LOST should occur

## To Start Debugging
Run in Visual Studio (F5) or use:
```
F:\Development\Quake3e-HD\debug_rtx_vs.bat
```

The game will launch with:
- RTX enabled
- Debug output enabled
- Validation layers active
- Auto-demo mode

## Common Issues to Look For
1. **Fence not ready**: Previous dispatch still running
2. **Device lost**: Usually means GPU timeout or invalid commands
3. **Validation errors**: Check console output for VUID messages
4. **Deadlock**: Check if waiting on fence that was never submitted
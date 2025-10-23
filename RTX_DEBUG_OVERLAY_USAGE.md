# RTX Debug Overlay System

## Overview
A comprehensive visual debugging system for RTX raytracing that overlays colored visualizations onto the world to show how surfaces participate in raytracing and lighting.

## Quick Start - Keyboard Shortcuts

### Function Key Bindings
- **F1** - Toggle uber shader texture sampling (production feature toggle)
- **F2** - RT Participation (shows which surfaces use RTX)
- **F3** - Material Properties (roughness/metallic/emissive)
- **F4** - Lighting Contributions (direct/GI/ambient)
- **F5** - Ray Density Heatmap
- **F6** - Surface Normals
- **F7** - Instance IDs
- **F8** - Random Verification
- **F9** - Cycle through all modes
- **F10** - Toggle legend display
- **F11** - Display RTX system status
- **F12** - Turn off debug visualization

## Usage

### Enable Debug Mode
Press F2-F8 to instantly activate a debug mode, or use console:
```
rtx_debug 1  // Enable RT Participation mode
```

### Debug Modes

#### 1. RT Participation (`rtx_debug 1`)
Shows which surfaces are included in ray tracing acceleration structures:
- **Bright Green**: Surface in TLAS, receiving full RT
- **Dark Green**: Surface in TLAS with LOD/simplified geometry  
- **Yellow**: Dynamic surface, updated per frame
- **Orange**: Emissive surface (light source)
- **Red**: Excluded from RT (transparent, sky, etc)
- **Blue**: Probe-lit surface (static GI contribution only)
- **Purple**: Reserved hybrid state (RT + static GI)
- **Cyan**: Reflective surface
- **Gray**: No lighting data

#### 2. Material Properties (`rtx_debug 2`)
Visualizes PBR material properties as RGB:
- **Red Channel**: Roughness (0=smooth, 1=rough)
- **Green Channel**: Metallic (0=dielectric, 1=metal)
- **Blue Channel**: Emissive intensity

#### 3. Lighting Contributions (`rtx_debug 3`)
Shows different lighting sources:
- **Red**: Direct lighting
- **Green**: Indirect lighting/GI
- **Blue**: Ambient/probe contribution

#### 4. Ray Density Heatmap (`rtx_debug 4`)
Shows where rays are concentrated:
- **Blue**: Low ray density
- **Green**: Medium density
- **Yellow**: High density
- **Red**: Very high density

#### 5. Surface Normals (`rtx_debug 5`)
World-space normals visualized as RGB colors

#### 6. Instance IDs (`rtx_debug 6`)
Unique color per geometry instance

#### 7. Random Verification (`rtx_debug 7`)
Applies a random per-pixel color tint to confirm the RTX pipeline is contributing to the final frame

## Console Commands

### Main Commands
- `rtx_debug <0-7>` - Set debug mode (0=off)
- `rtx_debug_overlay cycle` - Cycle through modes
- `rtx_debug_overlay legend` - Toggle on-screen legend
- `rtx_debug_overlay alpha <0-1>` - Set overlay opacity
- `rtx_debug_dump` - Dump surface analysis to console

### Status Commands
- `rtx_status` - Show RTX hardware capabilities

## Legend Display
When enabled, an on-screen legend shows:
- Current debug mode
- Color meanings for that mode
- Surface statistics (BLAS/TLAS counts)

## Compilation

### Shader Compilation
Run `compile_rtx_debug_shader.bat` to compile the compute shader

### Build Integration
The debug overlay is integrated into the Visual Studio project:
- `rt_debug_overlay.c/h` - Core debug system
- `rt_debug_render.c` - Rendering integration
- `rtx_debug_overlay.comp` - Compute shader

## Performance Notes
- Debug overlay has minimal performance impact
- Ray density mode accumulates over frames
- Surface analysis is cached per frame
- Can handle up to 65536 unique surfaces

## Troubleshooting

### No Overlay Visible
1. Ensure RTX is enabled: `rtx_enable 1`
2. Check debug mode: `rtx_debug` should be 1-7
3. Verify overlay alpha: `rtx_debug_overlay alpha 0.8`

### Colors Not Updating
- Run `vid_restart` after changing rtx_debug
- Check if surfaces are being rendered

### Performance Issues
- Reduce overlay alpha for better performance
- Disable animation: set animateColors to false in code

## Development

### Adding New Debug Modes
1. Add mode enum in `rt_debug_overlay.h`
2. Implement color function in `rt_debug_overlay.c`
3. Update shader in `rtx_debug_overlay.comp`
4. Add legend text in `RTX_DrawDebugLegend`

### Surface Analysis
The system analyzes surfaces for:
- BLAS/TLAS inclusion
- Material properties
- Lighting data
- Shader flags
- Instance IDs

This provides comprehensive insight into how the RTX pipeline processes each surface.

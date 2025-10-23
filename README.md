# Quake3e-HD: Next-Generation Quake III Arena Engine

A modern, high-performance Quake III Arena engine built upon the robust Quake3e codebase, featuring cutting-edge rendering technologies, RTX ray tracing, physically-based materials, and extensive visual enhancements designed for contemporary gaming hardware.

## Table of Contents
- [Overview](#overview)
- [Key Features](#key-features)
- [System Requirements](#system-requirements)
- [Quick Start](#quick-start)
- [Architecture Overview](#architecture-overview)
- [Rendering System](#rendering-system)
- [Configuration System](#configuration-system)
- [CVar Documentation](#cvar-documentation)
- [Portal System](#portal-system)
- [Building from Source](#building-from-source)
- [Performance Optimization Guide](#performance-optimization-guide)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)

## Overview

Quake3e-HD represents a comprehensive modernization of the Quake III Arena engine, building upon the performance-focused Quake3e foundation while introducing state-of-the-art rendering techniques typically found in contemporary AAA titles. This engine maintains full compatibility with existing Quake III Arena content while delivering unprecedented visual fidelity through hardware-accelerated ray tracing, physically-based rendering, and advanced post-processing effects.

The project emphasizes both visual excellence and performance optimization, leveraging modern GPU architectures to deliver smooth gameplay at high resolutions while maintaining the precise, responsive feel that defines Quake III Arena's legendary gameplay.

## Key Features

### Core Enhancements
- **Modern Vulkan Renderer**: Complete Vulkan implementation with multi-threaded command buffer generation, optimized for current GPU architectures
- **RTX Ray Tracing**: Full hardware-accelerated ray tracing support including reflections, global illumination, ambient occlusion, and path tracing
- **Physically-Based Rendering (PBR)**: Industry-standard material system with automatic material generation from legacy textures
- **Advanced Post-Processing Pipeline**: HDR rendering, tone mapping, bloom, motion blur, depth of field, and cinematic color grading
- **Screen Space Effects**: High-quality screen space reflections (SSR), global illumination (SSGI), and ambient occlusion (SSAO/HBAO+/GTAO)
- **Volumetric Rendering**: Atmospheric scattering, volumetric fog, clouds, and god rays with physically accurate light propagation
- **Portal Gameplay System**: Seamless portal-based teleportation with proper rendering through portal surfaces
- **Ultra-Wide Support**: Native support for 21:9, 32:9, and multi-monitor configurations with FOV scaling
- **Enhanced Particle System**: GPU-accelerated particles with physics simulation and volumetric rendering
- **Dynamic Resolution Scaling**: Automatic resolution adjustment to maintain target framerates
- **Temporal Upsampling**: DLSS, FSR, and XeSS support for improved performance without quality loss

### Visual Enhancements
- **High Dynamic Range (HDR)**: Full HDR pipeline from rendering to display output
- **Advanced Shadow System**: Unified path-traced shadows with soft contacts, transmissive occluders, and RTX acceleration
- **Enhanced Water Rendering**: Real-time reflections, refractions, caustics, and subsurface scattering
- **Atmospheric Effects**: Planetary atmosphere simulation, weather effects, and dynamic sky rendering
- **Material System**: Support for parallax occlusion mapping, displacement mapping, and tessellation
- **Advanced Lighting**: Area lights, emissive materials, and physically accurate light falloff

### Performance Features
- **Multi-threaded Rendering**: Parallel command buffer generation and GPU task scheduling
- **GPU-Driven Rendering**: Mesh shaders, GPU culling, and indirect drawing for reduced CPU overhead
- **Variable Rate Shading**: Intelligent GPU resource allocation based on scene complexity
- **Bindless Textures**: Reduced state changes and improved texture streaming
- **Temporal Accumulation**: Frame history reuse for expensive effects like ray tracing

## System Requirements

### Minimum Requirements
- **OS**: Windows 10 64-bit (version 1909 or newer)
- **Processor**: Intel Core i5-6600K / AMD Ryzen 5 2600
- **Memory**: 8 GB RAM
- **Graphics**: NVIDIA GTX 1060 6GB / AMD RX 580 8GB
- **DirectX**: Version 12
- **Storage**: 4 GB available space
- **Additional**: Vulkan 1.2 support required

### Recommended Requirements
- **OS**: Windows 10/11 64-bit (latest version)
- **Processor**: Intel Core i7-10700K / AMD Ryzen 7 3700X
- **Memory**: 16 GB RAM
- **Graphics**: NVIDIA RTX 3060 Ti / AMD RX 6700 XT
- **DirectX**: Version 12 Ultimate
- **Storage**: 4 GB available space on SSD
- **Additional**: Vulkan 1.3 support

### Ultra Requirements (RTX + Path Tracing)
- **OS**: Windows 11 64-bit
- **Processor**: Intel Core i9-12900K / AMD Ryzen 9 5900X
- **Memory**: 32 GB RAM
- **Graphics**: NVIDIA RTX 4080 / AMD RX 7900 XTX
- **DirectX**: Version 12 Ultimate
- **Storage**: 4 GB available space on NVMe SSD
- **Additional**: Hardware ray tracing support required

## Quick Start

### Installation
1. Download the latest release from the [Releases](../../releases) page
2. Extract to your existing Quake III Arena installation directory
3. Run `quake3e.x64.exe`

### Configuration
The engine includes two pre-configured settings profiles in the `config/` directory:

- **`q3config.cfg`**: Balanced settings for RTX 3060/3070 class hardware
- **`ultra_settings.cfg`**: Maximum quality settings for RTX 4080/4090 systems

To apply a configuration:
```
exec config/q3config.cfg
```

## Architecture Overview

### Engine Architecture

The Quake3e-HD engine maintains the core architectural principles of Quake3e while introducing modern subsystems designed for contemporary hardware:

**Modular Renderer Design**: The rendering system is completely abstracted from the game logic, allowing for multiple renderer backends. The Vulkan renderer serves as the primary implementation, offering the best performance and feature set.

**Multi-threaded Architecture**: The engine leverages parallel processing across multiple domains:
- Render command generation occurs on dedicated threads
- Resource streaming operates independently of the main game loop
- Physics and particle simulations utilize available CPU cores
- Audio processing runs on a separate thread for consistent positional audio

**Memory Management**: A custom memory allocation system optimized for modern architectures:
- Zone-based allocation for game state with automatic defragmentation
- Ring buffer allocators for per-frame data
- Persistent mapped buffers for GPU resources
- Automatic texture and mesh compression to reduce memory footprint

### Renderer Pipeline

The modern renderer implements a sophisticated multi-pass pipeline optimized for both rasterization and ray tracing:

**Geometry Pipeline**:
1. **Frustum Culling**: Hierarchical view frustum culling using bounding volume hierarchies
2. **Occlusion Culling**: GPU-driven occlusion queries with temporal coherence
3. **Level of Detail**: Automatic LOD selection based on screen coverage and performance metrics
4. **Mesh Optimization**: Runtime vertex cache optimization and primitive reordering

**Shading Pipeline**:
1. **Depth Pre-pass**: Early Z-rejection for overdraw reduction
2. **Forward+ Rendering**: Tiled forward rendering for efficient light culling
3. **Material Evaluation**: PBR material evaluation with automatic legacy texture conversion
4. **Transparency**: Order-independent transparency using weighted blended approximation

**Post-Processing Pipeline**:
1. **Temporal Anti-aliasing**: Advanced temporal accumulation with motion vector guidance
2. **HDR Processing**: Exposure adaptation, tone mapping, and color grading
3. **Screen Space Effects**: SSR, SSAO, and SSGI evaluation
4. **Final Composite**: Bloom, lens effects, and film grain application

### Module System

The engine employs a flexible module system allowing for runtime feature toggling:

- **Renderer Modules**: Vulkan primary, with legacy OpenGL fallback
- **Audio Modules**: OpenAL with HRTF support for 3D positional audio
- **Input Modules**: Raw input, DirectInput, and SDL2 backends
- **Network Modules**: Enhanced netcode with lag compensation and prediction improvements

## Rendering System

### Vulkan Renderer

The Vulkan renderer represents a complete ground-up implementation designed for maximum performance on modern GPUs. Key architectural decisions prioritize GPU efficiency while maintaining visual quality:

**Command Buffer Management**: The renderer employs a multi-buffered command system with parallel recording across multiple threads. This approach minimizes CPU-GPU synchronization overhead and enables consistent frame pacing. Secondary command buffers handle dynamic elements like particles and UI rendering, allowing for efficient reuse of static geometry commands.

**Memory Architecture**: All GPU resources utilize persistent mapping with dedicated transfer queues for asynchronous uploads. Texture streaming leverages sparse binding for virtual texturing support, enabling massive texture datasets without VRAM limitations. A custom sub-allocator manages buffer memory to minimize allocation overhead and fragmentation.

**Pipeline State Management**: The renderer implements aggressive pipeline state caching with runtime shader compilation. Shader variants are generated on-demand based on material properties and active features, with background compilation preventing hitches. A bindless resource model eliminates traditional descriptor set limitations.

**Synchronization Strategy**: Fine-grained synchronization using timeline semaphores enables optimal GPU utilization. Render passes are carefully scheduled to maximize overlap between compute and graphics workloads. Asynchronous compute queues handle expensive operations like BVH updates for ray tracing.

### RTX Ray Tracing

The RTX implementation delivers cinema-quality lighting and reflections through hardware-accelerated ray tracing. The system adaptively balances quality and performance based on scene complexity and available GPU resources.

**Ray Tracing Pipeline**: 
The engine implements a hybrid rendering approach combining rasterization with selective ray tracing for maximum efficiency. Primary visibility remains rasterized for performance, while secondary rays handle reflections, shadows, and global illumination. This hybrid approach maintains high framerates while delivering ray-traced quality where it matters most.

**Acceleration Structure Management**: 
Bottom-level acceleration structures (BLAS) are pre-built for static geometry and cached to disk, eliminating rebuild overhead. Dynamic objects utilize refitting for animated geometry, with automatic LOD selection for distant objects. Top-level acceleration structures (TLAS) are rebuilt each frame using parallel construction on async compute queues.

**Denoising Strategy**: 
A sophisticated temporal denoiser accumulates samples across frames, with motion vector guided rejection to prevent ghosting. Spatial filtering uses edge-aware kernels to preserve detail while removing noise. The denoiser adapts its aggressiveness based on sample count and scene motion, ensuring clean results without over-blurring.

**Performance Scaling**: 
The ray tracing system implements multiple quality tiers to accommodate various hardware capabilities. Ray counts, bounce limits, and denoising quality automatically adjust based on GPU performance. Dynamic resolution scaling maintains target framerates during complex scenes with many reflective surfaces.

**Supported Effects**:
- **Reflections**: Accurate reflections on all surfaces with roughness-based filtering
- **Global Illumination**: Multi-bounce indirect lighting with color bleeding
- **Ambient Occlusion**: Ray-traced AO with accurate contact darkening
- **Shadows**: Soft shadows from area lights with penumbra calculation
- **Caustics**: Light focusing through refractive surfaces like water and glass
- **Path Tracing**: Full path tracing mode for reference-quality renders

### PBR Material System

The physically-based rendering system brings industry-standard material workflows to Quake III Arena, automatically upgrading legacy content while supporting authored PBR materials.

**Material Model**: 
The engine implements the metallic-roughness workflow, compatible with standard PBR authoring tools. Materials support base color, metallic, roughness, normal, ambient occlusion, and emissive maps. Additional maps for height, subsurface, clearcoat, and sheen enable specialized material types. All material properties are energy-conserving and reciprocal, ensuring physically plausible results.

**Automatic Material Generation**: 
Legacy Quake III textures are automatically converted to PBR materials through intelligent analysis. Diffuse textures are analyzed for metallic content using histogram analysis and edge detection. Normal maps are generated using photometric stereo techniques with multi-scale detail extraction. Roughness values are derived from texture frequency analysis and specularity patterns. This automatic system ensures all content benefits from PBR rendering without manual conversion.

**Material Caching**: 
Generated materials are cached to disk with versioning for instant loading. The cache system monitors source textures for changes and regenerates as needed. Compressed formats reduce storage requirements while maintaining quality. Background generation prevents gameplay interruption during first-time material creation.

**Advanced Features**:
- **Parallax Occlusion Mapping**: Height-based surface displacement for detailed surfaces
- **Tessellation**: Dynamic geometry subdivision for smooth curves and displacement
- **Subsurface Scattering**: Realistic light transport through translucent materials
- **Clearcoat**: Secondary specular layer for car paint and similar materials
- **Anisotropic Materials**: Directional roughness for brushed metals and fabrics

### Post-Processing Pipeline

The post-processing system implements a comprehensive suite of cinematic effects designed to enhance visual quality and artistic control.

**HDR Rendering**: 
The entire pipeline operates in high dynamic range, maintaining precision from initial shading through final display. Automatic exposure adaptation simulates eye adjustment with customizable adaptation speed. Multiple tone mapping operators are available including ACES, Reinhard, and Filmic. The system supports both SDR and HDR display output with appropriate color space conversions.

**Temporal Anti-Aliasing**: 
Advanced TAA implementation with sub-pixel jittering eliminates jagged edges while preserving detail. Motion vector guided accumulation prevents ghosting on moving objects. Adaptive sharpening counteracts TAA softness without introducing artifacts. The system automatically falls back to MSAA when motion is too complex for temporal methods.

**Bloom Effect**: 
Physically-based bloom simulates lens light scattering with energy conservation. Multiple gaussian passes create smooth light spreading without banding. Threshold and intensity controls allow artistic adjustment while maintaining realism. Lens dirt textures add cinematic lens contamination effects.

**Depth of Field**: 
Cinematic DOF with accurate circle of confusion calculation based on physical camera parameters. Bokeh shape simulation supports various aperture blade configurations. Near and far blur planes with smooth transitions prevent artifacts. Performance-scaled quality from simple gaussian to complex scatter-gather approaches.

**Motion Blur**: 
Per-object and camera motion blur with velocity buffer generation. Adaptive sample counts based on motion magnitude for consistent quality. Reconstruction filters prevent artifacts on fast-moving objects. Optional per-object toggle for competitive play preferences.

**Color Grading**: 
Industry-standard color grading with lift, gamma, gain controls. 3D LUT support for complex color transforms. Channel mixer for creative color manipulation. Film grain simulation with customizable size and intensity.

### Screen Space Effects

Screen space techniques provide cost-effective approximations of complex lighting phenomena, enhancing visual quality without the full cost of ray tracing.

**Screen Space Reflections (SSR)**: 
Hierarchical ray marching through depth buffer for efficient intersection finding. Roughness-based blur using importance sampling for realistic material response. Temporal accumulation stabilizes results across frames. Automatic fallback to cubemaps for rays leaving screen space. Contact hardening creates sharp reflections near surface contacts.

**Screen Space Global Illumination (SSGI)**: 
Approximate indirect lighting using screen space information for real-time performance. Multi-scale sampling captures both local and distant lighting contributions. Temporal filtering stabilizes results while preserving responsiveness to lighting changes. Bent normal calculation improves directional accuracy of indirect lighting.

**Ambient Occlusion Variants**:
- **SSAO**: Basic screen space ambient occlusion with normal-oriented sampling
- **HBAO+**: Horizon-based AO with improved accuracy and performance
- **GTAO**: Ground truth ambient occlusion with cosine-weighted sampling
- **RTAO**: Ray-traced ambient occlusion when RTX hardware is available

Each technique includes temporal accumulation and bilateral filtering for stable, noise-free results. Quality scales automatically based on resolution and performance targets.

### Volumetric Rendering

The volumetric rendering system creates atmospheric depth and environmental effects through physically-based light scattering simulation.

**Volumetric Lighting**: 
Froxel-based volumetric integration with exponential fog density distribution. Anisotropic scattering using Henyey-Greenstein phase function for realistic light shafts. Temporal reprojection amortizes expensive scattering calculations across frames. Shadow map sampling ensures volumetrics properly interact with scene shadows.

**Atmospheric Scattering**: 
Physically-based atmospheric model with Rayleigh and Mie scattering. Precomputed atmospheric scattering tables for real-time performance. Aerial perspective rendering for distant objects. Dynamic time-of-day with accurate sun and sky colors.

**Volumetric Clouds**: 
Raymarched volumetric clouds with multiple octaves of noise for realistic formations. Physical lighting model with multiple scattering approximation. Wind animation with evolving cloud shapes. Performance-scaled from simple billboards to full volumetric evaluation.

**God Rays**: 
Screen space light shafts with radial blur from bright light sources. Depth-aware sampling prevents incorrect occlusion. Intensity modulation based on viewing angle and atmospheric density. Combines with volumetric fog for cohesive atmospheric effects.

### Performance Optimizations

Extensive optimization ensures smooth performance across a wide range of hardware configurations.

**GPU-Driven Rendering**: 
Mesh shaders eliminate primitive assembly bottlenecks on supported hardware. GPU culling moves visibility determination entirely to GPU, freeing CPU resources. Indirect drawing allows GPU to generate its own draw commands. Multi-draw indirect batches hundreds of draw calls into single submissions.

**Variable Rate Shading (VRS)**: 
Content-adaptive shading rates reduce GPU load in less important screen regions. Motion-based VRS reduces shading in fast-moving areas where detail is less visible. Foveated rendering support for VR applications. Conservative shading rates preserve quality on important surfaces.

**Temporal Techniques**: 
Frame history reuse amortizes expensive calculations across multiple frames. Temporal upsampling reconstructs high-resolution images from lower resolution renders. Adaptive temporal sample counts based on scene motion and convergence. Automatic cache invalidation on scene changes prevents artifacts.

**Dynamic Resolution Scaling**: 
Automatic resolution adjustment maintains target framerates during demanding scenes. Temporal upsampling reconstructs full resolution from dynamically scaled renders. Independent scaling for different rendering passes optimizes quality/performance. Smooth transitions prevent visible resolution changes.

**Asynchronous Compute**: 
Parallel execution of compute and graphics workloads maximizes GPU utilization. BVH updates, particle simulation, and post-processing on compute queues. Automatic scheduling ensures optimal overlap without synchronization overhead. Performance counters guide workload distribution decisions.

## Configuration System

### Configuration Profiles

The engine includes carefully tuned configuration profiles for different hardware tiers:

**Base Configuration** (`config/q3config.cfg`):
Designed for mainstream gaming systems with RTX 3060/3070 or equivalent GPUs. Balances visual quality with consistent 100+ FPS at 1080p. Includes essential visual enhancements without expensive ray tracing. Suitable for competitive play with minimal input latency.

**Ultra Configuration** (`config/ultra_settings.cfg`):
Targets enthusiast hardware with RTX 4080/4090 GPUs. Enables all visual features including path tracing and maximum quality settings. Designed for 4K gaming with 60+ FPS or 1440p with 144+ FPS. Showcases the full visual potential of the engine.

### Performance Tuning

The engine provides extensive performance tuning options for optimal configuration:

**Automatic Detection**: 
Hardware detection automatically suggests appropriate quality settings. Benchmark mode tests system capabilities and recommends configurations. Dynamic adjustment maintains target framerates during gameplay. Performance overlay displays real-time metrics for tuning validation.

**Scaling Options**:
- Resolution scaling from 50% to 200% with temporal upsampling
- Effect quality tiers for shadows, reflections, and post-processing
- Ray tracing quality levels from basic reflections to full path tracing
- Texture and model LOD bias for memory-constrained systems

## CVar Documentation

### Display Configuration

| CVar | Default | Description | Values |
|------|---------|-------------|--------|
| `r_mode` | -2 | Display resolution mode | -2: Desktop, -1: Custom, 0-n: Preset modes |
| `r_fullscreen` | 1 | Fullscreen mode | 0: Windowed, 1: Fullscreen, 2: Borderless |
| `r_customwidth` | 1920 | Custom resolution width | Any positive integer |
| `r_customheight` | 1080 | Custom resolution height | Any positive integer |
| `r_displayRefresh` | 0 | Display refresh rate | 0: Auto-detect, n: Specific Hz |
| `r_swapInterval` | 1 | VSync control | 0: Off, 1: On, 2: Adaptive |
| `r_gamma` | 1.0 | Display gamma | 0.5 to 3.0 |

### Renderer Core

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `r_renderAPI` | 2 | Renderer backend | 0: OpenGL, 2: Vulkan | Vulkan recommended |
| `r_smp` | 1 | Multi-threaded rendering | 0: Off, 1: On | Significant performance boost |
| `r_primitives` | 2 | Primitive type | 0: Auto, 1: Strips, 2: Batched | |
| `r_lodBias` | 0 | Level of detail bias | -2 to 2 | Negative = higher quality |
| `r_lodScale` | 5 | LOD distance scaling | 1 to 10 | Higher = more detail at distance |
| `r_subdivisions` | 4 | Curve subdivisions | 1 to 80 | Higher = smoother curves |
| `r_znear` | 4 | Near clipping plane | 1 to 16 | Lower can cause z-fighting |

### RTX Ray Tracing

> **Note**: RTX features require `r_rtx 1` and compatible hardware

| CVar | Default | Description | Values | Dependencies |
|------|---------|-------------|--------|--------------|
| `r_rtx` | 0 | Enable RTX ray tracing | 0: Off, 1: On | Requires RTX GPU |
| `r_pathTracing` | 0 | Full path tracing mode | 0: Off, 1: On | Requires `r_rtx 1` |
| `r_pathTracingSamples` | 8 | Samples per pixel | 1 to 64 | Higher = better quality, lower FPS |
| `r_pathTracingBounces` | 4 | Maximum ray bounces | 1 to 8 | Higher = more accurate GI |
| `r_rtxReflections` | 0 | Ray-traced reflections | 0: Off, 1: On | Requires `r_rtx 1` |
| `r_rtxReflectionQuality` | 2 | Reflection quality | 1: Low, 2: Medium, 3: High, 4: Ultra | |
| `r_rtxGlobalIllumination` | 0 | Ray-traced GI | 0: Off, 1: On | Requires `r_rtx 1` |
| `r_rtxGIQuality` | 2 | GI quality level | 1: Low, 2: Medium, 3: High | |
| `r_rtxAmbientOcclusion` | 0 | Ray-traced AO | 0: Off, 1: On | Requires `r_rtx 1` |
| `r_rtxShadows` | 0 | Ray-traced shadows | 0: Off, 1: On | Requires `r_rtx 1` |
| `r_rtxDenoiser` | 1 | Denoising filter | 0: Off, 1: On | Recommended for quality |
| `r_rtxDenoisingStrength` | 0.5 | Denoiser strength | 0.0 to 1.0 | Higher = smoother but less detail |

### PBR Material System

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `r_pbr` | 1 | Enable PBR rendering | 0: Off, 1: On | Dramatic quality improvement |
| `r_pbrBaseMap` | 1 | Use base color maps | 0: Off, 1: On | |
| `r_pbrNormalMap` | 1 | Use normal maps | 0: Off, 1: On | |
| `r_pbrRoughnessMap` | 1 | Use roughness maps | 0: Off, 1: On | |
| `r_pbrMetallicMap` | 1 | Use metallic maps | 0: Off, 1: On | |
| `r_pbrEmissiveMap` | 1 | Use emissive maps | 0: Off, 1: On | |
| `r_parallaxMapping` | 1 | Parallax occlusion | 0: Off, 1: On | Adds surface depth |
| `r_parallaxDepth` | 0.05 | Parallax depth scale | 0.01 to 0.2 | Higher = more pronounced |
| `r_normalMapping` | 1 | Normal mapping | 0: Off, 1: On | |
| `r_specularMapping` | 1 | Specular mapping | 0: Off, 1: On | |
| `r_materialAutoGen` | 1 | Auto-generate PBR materials | 0: Off, 1: On | For legacy textures |
| `r_materialGenQuality` | 2 | Generation quality | 1: Fast, 2: Balanced, 3: Quality | |

### Path-Traced Lighting

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `rt_enable` | 1 | Enable the unified path tracer | 0: Off, 1: On | Required for modern lighting |
| `rt_mode` | dynamic | Lighting coverage | `dynamic`, `all` | `all` includes static light extraction |
| `rt_samples` | 2 | Samples per pixel | 1–8 | Higher values reduce noise at a performance cost |
| `rt_bounces` | 2 | Indirect light bounces | 0–5 | Increase for richer global illumination |
| `rt_temporal` | 1 | Temporal accumulation | 0: Off, 1: On | Smooths noise over time |
| `rt_denoise` | 1 | Reprojection denoiser | 0: Off, 1: On | Works best with temporal accumulation |
| `rt_staticLights` | 1 | Include static BSP lights | 0: Off, 1: On | Keeps baked lights in the unified feed |

> Note: All legacy shadow toggles (`r_shadows`, `r_shadowMap*`, `r_shadowFilter*`, etc.) have been removed. Hardware and software backends now share the exact same path-traced lighting model.

### Post-Processing

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `r_hdr` | 1 | HDR rendering | 0: Off, 1: On | Required for tone mapping |
| `r_toneMap` | 1 | Tone mapping operator | 0: Off, 1: Reinhard, 2: Filmic, 3: ACES | Requires `r_hdr 1` |
| `r_autoExposure` | 1 | Auto exposure | 0: Off, 1: On | Requires `r_hdr 1` |
| `r_bloom` | 1 | Bloom effect | 0: Off, 1: On | |
| `r_bloomIntensity` | 0.5 | Bloom intensity | 0.0 to 2.0 | |
| `r_bloomThreshold` | 0.8 | Bloom threshold | 0.0 to 2.0 | Higher = only bright areas |
| `r_motionBlur` | 0 | Motion blur | 0: Off, 1: On | Can affect visibility |
| `r_motionBlurStrength` | 0.5 | Blur strength | 0.0 to 1.0 | |
| `r_dof` | 0 | Depth of field | 0: Off, 1: On | Cinematic effect |
| `r_filmGrain` | 0 | Film grain effect | 0.0 to 1.0 | 0 = off |
| `r_chromaticAberration` | 0 | Chromatic aberration | 0: Off, 1: On | Lens distortion effect |
| `r_vignette` | 0 | Vignette effect | 0.0 to 1.0 | 0 = off |

### Screen Space Effects

> **Note**: SSR/SSGI provide ray tracing-like effects without RTX hardware

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `r_ssr` | 1 | Screen space reflections | 0: Off, 1: On | Good quality/performance |
| `r_ssrQuality` | 2 | SSR quality | 1: Low, 2: Medium, 3: High | |
| `r_ssrMaxDistance` | 1000 | SSR ray distance | 100 to 2000 | |
| `r_ssrThickness` | 0.5 | SSR thickness | 0.1 to 2.0 | |
| `r_ssgi` | 0 | Screen space GI | 0: Off, 1: On | Approximate global illumination |
| `r_ssgiQuality` | 2 | SSGI quality | 1: Low, 2: Medium, 3: High | |
| `r_ssao` | 1 | Screen space AO | 0: Off, 1: On | |
| `r_ssaoIntensity` | 0.5 | SSAO intensity | 0.0 to 2.0 | |
| `r_ssaoRadius` | 32 | SSAO radius | 8 to 128 | |
| `r_hbao` | 0 | HBAO+ | 0: Off, 1: On | Better than SSAO |
| `r_gtao` | 0 | Ground truth AO | 0: Off, 1: On | Most accurate |

### Volumetric Effects

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `r_volumetricLighting` | 1 | Volumetric lighting | 0: Off, 1: On | Atmospheric light shafts |
| `r_volumetricDensity` | 0.5 | Fog density | 0.0 to 2.0 | |
| `r_volumetricSamples` | 32 | Sample count | 8 to 128 | Higher = better quality |
| `r_volumetricQuality` | 2 | Quality preset | 1: Low, 2: Medium, 3: High | |
| `r_godRays` | 1 | God rays/light shafts | 0: Off, 1: On | |
| `r_godRayIntensity` | 1.0 | God ray intensity | 0.0 to 2.0 | |
| `r_volumetricClouds` | 0 | Volumetric clouds | 0: Off, 1: On | Performance intensive |
| `r_fog` | 2 | Fog rendering | 0: Off, 1: Simple, 2: Volumetric | |

### Performance

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `r_dynamicLight` | 1 | Dynamic lights | 0: Off, 1: On, 2: Enhanced | |
| `r_dlightMode` | 1 | Dynamic light quality | 0: Vertex, 1: Pixel, 2: Advanced | |
| `r_maxPolys` | 8192 | Maximum polygons | 1024 to 32768 | |
| `r_maxPolyVerts` | 32768 | Maximum vertices | 4096 to 131072 | |
| `r_textureMode` | GL_LINEAR_MIPMAP_LINEAR | Texture filtering | See notes | Trilinear recommended |
| `r_picmip` | 0 | Texture detail | 0 to 3 | 0 = highest quality |
| `r_ext_max_anisotropy` | 8 | Anisotropic filtering | 1 to 16 | Higher = sharper textures |
| `r_ext_multisample` | 4 | MSAA samples | 0, 2, 4, 8 | Anti-aliasing quality |
| `r_temporalAA` | 1 | Temporal AA | 0: Off, 1: On | Better than MSAA |
| `r_vbo` | 1 | Vertex buffer objects | 0: Off, 1: On | Improves performance |
| `r_gpuCulling` | 1 | GPU-based culling | 0: Off, 1: On | Reduces CPU load |

### Advanced Features

| CVar | Default | Description | Values | Notes |
|------|---------|-------------|--------|-------|
| `r_dlss` | 0 | NVIDIA DLSS | 0: Off, 1: Performance, 2: Balanced, 3: Quality | Requires RTX GPU |
| `r_fsr` | 0 | AMD FSR | 0: Off, 1: Performance, 2: Balanced, 3: Quality | |
| `r_variableRateShading` | 0 | VRS | 0: Off, 1: On | Requires Turing+ GPU |
| `r_meshShaders` | 0 | Mesh shaders | 0: Off, 1: On | Requires Turing+ GPU |
| `r_bindlessTextures` | 1 | Bindless textures | 0: Off, 1: On | Improves performance |
| `r_gpuDrivenRendering` | 1 | GPU-driven rendering | 0: Off, 1: On | Reduces CPU overhead |

## Portal System

The portal system introduces seamless spatial connections between different areas of the map, enabling innovative gameplay mechanics and level design possibilities.

### Gameplay Features

**Portal Mechanics**: 
Players can traverse through portal surfaces that connect disparate locations. Momentum is preserved through portals, enabling advanced movement techniques. Visual and audio continuity is maintained across portal boundaries. Projectiles and effects properly transition through portal surfaces.

**Rendering Through Portals**: 
The renderer recursively draws scenes visible through portals up to a configurable depth. Each portal maintains its own view frustum and culling context. Lighting and shadows correctly propagate through portal boundaries. Performance scales gracefully with portal depth and screen coverage.

**Portal Configuration**:

| CVar | Default | Description | Values |
|------|---------|-------------|-----|
| `r_usePortals` | 1 | Enable portal rendering | 0: Off, 1: On |
| `r_maxPortalDepth` | 8 | Maximum portal recursion | 1 to 16 |
| `r_portalOnly` | 0 | Debug portal surfaces | 0: Off, 1: On |

### Level Design Integration

Portal surfaces are defined using special shaders in the `baseq3/shaders/portal.shader` file. Map designers can create bidirectional or unidirectional portals with customizable visual effects. The system supports multiple portals per scene with proper depth sorting and clipping.

## Building from Source

### Prerequisites

- Visual Studio 2017 or newer (Windows)
- CMake 3.12 or newer (Linux/macOS)
- Vulkan SDK 1.2 or newer
- Git

### Windows Build

```batch
# Clone the repository
git clone https://github.com/yourusername/Quake3e-HD.git
cd Quake3e-HD

# Build using included batch script
build.bat

# Binaries will be in src/project/msvc2017/output/
```

### Linux/macOS Build

```bash
# Clone the repository
git clone https://github.com/yourusername/Quake3e-HD.git
cd Quake3e-HD

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)
```

## Performance Optimization Guide

### Achieving Target Framerates

**For Competitive Play (240+ FPS)**:
- Disable ray tracing features (`r_rtx 0`)
- Use base configuration as starting point
- Reduce path-tracing cost (`rt_samples 1`, `rt_temporal 0`, `rt_denoise 0`)
- Disable expensive post-processing (`r_bloom 0`, `r_motionBlur 0`)
- Enable dynamic resolution scaling with 240 FPS target

**For Visual Quality (60+ FPS)**:
- Enable DLSS/FSR (`r_dlss 2` or `r_fsr 2`)
- Use selective ray tracing (reflections only)
- Reduce ray tracing quality for better performance
- Enable temporal accumulation for denoising
- Use VRS to optimize shading rate

**For Maximum Quality (Screenshot/Video)**:
- Crank path tracer quality (`rt_samples 8`, `rt_bounces 4`)
- Keep temporal accumulation and denoising enabled
- Disable all performance optimizations
- Use offline rendering mode for best results

### Troubleshooting Performance Issues

**GPU Bottlenecks**:
- Check GPU utilization with `r_speeds 1`
- Reduce resolution or enable upsampling
- Lower shadow and reflection quality
- Disable volumetric effects

**CPU Bottlenecks**:
- Ensure `r_smp 1` is enabled
- Enable GPU-driven rendering features
- Reduce draw call count with `r_dynamicLight 0`
- Check for background processes

**Memory Issues**:
- Reduce texture quality with `r_picmip 1`
- Lower path-tracing load (`rt_samples 1`, `rt_bounces 1`)
- Disable texture streaming if stuttering occurs
- Clear shader cache if corruption suspected

## Troubleshooting

### Common Issues

**Black Screen on Launch**:
- Update GPU drivers to latest version
- Verify Vulkan runtime is installed
- Try windowed mode (`r_fullscreen 0`)
- Delete generated config files and restart

**Crashes with RTX Enabled**:
- Ensure GPU supports hardware ray tracing
- Update to latest NVIDIA/AMD drivers
- Reduce ray tracing quality settings
- Check Windows 10/11 is fully updated

**Poor Performance**:
- Start with base configuration
- Disable ray tracing if GPU lacks RT cores
- Enable DLSS/FSR for free performance
- Check thermal throttling with GPU monitoring tools

**Visual Artifacts**:
- Clear shader cache in `baseq3/shadercache/`
- Verify game files integrity
- Disable problematic effects one by one
- Report persistent issues with screenshots

### Debug Commands

| Command | Description |
|---------|-------------|
| `r_speeds 1` | Display rendering statistics |
| `r_showTris 1` | Wireframe rendering mode |
| `r_showNormals 1` | Display surface normals |
| `r_showLightmaps 1` | Display lightmap textures |
| `r_showImages 1` | Display loaded textures |
| `r_printShaders 1` | Log shader compilation |
| `gfx_info` | Display graphics capabilities |
| `imagelist` | List loaded images with memory usage |
| `shaderlist` | List loaded shaders |
| `meminfo` | Display memory statistics |

## Contributing

Contributions are welcome! Please follow these guidelines:

1. Fork the repository and create a feature branch
2. Follow existing code style and conventions
3. Add unit tests for new functionality
4. Update documentation for user-facing changes
5. Submit a pull request with clear description

### Development Focus Areas

- Additional RT effects and optimizations
- Vulkan performance improvements
- Linux and macOS platform support
- VR rendering support
- Network code modernization
- Anti-cheat compatibility

## License

This project maintains the original Quake III Arena GPL v2 license. See [COPYING.txt](COPYING.txt) for full license text.

### Acknowledgments

- id Software for the original Quake III Arena
- Quake3e team for the performance-focused foundation
- The open source community for continued support and contributions

### Third-Party Libraries

- Vulkan SDK - Rendering API
- OpenAL Soft - 3D audio
- SDL2 - Platform abstraction (optional)
- stb_image - Image loading
- Dear ImGui - Development UI

---

*For additional support, bug reports, or feature requests, please visit the [Issues](../../issues) page.*

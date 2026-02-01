# Cyberpunk 2077 Graphics Subsystems (macOS ARM64)

> Analysis of Ray Tracing, Path Tracing, FSR, and MetalFX support in the macOS binary.

## Overview

The macOS version of Cyberpunk 2077 includes full graphics feature parity with the PC version:

| Feature | Status | Implementation |
|---------|--------|----------------|
| FSR 2.1 | ✅ Present | `CRenderNode_ApplyFSR2` |
| FSR 3 | ✅ Present | `CRenderNode_ApplyFSR3` |
| MetalFX | ✅ Present | Apple's upscaling API |
| Ray Tracing | ✅ Present | Multiple RT render nodes |
| Path Tracing | ✅ Present | `PathTracingSettings` |

---

## FSR (AMD FidelityFX Super Resolution)

### FSR2

```
Class: CRenderNode_ApplyFSR2
Data:  FSR2CustomData (0xF0 bytes)
Hash:  0x073b07c8 (type hash storage)
```

**Quality Levels** (from `BenchmarkSummary`):
- `FSR2Enabled` (offset 0x1A8)
- `FSR2Quality` (offset 0x1AC) 
- `FSR2Sharpness` (offset 0x1B0)

### FSR3

```
Class: CRenderNode_ApplyFSR3  
Data:  FSR3CustomData (0x30 bytes)
Hash:  0x073b0818 (type hash storage)
```

**Quality Levels**:
- `FSR3Enabled` (offset 0x1B4)
- `FSR3Quality` (offset 0x1B8)
- `FSR3Sharpness` (offset 0x1BC)
- `FSR3FrameGenEnabled` (offset 0x1C0)

### FSR4

```
Settings present but implementation unclear:
- FSR4Enabled (offset 0x1C1)
- FSR4Quality (offset 0x1C4)
- FSR4Sharpness (offset 0x1C8)
```

---

## MetalFX (Apple)

### Evidence

String references found in binary:
- `"MetalFX"` - Feature identifier
- `"MetalFX_Sharpness"` - Sharpness control variable

### Implementation

MetalFX is Apple's upscaling technology for Metal, similar to DLSS/FSR.
The game appears to support it as an alternative to FSR on macOS.

**Likely tied to**:
- `CRenderNode_ApplyStaticUpscaling` (general upscaling node)
- `m_upscalingType` (offset 0x184 in BenchmarkSummary)

---

## Ray Tracing

### Render Nodes

| Node | Purpose | Data Symbol |
|------|---------|-------------|
| `CRenderNode_RenderRayTracedRTXDI` | Direct illumination | 0x073b1d78 |
| `CRenderNode_RenderRayTracedReSTIRGI` | Global illumination | 0x073b1d18 |
| `CRenderNode_RenderRayTracedReflections` | RT reflections | - |
| `CRenderNode_RenderRayTracedGlobalShadow` | Sun shadows | - |
| `CRenderNode_RenderRayTracedLocalShadow` | Local shadows | - |
| `CRenderNode_FilterRayTracedLocalShadow` | Shadow filtering | - |
| `CRenderNode_RenderRayTracedAmbientOcclusion` | RT AO | - |
| `CRenderNode_RayTracingRenderDebug` | Debug visualization | 0x073b1cb8 |
| `CRenderNode_RayTracingFilterOutput` | Output filtering | 0x073b1dc0 |

### Custom Data

```cpp
struct RayTracingCustomData : ICameraStorageCustomData {
    static constexpr const char* NAME = "RayTracingCustomData";
    // Size: 0x8610 bytes - contains NRD denoiser state
};

// Memory allocation for denoiser
RayTracingCustomData::NrdAllocate(void*, size_t, size_t)
RayTracingCustomData::NrdReallocate(void*, void*, size_t, size_t)  
RayTracingCustomData::NrdFree(void*, void*)
```

### Settings Flags (BenchmarkSummary)

| Setting | Offset | Type |
|---------|--------|------|
| `rayTracingEnabled` | 0x1F0 | bool |
| `rayTracedReflections` | 0x1F1 | bool |
| `rayTracedSunShadows` | 0x1F2 | bool |
| `rayTracedLocalShadows` | 0x1F3 | bool |
| `rayTracedLightingQuality` | 0x1F4 | int32 |
| `rayTracedPathTracingEnabled` | 0x1F8 | bool |

### Quality Presets

From `ConfigGraphicsQualityLevel`:
- `RaytracingLow = 5`
- `RaytracingMedium = 6`
- `RaytracingUltra = 7`
- `RaytracingOverdrive = 8` (Path Tracing)
- `Cinematic_Raytracing = 10`
- `CinematicEXR_Raytracing = 12`

---

## Path Tracing

### Settings Structure

```cpp
struct PathTracingSettings : IAreaSettings {
    static constexpr const char* NAME = "PathTracingSettings";
    // Size: 0x90 bytes
};
```

**Type hash storage**: 0x073fc3c0

### Light Usage Enum

```cpp
enum class EPathTracingLightUsage : int8_t {
    PTLU_Default = 0,
    PTLU_OnlyInPathTracing = 1,
    PTLU_ExcludeFromPathTracing = 2,
};
```

### Component Properties

Light components have path tracing specific properties:
- `pathTracingLightUsage` (ent::LightComponent @ 0x198)
- `pathTracingOverrideScaleGI` (@ 0x199)

---

## GPU Memory Pools

```cpp
// Ray tracing memory allocators
struct PoolRaytrace { Name = "PoolRaytrace"; };
struct GPUM_TG_System_RayTracing { Name = "GPUM_TG_System_RayTracing"; };
struct GPUM_Buffer_Raytracing { Name = "GPUM_Buffer_Raytracing"; };
struct GPUM_Buffer_RaytracingUpload { Name = "GPUM_Buffer_RaytracingUpload"; };
struct GPUM_Buffer_RaytracingAS { Name = "GPUM_Buffer_RaytracingAS"; };
```

Buffer groups (`GpuWrapApi::BufferGroup`):
- `Raytracing = 23`
- `RaytracingUpload = 24`
- `RaytracingAS = 25` (Acceleration Structures)
- `RaytracingOMM = 26` (Opacity Micro-Maps)

---

## Environment Modifiers

From `EEnvManagerModifier`:
- `EMM_MaskReactivityFSR2 = 51`
- `EMM_RayTracingDebug = 57`

---

## Finding Function Addresses

To find actual function implementations (not just data symbols):

### 1. From GetName() Static Data

Data symbols like `CRenderNode_ApplyFSR2::GetName()::name` at 0x073b0800
indicate the static string storage. The actual `GetName()` function is nearby.

### 2. VTable Lookup

Render nodes inherit from a base class with virtual methods.
Find vtable → decode chained fixups → get method addresses.

### 3. String Reference Tracing

```bash
# Find references to render node name string
strings $BINARY | grep "CRenderNode_ApplyFSR2"
# Then trace ADRP+ADD references
```

---

## Potential Modding Applications

1. **Force Enable Path Tracing** - Override `rayTracedPathTracingEnabled`
2. **Custom Quality Presets** - Modify `ConfigGraphicsQualityLevel` mappings
3. **FSR Sharpness Tweaks** - Adjust `FSR2Sharpness`/`FSR3Sharpness` values
4. **MetalFX Integration** - Hook upscaling to prefer MetalFX over FSR
5. **RT Feature Toggles** - Individually enable/disable RT effects

---

## Related Files

- `RED4ext.SDK/include/RED4ext/Scripting/Natives/Generated/PathTracingSettings.hpp`
- `RED4ext.SDK/include/RED4ext/Scripting/Natives/Generated/FSR2CustomData.hpp`
- `RED4ext.SDK/include/RED4ext/Scripting/Natives/Generated/FSR3CustomData.hpp`
- `RED4ext.SDK/include/RED4ext/Scripting/Natives/Generated/RayTracingCustomData.hpp`
- `RED4ext.SDK/include/RED4ext/Scripting/Natives/Generated/world/BenchmarkSummary.hpp`

---

## Version Information

- Game Version: 2.x (macOS ARM64)
- Analysis Date: January 2026
- Binary: `Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077`

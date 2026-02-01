# Ray Tracing & Path Tracing Optimization Research Plan

> Investigating optimization opportunities for Cyberpunk 2077's RT/PT implementation on Apple Silicon

## Executive Summary

Cyberpunk 2077 was ported from Windows (DXR/Vulkan RT) to macOS (Metal RT). The original implementation was optimized for NVIDIA RTX GPUs. Apple Silicon has fundamentally different architecture that may not be fully exploited:

| Aspect | NVIDIA RTX | Apple Silicon |
|--------|-----------|---------------|
| Memory | Discrete VRAM | Unified Memory |
| RT Cores | Dedicated hardware | GPU compute shaders |
| Architecture | Traditional rasterization | Tile-Based Deferred (TBDR) |
| Upscaling | DLSS (tensor cores) | MetalFX (temporal) |

**Hypothesis**: The port likely does a 1:1 API translation rather than architectural optimization for Apple Silicon.

---

## Key Discovery: Native Metal RT Implementation

**The game uses native Metal Ray Tracing APIs, not emulation!**

### Evidence: Metal Acceleration Structure Selectors Found

```objc
// Metal API usage confirmed
MTLAccelerationStructure
MTLAccelerationStructureTriangleGeometryDescriptor
MTLInstanceAccelerationStructureDescriptor
MTLPrimitiveAccelerationStructureDescriptor
accelerationStructureSizesWithDescriptor:
buildAccelerationStructure:descriptor:scratchBuffer:scratchBufferOffset:
refitAccelerationStructure:descriptor:destination:scratchBuffer:scratchBufferOffset:
```

### Render Nodes for AS Management

```
CRenderNode_AccelerationStructurePrepare
CRenderNode_AccelerationStructureUpdateDynamic
CRenderNode_AccelerationStructureUpdateStatic
CRenderNode_AccelerationStructureUpdateEpilogue
```

### Apple-Specific Buffers (AAPL prefix)

```
aaplRayBuffer                        - Ray storage
aaplPayloadBuffer                    - Hit/miss payloads
aaplTracingAttributesBuffer          - Tracing state
aaplBucketsBuffer                    - Ray sorting buckets
aaplMissRayIndexBuffer               - Miss ray indices
aaplHitRayIndexBuffer                - Hit ray indices
aaplIndirectDispatchArgsMissBuffer   - Miss shader dispatch
aaplIndirectDispatchArgsHitBuffer    - Hit shader dispatch
aaplIndirectDispatchArgsBucketsBuffer - Bucket dispatch
```

### ReSTIR GI Apple Implementation

```
m_aaplReSTIRGIDiffuseBinOutputBuffer
m_aaplReSTIRGIDiffuseOuputBuffer
m_aaplReSTIRGISpecularBinOutputBuffer
m_aaplReSTIRGISpecularOuputBuffer
```

**This shows CD Projekt RED implemented Apple-specific code paths, not just a translation layer!**

---

## Phase 1: Current Implementation Analysis

### 1.1 Render Pipeline Structure

**Information Needed:**
- [ ] Complete render node execution graph for RT passes
- [ ] Dependencies between RT nodes
- [ ] Memory allocation/deallocation patterns per frame
- [ ] Buffer sizes for `GPUM_Buffer_Raytracing*` pools

**How to Gather:**
```bash
# Find render node registration
strings $BINARY | grep "CRenderNode.*Ray"

# Find render graph setup
nm -n $BINARY | c++filt | grep -i "rendergraph\|renderpass"

# Trace execution flow via hooks
```

**Key Questions:**
1. Are RT passes running sequentially or parallelized?
2. What's the memory pressure during RT frame?
3. How many rays per pixel at each quality level?

### 1.2 Acceleration Structure Management

**Information Needed:**
- [ ] BVH build frequency (per-frame vs cached)
- [ ] BLAS/TLAS update strategy
- [ ] Acceleration structure memory layout
- [ ] Skinned mesh BVH rebuilds

**How to Gather:**
```bash
# Find AS-related functions
nm -n $BINARY | c++filt | grep -iE "accel|bvh|blas|tlas"

# Check buffer groups
grep "RaytracingAS" $SDK_PATH/include/RED4ext/
```

**Key Questions:**
1. Is the AS being rebuilt unnecessarily?
2. Are static meshes cached properly?
3. How is dynamic geometry handled?

### 1.3 Denoiser Analysis (NRD)

**Information Needed:**
- [ ] NVIDIA Real-time Denoisers (NRD) integration points
- [ ] Denoiser pass count and configuration
- [ ] Temporal accumulation strategy
- [ ] History buffer management

**Evidence Found:**
```cpp
RayTracingCustomData::NrdAllocate()
RayTracingCustomData::NrdReallocate()
RayTracingCustomData::NrdFree()
// Size: 0x8610 bytes - substantial denoiser state
```

**Key Questions:**
1. Is NRD optimized for Metal or running generic compute?
2. Are there Apple-specific denoiser alternatives?
3. What's the denoiser memory footprint?

---

## Phase 2: Apple Silicon Architecture Considerations

### 2.1 Unified Memory Exploitation

**Optimization Opportunities:**
- Zero-copy buffer sharing between CPU/GPU
- Reduced memory transfer overhead
- Larger working sets without VRAM limits

**Investigation:**
- [ ] Check if buffers use `MTLResourceStorageModeShared`
- [ ] Identify unnecessary CPU↔GPU copies
- [ ] Find buffer staging patterns from DXR port

### 2.2 Tile-Based Deferred Rendering (TBDR)

**Apple Silicon Feature:**
- On-chip tile memory (fast, local to GPU)
- Deferred rendering optimizations
- Reduced memory bandwidth

**Potential Issues:**
- RT may bypass tile memory benefits
- Traditional render passes may not use TBDR optimally

**Investigation:**
- [ ] Check render pass configurations
- [ ] Identify if RT output goes through tile memory
- [ ] Find opportunities to merge passes

### 2.3 Metal Ray Tracing API Usage

**Key APIs:**
```objc
MTLAccelerationStructure
MTLIntersectionFunctionTable
raytracing_acceleration_structure
[[intersection()]] shader functions
```

**Investigation:**
- [ ] Compare to DXR patterns (DispatchRays vs compute-based)
- [ ] Check ray coherence optimization
- [ ] Analyze intersection shader complexity

### 2.4 Thread Occupancy & Wavefronts

**Apple Silicon:**
- Different SIMD width than NVIDIA (32 threads vs 32/64)
- Different occupancy characteristics
- Different register pressure limits

**Investigation:**
- [ ] Shader complexity analysis
- [ ] Register usage per kernel
- [ ] Occupancy bottlenecks

---

## Phase 3: Specific Optimization Targets

### 3.1 Ray Tracing Shadows

**Current Implementation:**
```
CRenderNode_RenderRayTracedGlobalShadow  (sun shadows)
CRenderNode_RenderRayTracedLocalShadow   (point/spot)
CRenderNode_FilterRayTracedLocalShadow   (denoising)
```

**Optimization Opportunities:**
1. **Shadow ray coherence** - Sort rays by direction
2. **Early termination** - Any-hit vs closest-hit
3. **Temporal stability** - Reuse previous frame data
4. **Resolution scaling** - Half-res shadows with upscale

### 3.2 Global Illumination (ReSTIR GI)

**Current Implementation:**
```
CRenderNode_RenderRayTracedReSTIRGI
```

**ReSTIR Algorithm:**
- Reservoir-based importance sampling
- Spatiotemporal reuse of samples

**Optimization Opportunities:**
1. **Reservoir management** - Memory layout for Apple Silicon
2. **Sample reuse** - May need different parameters for M-series
3. **Spatial filtering** - Tile-aware patterns

### 3.3 Direct Illumination (RTXDI)

**Current Implementation:**
```
CRenderNode_RenderRayTracedRTXDI
CRenderNode_RenderRayTracedRTXDIDebug
```

**RTXDI = Real-Time Direct Illumination**
- Many light sampling with resampling
- Originally optimized for RTX hardware

**Discovered RTXDI Configuration:**
```
UseRTXDI                  - Master enable
UseRTXDIAtPrimary         - Primary ray RTXDI
UseRTXDIWithAlbedo        - Albedo modulation
EnableRTXDIDenoising      - Denoiser toggle
AllowRTXDIRejitter        - Temporal jitter
RTXDIEnvMap               - Environment map sampling
RTXDILocalLightPdf        - Light importance PDF
m_rtxdiShadowStartingDistance - Shadow ray offset
m_rtxdiNeighborOffsetsBuffer  - Spatial reuse offsets
```

**RTXDI Denoiser Buffers:**
```
rtxdiDenoiserDiffuseTemp
rtxdiDenoiserSpecularTemp
rtxdiDenoiserDiffuseAccumulation1/2
rtxdiDenoiserSpecularAccumulation1/2
m_rtxdiDenoiserVariance1/2
rtxdiDiffuseOutputDenoised
rtxdiSpecularOutputDenoised
```

**Environment Modifiers:**
```
EMM_RTXDIBRDFFactor
EMM_RTXDIDiffuseDenoised
EMM_RTXDIDiffuseRaw
EMM_RTXDISpecularDenoised
EMM_RTXDISpecularRaw
```

**Optimization Opportunities:**
1. **Light clustering** - Spatial data structures
2. **Sample counts** - May need adjustment for Apple GPU
3. **Temporal stability** - Different motion vector handling
4. **Denoiser tuning** - Apple-specific variance thresholds

### 3.4 Path Tracing

**Current Implementation:**
```
PathTracingSettings (0x90 bytes config)
rayTracedPathTracingEnabled flag
Quality: RaytracingOverdrive (level 8)
```

**Path Tracing Characteristics:**
- Full light transport simulation
- Many bounces (expensive)
- Heavy denoising required

**Optimization Opportunities:**
1. **Bounce limits** - Optimize for Apple GPU throughput
2. **Russian roulette** - Adaptive termination tuning
3. **NEE (Next Event Estimation)** - Light sampling efficiency
4. **Denoiser quality** - MetalFX temporal vs NRD

---

## Phase 4: Data Collection Methods

### 4.1 Runtime Profiling

**Tools:**
- Xcode Metal System Trace
- Metal Performance HUD
- Custom instrumentation via RED4ext hooks

**Metrics to Collect:**
```
- GPU time per render pass
- Memory bandwidth usage
- Shader occupancy
- AS build times
- Ray/pixel counts
```

### 4.2 Shader Analysis

**Methods:**
- Extract Metal shaders from game archive
- Analyze compiled AIR (Apple Intermediate Representation)
- Compare to theoretical optima

**Looking For:**
- Register spilling
- Divergent branches
- Memory access patterns
- Unnecessary barriers

### 4.3 Binary Analysis

**Functions to Reverse:**
```
CRenderNode_*::Execute()    - Render pass implementation
*AccelerationStructure*     - BVH management
NRD*                        - Denoiser integration
ReSTIR*                     - GI sampling
RTXDI*                      - Direct illumination
```

### 4.4 Comparison Testing

**Methodology:**
1. Capture same scene on Windows + macOS
2. Compare frame times per pass
3. Identify disproportionate costs
4. Focus optimization on biggest gaps

---

## Phase 5: Potential Optimizations

### 5.1 Quick Wins (Low Effort)

| Optimization | Expected Impact | Effort |
|--------------|-----------------|--------|
| Unified memory buffer modes | 5-10% | Low |
| Reduce CPU-GPU sync points | 5-15% | Low |
| Adjust ray counts for M-series | 10-20% | Low |
| MetalFX for denoising | 10-30% | Medium |

### 5.2 Medium Effort

| Optimization | Expected Impact | Effort |
|--------------|-----------------|--------|
| Ray sorting/coherence | 15-25% | Medium |
| Tile-aware denoiser | 10-20% | Medium |
| AS caching improvements | 5-15% | Medium |
| Shader complexity reduction | 10-20% | Medium |

### 5.3 Major Refactors

| Optimization | Expected Impact | Effort |
|--------------|-----------------|--------|
| Custom Metal RT pipeline | 20-40% | High |
| Apple Silicon specific shaders | 15-30% | High |
| Hybrid raster+RT approach | 20-50% | High |
| Full TBDR integration | 10-25% | High |

---

## Phase 6: Required Resources

### 6.1 Documentation

- [ ] Metal Ray Tracing Programming Guide
- [ ] Apple GPU Architecture documentation
- [ ] NVIDIA NRD documentation
- [ ] ReSTIR/RTXDI papers
- [ ] Original Cyberpunk GDC presentations

### 6.2 Tools

- [ ] Xcode with Metal tools
- [ ] GPU profiler access
- [ ] Shader disassembler
- [ ] Memory analysis tools

### 6.3 Access Requirements

- [ ] Game binary for analysis
- [ ] Shader archives
- [ ] Test scenes for benchmarking
- [ ] Comparison Windows system (optional)

---

## Next Steps

### Immediate Actions

1. **Profile Current State**
   - Run Metal System Trace on RT-enabled gameplay
   - Identify top GPU time consumers
   - Measure memory bandwidth usage

2. **Extract Shader Information**
   - Find shader archive format
   - Analyze compiled Metal shaders
   - Identify optimization targets

3. **Instrument Key Functions**
   - Hook render node execution
   - Log pass timing
   - Track memory allocations

### Research Questions to Answer

1. Is the port using Metal RT primitives or compute-based RT?
2. What denoiser is actually running (NRD native or ported)?
3. Are there any Apple-specific code paths already?
4. What's the ray count per pixel at each quality level?
5. How does AS management compare to DXR original?

---

## Appendix A: Discovered Apple-Specific Implementation

### Denoiser Shader Masks (AAPL Suffix)

The game has **Apple-specific denoiser configurations**:

```
DenoisingPathTracingShaderMaskAAPL  - Path tracing denoiser
DenoisingRayTracingShaderMaskAAPL   - RT denoiser  
DenoisingRtxdiShaderMaskAAPL        - RTXDI denoiser
denoiserNrdShaderPreferenceAAPL     - NRD preferences
```

This indicates CD Projekt RED created Metal-specific denoiser shaders, not just ported DXR shaders.

### Metal Intersection Function Tables

Native Metal RT intersection handling:

```objc
MTLIntersectionFunctionDescriptor
MTLIntersectionFunctionTableDescriptor
MTLIntersectionFunctionTable
newIntersectionFunctionTableWithDescriptor:stage:
allowDuplicateIntersectionFunctionInvocation
```

### Console Variables

```
cvRayTracingEnableNRD           - NRD denoiser toggle
cvRayTracingEnableReferenceSER  - Shader Execution Reordering
                                  (NVIDIA-specific, may be stubbed)
```

### Apple-Specific Buffers Summary

| Buffer | Purpose |
|--------|---------|
| `aaplRayBuffer` | Primary ray storage |
| `aaplPayloadBuffer` | Hit/miss payloads |
| `aaplTracingAttributesBuffer` | Trace state |
| `aaplBucketsBuffer` | Ray coherence sorting |
| `aaplMissRayIndexBuffer` | Miss ray indices |
| `aaplHitRayIndexBuffer` | Hit ray indices |
| `m_aaplReSTIRGIDiffuseBinOutputBuffer` | ReSTIR diffuse GI |
| `m_aaplReSTIRGISpecularBinOutputBuffer` | ReSTIR specular GI |

---

## Appendix B: Key Addresses to Find

| Function | Purpose | Priority |
|----------|---------|----------|
| `RenderRayTracedReSTIRGI::Execute` | GI main loop | High |
| `RenderRayTracedRTXDI::Execute` | DI main loop | High |
| `AccelerationStructure::Build` | BVH construction | High |
| `NRD::Denoise` | Denoiser entry | Medium |
| `PathTracer::TraceRays` | PT main loop | Medium |
| `RaySort` / `RayCoherence` | Ray management | Medium |

---

*This research plan outlines the comprehensive investigation needed to identify Apple Silicon optimization opportunities in Cyberpunk 2077's ray tracing implementation.*

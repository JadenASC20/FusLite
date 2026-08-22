# FusLite - Physically Based Vulkan Renderer

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![API](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Language](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)](#supported-platforms)

## What is it?

FusLite is a physically based real-time renderer built from scratch in **Vulkan 1.3**, with no engine or rendering framework underneath. It implements a modern Forward+ pipeline as a study in low-level GPU architecture and real-time rendering technique. Developed and profiled on an **NVIDIA RTX 2070 SUPER**.

<img width="946" height="533" alt="FusLite hero render" src="https://github.com/user-attachments/assets/d0628ef4-7f10-4500-a8c9-583695c45f84" />

**Features**

* Physically Based Shading (Cook-Torrance GGX, metallic/roughness, layered automotive clearcoat)
* Image-Based Lighting (irradiance, prefiltered specular, BRDF LUT)
* Clustered Forward+ Lighting (GPU compute light culling, 4.5 ms -> 1.2 ms at 128 lights)
* Hi-Z Screen-Space Reflections (compute-built depth pyramid, hierarchical march, IBL fallback)
* PCSS Soft Shadows (Poisson-disk blocker search + PCF)
* Stylized Shadow Penumbra authoring (color ramps, banding, pattern modes)
* Temporal Antialiasing (Halton jitter, motion vectors, YCoCg variance clipping)
* Screen-Space Ambient Occlusion (hemisphere kernel, depth-aware blur)
* Configurable Tonemapping (Reinhard, ACES, AgX, GT7) with async auto-exposure
* Deferred G-buffer with live per-pass debug views

**Supported Rendering Backends**

* Vulkan 1.3 (dynamic rendering, no VkRenderPass)

**Supported Platforms**

* Windows 10 / 11

**Supported Compilers**

* Visual Studio 2022 (C++20)

## Performance

Profiled with **Nsight GPU Trace**. The frame runs **97.7% GPU-active** with minimal idle. The dominant scene-shading pass is **L1TEX/L2 throughput-bound (88.5% / 66.6%), not ALU-bound (SM 30.5%)** - consistent with the texture-heavy PBR, IBL, and 56-tap PCSS sampling, which points the next optimization at texture bandwidth rather than shader math.

## Installation

> Prerequisites: the [Vulkan SDK](https://vulkan.lunarg.com/) (1.3+), CMake 3.20+, and a C++20 compiler.

```
git clone https://github.com/JadenASC20/FusLite.git
cd FusLite
cmake -B build -G Ninja
cmake --build build
```

Run the resulting executable from the build directory. Shaders are compiled to SPIR-V as part of the build.

## Tech Stack

| Area | Details |
|------|---------|
| API | Vulkan 1.3 (dynamic rendering) |
| Loader / allocation | volk, Vulkan Memory Allocator (VMA) |
| Math | GLM |
| Asset import | cgltf (hand-written glTF 2.0 loader) |
| Windowing | GLFW |
| UI | Dear ImGui (v1.91.9, pinned) + ImGuizmo |
| Build | CMake, Ninja, Visual Studio 2022 |
| Shaders | GLSL -> SPIR-V |
| Profiling | Nsight Graphics, RenderDoc |

## Media

<img width="959" height="566" alt="fs3" src="https://github.com/user-attachments/assets/00fe3cd4-dc39-4529-b9ef-5c3234c918e5" />
<img width="957" height="563" alt="fs4" src="https://github.com/user-attachments/assets/a8d7a990-fe89-4ff2-935d-ad6348117f23" />
<img width="957" height="620" alt="fs5" src="https://github.com/user-attachments/assets/cdce260b-bbfb-48be-be2c-b2dbc907c9e2" />


## References

* Beug, A. P. (2020). *Screen Space Reflection Techniques.* MSc thesis, University of Regina. (Benchmarks five traversal schemes; Min-Max Hi-Z was the basis for FusLite's SSR marcher.)
* Yang, L., Liu, S. & Salvi, M. (2020). *A Survey of Temporal Antialiasing Techniques.* Computer Graphics Forum.
* Polyphony Digital (2025). *GT7 Tonemapper.* SIGGRAPH reference implementation (MIT-licensed).

## Credits

* GT7 tonemapper adapted from Polyphony Digital's SIGGRAPH 2025 reference implementation (MIT-licensed).
* HDRI environment [AutoShop](https://polyhaven.com/a/autoshop_01) and [Leibstadt](https://polyhaven.com/a/leibstadt) from Polyhaven.
* Car Model Asset [McLaren 600LT](https://sketchfab.com/3d-models/mclaren-600lt-23d87647c2b840d99ff06215613e8788) from Sketchfab.
* ShaderBall from Autodesk Maya Hypershade

---

Built by **Jaden Halevi** - [portfolio](https://jadenhalevi.design) · [LinkedIn](https://linkedin.com/in/jaden-halevi)

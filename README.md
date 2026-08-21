# FusLite

A physically based real-time renderer built from scratch in **Vulkan 1.3**, with no engine or rendering framework underneath. FusLite implements a modern forward+ rendering pipeline: PBR, image based lighting, clustered light culling, cascaded shadows, and temporal antialiasing, as a study in low-level GPU architecture and real-time rendering technique.

Developed and tested on an **NVIDIA RTX 2070 SUPER**.

<img width="946" height="533" alt="fsl_2" src="https://github.com/user-attachments/assets/7166a613-a1e9-409d-bbcd-146b6e62fcd8" />


---

## Highlights

- **Vulkan 1.3 renderer** — swapchain, dynamic rendering, render passes, synchronization, and descriptor management written by hand, no engine.
- **Physically based shading** — Cook-Torrance GGX with metallic/roughness workflow, plus a layered automotive clearcoat model.
- **Full image-based lighting pipeline** — irradiance convolution, prefiltered specular with mip-chained roughness, and a BRDF integration LUT.
- **Clustered Forward+ lighting** — GPU compute light culling across a 3D cluster grid, measured against brute-force at high light counts.
- **Cascaded shadow maps** - with PCSS soft shadows and Poisson-disk sampling. Custom Shadow Penumbra feature for stylistic renders.
- **Temporal antialiasing** — Halton jitter, motion-vector reprojection, YCoCg variance clipping, and a disocclusion-confidence heuristic to suppress ghosting.
- **Live debug tooling** — Dear ImGui + ImGuizmo, with a multi-window inspector for tuning the pipeline at runtime.

---

## Tech Stack

| Area | Details |
|------|---------|
| **API** | Vulkan 1.3 (dynamic rendering) |
| **Loader / allocation** | volk, Vulkan Memory Allocator (VMA) |
| **Math** | GLM |
| **Asset import** | Assimp |
| **Windowing** | GLFW |
| **UI** | Dear ImGui (v1.91.9, pinned) + ImGuizmo |
| **Build** | CMake, Ninja, Visual Studio 2022 |
| **Shaders** | GLSL -> SPIR-V |
| **Reference GPU** | NVIDIA RTX 2070 SUPER |

---

## Building

> Prerequisites: the [Vulkan SDK](https://vulkan.lunarg.com/) (1.3+), CMake 3.20+, and a C++20 compiler. Dependencies are fetched/configured through CMake.

```bash
git clone https://github.com/JadenASC20/FusLite.git
cd FusLite
cmake -B build -G Ninja
cmake --build build
```

Run the resulting executable from the build directory. Shaders are compiled to SPIR-V as part of the build.

## Footer

Built by **Jaden Halevi** — [portfolio](https://jadenhalevi.design) · [LinkedIn](https://linkedin.com/in/jaden-halevi)

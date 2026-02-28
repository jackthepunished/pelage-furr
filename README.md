<div align="center">

# 🐺 Pelage Fur Renderer

**A high-performance, layered shell and fin billboard fur rendering system for Direct3D 12.**

[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)
[![DirectX 12](https://img.shields.io/badge/API-DirectX%2012-green.svg)](https://devblogs.microsoft.com/directx/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

</div>

<br>

Pelage (*French for "fur" or "coat"*) is a standalone rendering prototype demonstrating the classic "Shadow of the Colossus" fur technique running on modern Direct3D 12. It utilizes hardware instancing, geometry shader silhouette detection, and opacity shadow maps to achieve fluffy, stylized fur capable of dynamic wind and gravity reactions at 60fps on mid-range hardware.

---

## ✨ Features

- **Layered Shell Texturing**: 32 instanced shells pushed out along vertex normals in a single draw call.
- **Fin Billboards**: Geometry shader-driven adjacency silhouettes to hide grazing-angle stair-stepping artifacts.
- **Cellular Alpha Discard**: Voronoi noise sampling for thick, tapering root-to-tip strand geometry.
- **Physics Simulation**:
  - **Quadratic Gravity Droop**: $t^2$ stiffness weighting creates realistic cantilever-style hair bending.
  - **Animated Wind**: Multi-frequency harmonic sine waves combined with world-space phase offsets to eliminate mechanical looping.
  - **Length Preservation**: Vector normalization ensures fur strands arc rather than stretch artificially.
- **Opacity Shadow Maps (OSM)**: 4-layer MRT additive blending setup preparing the ground for deep Beer's Law self-shadowing.

## 🛠 Architecture & Pipeline

The pipeline is strictly ordered using D3D12 resource barriers to prevent GPU race conditions and maximize throughput.

### Render Loop
1. **Pass 1 (OSM Shadows):** Render 32 instanced shells from the light's perspective into 4 separate `R8` RenderTargets using additive blending.
2. **Pass 2 (Base Mesh):** Render the underlying opaque creature/animal skin.
3. **Pass 3 (Fins):** Render silhouette extrusions using the Geometry Shader with `triangleadj` topology.
4. **Pass 4 (Shells):** Render 32 instanced fur layers over the top.

### D3D12 Root Signature
A unified root signature is shared across all pipeline states:
- `b0`: Frame / Camera / Wind CBV
- `b1`: Fur Parameters CBV
- `t0`: Voronoi Noise SRV
- `t1-t4`: OSM Shadow Map SRVs
- `s0`: Static Linear Wrap Sampler

## 🚀 Getting Started

### Prerequisites
- Windows 10/11
- Visual Studio 2022 (or build tools)
- CMake 3.20+
- Windows SDK (10.0.22621.0 or newer)

### Building the Project

```bash
git clone https://github.com/jackthepunished/pelage-furr.git
cd pelage-furr
mkdir build
cd build
cmake ..
cmake --build .
```
Run `Debug\PelageFur.exe` or open the generated `PelageFur.sln` in Visual Studio.

## 🎛️ Tuning Parameters

All fur logic is exposed via the `FurCB` constant buffer. You can hook these up to a UI like ImGui for real-time tweaking:

| Parameter | Recommended | Description |
| :--- | :--- | :--- |
| `ShellCount` | `32` | Number of instanced layers. Higher = smoother angles, lower = visible stair-stepping. |
| `FurLength` | `0.15f` | World-space extrusion length. Above 0.3f, 32 shells may start visibly detaching. |
| `Density` | `15.0f` | UV multiplier for Voronoi noise. Higher = thinner, tighter strands. |
| `Thickness` | `0.8f` | Noise subtraction multiplier. `1.0` = needle tips, `0.5` = thick clumpy tips. |
| `Gravity` | `(0, -2.5, 0)` | Adds the characteristic heavy arc to strands. |
| `WindStrength`| `0.2f` | Amplitude of the sine wave. Too high breaks the root attachment illusion. |

## ⚠️ D3D12 Gotchas (and how they were solved)

1. **Geometry Shader Adjacency Mismatch**: D3D12 validation will crash if you bind a GS expecting `triangleadj` but issue a standard `TRIANGLELIST`. The `FurRenderer` swaps `IASetPrimitiveTopology` appropriately before every draw call.
2. **Z-Fighting on Roots**: Floating-point inaccuracies between the VS and GS extrusions cause Z-fighting. Fixed by applying a `-0.001f` bias to the Fin GS base vertices so they sink slightly beneath the shells.
3. **OSM Additive Saturation**: 32 shells overlapping with `BLEND_ONE` rapidly saturates the RenderTargets to 1.0 (white-out). Solved by scaling down output opacity in `osm_ps.hlsl` by `(1.0f / ShellCount)`.

## 📜 License

This project is licensed under the MIT License - see the LICENSE file for details.

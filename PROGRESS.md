# Pelage Fur Renderer: Progress & Roadmap

## 1. What We Have Done So Far
*   **Engine Stabilization:** Debugged and fixed initial DirectX 12 initialization crashes caused by uninitialized pipeline states and missing descriptor table bindings.
*   **Core Shading Upgrade (Kajiya-Kay):** Replaced basic Lambertian/Phong lighting with the Kajiya-Kay mathematical hair model. This physically-based approximation gives fur strands accurate primary and secondary cone-based specular highlights.
*   **Edge Smoothing (A2C & MSAA):** Enabled Alpha-to-Coverage and 4x MSAA, resolving them to the backbuffer to eliminate the harsh, jagged "stair-step" aliasing caused by alpha discarding (`clip()`) on the fur shells.
*   **Deep Self-Shadowing (OSM):** Implemented Opacity Shadow Maps. We added an initial rendering pass that slices the mesh from the light's perspective into 4 RenderTargets, and updated the main pixel shader to use Beer's Law to calculate volumetric shadow attenuation deep within the fur.
*   **Custom Asset Integration:** Integrated the lightweight `tinygltf` header library to parse and load a massive custom `.gltf`/`.bin` "fur carpet" asset, replacing the basic procedural sphere.
*   **Geometry Optimization:** Built a decimation algorithm into the glTF loader to safely downsample the 4-million triangle asset on load, preventing GPU timeouts (TDR) while maintaining high frame rates. Corrected the mesh's physical scale and rotation to display properly in the viewport.

## 2. What We Will Be Doing Next
To push the graphics from a basic prototype to AAA-level realism, we will implement the following features:
*   **High-Resolution Cellular Noise:** Replace the placeholder 2x2 noise texture with a procedurally generated, tiling 512x512 Voronoi (cellular) noise texture generated on the CPU. This will create fine, distinct, tight individual hairs.
*   **Fur Parameter Tuning:** Adjust the physical properties in the constant buffer specifically for a carpet (massively increased strand density, shorter length, and realistic color values).
*   **Jittered Strand Directions (Frizz):** Modify the vertex shader to introduce a math-based hash function that adds a slight random angular "jitter" to the extrusion vector, breaking up the uniform, perfectly-straight plastic look.
*   **Advanced Lighting (Rim & SSS Fake):** Enhance the pixel shader with a Fresnel Rim Light term to catch grazing angles, and apply an exponential curve to the root ambient occlusion to simulate deep subsurface scattering.

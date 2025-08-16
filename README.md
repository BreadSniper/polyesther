# Description

Simple obj model viewer. Renders obj models with software rasterizer or DX12 renderer.

## Build steps Windows

1. Clone the repository with submodules.
1. Install [VisualStudio](https://visualstudio.microsoft.com/downloads/) with MSVC Platform Toolset v143.
1. Run build.bat

## TODO

1. Add model load with imgui.
1. Change camera controls to resemble those of windows 3D viewer (rotating around the model with mouse)
1. Add lighting, render feature, switching renderer controls to imgui.
1. Minimize code duplication in DX12 renderer.
1. Add SSAO to DX12 renderer.
1. Add SSR to DX12 renderer.
1. Fix backface culling in software renderer.
1. Refactoring: fix todos in code.
1. Add more render features to DX12 implementation for education purposes.
1. Fix window resize issues.
1. Speed up software rasterizer. 
1. Render imgui and image from renderer on separate threads.
1. Add raytracer.
1. Use dropdown to choose current renderer.
1. Fix hardware rasterizer not drawing during window drag.
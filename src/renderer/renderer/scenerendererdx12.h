#pragma once

#include <renderer/scenerenderer.h>
#include <string>
#include <memory>

#pragma comment(lib, "D3d12.lib")
#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace Renderer
{
    struct RendererDX12Context;

    struct SceneRendererDX12 : public SceneRenderer
    {
        DELETE_CTORS(SceneRendererDX12);
        SceneRendererDX12(const std::string& path, DeviceDX12& device) : deviceDX12(device), shaderFolderPath(path) {};
        ~SceneRendererDX12();

        bool Render(const Scene& scene, Texture& texture) override;

    private:
        std::shared_ptr<RendererDX12Context> context;
        std::string shaderFolderPath;
        DeviceDX12& deviceDX12;
    };
}
#pragma once

#include <renderer/devicedx12.h>
#include <renderer/texture.h>

#include <memory>

#include <imgui.h>
#include <functional>
#include <dxgi1_4.h>

namespace Renderer
{
    struct ImguiRendererContext;

    struct ImguiRenderer
    {
        DELETE_CTORS(ImguiRenderer);
        ImguiRenderer(DeviceDX12& device, uint32_t gameWidth, uint32_t gameHeight, HWND window);
        ~ImguiRenderer();

        void Render(const Texture& texture, const std::function<void(ImTextureID)>& func);

    private:
        D3D12_GPU_DESCRIPTOR_HANDLE UploadTexturesToGPU(const Texture& texture); //todo.pavelza: code duplication with scenerendererdx12.cpp
        std::shared_ptr<ImguiRendererContext> context;
        DeviceDX12& deviceDX12;
    };
}
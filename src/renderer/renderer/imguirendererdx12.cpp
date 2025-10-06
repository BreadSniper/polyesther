#include <renderer/imguirendererdx12.h>
#include <utils.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

namespace Renderer
{
    struct ImguiRendererContext
    {
        ComPtr<ID3D12Resource> mainRenderTargetResource[2] = {};
        D3D12_CPU_DESCRIPTOR_HANDLE mainRenderTargetDescriptor[2] = {};

        ComPtr<IDXGISwapChain3> swapChain;
        ComPtr<ID3D12DescriptorHeap> rootDescriptorHeap;

        ComPtr<ID3D12DescriptorHeap> backBufferDescHeap;
    };

    ImguiRenderer::ImguiRenderer(DeviceDX12& device, uint32_t gameWidth, uint32_t gameHeight, HWND window)
        : deviceDX12(device)
    {
        context = std::make_shared<ImguiRendererContext>();

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 2;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        D3D_NOT_FAILED(device.GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&context->rootDescriptorHeap)));
        context->rootDescriptorHeap->SetName(L"Imgui Root Descriptor Heap.");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

        // Zero index is saved for fonts.
        ImGui_ImplDX12_Init(device.GetDevice(), 1,
            DXGI_FORMAT_R8G8B8A8_UNORM, context->rootDescriptorHeap.Get(),
            context->rootDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            context->rootDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

        DXGI_SWAP_CHAIN_DESC swapChainDescription;

        swapChainDescription.BufferDesc.Width = gameWidth;
        swapChainDescription.BufferDesc.Height = gameHeight;
        swapChainDescription.BufferDesc.RefreshRate.Numerator = 60;
        swapChainDescription.BufferDesc.RefreshRate.Denominator = 1;
        swapChainDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDescription.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        swapChainDescription.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

        swapChainDescription.SampleDesc.Count = 1;
        swapChainDescription.SampleDesc.Quality = 0;

        swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescription.BufferCount = 2;

        swapChainDescription.OutputWindow = window;
        swapChainDescription.Windowed = true;

        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDescription.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        ComPtr<IDXGIFactory4> factory;
        D3D_NOT_FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory)));

        ComPtr<IDXGISwapChain> tempSwapChain;
        D3D_NOT_FAILED(factory->CreateSwapChain(device.GetQueue().GetQueue(), &swapChainDescription, &tempSwapChain));

        tempSwapChain->QueryInterface<IDXGISwapChain3>(&context->swapChain);

        D3D12_DESCRIPTOR_HEAP_DESC backBufferDescHeapDesc = {};
        backBufferDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        backBufferDescHeapDesc.NumDescriptors = 2;
        backBufferDescHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        backBufferDescHeapDesc.NodeMask = 1;
        D3D_NOT_FAILED(deviceDX12.GetDevice()->CreateDescriptorHeap(&backBufferDescHeapDesc, IID_PPV_ARGS(&context->backBufferDescHeap)));

        SIZE_T rtvDescriptorSize = deviceDX12.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = context->backBufferDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < 2; i++)
        {
            context->mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }

        for (UINT i = 0; i < 2; i++)
        {
            ComPtr<ID3D12Resource> pBackBuffer;
            context->swapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
            deviceDX12.GetDevice()->CreateRenderTargetView(pBackBuffer.Get(), nullptr, context->mainRenderTargetDescriptor[i]);
            context->mainRenderTargetResource[i] = pBackBuffer;
            pBackBuffer->SetName(L"Imgui Main Render Target Resource.");
        }
    }

    void ImguiRenderer::Render(const Texture& texture, const std::function<void(ImTextureID)>& func)
    {
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport();

        // First index is for FinalImage texture
        deviceDX12.PutSRVIntoDescriptorHeap(deviceDX12.UploadTextureToGPU("FinalImage", texture), 1, context->rootDescriptorHeap.Get());
        D3D12_GPU_DESCRIPTOR_HANDLE handle = deviceDX12.GetSRVDescriptorHandle<D3D12_GPU_DESCRIPTOR_HANDLE>(1, context->rootDescriptorHeap.Get());

        func((ImTextureID)handle.ptr);

        ImGui::Render();

        UINT backBufferIdx = context->swapChain->GetCurrentBackBufferIndex();

        deviceDX12.GetList().AddBarrier(context->mainRenderTargetResource[backBufferIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        // Render Dear ImGui graphics
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        deviceDX12.GetList().GetList()->ClearRenderTargetView(context->mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
        deviceDX12.GetList().GetList()->OMSetRenderTargets(1, &context->mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
        deviceDX12.GetList().GetList()->SetDescriptorHeaps(1, context->rootDescriptorHeap.GetAddressOf());
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), deviceDX12.GetList().GetList());

        deviceDX12.GetList().AddBarrier(context->mainRenderTargetResource[backBufferIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        deviceDX12.GetQueue().Execute(deviceDX12.GetList());

        context->swapChain->Present(1, 0);
    }

    ImguiRenderer::~ImguiRenderer()
    {
        deviceDX12.GetQueue().WaitForCommandListCompletion();
        ImGui_ImplDX12_Shutdown();
    }
}
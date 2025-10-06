#pragma once

#include <wrl/client.h>
#include <d3d12.h>
#include <memory>
#include <vector>
#include <utils.h>

namespace Renderer
{
    struct Texture;

    struct CommandList
    {
        DELETE_CTORS(CommandList);
        CommandList(ID3D12Device* device);

        void AddBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);

        void SetCurrentPipelineStateObject(ID3D12PipelineState* pso);

        ID3D12GraphicsCommandList* GetList();

        void Reset();

    private:
        ID3D12PipelineState* currentPSO = nullptr; // not owned
    
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    };

    struct GraphicsQueue
    {
        DELETE_CTORS(GraphicsQueue);
        GraphicsQueue(ID3D12Device* device);
        ~GraphicsQueue();

        ID3D12CommandQueue* GetQueue();

        void Execute(CommandList& list);
        void WaitForCommandListCompletion();

    private:
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;

        UINT64 currentFenceValue = 0;
        HANDLE fenceEventHandle = 0;
    };

    struct DeviceDX12
    {
        enum class Mode
        {
            Default,
            UseSoftwareRasterizer
        };

        DELETE_CTORS(DeviceDX12);
        DeviceDX12(Mode mode = Mode::Default);

        GraphicsQueue& GetQueue() const;
        CommandList& GetList() const;
        ID3D12Device* GetDevice() const;

        ID3D12Resource* UploadTextureToGPU(const std::string& id, const Texture& texture);

        template<typename T>
        T GetSRVDescriptorHandle(size_t index, ID3D12DescriptorHeap* srvDescriptorHeap)
        {
            return {};
        }

        template<>
        D3D12_CPU_DESCRIPTOR_HANDLE GetSRVDescriptorHandle(size_t index, ID3D12DescriptorHeap* srvDescriptorHeap)
        {
            return { UINT64(
                INT64(srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr) + 
                INT64(GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * index)
            )};
        }

        template<>
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVDescriptorHandle(size_t index, ID3D12DescriptorHeap* srvDescriptorHeap)
        {
            return { UINT64(
                INT64(srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr) + 
                INT64(GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * index)
            )};
        }

        void PutSRVIntoDescriptorHeap(ID3D12Resource* resource, size_t index, ID3D12DescriptorHeap* srvDescriptorHeap)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // todo.pavelza: put the parameter inside
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            GetDevice()->CreateShaderResourceView(resource, &srvDesc, GetSRVDescriptorHandle<D3D12_CPU_DESCRIPTOR_HANDLE>(index, srvDescriptorHeap));
        }

    private:
        std::unique_ptr<GraphicsQueue> graphicsQueue;
        std::unique_ptr<CommandList> commandList;
        Microsoft::WRL::ComPtr<ID3D12Device> device;

        std::map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> textureResources;
        // todo.pavelza: re-make to take less space, since they are never used together at the same time
        std::map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> textureUploadBuffers;
    };

    struct RenderTarget
    {
        enum Type
        {
            GBuffer,
            FinalImage
        };

        DELETE_CTORS(RenderTarget);
        RenderTarget(DeviceDX12& device, ID3D12DescriptorHeap* srvDescriptorHeap, size_t width, size_t height, Type type);
        ~RenderTarget() = default;

        void ClearAndSetRenderTargets(CommandList& list);

        ID3D12Resource* GetBuffer(size_t i);

    private:
        void CreateDepthBuffer(size_t width, size_t height);
        void CreateBuffer(size_t i, size_t width, size_t height, ID3D12DescriptorHeap* srvDescriptorHeap);

        D3D12_CLEAR_VALUE CreateClearValue(D3D12_RESOURCE_DESC textureDescription);
        D3D12_RESOURCE_DESC CreateTextureDescription(DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

        DeviceDX12& deviceDX12;

        Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> depthStencilViewDescriptorHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE depthBufferHandle{};

        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> renderTargets;
        std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> rtvDescriptorHeaps;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvHandles;

        FLOAT clearColor[4] = { 0.0f, 0.f, 0.f, 1.000000000f };
        Type bufferType;
        size_t targetWidth;
        size_t targetHeight;
    };
}
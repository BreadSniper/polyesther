#include <renderer/devicedx12.h>
#include <utils.h>
#include <sstream>
#include <string>

#include <dxgi1_4.h>

namespace Renderer
{
    CommandList::CommandList(ID3D12Device* device)
    {
        D3D_NOT_FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        D3D_NOT_FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

        commandList->SetName(L"Main command list.");
    }

    void CommandList::SetCurrentPipelineStateObject(ID3D12PipelineState* pso)
    {
        if (pso != currentPSO)
        {
            currentPSO = pso;
            commandList->SetPipelineState(pso);
        }
    }

    void CommandList::AddBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
    {
        D3D12_RESOURCE_BARRIER barrier;
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barrier.Transition.StateBefore = from;
        barrier.Transition.StateAfter = to;
        barrier.Transition.pResource = resource;

        commandList->ResourceBarrier(1, &barrier);
    }

    ID3D12GraphicsCommandList* CommandList::GetList()
    {
        return commandList.Get();
    }

    void CommandList::Reset()
    {
        D3D_NOT_FAILED(allocator->Reset());
        D3D_NOT_FAILED(commandList->Reset(allocator.Get(), currentPSO));
    }

    GraphicsQueue::GraphicsQueue(ID3D12Device* device)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        D3D_NOT_FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        queue->SetName(L"Main command queue.");

        D3D_NOT_FAILED(device->CreateFence(currentFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        NOT_FAILED(fenceEventHandle = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS), 0);
    }

    GraphicsQueue::~GraphicsQueue()
    {
        CloseHandle(fenceEventHandle);
    }

    ID3D12CommandQueue* GraphicsQueue::GetQueue()
    {
        return queue.Get();
    }

    void GraphicsQueue::Execute(CommandList& list)
    {
        list.GetList()->Close();

        ID3D12CommandList* cmdsLists[1] = { list.GetList() };
        queue->ExecuteCommandLists(1, cmdsLists);

        WaitForCommandListCompletion();

        list.Reset();
    }

    void GraphicsQueue::WaitForCommandListCompletion()
    {
        currentFenceValue++;
        queue->Signal(fence.Get(), currentFenceValue);

        if (fence->GetCompletedValue() != currentFenceValue)
        {
            D3D_NOT_FAILED(fence->SetEventOnCompletion(currentFenceValue, fenceEventHandle));
            WaitForSingleObject(fenceEventHandle, INFINITE);
            ResetEvent(fenceEventHandle);
        }
    }

    DeviceDX12::DeviceDX12(Mode mode)
    {
        ComPtr<ID3D12Debug> debugController;
        D3D_NOT_FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();

        ComPtr<IDXGIFactory4> factory;
        D3D_NOT_FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        
        if (mode == Mode::UseSoftwareRasterizer)
        {
            ComPtr<IDXGIAdapter> warpAdapter;
            D3D_NOT_FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
            D3D_NOT_FAILED(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)));
        }
        else
        {
            D3D_NOT_FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)));
        }

        graphicsQueue = std::make_unique<GraphicsQueue>(device.Get());
        commandList = std::make_unique<CommandList>(device.Get());
    }

    ID3D12Resource* DeviceDX12::UploadTextureToGPU(const std::string& id, const Texture& texture)
    {
        // todo.pavelza: verify that we are uploading new texture or overwriting texture with the same format and size
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // todo.pavelza: move to texture?
        textureDesc.Width = texture.GetWidth();
        textureDesc.Height = static_cast<UINT>(texture.GetHeight());
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
        GetDevice()->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

        if (textureUploadBuffers[id] == nullptr)
        {
            // create upload buffer for texture id
            D3D12_RESOURCE_DESC uploadBufferDescription;
            uploadBufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadBufferDescription.Alignment = 0;
            uploadBufferDescription.Width = totalBytes;
            uploadBufferDescription.Height = 1;
            uploadBufferDescription.DepthOrArraySize = 1;
            uploadBufferDescription.MipLevels = 1;
            uploadBufferDescription.Format = DXGI_FORMAT_UNKNOWN;
            uploadBufferDescription.SampleDesc.Count = 1;
            uploadBufferDescription.SampleDesc.Quality = 0;
            uploadBufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            uploadBufferDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

            D3D12_HEAP_PROPERTIES uploadProperties = GetDevice()->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);
            D3D_NOT_FAILED(GetDevice()->CreateCommittedResource(&uploadProperties, D3D12_HEAP_FLAG_NONE, &uploadBufferDescription, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&textureUploadBuffers[id])));

            std::wstringstream uploadBufferName;
            uploadBufferName << L"Texture upload buffer for: " << std::wstring(id.begin(), id.end());
            textureUploadBuffers[id]->SetName(uploadBufferName.str().c_str());

            GetList().AddBarrier(textureUploadBuffers[id].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_GENERIC_READ);
        }

        BYTE* mappedData = nullptr;
        textureUploadBuffers[id]->Map(0, nullptr, (void**)&mappedData);

        for (size_t i = 0; i < numRows; i++)
        {
            memcpy(mappedData + footprint.Footprint.RowPitch * i, texture.GetBuffer() + rowSizeInBytes * i, rowSizeInBytes);
        }
        textureUploadBuffers[id]->Unmap(0, nullptr);

        if (textureResources[id] == nullptr)
        {
            // create texture with id
            D3D12_HEAP_PROPERTIES defaultProperties = GetDevice()->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_DEFAULT);
            D3D_NOT_FAILED(GetDevice()->CreateCommittedResource(&defaultProperties, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&textureResources[id])));

            std::wstringstream textureName;
            textureName << L"Texture resource with id: " << std::wstring(id.begin(), id.end());
            textureResources[id]->SetName(textureName.str().c_str());
        }

        GetList().AddBarrier(textureResources[id].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION dest;
        dest.pResource = textureResources[id].Get();
        dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dest.PlacedFootprint = {};
        dest.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src;
        src.pResource = textureUploadBuffers[id].Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        GetList().GetList()->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
        GetList().AddBarrier(textureResources[id].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        
        GetQueue().Execute(GetList());

        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;     

        return textureResources[id].Get();
    }

    CommandList& DeviceDX12::GetList() const
    {
        return *commandList;
    }

    GraphicsQueue& DeviceDX12::GetQueue() const
    {
        return *graphicsQueue;
    }

    ID3D12Device* DeviceDX12::GetDevice() const
    {
        return device.Get();
    }

    RenderTarget::RenderTarget(DeviceDX12& device, ID3D12DescriptorHeap* srvDescriptorHeap, size_t width, size_t height, Type type)
        : deviceDX12(device)
        , bufferType(type)
        , targetWidth(width)
        , targetHeight(height)
    {
        size_t numBuffers = bufferType == Type::GBuffer ? 3 : 1;
        renderTargets.resize(numBuffers);
        rtvDescriptorHeaps.resize(numBuffers);
        rtvHandles.resize(numBuffers);
        srvHandles.resize(numBuffers);

        for (size_t i = 0; i < numBuffers; i++)
        {
            CreateBuffer(i, width, height, srvDescriptorHeap);
        }
        CreateDepthBuffer(width, height);
    }

    void RenderTarget::ClearAndSetRenderTargets(CommandList& list)
    {
        for (size_t i = 0; i < rtvHandles.size(); i++)
        {
            list.GetList()->ClearRenderTargetView(rtvHandles[i], clearColor, 0, nullptr);
        }
        list.GetList()->ClearDepthStencilView(depthBufferHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        list.GetList()->OMSetRenderTargets((UINT)rtvHandles.size(), rtvHandles.data(), false, &depthBufferHandle);
    }

    ID3D12Resource* RenderTarget::GetBuffer(size_t i)
    {
        return renderTargets[i].Get();
    }

    void RenderTarget::CreateBuffer(size_t i, size_t width, size_t height, ID3D12DescriptorHeap* srvDescriptorHeap)
    {
        ASSERT(rtvDescriptorHeaps.size() >= i);
        ASSERT(renderTargets.size() >= 0);
        ASSERT(rtvHandles.size() >= 0);

        D3D12_DESCRIPTOR_HEAP_DESC heapDescription;
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDescription.NumDescriptors = 1;
        heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDescription.NodeMask = 0;

        D3D_NOT_FAILED(deviceDX12.GetDevice()->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&rtvDescriptorHeaps[i])));

        DXGI_FORMAT format = bufferType == GBuffer ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_RESOURCE_DESC textureDesc = CreateTextureDescription(format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clearValue = CreateClearValue(textureDesc);
        D3D12_HEAP_PROPERTIES defaultProperties = deviceDX12.GetDevice()->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_DEFAULT);

        D3D_NOT_FAILED(deviceDX12.GetDevice()->CreateCommittedResource(&defaultProperties, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&renderTargets[i])));
        
        if (bufferType == FinalImage)
        {
            renderTargets[i]->SetName(L"Final image.");
        }
        else
        {
            renderTargets[i]->SetName((L"GBuffer: " + std::to_wstring(i)).c_str());
        }

        rtvHandles[i] = { SIZE_T(INT64(rtvDescriptorHeaps[i]->GetCPUDescriptorHandleForHeapStart().ptr)) };
        deviceDX12.GetDevice()->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandles[i]);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        srvHandles[i] = { SIZE_T(INT64(srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr) + INT64(deviceDX12.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * (bufferType == FinalImage ? (i + 1) : (i + 2)))) }; // todo.pavelza: should use constants like NumberOfConstantStructs
        deviceDX12.GetDevice()->CreateShaderResourceView(renderTargets[i].Get(), &srvDesc, srvHandles[i]);
    }

    void RenderTarget::CreateDepthBuffer(size_t width, size_t height)
    {
        D3D12_RESOURCE_DESC depthBufferDescription = CreateTextureDescription(DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE clearValue = CreateClearValue(depthBufferDescription);
        D3D12_HEAP_PROPERTIES defaultHeapProperties = deviceDX12.GetDevice()->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_DEFAULT);

        D3D_NOT_FAILED(deviceDX12.GetDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &depthBufferDescription, D3D12_RESOURCE_STATE_COMMON, &clearValue, IID_PPV_ARGS(&depthStencilBuffer)));
        depthStencilBuffer->SetName(L"Depth stencil buffer.");

        D3D12_DESCRIPTOR_HEAP_DESC heapDescription;
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heapDescription.NumDescriptors = 1;
        heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDescription.NodeMask = 0;

        D3D_NOT_FAILED(deviceDX12.GetDevice()->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&depthStencilViewDescriptorHeap)));

        depthBufferHandle = D3D12_CPU_DESCRIPTOR_HANDLE(depthStencilViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        deviceDX12.GetDevice()->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, depthBufferHandle);

        deviceDX12.GetList().AddBarrier(depthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        deviceDX12.GetQueue().Execute(deviceDX12.GetList());
    }

    D3D12_CLEAR_VALUE RenderTarget::CreateClearValue(D3D12_RESOURCE_DESC textureDescription)
    {
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = textureDescription.Format;

        if (textureDescription.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        {
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
        }
        else
        {
            std::copy(clearColor, clearColor + 4, clearValue.Color);
        }

        return clearValue;
    }

    D3D12_RESOURCE_DESC RenderTarget::CreateTextureDescription(DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
    {
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.Format = format;
        textureDesc.Flags = flags;

        textureDesc.Alignment = 0;
        textureDesc.MipLevels = 1;
        textureDesc.Width = static_cast<UINT64>(targetWidth);
        textureDesc.Height = static_cast<UINT>(targetHeight);
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        return textureDesc;
    }
}

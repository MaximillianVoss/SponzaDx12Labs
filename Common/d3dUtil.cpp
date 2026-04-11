
#include "d3dUtil.h"
#include <comdef.h>
#include <fstream>

using Microsoft::WRL::ComPtr;

namespace
{
    namespace fs = std::filesystem;

    bool IsProjectRoot(const fs::path& candidate)
    {
        return fs::exists(candidate / "Assets")
            && fs::exists(candidate / "Shaders")
            && fs::exists(candidate / "SponzaDx12Labs.vcxproj");
    }

    fs::path FindRootFrom(fs::path start)
    {
        if(start.empty())
        {
            return {};
        }

        std::error_code errorCode;
        start = fs::weakly_canonical(start, errorCode);
        if(errorCode)
        {
            start = start.lexically_normal();
        }

        if(fs::is_regular_file(start, errorCode))
        {
            start = start.parent_path();
        }

        for(fs::path current = start; !current.empty(); current = current.parent_path())
        {
            if(IsProjectRoot(current))
            {
                return current;
            }

            if(current == current.root_path())
            {
                break;
            }
        }

        return {};
    }
}

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
    ErrorCode(hr),
    FunctionName(functionName),
    Filename(filename),
    LineNumber(lineNumber)
{
}

bool d3dUtil::IsKeyDown(int vkeyCode)
{
    return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}

std::filesystem::path d3dUtil::GetExecutableDirectory()
{
    std::array<wchar_t, 32768> buffer = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if(length == 0 || length == buffer.size())
    {
        throw std::runtime_error("Failed to query executable path.");
    }

    return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
}

std::filesystem::path d3dUtil::FindProjectRoot()
{
    static const std::filesystem::path projectRoot = []()
    {
        if(auto fromExecutable = FindRootFrom(GetExecutableDirectory()); !fromExecutable.empty())
        {
            return fromExecutable;
        }

        if(auto fromCurrentDir = FindRootFrom(std::filesystem::current_path()); !fromCurrentDir.empty())
        {
            return fromCurrentDir;
        }

        throw std::runtime_error("Failed to locate project root containing Assets and Shaders.");
    }();

    return projectRoot;
}

std::filesystem::path d3dUtil::ResolveProjectPath(const std::filesystem::path& path)
{
    if(path.is_absolute())
    {
        return path.lexically_normal();
    }

    return (FindProjectRoot() / path).lexically_normal();
}

void d3dUtil::EnsureProjectWorkingDirectory()
{
    const std::filesystem::path root = FindProjectRoot();
    SetCurrentDirectoryW(root.c_str());
}

ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    const std::filesystem::path resolvedPath = ResolveProjectPath(filename);
    std::ifstream fin(resolvedPath, std::ios::binary);
    if(!fin)
    {
        throw std::runtime_error("Failed to open binary file: " + resolvedPath.string());
    }

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

    fin.read((char*)blob->GetBufferPointer(), size);
    fin.close();

    return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;

    // Create the actual default buffer resource.
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    auto bufferSize = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferSize,
		D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    // In order to copy CPU memory data into our default buffer, we need to create
    // an intermediate upload heap. 
	heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	bufferSize = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
		D3D12_HEAP_FLAG_NONE,
        &bufferSize,
		D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


    // Describe the data we want to copy into the default buffer.
    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = subResourceData.RowPitch;

    // Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
    // will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
    // the intermediate upload heap data will be copied to mBuffer.
    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &transition);
    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
    transition = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &transition);

    // Note: uploadBuffer has to be kept alive after the above function calls because
    // the command list has not been executed yet that performs the actual copy.
    // The caller can Release the uploadBuffer after it knows the copy has been executed.


    return defaultBuffer;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
    const std::filesystem::path resolvedPath = ResolveProjectPath(filename);
	hr = D3DCompileFromFile(resolvedPath.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if(errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}

std::wstring DxException::ToString()const
{
    // Get the string description of the error code.
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}



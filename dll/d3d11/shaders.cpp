/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader objects of every stage and the input layout built against a vertex shader
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shaders.hpp"

#include <array>

constexpr UINT RxInputSlotCount = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;

/*
 * The entries as the driver gets them, for one output stream. The stream byte only
 * means something to the runtime, which uses it to pick the entries.
 */
static
std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2>
RxDriverSignature(
    _In_ const RxShaderSignature& Signature,
    _In_ UINT Stream = 0)
{
    std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2> Entries;

    for (const RxSignatureElement& Element : Signature.GetElements())
    {
        if (Element.Entry.Stream != Stream)
            continue;

        Entries.push_back(Element.Entry);
        Entries.back().Stream = 0;
    }

    return Entries;
}

/* An output named by a stream output entry, looked up in that entry's stream */
_Ret_maybenull_
static
const RxSignatureElement*
RxFindStreamOutput(
    _In_ const RxShaderSignature& Output,
    _In_ const D3D11_SO_DECLARATION_ENTRY& Entry)
{
    for (const RxSignatureElement& Element : Output.GetElements())
    {
        if (Element.Entry.Stream == Entry.Stream && Element.SemanticIndex == Entry.SemanticIndex &&
            _stricmp(Element.SemanticName.c_str(), Entry.SemanticName) == 0)
        {
            return &Element;
        }
    }

    return nullptr;
}

static
BOOLEAN
RxIsValidStreamOutput(
    _In_ const RxStreamOutputDesc& Desc)
{
    UINT Index;

    if (Desc.EntryCount == 0 || Desc.Entries == nullptr ||
        Desc.EntryCount > D3D11_SO_STREAM_COUNT * D3D11_SO_OUTPUT_COMPONENT_COUNT ||
        Desc.StrideCount > D3D11_SO_BUFFER_SLOT_COUNT || (Desc.StrideCount != 0 && Desc.Strides == nullptr))
    {
        return FALSE;
    }

    if (Desc.RasterizedStream >= D3D11_SO_STREAM_COUNT && Desc.RasterizedStream != D3D11_SO_NO_RASTERIZED_STREAM)
        return FALSE;

    for (Index = 0; Index < Desc.EntryCount; Index++)
    {
        const D3D11_SO_DECLARATION_ENTRY& Entry = Desc.Entries[Index];

        if (Entry.Stream >= D3D11_SO_STREAM_COUNT || Entry.OutputSlot >= D3D11_SO_BUFFER_SLOT_COUNT ||
            Entry.ComponentCount == 0)
        {
            return FALSE;
        }

        if (Entry.SemanticName != nullptr && Entry.StartComponent + Entry.ComponentCount > 4)
            return FALSE;
    }

    return TRUE;
}

template <typename Interface>
RxShader<Interface>::~RxShader()
{
    if (IsCreated)
        this->Device.GetDeviceFuncs().pfnDestroyShader(this->Device.GetDriverDevice(), Handle);
}

template <typename Interface>
HRESULT
RxShader<Interface>::Finish(
    _In_ HRESULT Result)
{
    if (FAILED(Result))
    {
        RX_TRACE(L"UMD shader create failed 0x%08lX", Result);
        return Result;
    }

    IsCreated = TRUE;
    return S_OK;
}

template <typename Interface>
HRESULT
RxShader<Interface>::CreateDriverShader(
    _In_opt_ const RxStreamOutputDesc* StreamOutput)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = this->Device.GetDeviceFuncs();
    D3D10DDI_HDEVICE DriverDevice = this->Device.GetDriverDevice();
    const UINT* Program = Bytecode.GetProgram();
    D3D10DDI_HRTSHADER RuntimeHandle;
    SIZE_T PrivateSize;

    RuntimeHandle.handle = static_cast<Interface*>(this);

    if constexpr (Stage::Form == RxSignatureForm::Tessellation)
    {
        Inputs = RxDriverSignature(Bytecode.GetInput());
        Outputs = RxDriverSignature(Bytecode.GetOutput());
        PatchConstants = RxDriverSignature(Bytecode.GetPatchConstant());
        TessellationSignatures.pInputSignature = Inputs.data();
        TessellationSignatures.NumInputSignatureEntries = (UINT)Inputs.size();
        TessellationSignatures.pOutputSignature = Outputs.data();
        TessellationSignatures.NumOutputSignatureEntries = (UINT)Outputs.size();
        TessellationSignatures.pPatchConstantSignature = PatchConstants.data();
        TessellationSignatures.NumPatchConstantSignatureEntries = (UINT)PatchConstants.size();

        PrivateSize = Funcs.pfnCalcPrivateTessellationShaderSize(DriverDevice, Program, &TessellationSignatures);
        Handle.pDrvPrivate = Memory.Allocate(PrivateSize);
        if (Handle.pDrvPrivate == nullptr)
            return E_OUTOFMEMORY;

        (Funcs.*Stage::Create)(DriverDevice, Program, Handle, RuntimeHandle, &TessellationSignatures);
        return Finish(this->Device.TakeDriverError());
    }
    else if constexpr (Stage::Form == RxSignatureForm::None)
    {
        PrivateSize = Funcs.pfnCalcPrivateShaderSize(DriverDevice, Program, &Signatures);
        Handle.pDrvPrivate = Memory.Allocate(PrivateSize);
        if (Handle.pDrvPrivate == nullptr)
            return E_OUTOFMEMORY;

        (Funcs.*Stage::Create)(DriverDevice, Program, Handle, RuntimeHandle);
        return Finish(this->Device.TakeDriverError());
    }
    else if constexpr (Stage::Form == RxSignatureForm::Geometry)
    {
        D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT Args = {};
        D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY Decl;
        D3D11_1DDIARG_STAGE_IO_SIGNATURES Unsized = {};
        UINT SlotDwords[D3D11_SO_BUFFER_SLOT_COUNT] = {};
        const RxSignatureElement* Match;
        BOOLEAN IsGeometry;
        UINT Remaining;
        UINT Chunk;
        UINT First;
        UINT Index;

        if (StreamOutput == nullptr || StreamOutput->EntryCount == 0)
        {
            /* Sized against the input and stream zero, created without them as the 11.1 path does */
            Inputs = RxDriverSignature(Bytecode.GetInput());
            Outputs = RxDriverSignature(Bytecode.GetOutput());
            Signatures.pInputSignature = Inputs.data();
            Signatures.NumInputSignatureEntries = (UINT)Inputs.size();
            Signatures.pOutputSignature = Outputs.data();
            Signatures.NumOutputSignatureEntries = (UINT)Outputs.size();

            PrivateSize = Funcs.pfnCalcPrivateShaderSize(DriverDevice, Program, &Signatures);
            Handle.pDrvPrivate = Memory.Allocate(PrivateSize);
            if (Handle.pDrvPrivate == nullptr)
                return E_OUTOFMEMORY;

            (Funcs.*Stage::Create)(DriverDevice, Program, Handle, RuntimeHandle, &Unsized);
            return Finish(this->Device.TakeDriverError());
        }

        for (Index = 0; Index < StreamOutput->EntryCount; Index++)
        {
            const D3D11_SO_DECLARATION_ENTRY& Entry = StreamOutput->Entries[Index];

            SlotDwords[Entry.OutputSlot] += Entry.ComponentCount;
            Decl.Stream = Entry.Stream;
            Decl.OutputSlot = Entry.OutputSlot;

            /* A nameless entry is a gap: skipped registers written in runs of up to four */
            if (Entry.SemanticName == nullptr)
            {
                for (Remaining = Entry.ComponentCount; Remaining != 0; Remaining -= Chunk)
                {
                    Chunk = (Remaining > 4) ? 4 : Remaining;
                    Decl.RegisterIndex = (UINT)-1;
                    Decl.RegisterMask = (BYTE)((1u << Chunk) - 1);
                    StreamDecls.push_back(Decl);
                }
                continue;
            }

            Match = RxFindStreamOutput(Bytecode.GetOutput(), Entry);
            if (Match == nullptr)
                continue;

            /* Components count from the first one the output register declares */
            for (First = 0; First < 3 && (Match->Entry.Mask & (1u << First)) == 0; First++);
            Decl.RegisterIndex = Match->Entry.Register;
            Decl.RegisterMask = (BYTE)(((1u << Entry.ComponentCount) - 1) << (Entry.StartComponent + First));
            StreamDecls.push_back(Decl);
        }

        /* No strides means each slot is packed as tightly as its entries */
        if (StreamOutput->StrideCount == 0)
        {
            for (Index = 0; Index < D3D11_SO_BUFFER_SLOT_COUNT; Index++)
                StreamStrides[Index] = SlotDwords[Index] * sizeof(UINT);
            Args.NumStrides = D3D11_SO_BUFFER_SLOT_COUNT;
        }
        else
        {
            for (Index = 0; Index < StreamOutput->StrideCount; Index++)
                StreamStrides[Index] = StreamOutput->Strides[Index];
            Args.NumStrides = StreamOutput->StrideCount;
        }

        /* Vertex shader bytecode brings no program here; its outputs pass straight through */
        IsGeometry = (Bytecode.GetProgramType() == D3D10_SB_GEOMETRY_SHADER);
        Inputs = IsGeometry ? RxDriverSignature(Bytecode.GetInput()) : RxDriverSignature(Bytecode.GetOutput());
        if (StreamOutput->RasterizedStream < D3D11_SO_STREAM_COUNT)
            Outputs = RxDriverSignature(Bytecode.GetOutput(), StreamOutput->RasterizedStream);
        Signatures.pInputSignature = Inputs.data();
        Signatures.NumInputSignatureEntries = (UINT)Inputs.size();
        Signatures.pOutputSignature = Outputs.data();
        Signatures.NumOutputSignatureEntries = (UINT)Outputs.size();

        Args.pShaderCode = IsGeometry ? Program : nullptr;
        Args.pOutputStreamDecl = StreamDecls.data();
        Args.NumEntries = (UINT)StreamDecls.size();
        Args.BufferStridesInBytes = StreamStrides;
        Args.RasterizedStream = StreamOutput->RasterizedStream;

        PrivateSize = Funcs.pfnCalcPrivateGeometryShaderWithStreamOutput(DriverDevice, &Args, &Signatures);
        Handle.pDrvPrivate = Memory.Allocate(PrivateSize);
        if (Handle.pDrvPrivate == nullptr)
            return E_OUTOFMEMORY;

        Funcs.pfnCreateGeometryShaderWithStreamOutput(DriverDevice, &Args, Handle, RuntimeHandle, &Signatures);
        return Finish(this->Device.TakeDriverError());
    }
    else
    {
        Inputs = RxDriverSignature(Bytecode.GetInput());
        Outputs = RxDriverSignature(Bytecode.GetOutput());
        Signatures.pInputSignature = Inputs.data();
        Signatures.NumInputSignatureEntries = (UINT)Inputs.size();
        Signatures.pOutputSignature = Outputs.data();
        Signatures.NumOutputSignatureEntries = (UINT)Outputs.size();

        PrivateSize = Funcs.pfnCalcPrivateShaderSize(DriverDevice, Program, &Signatures);
        Handle.pDrvPrivate = Memory.Allocate(PrivateSize);
        if (Handle.pDrvPrivate == nullptr)
            return E_OUTOFMEMORY;

        (Funcs.*Stage::Create)(DriverDevice, Program, Handle, RuntimeHandle, &Signatures);
        return Finish(this->Device.TakeDriverError());
    }
}

template <typename Interface>
HRESULT
RxShader<Interface>::Create(
    _In_ RxDevice& Device,
    _In_reads_bytes_(Size) const VOID* Bytecode,
    _In_ SIZE_T Size,
    _Outptr_ RxShader** Shader)
{
    RxShader* NewShader;
    HRESULT hr;

    *Shader = nullptr;

    NewShader = new (std::nothrow) RxShader(Device);
    if (NewShader == nullptr)
        return E_OUTOFMEMORY;

    /* The bytecode has to parse and has to be a program for this stage */
    hr = NewShader->Bytecode.Parse(Bytecode, Size);
    if (FAILED(hr) || NewShader->Bytecode.GetProgramType() != Stage::ProgramType)
    {
        NewShader->Release();
        return E_INVALIDARG;
    }

    hr = NewShader->CreateDriverShader(nullptr);
    if (FAILED(hr))
    {
        NewShader->Release();
        return hr;
    }

    *Shader = NewShader;
    return S_OK;
}

template <typename Interface>
HRESULT
RxShader<Interface>::CreateWithStreamOutput(
    _In_ RxDevice& Device,
    _In_reads_bytes_(Size) const VOID* Bytecode,
    _In_ SIZE_T Size,
    _In_ const RxStreamOutputDesc& StreamOutput,
    _Outptr_ RxShader** Shader)
{
    *Shader = nullptr;

    if constexpr (Stage::Form != RxSignatureForm::Geometry)
    {
        return E_NOTIMPL;
    }
    else
    {
        RxShader* NewShader;
        UINT ProgramType;
        UINT Index;
        HRESULT hr;

        if (!RxIsValidStreamOutput(StreamOutput))
            return E_INVALIDARG;

        NewShader = new (std::nothrow) RxShader(Device);
        if (NewShader == nullptr)
            return E_OUTOFMEMORY;

        hr = NewShader->Bytecode.Parse(Bytecode, Size);
        ProgramType = SUCCEEDED(hr) ? NewShader->Bytecode.GetProgramType() : 0;
        if (FAILED(hr) || (ProgramType != D3D10_SB_GEOMETRY_SHADER && ProgramType != D3D10_SB_VERTEX_SHADER))
        {
            NewShader->Release();
            return E_INVALIDARG;
        }

        /* Every named entry has to be something the shader writes to that stream */
        for (Index = 0; Index < StreamOutput.EntryCount; Index++)
        {
            if (StreamOutput.Entries[Index].SemanticName != nullptr &&
                RxFindStreamOutput(NewShader->Bytecode.GetOutput(), StreamOutput.Entries[Index]) == nullptr)
            {
                NewShader->Release();
                return E_INVALIDARG;
            }
        }

        hr = NewShader->CreateDriverShader(&StreamOutput);
        if (FAILED(hr))
        {
            NewShader->Release();
            return hr;
        }

        *Shader = NewShader;
        return S_OK;
    }
}

template <typename Interface>
HRESULT
STDMETHODCALLTYPE
RxShader<Interface>::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(Interface))
    {
        *ppvObject = static_cast<Interface*>(this);
        this->AddRef();
        return S_OK;
    }

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

template class RxShader<ID3D11VertexShader>;
template class RxShader<ID3D11PixelShader>;
template class RxShader<ID3D11GeometryShader>;
template class RxShader<ID3D11HullShader>;
template class RxShader<ID3D11DomainShader>;
template class RxShader<ID3D11ComputeShader>;

/* Input layout */

RxInputLayout::~RxInputLayout()
{
    if (IsCreated)
        Device.GetDeviceFuncs().pfnDestroyElementLayout(Device.GetDriverDevice(), Handle);
}

HRESULT
RxInputLayout::Create(
    _In_ RxDevice& Device,
    _In_reads_(ElementCount) const D3D11_INPUT_ELEMENT_DESC* Elements,
    _In_ UINT ElementCount,
    _In_reads_bytes_(Size) const VOID* Bytecode,
    _In_ SIZE_T Size,
    _Outptr_ RxInputLayout** Layout)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    std::vector<D3D10DDIARG_INPUT_ELEMENT_DESC> DdiElements;
    std::array<UINT, RxInputSlotCount> SlotEnd = {};
    D3D10DDIARG_CREATEELEMENTLAYOUT Args = {};
    D3D10DDI_HRTELEMENTLAYOUT RuntimeHandle;
    const RxSignatureElement* Match;
    D3D10DDIARG_INPUT_ELEMENT_DESC Ddi;
    RxShaderBytecode Shader;
    RxInputLayout* NewLayout;
    UINT Offset;
    UINT Index;
    UINT Bits;
    HRESULT hr;

    *Layout = nullptr;

    if (ElementCount > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT || (ElementCount != 0 && Elements == nullptr) ||
        FAILED(Shader.Parse(Bytecode, Size)))
    {
        return E_INVALIDARG;
    }

    for (Index = 0; Index < ElementCount; Index++)
    {
        const D3D11_INPUT_ELEMENT_DESC& Element = Elements[Index];

        if (Element.InputSlot >= RxInputSlotCount || Element.SemanticName == nullptr)
            return E_INVALIDARG;

        /* Appended elements start where the previous one in the same slot stopped */
        Bits = RxFormatBits(Element.Format);
        Offset = (Element.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT) ? SlotEnd[Element.InputSlot]
                                                                             : Element.AlignedByteOffset;
        SlotEnd[Element.InputSlot] = Offset + Bits / 8;

        /* Elements the shader never reads have no register to go to */
        Match = Shader.GetInput().Find(Element.SemanticName, Element.SemanticIndex);
        if (Match == nullptr)
            continue;

        Ddi.InputSlot = Element.InputSlot;
        Ddi.AlignedByteOffset = Offset;
        Ddi.Format = Element.Format;
        Ddi.InputSlotClass = (D3D10_DDI_INPUT_CLASSIFICATION)Element.InputSlotClass;
        Ddi.InstanceDataStepRate = Element.InstanceDataStepRate;
        Ddi.InputRegister = Match->Entry.Register;
        DdiElements.push_back(Ddi);
    }

    /* Every input the shader declares must be fed by some element */
    for (const RxSignatureElement& Input : Shader.GetInput().GetElements())
    {
        if (Input.Entry.SystemValue != D3D10_SB_NAME_UNDEFINED)
            continue;

        Match = nullptr;
        for (Index = 0; Index < ElementCount && Match == nullptr; Index++)
        {
            if (Elements[Index].SemanticIndex == Input.SemanticIndex &&
                _stricmp(Elements[Index].SemanticName, Input.SemanticName.c_str()) == 0)
            {
                Match = &Input;
            }
        }

        if (Match == nullptr)
        {
            RX_TRACE(L"input layout misses %S%u", Input.SemanticName.c_str(), Input.SemanticIndex);
            return E_INVALIDARG;
        }
    }

    NewLayout = new (std::nothrow) RxInputLayout(Device);
    if (NewLayout == nullptr)
        return E_OUTOFMEMORY;

    Args.pVertexElements = DdiElements.data();
    Args.NumElements = (UINT)DdiElements.size();
    NewLayout->Handle.pDrvPrivate =
        NewLayout->Memory.Allocate(Funcs.pfnCalcPrivateElementLayoutSize(Device.GetDriverDevice(), &Args));
    if (NewLayout->Handle.pDrvPrivate == nullptr)
    {
        NewLayout->Release();
        return E_OUTOFMEMORY;
    }

    RuntimeHandle.handle = static_cast<ID3D11InputLayout*>(NewLayout);
    Funcs.pfnCreateElementLayout(Device.GetDriverDevice(), &Args, NewLayout->Handle, RuntimeHandle);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateElementLayout failed 0x%08lX", hr);
        NewLayout->Release();
        return hr;
    }

    NewLayout->IsCreated = TRUE;
    *Layout = NewLayout;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxInputLayout::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11InputLayout))
    {
        *ppvObject = static_cast<ID3D11InputLayout*>(this);
        AddRef();
        return S_OK;
    }

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

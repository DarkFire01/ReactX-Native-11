/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D11DeviceContext1 and ID3D11DeviceContext2 methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shaders.hpp"
#include "views.hpp"

/* Constant buffer ranges */

VOID
RxImmediateContext::SetConstantBufferRanges(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_opt_(Count) ID3D11Buffer* const* Buffers,
    _In_reads_opt_(Count) const UINT* FirstConstants,
    _In_reads_opt_(Count) const UINT* NumConstants)
{
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    UINT Index;

    if (Start >= State.ConstantBuffers.size() || Count > State.ConstantBuffers.size() - Start)
        return;

    /* Both arrays or neither; ranges start and grow in whole 256 byte steps up to 4096 constants */
    if ((FirstConstants == nullptr) != (NumConstants == nullptr))
        return;

    for (Index = 0; FirstConstants != nullptr && Index < Count; Index++)
    {
        if ((FirstConstants[Index] % 16) != 0 || (NumConstants[Index] % 16) != 0 ||
            NumConstants[Index] > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT)
        {
            return;
        }
    }

    SetConstantBuffers(Stage, Start, Count, Buffers);
    if (FirstConstants == nullptr)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        State.FirstConstants[Start + Index] = FirstConstants[Index];
        State.ConstantCounts[Start + Index] = NumConstants[Index];
    }

    SendConstantBuffers(Stage, Start, Count);
}

VOID
RxImmediateContext::GetConstantBufferRanges(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _Out_writes_opt_(Count) ID3D11Buffer** Buffers,
    _Out_writes_opt_(Count) UINT* FirstConstants,
    _Out_writes_opt_(Count) UINT* NumConstants)
{
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    UINT Index;
    UINT Slot;

    if (Start >= State.ConstantBuffers.size() || Count > State.ConstantBuffers.size() - Start)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        Slot = Start + Index;
        if (Buffers != nullptr)
            Buffers[Index] = RxHandBack<ID3D11Buffer>(State.ConstantBuffers[Slot]);

        /* A whole buffer reads back as the largest range a shader can see */
        if (FirstConstants != nullptr)
            FirstConstants[Index] = State.FirstConstants[Slot];
        if (NumConstants != nullptr)
            NumConstants[Index] = State.ConstantCounts[Slot] ? State.ConstantCounts[Slot] : D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;
    }
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSSetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pFirstConstant,
    _In_reads_opt_(NumBuffers) const UINT* pNumConstants)
{
    SetConstantBufferRanges(RxStage::Vertex, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSGetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pFirstConstant,
    _Out_writes_opt_(NumBuffers) UINT* pNumConstants)
{
    GetConstantBufferRanges(RxStage::Vertex, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSSetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pFirstConstant,
    _In_reads_opt_(NumBuffers) const UINT* pNumConstants)
{
    SetConstantBufferRanges(RxStage::Hull, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSGetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pFirstConstant,
    _Out_writes_opt_(NumBuffers) UINT* pNumConstants)
{
    GetConstantBufferRanges(RxStage::Hull, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSSetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pFirstConstant,
    _In_reads_opt_(NumBuffers) const UINT* pNumConstants)
{
    SetConstantBufferRanges(RxStage::Domain, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSGetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pFirstConstant,
    _Out_writes_opt_(NumBuffers) UINT* pNumConstants)
{
    GetConstantBufferRanges(RxStage::Domain, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSSetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pFirstConstant,
    _In_reads_opt_(NumBuffers) const UINT* pNumConstants)
{
    SetConstantBufferRanges(RxStage::Geometry, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSGetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pFirstConstant,
    _Out_writes_opt_(NumBuffers) UINT* pNumConstants)
{
    GetConstantBufferRanges(RxStage::Geometry, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSSetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pFirstConstant,
    _In_reads_opt_(NumBuffers) const UINT* pNumConstants)
{
    SetConstantBufferRanges(RxStage::Pixel, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSGetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pFirstConstant,
    _Out_writes_opt_(NumBuffers) UINT* pNumConstants)
{
    GetConstantBufferRanges(RxStage::Pixel, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSSetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pFirstConstant,
    _In_reads_opt_(NumBuffers) const UINT* pNumConstants)
{
    SetConstantBufferRanges(RxStage::Compute, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSGetConstantBuffers1(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pFirstConstant,
    _Out_writes_opt_(NumBuffers) UINT* pNumConstants)
{
    GetConstantBufferRanges(RxStage::Compute, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

/* Discards */

/* The driver handle behind any view this runtime made, and which kind it is */
static
BOOLEAN
RxFindViewHandle(
    _In_opt_ ID3D11View* View,
    _Out_ D3D11DDI_HANDLETYPE* Type,
    _Out_ VOID** Handle,
    _Out_ DdiResource** Backing)
{
    RxRenderTargetView* Target = RxUnwrap<RxRenderTargetView>(View);
    RxDepthStencilView* Depth = RxUnwrap<RxDepthStencilView>(View);
    RxShaderResourceView* Read = RxUnwrap<RxShaderResourceView>(View);
    RxUnorderedAccessView* Unordered = RxUnwrap<RxUnorderedAccessView>(View);

    if (Target != nullptr)
    {
        *Type = D3D10DDI_HT_RENDERTARGETVIEW;
        *Handle = Target->GetHandle().pDrvPrivate;
        *Backing = Target->GetBacking();
        return TRUE;
    }

    if (Depth != nullptr)
    {
        *Type = D3D10DDI_HT_DEPTHSTENCILVIEW;
        *Handle = Depth->GetHandle().pDrvPrivate;
        *Backing = Depth->GetBacking();
        return TRUE;
    }

    if (Read != nullptr)
    {
        *Type = D3D10DDI_HT_SHADERRESOURCEVIEW;
        *Handle = Read->GetHandle().pDrvPrivate;
        *Backing = Read->GetBacking();
        return TRUE;
    }

    if (Unordered != nullptr)
    {
        *Type = D3D11DDI_HT_UNORDEREDACCESSVIEW;
        *Handle = Unordered->GetHandle().pDrvPrivate;
        *Backing = Unordered->GetBacking();
        return TRUE;
    }

    return FALSE;
}

VOID
RxImmediateContext::Discard(
    _In_ D3D11DDI_HANDLETYPE Type,
    _In_ VOID* Handle,
    _In_reads_opt_(NumRects) const D3D11_RECT* Rects,
    _In_ UINT NumRects)
{
    static_assert(sizeof(D3D11_RECT) == sizeof(D3D10_DDI_RECT));

    Device.GetDeviceFuncs().pfnDiscard(Device.GetDriverDevice(), Type, Handle,
                                       reinterpret_cast<const D3D10_DDI_RECT*>(Rects), NumRects);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DiscardResource(
    _In_ ID3D11Resource* pResource)
{
    DdiResource* Resource = RxGetDdiResource(pResource);
    RxResourceInfo Info;

    /* Only resources the GPU writes can lose their contents this way */
    if (Resource == nullptr || !RxDescribeResource(pResource, &Info) ||
        (Info.Usage != D3D11_USAGE_DEFAULT && Info.Usage != D3D11_USAGE_DYNAMIC))
    {
        return;
    }

    Resource->NoteWrite();
    Discard(D3D10DDI_HT_RESOURCE, Resource->GetHandle().pDrvPrivate, nullptr, 0);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DiscardView(
    _In_ ID3D11View* pResourceView)
{
    DiscardView1(pResourceView, nullptr, 0);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DiscardView1(
    _In_ ID3D11View* pResourceView,
    _In_reads_opt_(NumRects) const D3D11_RECT* pRects,
    UINT NumRects)
{
    D3D11DDI_HANDLETYPE Type;
    DdiResource* Backing;
    VOID* Handle;

    if (!RxFindViewHandle(pResourceView, &Type, &Handle, &Backing))
        return;
    if ((pRects == nullptr) != (NumRects == 0))
        return;

    if (Backing != nullptr)
        Backing->NoteWrite();
    Discard(Type, Handle, pRects, NumRects);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::ClearView(
    _In_ ID3D11View* pView,
    _In_ const FLOAT Color[4],
    _In_reads_opt_(NumRects) const D3D11_RECT* pRect,
    UINT NumRects)
{
    D3D11DDI_HANDLETYPE Type;
    DdiResource* Backing;
    VOID* Handle;

    /* Render targets and unordered access views only */
    if (!RxFindViewHandle(pView, &Type, &Handle, &Backing) ||
        (Type != D3D10DDI_HT_RENDERTARGETVIEW && Type != D3D11DDI_HT_UNORDEREDACCESSVIEW))
    {
        return;
    }

    if ((pRect == nullptr) != (NumRects == 0))
        return;

    if (Backing != nullptr)
        Backing->NoteWrite();
    Device.GetDeviceFuncs().pfnClearView(Device.GetDriverDevice(), Type, Handle, Color,
                                         reinterpret_cast<const D3D10_DDI_RECT*>(pRect), NumRects);
}

/* Annotations: no tool is listening, so markers go nowhere */

BOOL
STDMETHODCALLTYPE
RxImmediateContext::IsAnnotationEnabled(VOID)
{
    return FALSE;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::SetMarkerInt(
    _In_ LPCWSTR pLabel,
    INT Data)
{
    UNREFERENCED_PARAMETER(pLabel);
    UNREFERENCED_PARAMETER(Data);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::BeginEventInt(
    _In_ LPCWSTR pLabel,
    INT Data)
{
    UNREFERENCED_PARAMETER(pLabel);
    UNREFERENCED_PARAMETER(Data);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::EndEvent(VOID)
{
}

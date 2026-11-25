/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Sampler feedback unordered access views
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

static
BOOLEAN
RxIsFeedbackMap(
    _In_ DXGI_FORMAT Format)
{
    return Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ||
           Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
}

/* A feedback map pairs with a texture of its own size and mips; anything else loses the device */
VOID
STDMETHODCALLTYPE
RxDevice12::CreateSamplerFeedbackUnorderedAccessView(
    _In_opt_ ID3D12Resource* pTargetedResource,
    _In_opt_ ID3D12Resource* pFeedbackResource,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    RxResource* Targeted = RxUnwrap12<RxResource>(pTargetedResource);
    RxResource* Feedback = RxUnwrap12<RxResource>(pFeedbackResource);
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination;
    D3D12DDI_HRESOURCE None = {};
    UINT Errors = 0;

    if (GetValidationCaps().SamplerFeedbackTier == 0)
        Errors++;
    if (pTargetedResource == nullptr && pFeedbackResource == nullptr)
        Errors++;
    if (Feedback != nullptr && !RxIsFeedbackMap(Feedback->GetApiDesc().Format))
        Errors++;

    if (Targeted != nullptr && Feedback != nullptr)
    {
        const D3D12_RESOURCE_DESC1& Target = Targeted->GetApiDesc();
        const D3D12_RESOURCE_DESC1& Map = Feedback->GetApiDesc();

        if (Target.Width != Map.Width)
            Errors++;
        if (Target.Height != Map.Height)
            Errors++;
        if (Target.MipLevels != Map.MipLevels)
            Errors++;
        if (Target.DepthOrArraySize != Map.DepthOrArraySize)
            Errors++;
    }

    if (Errors != 0)
    {
        RemoveOnBadView();
        return;
    }

    Destination.ptr = DestDescriptor.ptr;
    CoreFunctions.pfnCreateSamplerFeedbackUnorderedAccessView(DriverDevice,
                                                              Targeted != nullptr ? Targeted->GetDriverHandle() : None,
                                                              Feedback != nullptr ? Feedback->GetDriverHandle() : None,
                                                              Destination);
}

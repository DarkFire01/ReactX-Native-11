/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Internal references held by pipeline bindings and device defaults
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* An internal reference to a runtime object, as pipeline bindings hold them */
template <typename Object>
class RxBound
{
public:
    RxBound() = default;

    RxBound(
        _In_opt_ Object* Target)
        : Pointer(Target)
    {
        if (Pointer != nullptr)
            Pointer->AddInternalRef();
    }

    RxBound(
        _In_ const RxBound& Other)
        : RxBound(Other.Pointer)
    {
    }

    ~RxBound()
    {
        Reset();
    }

    RxBound&
    operator=(
        _In_ const RxBound& Other)
    {
        if (Other.Pointer != nullptr)
            Other.Pointer->AddInternalRef();
        Reset();
        Pointer = Other.Pointer;
        return *this;
    }

    VOID
    Reset()
    {
        Object* Previous = Pointer;

        Pointer = nullptr;
        if (Previous != nullptr)
            Previous->ReleaseInternalRef();
    }

    Object*
    Get() const
    {
        return Pointer;
    }

    Object*
    operator->() const
    {
        return Pointer;
    }

    explicit
    operator bool() const
    {
        return Pointer != nullptr;
    }

private:
    Object* Pointer = nullptr;
};

/* A new public reference to a bound object, or NULL for an empty slot or a default */
template <typename Interface, typename Object>
Interface*
RxHandBack(
    _In_ const RxBound<Object>& Bound)
{
    Interface* Pointer = Bound.Get();

    if (Pointer != nullptr)
        Pointer->AddRef();
    return Pointer;
}

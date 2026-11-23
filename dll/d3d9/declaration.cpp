/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex declarations, their checks and their flexible vertex format forms
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Bytes each declaration type takes */
static const BYTE D9DeclTypeSizes[] = { 4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8 };

/* Bytes of one texture coordinate set by its format code in a flexible vertex format */
static const BYTE D9FvfCoordinateSizes[] = { 8, 12, 16, 4 };

/* The highest usage index a declaration may give */
static constexpr UINT D9LastUsageIndex = 15;

/* The format bit for a fog value no public header names */
static constexpr DWORD D9FvfFog = 0x2000;

static
D3DVERTEXELEMENT9
D9Element(
    _In_ WORD Offset,
    _In_ BYTE Type,
    _In_ BYTE Usage,
    _In_ BYTE UsageIndex)
{
    D3DVERTEXELEMENT9 Element = { 0, Offset, Type, D3DDECLMETHOD_DEFAULT, Usage, UsageIndex };

    return Element;
}

/* Builds the elements a flexible vertex format stands for, in the order the system lays them out */
static
UINT
D9ElementsFromFvf(
    _In_ DWORD Fvf,
    _Out_writes_(MAXD3DDECLLENGTH + 1) D3DVERTEXELEMENT9* Elements)
{
    static const D3DVERTEXELEMENT9 End = D3DDECL_END();
    DWORD Position = Fvf & (D3DFVF_POSITION_MASK | D3DFVF_XYZW);
    BYTE IndexType = (Fvf & D3DFVF_LASTBETA_UBYTE4) != 0 ? D3DDECLTYPE_UBYTE4 :
                     (Fvf & D3DFVF_LASTBETA_D3DCOLOR) != 0 ? D3DDECLTYPE_D3DCOLOR : 0xFF;
    UINT Count = 0;
    WORD Offset = 0;
    UINT Coordinates = (Fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

    if (Position == D3DFVF_XYZRHW)
    {
        Elements[Count++] = D9Element(0, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0);
        Offset = 16;
    }
    else if (Position == D3DFVF_XYZW)
    {
        Elements[Count++] = D9Element(0, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0);
        Offset = 16;
    }
    else if (Position >= D3DFVF_XYZ && Position <= D3DFVF_XYZB5 && (Position & 1) == 0)
    {
        /* Blend weights follow the position; with a last beta the final weight slot holds the indices */
        UINT Weights = Position == D3DFVF_XYZ ? 0 : (Position - D3DFVF_XYZRHW) / 2;

        Elements[Count++] = D9Element(0, D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0);
        Offset = 12;
        if (Weights == 5)
        {
            Elements[Count++] = D9Element(12, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_BLENDWEIGHT, 0);
            Elements[Count++] = D9Element(28, IndexType == 0xFF ? D3DDECLTYPE_FLOAT1 : IndexType,
                                          D3DDECLUSAGE_BLENDINDICES, 0);
            Offset = 32;
        }
        else if (Weights == 1 && IndexType != 0xFF)
        {
            Elements[Count++] = D9Element(12, IndexType, D3DDECLUSAGE_BLENDINDICES, 0);
            Offset = 16;
        }
        else if (Weights == 2 && IndexType == D3DDECLTYPE_D3DCOLOR)
        {
            Elements[Count++] = D9Element(12, D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_BLENDWEIGHT, 0);
            Elements[Count++] = D9Element(16, D3DDECLTYPE_UBYTE4, D3DDECLUSAGE_BLENDINDICES, 0);
            Offset = 20;
        }
        else if (Weights >= 1)
        {
            UINT Floats = IndexType == 0xFF ? Weights : Weights - 1;

            Elements[Count++] = D9Element(12, static_cast<BYTE>(D3DDECLTYPE_FLOAT1 + Floats - 1),
                                          D3DDECLUSAGE_BLENDWEIGHT, 0);
            Offset = static_cast<WORD>(12 + Floats * 4);
            if (IndexType != 0xFF)
            {
                Elements[Count++] = D9Element(Offset, IndexType, D3DDECLUSAGE_BLENDINDICES, 0);
                Offset += 4;
            }
        }
    }

    if ((Fvf & D3DFVF_NORMAL) != 0)
    {
        Elements[Count++] = D9Element(Offset, D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0);
        Offset += 12;
    }
    if ((Fvf & D3DFVF_PSIZE) != 0)
    {
        Elements[Count++] = D9Element(Offset, D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0);
        Offset += 4;
    }
    if ((Fvf & D3DFVF_DIFFUSE) != 0)
    {
        Elements[Count++] = D9Element(Offset, D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0);
        Offset += 4;
    }
    if ((Fvf & D3DFVF_SPECULAR) != 0)
    {
        Elements[Count++] = D9Element(Offset, D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1);
        Offset += 4;
    }
    if ((Fvf & D9FvfFog) != 0)
    {
        Elements[Count++] = D9Element(Offset, D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_FOG, 0);
        Offset += 4;
    }
    for (UINT Set = 0; Set < Coordinates; Set++)
    {
        BYTE Size = D9FvfCoordinateSizes[(Fvf >> (16 + Set * 2)) & 3];
        BYTE Type = static_cast<BYTE>(Size / 4 - 1);

        Elements[Count++] = D9Element(Offset, Type, D3DDECLUSAGE_TEXCOORD, static_cast<BYTE>(Set));
        Offset = static_cast<WORD>(Offset + D9DeclTypeSizes[Type]);
    }
    Elements[Count] = End;
    return Count;
}

/* Whether a usage and index pair is one the fixed function pipeline reads */
static
BOOLEAN
D9FixedFunctionPair(
    _In_ const D3DVERTEXELEMENT9* Element)
{
    switch (Element->Usage)
    {
    case D3DDECLUSAGE_COLOR:
        return Element->UsageIndex <= 1;
    case D3DDECLUSAGE_TEXCOORD:
        return Element->UsageIndex <= 7;
    case D3DDECLUSAGE_POSITION:
    case D3DDECLUSAGE_POSITIONT:
    case D3DDECLUSAGE_BLENDWEIGHT:
    case D3DDECLUSAGE_BLENDINDICES:
    case D3DDECLUSAGE_NORMAL:
    case D3DDECLUSAGE_PSIZE:
        return Element->UsageIndex == 0;
    default:
        return FALSE;
    }
}

/* The flexible vertex format the fixed function pipeline reads a declaration as, or zero when it cannot; elements
   after the last one it reads stand in as further texture coordinates of their size */
static
DWORD
D9FvfFromElements(
    _In_reads_(Count) const D3DVERTEXELEMENT9* Elements,
    _In_ UINT Count)
{
    DWORD Fvf = 0;
    UINT Weights = 0;
    UINT Coordinates = 0;
    UINT CoordinateSets = 0;
    BOOLEAN Position = FALSE;
    BOOLEAN Transformed = FALSE;
    BYTE Aliases[MAXD3DDECLLENGTH];
    UINT AliasCount = 0;
    UINT NextAlias = 0;

    for (UINT Index = 0; Index < Count; Index++)
    {
        const D3DVERTEXELEMENT9* Element = &Elements[Index];

        if (Element->Method != D3DDECLMETHOD_DEFAULT)
            return 0;
        if (!D9FixedFunctionPair(Element))
        {
            Aliases[AliasCount++] = D9DeclTypeSizes[Element->Type];
            continue;
        }
        if (AliasCount != 0)
            return 0;
        switch (Element->Usage)
        {
        case D3DDECLUSAGE_POSITION:
            if (Element->UsageIndex != 0 || Element->Type != D3DDECLTYPE_FLOAT3)
                return 0;
            Position = TRUE;
            break;
        case D3DDECLUSAGE_POSITIONT:
            if (Element->UsageIndex != 0 || Element->Type != D3DDECLTYPE_FLOAT4)
                return 0;
            Transformed = TRUE;
            break;
        case D3DDECLUSAGE_BLENDWEIGHT:
            if (Element->Type > D3DDECLTYPE_FLOAT4)
                return 0;
            Weights += Element->Type - D3DDECLTYPE_FLOAT1 + 1;
            break;
        case D3DDECLUSAGE_BLENDINDICES:
            if (Element->Type == D3DDECLTYPE_UBYTE4)
                Fvf |= D3DFVF_LASTBETA_UBYTE4;
            else if (Element->Type == D3DDECLTYPE_D3DCOLOR)
                Fvf |= D3DFVF_LASTBETA_D3DCOLOR;
            Weights += 1;
            break;
        case D3DDECLUSAGE_NORMAL:
            if (Element->UsageIndex != 0 || Element->Type != D3DDECLTYPE_FLOAT3)
                return 0;
            Fvf |= D3DFVF_NORMAL;
            break;
        case D3DDECLUSAGE_PSIZE:
            if (Element->Type != D3DDECLTYPE_FLOAT1)
                return 0;
            Fvf |= D3DFVF_PSIZE;
            break;
        case D3DDECLUSAGE_COLOR:
            if (Element->Type != D3DDECLTYPE_D3DCOLOR || Element->UsageIndex > 1)
                return 0;
            Fvf |= Element->UsageIndex == 0 ? D3DFVF_DIFFUSE : D3DFVF_SPECULAR;
            break;
        case D3DDECLUSAGE_TEXCOORD:
            if (Element->Type > D3DDECLTYPE_FLOAT4)
                return 0;
            if (Element->Type != D3DDECLTYPE_FLOAT2)
                Fvf |= (Element->Type == D3DDECLTYPE_FLOAT1 ? 3u : Element->Type - 1u) << (16 + Element->UsageIndex * 2);
            CoordinateSets |= 1u << Element->UsageIndex;
            NextAlias = Element->UsageIndex + 1u;
            break;
        default:
            return 0;
        }
    }

    /* Only sets counted up from the first without a gap become texture coordinates */
    /* Stand ins follow the texture coordinate set named last, taking over any set already there */
    for (UINT Alias = 0; Alias < AliasCount; Alias++)
    {
        static const BYTE Codes[] = { 3, 0, 1, 2 };
        UINT Shift = 16 + NextAlias * 2;

        if (NextAlias >= 8)
            return 0;
        Fvf = (Fvf & ~(3u << Shift)) | (static_cast<DWORD>(Codes[Aliases[Alias] / 4 - 1]) << Shift);
        CoordinateSets |= 1u << NextAlias;
        NextAlias += 1;
    }
    while ((CoordinateSets & (1u << Coordinates)) != 0)
        Coordinates += 1;
    if (Transformed)
    {
        if (Weights != 0)
            return 0;
        Fvf |= D3DFVF_XYZRHW;
    }
    else if (Position)
    {
        if (Weights > 5)
            return 0;
        Fvf |= Weights == 0 ? D3DFVF_XYZ : D3DFVF_XYZRHW + Weights * 2;
    }
    else
    {
        return 0;
    }
    return Fvf | (Coordinates << D3DFVF_TEXCOUNT_SHIFT);
}

/* The rules the system holds a declaration to before it makes one */
static
BOOLEAN
D9CheckElements(
    _In_reads_(Count) const D3DVERTEXELEMENT9* Elements,
    _In_ UINT Count)
{
    BOOLEAN HasPosition = FALSE;
    BOOLEAN HasTransformed = FALSE;
    BOOLEAN OtherStream = FALSE;

    for (UINT Index = 0; Index < Count; Index++)
    {
        const D3DVERTEXELEMENT9* Element = &Elements[Index];

        if (Element->Stream >= D9StreamCount || (Element->Offset & 3) != 0)
            return FALSE;
        if (Element->Type >= D3DDECLTYPE_UNUSED || Element->Method > D3DDECLMETHOD_LOOKUPPRESAMPLED)
            return FALSE;
        if (Element->Usage > D3DDECLUSAGE_SAMPLE || Element->UsageIndex > D9LastUsageIndex)
            return FALSE;
        if (Element->Usage == D3DDECLUSAGE_TESSFACTOR &&
            (Element->Type != D3DDECLTYPE_FLOAT1 || Element->UsageIndex != 0))
            return FALSE;
        for (UINT Earlier = 0; Earlier < Index; Earlier++)
        {
            if (Elements[Earlier].Usage == Element->Usage && Elements[Earlier].UsageIndex == Element->UsageIndex)
                return FALSE;
        }
        if (Element->Usage == D3DDECLUSAGE_POSITION && Element->UsageIndex == 0)
            HasPosition = TRUE;
        if (Element->Usage == D3DDECLUSAGE_POSITIONT && Element->UsageIndex == 0)
            HasTransformed = TRUE;
        if (Element->Stream != 0)
            OtherStream = TRUE;
    }
    if (HasPosition && HasTransformed)
        return FALSE;
    return !(HasTransformed && OtherStream);
}

D9Declaration::D9Declaration(
    _In_ D9Device* Owner)
    : D9Child(Owner)
{
    Owner->TrackDeclaration(this);
}

D9Declaration::~D9Declaration()
{
    if (DriverHandle != NULL)
        Device->DestroyDriverDeclaration(DriverHandle);
    Device->UntrackDeclaration(this);
}

HRESULT
D9Declaration::Initialize(
    _In_reads_(Count + 1) const D3DVERTEXELEMENT9* Source,
    _In_ UINT Count,
    _In_ DWORD Format)
{
    ElementCount = Count;
    CopyMemory(Elements, Source, (Count + 1) * sizeof(*Elements));
    Fvf = Format;
    for (UINT Index = 0; Index < Count; Index++)
    {
        if (Elements[Index].Usage == D3DDECLUSAGE_POSITIONT)
            Transformed = TRUE;
    }
    return S_OK;
}

STDMETHODIMP
D9Declaration::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return D3DERR_INVALIDCALL;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DVertexDeclaration9)))
    {
        *Object = static_cast<IDirect3DVertexDeclaration9*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9Declaration::AddRef()
{
    LONG Count = InterlockedIncrement(&RefCount);

    if (Count == 1)
    {
        Orphaned = FALSE;
        Device->AddRef();
    }
    return static_cast<ULONG>(Count);
}

STDMETHODIMP_(ULONG)
D9Declaration::Release()
{
    D9Device* Owner = Device;
    LONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
    {
        if (BindCount > 0)
            Orphaned = TRUE;
        else
            delete this;
        Owner->Release();
    }
    return static_cast<ULONG>(Count);
}

VOID
D9Declaration::Unbind(VOID)
{
    BindCount -= 1;
    if (BindCount == 0 && Orphaned)
        delete this;
}

STDMETHODIMP
D9Declaration::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Device;
    Device->AddRef();
    return S_OK;
}

/* The count includes the closing element; the elements are copied whatever room the caller has */
STDMETHODIMP
D9Declaration::GetDeclaration(
    _Out_writes_opt_(*NumElements) D3DVERTEXELEMENT9* Element,
    _Inout_ UINT* NumElements)
{
    *NumElements = ElementCount + 1;
    if (Element != nullptr)
        CopyMemory(Element, Elements, (ElementCount + 1) * sizeof(*Elements));
    return S_OK;
}

HRESULT
D9Device::DriverDeclaration(
    _In_ const D9Declaration* Made,
    _Out_ HANDLE* Handle)
{
    D3DDDIARG_CREATEVERTEXSHADERDECL Args = {};
    HRESULT hr;

    Args.NumVertexElements = Made->ElementCount;
    hr = DeviceFunctions.pfnCreateVertexShaderDecl(DriverDevice, &Args,
                                                   reinterpret_cast<const D3DDDIVERTEXELEMENT*>(Made->Elements));
    *Handle = Args.ShaderHandle;
    if (FAILED(hr))
        D9Trace(L"CreateVertexDeclaration: the driver failed with %08lX", hr);
    return hr;
}

VOID
D9Device::DestroyDriverDeclaration(
    _In_ HANDLE Handle)
{
    DrainBatch();
    DeviceFunctions.pfnDeleteVertexShaderDecl(DriverDevice, Handle);
}

VOID
D9Device::TrackDeclaration(
    _Inout_ D9Declaration* Made)
{
    Made->NextLive = LiveDeclarations;
    LiveDeclarations = Made;
}

VOID
D9Device::UntrackDeclaration(
    _In_ D9Declaration* Gone)
{
    D9Declaration** Link = &LiveDeclarations;

    while (*Link != nullptr && *Link != Gone)
        Link = &(*Link)->NextLive;
    if (*Link != nullptr)
        *Link = Gone->NextLive;
}

/* A reset of the plain object's device drops the driver's copy of every declaration, newest first; the next
   draw that binds one makes it again */
VOID
D9Device::ForgetDriverDeclarations(VOID)
{
    for (D9Declaration* Live = LiveDeclarations; Live != nullptr; Live = Live->NextLive)
    {
        HANDLE Handle = Live->DriverHandle;

        if (Handle == NULL)
            continue;
        Live->DriverHandle = NULL;
        DestroyDriverDeclaration(Handle);
    }
    DrawDirty |= D9DrawVertexInput;
}

STDMETHODIMP
D9Device::CreateVertexDeclaration(
    _In_opt_ const D3DVERTEXELEMENT9* VertexElements,
    _Out_ IDirect3DVertexDeclaration9** Decl)
{
    D9_DEVICE_LOCK();
    D9Declaration* Made;
    UINT Count = 0;
    HRESULT hr;

    if (VertexElements == nullptr || Decl == nullptr)
        return D3DERR_INVALIDCALL;
    *Decl = nullptr;
    while (Count <= MAXD3DDECLLENGTH && VertexElements[Count].Stream != 0xFF)
        Count += 1;
    if (Count > MAXD3DDECLLENGTH || !D9CheckElements(VertexElements, Count))
        return E_FAIL;

    /* Types beyond the plain ones need the device's caps for them */
    for (UINT Index = 0; Index < Count; Index++)
    {
        BYTE Type = VertexElements[Index].Type;
        DWORD Needed = 0;

        if (Type == D3DDECLTYPE_UBYTE4)
            Needed = D3DDTCAPS_UBYTE4;
        else if (Type >= D3DDECLTYPE_UBYTE4N && Type <= D3DDECLTYPE_FLOAT16_4)
            Needed = D3DDTCAPS_UBYTE4N << (Type - D3DDECLTYPE_UBYTE4N);
        if ((CurrentCaps()->DeclTypes & Needed) != Needed)
        {
            D9Trace(L"CreateVertexDeclaration: the device has no caps for element type %u", Type);
            return E_FAIL;
        }
    }

    Made = new (std::nothrow) D9Declaration(this);
    if (Made == nullptr)
        return E_OUTOFMEMORY;
    hr = Made->Initialize(VertexElements, Count, D9FvfFromElements(VertexElements, Count));
    if (SUCCEEDED(hr) && DriverHasVertexShaders())
        hr = DriverDeclaration(Made, &Made->DriverHandle);
    if (FAILED(hr))
    {
        delete Made;
        return hr;
    }
    Made->AddRef();
    *Decl = Made;
    return S_OK;
}

/* Binding a declaration sets the format the fixed function pipeline reads; the driver sees it at a draw */
STDMETHODIMP
D9Device::SetVertexDeclaration(
    _In_opt_ IDirect3DVertexDeclaration9* Decl)
{
    D9_DEVICE_LOCK();
    D9Declaration* Next = static_cast<D9Declaration*>(Decl);
    D9Declaration* Previous = Declaration;

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Declaration, 0, 0, &Decl, sizeof(Decl));
    if (Next == Previous)
        return S_OK;
    if (Next != nullptr)
        Next->Bind();
    Declaration = Next;
    CurrentFvf = Next != nullptr ? Next->Fvf : 0;
    ApiFvf = 0;
    DrawDirty |= D9DrawVertexInput;
    if (Previous != nullptr)
        Previous->Unbind();
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexDeclaration(
    _Out_ IDirect3DVertexDeclaration9** Decl)
{
    D9_DEVICE_LOCK();

    if (Decl == nullptr)
        return D3DERR_INVALIDCALL;
    *Decl = Declaration;
    if (Declaration != nullptr)
        Declaration->AddRef();
    return S_OK;
}

/* A format binds the declaration the device keeps for it, made the first time the format is set */
HRESULT
D9Device::BindFvfDeclaration(
    _In_ DWORD Fvf)
{
    D3DVERTEXELEMENT9 Elements[MAXD3DDECLLENGTH + 1];
    D9Declaration* Found = nullptr;
    D9Declaration* Previous = Declaration;
    UINT Count;
    HRESULT hr;

    for (D9Declaration* Kept = FvfDeclarations; Kept != nullptr; Kept = Kept->NextKept)
    {
        if (Kept->Fvf == Fvf)
        {
            Found = Kept;
            break;
        }
    }
    if (Found == nullptr)
    {
        Found = new (std::nothrow) D9Declaration(this);
        if (Found == nullptr)
            return E_OUTOFMEMORY;
        Count = D9ElementsFromFvf(Fvf, Elements);
        hr = Found->Initialize(Elements, Count, Fvf);
        if (FAILED(hr))
        {
            delete Found;
            return hr;
        }
        Found->Bind();
        Found->ForFormat = TRUE;
        Found->Orphaned = TRUE;
        Found->NextKept = FvfDeclarations;
        FvfDeclarations = Found;
    }

    if (Found == Previous)
        return S_OK;
    Found->Bind();
    Declaration = Found;
    if (Previous != nullptr)
        Previous->Unbind();
    return S_OK;
}

/* Lets go of the declarations made for formats, after the device has let go of the one it had bound */
VOID
D9Device::ReleaseFvfDeclarations(VOID)
{
    D9Declaration* Bound = Declaration;

    Declaration = nullptr;
    if (Bound != nullptr)
        Bound->Unbind();
    while (FvfDeclarations != nullptr)
    {
        D9Declaration* Kept = FvfDeclarations;

        FvfDeclarations = Kept->NextKept;
        Kept->Unbind();
    }
}

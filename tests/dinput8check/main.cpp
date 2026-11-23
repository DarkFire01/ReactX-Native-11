/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DirectInput 8 answers for every entry point and argument shape, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * dinput8check [-runtime system|reactx|<path>] [check ...]
 *
 * -read <file> lists the effects in a file instead of running checks.
 * The watch check prints live keyboard and mouse input and only runs when named.
 */

#include "dinput8check.hpp"

HMODULE DcModule;
PDC_CREATE DcCreate;
HWND DcWindow;

static const GUID DcRandomGuid = { 0x12345678, 0x1234, 0x5678, { 0x9A, 0xBC, 0xDE, 0xF0, 0x12, 0x34, 0x56, 0x78 } };

static const struct
{
    const GUID* Guid;
    PCSTR Name;
} DcGuidNames[] =
{
    { &GUID_SysKeyboard, "SysKeyboard" },
    { &GUID_SysMouse, "SysMouse" },
    { &GUID_SysKeyboardEm, "SysKeyboardEm" },
    { &GUID_SysKeyboardEm2, "SysKeyboardEm2" },
    { &GUID_SysMouseEm, "SysMouseEm" },
    { &GUID_SysMouseEm2, "SysMouseEm2" },
    { &GUID_Joystick, "Joystick" },
    { &GUID_XAxis, "XAxis" },
    { &GUID_YAxis, "YAxis" },
    { &GUID_ZAxis, "ZAxis" },
    { &GUID_RxAxis, "RxAxis" },
    { &GUID_RyAxis, "RyAxis" },
    { &GUID_RzAxis, "RzAxis" },
    { &GUID_Slider, "Slider" },
    { &GUID_Button, "Button" },
    { &GUID_Key, "Key" },
    { &GUID_POV, "POV" },
    { &GUID_Unknown, "Unknown" },
    { &GUID_ConstantForce, "ConstantForce" },
    { &GUID_RampForce, "RampForce" },
    { &GUID_Square, "Square" },
    { &GUID_Sine, "Sine" },
    { &GUID_Triangle, "Triangle" },
    { &GUID_SawtoothUp, "SawtoothUp" },
    { &GUID_SawtoothDown, "SawtoothDown" },
    { &GUID_Spring, "Spring" },
    { &GUID_Damper, "Damper" },
    { &GUID_Inertia, "Inertia" },
    { &GUID_Friction, "Friction" },
    { &GUID_CustomForce, "CustomForce" },
    { &GUID_NULL, "NULL" },
    { &DcRandomGuid, "Random" },
};

std::string
DcGuid(
    _In_opt_ const GUID* Guid)
{
    char Text[64];

    if (Guid == nullptr)
        return "(null)";
    for (const auto& Entry : DcGuidNames)
    {
        if (IsEqualGUID(*Entry.Guid, *Guid))
            return Entry.Name;
    }
    sprintf_s(Text, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", Guid->Data1, Guid->Data2, Guid->Data3,
              Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3], Guid->Data4[4], Guid->Data4[5],
              Guid->Data4[6], Guid->Data4[7]);
    return Text;
}

std::string
DcWide(
    _In_z_ PCWSTR Text)
{
    std::string Out;

    for (; *Text != L'\0'; Text++)
    {
        char Part[16];

        if (*Text >= 0x20 && *Text < 0x7F)
        {
            Out += static_cast<char>(*Text);
        }
        else
        {
            sprintf_s(Part, "\\u%04X", *Text);
            Out += Part;
        }
    }
    return Out;
}

VOID
DcBytes(
    _In_z_ PCSTR Name,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    printf("%s =", Name);
    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        if (Index != 0 && Index % 32 == 0)
            printf("\n   ");
        printf(" %02X", static_cast<const BYTE*>(Data)[Index]);
    }
    printf("\n");
}

PCSTR
DcPointer(
    _In_opt_ const VOID* Value)
{
    if (Value == nullptr)
        return "null";
    if (Value == DC_SENTINEL)
        return "untouched";
    return "set";
}

/* An outer object that hands every call to the inner one, enough to test aggregation */
class DcOuter : public IUnknown
{
public:
    ULONG Refs = 1;

    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _Outptr_ VOID** Object) override
    {
        if (IsEqualIID(Riid, IID_IUnknown))
        {
            *Object = this;
            AddRef();
            return S_OK;
        }
        *Object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG)
    AddRef() override
    {
        return ++Refs;
    }

    STDMETHODIMP_(ULONG)
    Release() override
    {
        Refs -= 1;
        return Refs;
    }
};

IDirectInput8W*
DcCreateWide(VOID)
{
    IDirectInput8W* Input = nullptr;

    if (FAILED(DcCreate(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                        reinterpret_cast<VOID**>(&Input), nullptr)))
    {
        return nullptr;
    }
    return Input;
}

IDirectInput8A*
DcCreateAnsi(VOID)
{
    IDirectInput8A* Input = nullptr;

    if (FAILED(DcCreate(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
                        reinterpret_cast<VOID**>(&Input), nullptr)))
    {
        return nullptr;
    }
    return Input;
}

static
VOID
DcCheckCreate(VOID)
{
    static const DWORD Versions[] = { 0, 0x300, 0x500, 0x700, 0x7FF, 0x800, 0x801, 0x900, 0xFFFFFFFF };
    static const struct
    {
        const GUID* Iid;
        PCSTR Name;
    } Iids[] =
    {
        { &IID_IDirectInput8W, "IDirectInput8W" },
        { &IID_IDirectInput8A, "IDirectInput8A" },
        { &IID_IUnknown, "IUnknown" },
        { &IID_IDirectInput7W, "IDirectInput7W" },
        { &IID_IDirectInputJoyConfig8, "IDirectInputJoyConfig8" },
        { &IID_IDirectInputDevice8W, "IDirectInputDevice8W" },
        { &DcRandomGuid, "Random" },
    };
    HINSTANCE Instance = GetModuleHandleW(nullptr);

    for (DWORD Version : Versions)
    {
        VOID* Object = DC_SENTINEL;
        HRESULT Result = DcCreate(Instance, Version, IID_IDirectInput8W, &Object, nullptr);

        printf("create version %#lx = %08lX %s\n", Version, Result, DcPointer(Object));
        if (SUCCEEDED(Result) && Object != nullptr)
            static_cast<IUnknown*>(Object)->Release();
    }
    for (const auto& Entry : Iids)
    {
        VOID* Object = DC_SENTINEL;
        HRESULT Result = DcCreate(Instance, DIRECTINPUT_VERSION, *Entry.Iid, &Object, nullptr);

        printf("create iid %s = %08lX %s\n", Entry.Name, Result, DcPointer(Object));
        if (SUCCEEDED(Result) && Object != nullptr && Object != DC_SENTINEL)
            static_cast<IUnknown*>(Object)->Release();
    }
    {
        VOID* Object = DC_SENTINEL;
        HRESULT Result = DcCreate(nullptr, DIRECTINPUT_VERSION, IID_IDirectInput8W, &Object, nullptr);

        printf("create null instance = %08lX %s\n", Result, DcPointer(Object));
        Result = DcCreate(reinterpret_cast<HINSTANCE>(static_cast<ULONG_PTR>(0x10000)), DIRECTINPUT_VERSION,
                          IID_IDirectInput8W, &Object, nullptr);
        printf("create bad instance = %08lX %s\n", Result, DcPointer(Object));
        Result = DcCreate(Instance, DIRECTINPUT_VERSION, IID_IDirectInput8W, nullptr, nullptr);
        printf("create null out = %08lX\n", Result);
    }
    for (const auto& Entry : Iids)
    {
        DcOuter Outer;
        VOID* Object = DC_SENTINEL;
        HRESULT Result = DcCreate(Instance, DIRECTINPUT_VERSION, *Entry.Iid, &Object, &Outer);

        printf("create outer %s = %08lX %s refs %lu\n", Entry.Name, Result, DcPointer(Object), Outer.Refs);
        if (SUCCEEDED(Result) && Object != nullptr && Object != DC_SENTINEL)
        {
            IUnknown* Inner = static_cast<IUnknown*>(Object);
            IDirectInput8W* Wide = nullptr;
            HRESULT Query = Inner->QueryInterface(IID_IDirectInput8W, reinterpret_cast<VOID**>(&Wide));

            printf("  inner qi wide = %08lX refs %lu\n", Query, Outer.Refs);
            if (SUCCEEDED(Query))
            {
                IDirectInputDevice8W* Device = nullptr;

                printf("  aggregated create device before init = %08lX\n",
                       Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr));
                printf("  aggregated init = %08lX\n", Wide->Initialize(Instance, DIRECTINPUT_VERSION));
                Device = nullptr;
                Query = Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr);
                printf("  aggregated create device = %08lX refs %lu\n", Query, Outer.Refs);
                if (SUCCEEDED(Query))
                    Device->Release();
                Wide->Release();
            }
            printf("  inner release = %lu\n", Inner->Release());
        }
    }
}

static
VOID
DcCheckQuery(VOID)
{
    static const struct
    {
        const GUID* Iid;
        PCSTR Name;
    } Iids[] =
    {
        { &IID_IUnknown, "IUnknown" },
        { &IID_IDirectInput8W, "IDirectInput8W" },
        { &IID_IDirectInput8A, "IDirectInput8A" },
        { &IID_IDirectInput7W, "IDirectInput7W" },
        { &IID_IDirectInput7A, "IDirectInput7A" },
        { &IID_IDirectInput2W, "IDirectInput2W" },
        { &IID_IDirectInputW, "IDirectInputW" },
        { &IID_IDirectInputJoyConfig8, "IDirectInputJoyConfig8" },
        { &IID_IDirectInputJoyConfig, "IDirectInputJoyConfig" },
        { &IID_IDirectInputDevice8W, "IDirectInputDevice8W" },
        { &IID_IClassFactory, "IClassFactory" },
        { &DcRandomGuid, "Random" },
    };
    IDirectInput8W* Wide = DcCreateWide();
    IUnknown* Identity = nullptr;

    if (Wide == nullptr)
        return;
    Wide->QueryInterface(IID_IUnknown, reinterpret_cast<VOID**>(&Identity));
    printf("addref = %lu\n", Wide->AddRef());
    printf("release = %lu\n", Wide->Release());
    for (const auto& Entry : Iids)
    {
        VOID* Object = DC_SENTINEL;
        HRESULT Result = Wide->QueryInterface(*Entry.Iid, &Object);

        printf("qi %s = %08lX %s", Entry.Name, Result, DcPointer(Object));
        if (SUCCEEDED(Result))
        {
            IUnknown* Unknown = static_cast<IUnknown*>(Object);
            IUnknown* Back = nullptr;

            Unknown->QueryInterface(IID_IUnknown, reinterpret_cast<VOID**>(&Back));
            printf(" same %s self %s", Back == Identity ? "yes" : "no", Object == Wide ? "yes" : "no");
            if (Back != nullptr)
                Back->Release();
            if (IsEqualIID(*Entry.Iid, IID_IDirectInputJoyConfig8))
            {
                VOID* Again = nullptr;

                Unknown->QueryInterface(IID_IDirectInput8W, &Again);
                printf(" back to wide %s", Again == Wide ? "yes" : Again == nullptr ? "null" : "other");
                if (Again != nullptr)
                    static_cast<IUnknown*>(Again)->Release();
            }
            Unknown->Release();
        }
        printf("\n");
    }
    printf("qi null out = %08lX\n", Wide->QueryInterface(IID_IUnknown, nullptr));
    Identity->Release();
    printf("final release = %lu\n", Wide->Release());
}

static
BOOL
CALLBACK
DcPrintDeviceW(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ VOID* Context)
{
    PCSTR Prefix = static_cast<PCSTR>(Context);

    printf("%s size %lu inst %s prod %s type %08lX ff %s page %04X usage %04X\n", Prefix, Instance->dwSize,
           DcGuid(&Instance->guidInstance).c_str(), DcGuid(&Instance->guidProduct).c_str(), Instance->dwDevType,
           DcGuid(&Instance->guidFFDriver).c_str(), Instance->wUsagePage, Instance->wUsage);
    printf("%s   name '%s' product '%s'\n", Prefix, DcWide(Instance->tszInstanceName).c_str(),
           DcWide(Instance->tszProductName).c_str());
    return DIENUM_CONTINUE;
}

static
BOOL
CALLBACK
DcPrintDeviceA(
    _In_ LPCDIDEVICEINSTANCEA Instance,
    _In_ VOID* Context)
{
    PCSTR Prefix = static_cast<PCSTR>(Context);

    printf("%s size %lu inst %s prod %s type %08lX ff %s page %04X usage %04X\n", Prefix, Instance->dwSize,
           DcGuid(&Instance->guidInstance).c_str(), DcGuid(&Instance->guidProduct).c_str(), Instance->dwDevType,
           DcGuid(&Instance->guidFFDriver).c_str(), Instance->wUsagePage, Instance->wUsage);
    printf("%s   name '%s' product '%s'\n", Prefix, Instance->tszInstanceName, Instance->tszProductName);
    return DIENUM_CONTINUE;
}

struct DcEnumList
{
    std::string Text;
    ULONG Count = 0;
    ULONG StopAfter = 0;
};

static
BOOL
CALLBACK
DcCollectDevice(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ VOID* Context)
{
    DcEnumList* List = static_cast<DcEnumList*>(Context);
    char Part[32];

    sprintf_s(Part, ":%08lX", Instance->dwDevType);
    List->Text += " " + DcGuid(&Instance->guidInstance) + Part;
    List->Count++;
    if (List->StopAfter != 0 && List->Count >= List->StopAfter)
        return DIENUM_STOP;
    return DIENUM_CONTINUE;
}

static
VOID
DcCheckEnum(VOID)
{
    static const DWORD Types[] =
    {
        0, 1, 2, 3, 4, 5, 6, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 0x100, 0x112, 0x10000,
        0x10004, 0x10011, 0x10012, 0x10013, 0x10014, 0x20000, 0x80000000,
    };
    static const DWORD Flags[] = { 0, 1, 2, 0x100, 0x10000, 0x20000, 0x40000, 0x70101, 0x80000, 0x80000000 };
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInput8A* Ansi = DcCreateAnsi();

    if (Wide == nullptr || Ansi == nullptr)
        return;
    printf("enum all wide = %08lX\n",
           Wide->EnumDevices(DI8DEVCLASS_ALL, DcPrintDeviceW, const_cast<PSTR>("  dev"), DIEDFL_ALLDEVICES));
    printf("enum all ansi = %08lX\n",
           Ansi->EnumDevices(DI8DEVCLASS_ALL, DcPrintDeviceA, const_cast<PSTR>("  dev"), DIEDFL_ALLDEVICES));
    printf("enum hidden wide = %08lX\n",
           Wide->EnumDevices(DI8DEVCLASS_ALL, DcPrintDeviceW, const_cast<PSTR>("  dev"), DIEDFL_INCLUDEHIDDEN));
    for (DWORD Type : Types)
    {
        for (DWORD Flag : Flags)
        {
            DcEnumList List;
            HRESULT Result = Wide->EnumDevices(Type, DcCollectDevice, &List, Flag);

            printf("enum %#lx %#lx = %08lX%s\n", Type, Flag, Result, List.Text.c_str());
        }
    }
    {
        DcEnumList List;

        List.StopAfter = 1;
        HRESULT Result = Wide->EnumDevices(0, DcCollectDevice, &List, 0);

        printf("enum stop = %08lX%s\n", Result, List.Text.c_str());
        printf("enum null callback = %08lX\n", Wide->EnumDevices(0, nullptr, nullptr, 0));
        printf("enum ansi null callback = %08lX\n", Ansi->EnumDevices(0, nullptr, nullptr, 0));
        printf("enum ansi bad type = %08lX\n", Ansi->EnumDevices(5, DcPrintDeviceA, nullptr, 0));
    }
    Ansi->Release();
    Wide->Release();
}

static
VOID
DcCheckStatus(VOID)
{
    static const GUID* Guids[] =
    {
        &GUID_SysKeyboard, &GUID_SysMouse, &GUID_SysKeyboardEm, &GUID_SysKeyboardEm2, &GUID_SysMouseEm,
        &GUID_SysMouseEm2, &GUID_Joystick, &DcRandomGuid, &GUID_NULL,
    };
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInput8A* Ansi = DcCreateAnsi();

    if (Wide == nullptr || Ansi == nullptr)
        return;
    for (const GUID* Guid : Guids)
    {
        printf("status %s = %08lX ansi %08lX\n", DcGuid(Guid).c_str(), Wide->GetDeviceStatus(*Guid),
               Ansi->GetDeviceStatus(*Guid));
    }
    Ansi->Release();
    Wide->Release();
}

static
VOID
DcCheckFind(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInput8A* Ansi = DcCreateAnsi();
    GUID Found;
    HRESULT Result;

    if (Wide == nullptr || Ansi == nullptr)
        return;
    Found = DcRandomGuid;
    Result = Wide->FindDevice(GUID_HIDClass, L"", &Found);
    printf("find hid empty = %08lX %s\n", Result, DcGuid(&Found).c_str());
    Found = DcRandomGuid;
    Result = Wide->FindDevice(GUID_HIDClass, L"\\\\?\\hid#bogus", &Found);
    printf("find hid bogus = %08lX %s\n", Result, DcGuid(&Found).c_str());
    Found = DcRandomGuid;
    Result = Wide->FindDevice(DcRandomGuid, L"x", &Found);
    printf("find other class = %08lX %s\n", Result, DcGuid(&Found).c_str());
    printf("find null out = %08lX\n", Wide->FindDevice(GUID_HIDClass, L"x", nullptr));
    Found = DcRandomGuid;
    Result = Ansi->FindDevice(GUID_HIDClass, "\\\\?\\hid#bogus", &Found);
    printf("find ansi hid bogus = %08lX %s\n", Result, DcGuid(&Found).c_str());
    Found = DcRandomGuid;
    Result = Ansi->FindDevice(DcRandomGuid, "x", &Found);
    printf("find ansi other = %08lX %s\n", Result, DcGuid(&Found).c_str());
    Ansi->Release();
    Wide->Release();
}

typedef HRESULT (WINAPI* PDC_GET_CLASS)(REFCLSID, REFIID, VOID**);
typedef HRESULT (WINAPI* PDC_VOID)(VOID);

static
VOID
DcCheckFactory(VOID)
{
    PDC_GET_CLASS GetClass = reinterpret_cast<PDC_GET_CLASS>(GetProcAddress(DcModule, "DllGetClassObject"));
    PDC_VOID CanUnload = reinterpret_cast<PDC_VOID>(GetProcAddress(DcModule, "DllCanUnloadNow"));
    static const GUID* Classes[] = { &CLSID_DirectInput8, &CLSID_DirectInputDevice8, &CLSID_DirectInput, &DcRandomGuid };
    HINSTANCE Instance = GetModuleHandleW(nullptr);

    printf("can unload = %08lX\n", CanUnload());
    for (const GUID* Class : Classes)
    {
        IClassFactory* Factory = static_cast<IClassFactory*>(DC_SENTINEL);
        HRESULT Result = GetClass(*Class, IID_IClassFactory, reinterpret_cast<VOID**>(&Factory));

        printf("class %s = %08lX %s\n", DcGuid(Class).c_str(), Result, DcPointer(Factory));
        if (FAILED(Result) || Factory == nullptr || Factory == DC_SENTINEL)
            continue;
        printf("  can unload with factory = %08lX\n", CanUnload());
        {
            VOID* Object = DC_SENTINEL;
            DcOuter Outer;

            printf("  create null out = %08lX\n", Factory->CreateInstance(nullptr, IID_IUnknown, nullptr));
            Result = Factory->CreateInstance(&Outer, IID_IUnknown, &Object);
            printf("  create outer = %08lX %s\n", Result, DcPointer(Object));
            Object = DC_SENTINEL;
            Result = Factory->CreateInstance(nullptr, DcRandomGuid, &Object);
            printf("  create random iid = %08lX %s\n", Result, DcPointer(Object));
            printf("  lock = %08lX unlock = %08lX\n", Factory->LockServer(TRUE), Factory->LockServer(FALSE));
        }
        if (IsEqualGUID(*Class, CLSID_DirectInput8))
        {
            IDirectInput8W* Wide = nullptr;
            IDirectInputDevice8W* Device = nullptr;
            DcEnumList List;

            Result = Factory->CreateInstance(nullptr, IID_IDirectInput8W, reinterpret_cast<VOID**>(&Wide));
            printf("  create wide = %08lX\n", Result);
            if (SUCCEEDED(Result))
            {
                printf("  uninit enum = %08lX\n", Wide->EnumDevices(0, DcCollectDevice, &List, 0));
                printf("  uninit status = %08lX\n", Wide->GetDeviceStatus(GUID_SysKeyboard));
                printf("  uninit create device = %08lX\n", Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr));
                GUID Found = GUID_NULL;

                printf("  uninit find = %08lX\n", Wide->FindDevice(GUID_HIDClass, L"x", &Found));
                printf("  uninit cpl = %08lX\n", Wide->RunControlPanel(nullptr, 0));
                printf("  init null = %08lX\n", Wide->Initialize(nullptr, DIRECTINPUT_VERSION));
                printf("  init 0x700 = %08lX\n", Wide->Initialize(Instance, 0x700));
                printf("  init 0x900 = %08lX\n", Wide->Initialize(Instance, 0x900));
                printf("  init 0 = %08lX\n", Wide->Initialize(Instance, 0));
                printf("  still uninit enum = %08lX\n", Wide->EnumDevices(0, DcCollectDevice, &List, 0));
                printf("  init = %08lX\n", Wide->Initialize(Instance, DIRECTINPUT_VERSION));
                printf("  init again = %08lX\n", Wide->Initialize(Instance, DIRECTINPUT_VERSION));
                printf("  init down 0x700 = %08lX\n", Wide->Initialize(Instance, 0x700));
                List.Text.clear();
                Result = Wide->EnumDevices(0, DcCollectDevice, &List, 0);
                printf("  after enum = %08lX%s\n", Result, List.Text.c_str());
                Wide->Release();
            }
        }
        else if (IsEqualGUID(*Class, CLSID_DirectInputDevice8))
        {
            IDirectInputDevice8W* Device = nullptr;

            Result = Factory->CreateInstance(nullptr, IID_IDirectInputDevice8W, reinterpret_cast<VOID**>(&Device));
            printf("  create device = %08lX\n", Result);
            if (SUCCEEDED(Result))
            {
                DIDEVCAPS Caps = { sizeof(Caps) };

                printf("  uninit caps = %08lX\n", Device->GetCapabilities(&Caps));
                printf("  uninit acquire = %08lX\n", Device->Acquire());
                printf("  uninit format = %08lX\n", Device->SetDataFormat(&DcKeyboardFormat));
                {
                    LPDIRECTINPUTEFFECT Effect = reinterpret_cast<LPDIRECTINPUTEFFECT>(DC_SENTINEL);
                    DIEFFECTINFOW Info = { sizeof(Info) };
                    DIEFFESCAPE Escape = { sizeof(Escape) };
                    DIDEVICEOBJECTDATA Data = {};
                    DWORD State = 0xCCCCCCCC;
                    DWORD Count = 1;

                    printf("  uninit create effect = %08lX", Device->CreateEffect(GUID_Sine, nullptr, &Effect, nullptr));
                    printf(" %s\n", DcPointer(Effect));
                    printf("  uninit enum effects = %08lX\n", Device->EnumEffects(
                        [](LPCDIEFFECTINFOW, VOID*) -> BOOL { return DIENUM_CONTINUE; }, nullptr, 0));
                    printf("  uninit effect info = %08lX\n", Device->GetEffectInfo(&Info, GUID_Sine));
                    printf("  uninit ff state = %08lX", Device->GetForceFeedbackState(&State));
                    printf(" %08lX\n", State);
                    printf("  uninit ff command = %08lX\n", Device->SendForceFeedbackCommand(DISFFC_RESET));
                    printf("  uninit escape = %08lX\n", Device->Escape(&Escape));
                    printf("  uninit enum created = %08lX\n", Device->EnumCreatedEffectObjects(
                        [](LPDIRECTINPUTEFFECT, VOID*) -> BOOL { return DIENUM_CONTINUE; }, nullptr, 0));
                    printf("  uninit send = %08lX", Device->SendDeviceData(sizeof(Data), &Data, &Count, 0));
                    printf(" count %lu\n", Count);
                }
                printf("  init null guid hinst = %08lX\n", Device->Initialize(nullptr, DIRECTINPUT_VERSION, GUID_SysKeyboard));
                printf("  init 0x700 = %08lX\n", Device->Initialize(Instance, 0x700, GUID_SysKeyboard));
                printf("  init random = %08lX\n", Device->Initialize(Instance, DIRECTINPUT_VERSION, DcRandomGuid));
                printf("  init keyboard = %08lX\n", Device->Initialize(Instance, DIRECTINPUT_VERSION, GUID_SysKeyboard));
                printf("  caps = %08lX type %08lX\n", Device->GetCapabilities(&Caps), Caps.dwDevType);
                printf("  init mouse = %08lX\n", Device->Initialize(Instance, DIRECTINPUT_VERSION, GUID_SysMouse));
                printf("  caps = %08lX type %08lX\n", Device->GetCapabilities(&Caps), Caps.dwDevType);
                Device->Release();
            }
        }
        printf("  release = %lu\n", Factory->Release());
    }
    {
        VOID* Object = DC_SENTINEL;
        HRESULT Result = GetClass(CLSID_DirectInput8, DcRandomGuid, &Object);

        printf("class unknown iid = %08lX %s\n", Result, DcPointer(Object));
        Object = DC_SENTINEL;
        Result = GetClass(CLSID_DirectInput8, IID_IUnknown, &Object);
        printf("class iunknown = %08lX %s\n", Result, DcPointer(Object));
        if (Object != nullptr && Object != DC_SENTINEL)
            static_cast<IUnknown*>(Object)->Release();
    }
}

static
VOID
DcCheckJoystickFormat(VOID)
{
    typedef LPCDIDATAFORMAT (WINAPI* PDC_GET_DF)(VOID);
    PDC_GET_DF GetFormat = reinterpret_cast<PDC_GET_DF>(GetProcAddress(DcModule, "GetdfDIJoystick"));
    LPCDIDATAFORMAT Format;

    if (GetFormat == nullptr)
    {
        printf("getdfdijoystick missing\n");
        return;
    }
    Format = GetFormat();
    printf("joyformat size %lu objsize %lu flags %lx data %lu objects %lu\n", Format->dwSize, Format->dwObjSize,
           Format->dwFlags, Format->dwDataSize, Format->dwNumObjs);
    for (DWORD Index = 0; Index < Format->dwNumObjs; Index++)
    {
        const DIOBJECTDATAFORMAT* Object = &Format->rgodf[Index];

        printf("  %2lu %s ofs %lu type %08lX flags %08lX\n", Index, DcGuid(Object->pguid).c_str(), Object->dwOfs,
               Object->dwType, Object->dwFlags);
    }
}

static
VOID
DcCheckControlPanel(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInputDevice8W* Device = nullptr;

    if (Wide == nullptr)
        return;
    printf("cpl flags = %08lX\n", Wide->RunControlPanel(nullptr, 1));
    printf("cpl bad window = %08lX\n", Wide->RunControlPanel(reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x12345)), 0));
    if (SUCCEEDED(Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr)))
    {
        printf("device cpl flags = %08lX\n", Device->RunControlPanel(nullptr, 1));
        printf("device cpl bad window = %08lX\n",
               Device->RunControlPanel(reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x12345)), 0));
        Device->Release();
    }
    Wide->Release();
}

static
VOID
DcCheckCreateDevice(VOID)
{
    static const GUID* Guids[] =
    {
        &GUID_SysKeyboard, &GUID_SysMouse, &GUID_SysKeyboardEm, &GUID_SysKeyboardEm2, &GUID_SysMouseEm,
        &GUID_SysMouseEm2, &GUID_Joystick, &DcRandomGuid, &GUID_NULL,
    };
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInput8A* Ansi = DcCreateAnsi();

    if (Wide == nullptr || Ansi == nullptr)
        return;
    for (const GUID* Guid : Guids)
    {
        VOID* Device = DC_SENTINEL;
        HRESULT Result = Wide->CreateDevice(*Guid, reinterpret_cast<IDirectInputDevice8W**>(&Device), nullptr);

        printf("createdevice %s = %08lX %s", DcGuid(Guid).c_str(), Result, DcPointer(Device));
        if (SUCCEEDED(Result))
        {
            VOID* Other = nullptr;

            printf(" qi8a %08lX", static_cast<IUnknown*>(Device)->QueryInterface(IID_IDirectInputDevice8A, &Other));
            if (Other != nullptr)
                static_cast<IUnknown*>(Other)->Release();
            Other = nullptr;
            printf(" qi7w %08lX", static_cast<IUnknown*>(Device)->QueryInterface(IID_IDirectInputDevice7W, &Other));
            if (Other != nullptr)
                static_cast<IUnknown*>(Other)->Release();
            Other = nullptr;
            printf(" qidev %08lX", static_cast<IUnknown*>(Device)->QueryInterface(IID_IDirectInputDeviceW, &Other));
            if (Other != nullptr)
                static_cast<IUnknown*>(Other)->Release();
            Other = nullptr;
            printf(" qieffect %08lX", static_cast<IUnknown*>(Device)->QueryInterface(IID_IDirectInputEffect, &Other));
            if (Other != nullptr)
                static_cast<IUnknown*>(Other)->Release();
            printf(" release %lu", static_cast<IUnknown*>(Device)->Release());
        }
        printf("\n");
        Device = DC_SENTINEL;
        Result = Ansi->CreateDevice(*Guid, reinterpret_cast<IDirectInputDevice8A**>(&Device), nullptr);
        printf("createdevice ansi %s = %08lX %s\n", DcGuid(Guid).c_str(), Result, DcPointer(Device));
        if (SUCCEEDED(Result))
            static_cast<IUnknown*>(Device)->Release();
    }
    printf("createdevice null out = %08lX\n", Wide->CreateDevice(GUID_SysKeyboard, nullptr, nullptr));
    {
        DcOuter Outer;
        VOID* Device = DC_SENTINEL;
        HRESULT Result = Wide->CreateDevice(GUID_SysMouse, reinterpret_cast<IDirectInputDevice8W**>(&Device), &Outer);

        printf("createdevice outer = %08lX %s refs %lu\n", Result, DcPointer(Device), Outer.Refs);
        if (SUCCEEDED(Result))
        {
            IDirectInputDevice8W* Inner = nullptr;
            DIDEVCAPS Caps = { sizeof(Caps) };

            static_cast<IUnknown*>(Device)->QueryInterface(IID_IDirectInputDevice8W, reinterpret_cast<VOID**>(&Inner));
            if (Inner != nullptr)
            {
                printf("  outer caps before init = %08lX\n", Inner->GetCapabilities(&Caps));
                printf("  outer init = %08lX\n", Inner->Initialize(GetModuleHandleW(nullptr), 0x800, GUID_SysMouse));
                printf("  outer caps = %08lX type %08lX\n", Inner->GetCapabilities(&Caps), Caps.dwDevType);
                Inner->Release();
            }
            printf("  outer release = %lu\n", static_cast<IUnknown*>(Device)->Release());
        }
    }
    Ansi->Release();
    Wide->Release();
}

static const struct
{
    PCSTR Name;
    VOID (*Run)(VOID);
    BOOLEAN ByHand;
} DcChecks[] =
{
    { "create", DcCheckCreate },
    { "query", DcCheckQuery },
    { "enum", DcCheckEnum },
    { "status", DcCheckStatus },
    { "find", DcCheckFind },
    { "factory", DcCheckFactory },
    { "joyformat", DcCheckJoystickFormat },
    { "cpl", DcCheckControlPanel },
    { "createdevice", DcCheckCreateDevice },
    { "devinfo", DcCheckDeviceInfo },
    { "objects", [] { DcRunDataChecks("objects"); } },
    { "formats", [] { DcRunDataChecks("formats"); } },
    { "coop", [] { DcRunDataChecks("coop"); } },
    { "acquire", [] { DcRunDataChecks("acquire"); } },
    { "props", [] { DcRunDataChecks("props"); } },
    { "hidprops", [] { DcRunDataChecks("hidprops"); } },
    { "misc", [] { DcRunDataChecks("misc"); } },
    { "ffargs", [] { DcRunDataChecks("ffargs"); } },
    { "effects", DcCheckEffectFiles },
    { "actions", DcCheckActions },
    { "joyconfig", DcCheckJoyConfig },
    { "watch", [] { DcRunDataChecks("watch"); }, TRUE },
};

static
std::wstring
DcRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"dinput8.dll";
    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(nullptr, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\dinput8.dll";
    }
    return Runtime;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    PCWSTR Runtime = L"system";
    PCWSTR ReadPath = nullptr;
    std::vector<std::string> Wanted;
    WCHAR Path[MAX_PATH];

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = argv[++Index];
        }
        else if (_wcsicmp(argv[Index], L"-read") == 0 && Index + 1 < argc)
        {
            ReadPath = argv[++Index];
        }
        else
        {
            char Name[64];

            sprintf_s(Name, "%ls", argv[Index]);
            Wanted.push_back(Name);
        }
    }

    DcModule = LoadLibraryW(DcRuntimePath(Runtime).c_str());
    if (DcModule == nullptr)
    {
        fprintf(stderr, "error: cannot load the runtime (%lu)\n", GetLastError());
        return 1;
    }
    GetModuleFileNameW(DcModule, Path, _countof(Path));
    fprintf(stderr, "runtime %ls\n", Path);
    DcCreate = reinterpret_cast<PDC_CREATE>(GetProcAddress(DcModule, "DirectInput8Create"));
    if (DcCreate == nullptr)
    {
        fprintf(stderr, "error: no DirectInput8Create\n");
        return 1;
    }
    DcInitFormats();
    DcWindow = CreateWindowExW(0, L"STATIC", L"dinput8check", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);

    if (ReadPath != nullptr)
    {
        DcCheckEffectFileRead(ReadPath);
        return 0;
    }
    for (const auto& Check : DcChecks)
    {
        BOOLEAN Run = Wanted.empty() && !Check.ByHand;

        for (const auto& Name : Wanted)
        {
            if (Name == Check.Name)
                Run = TRUE;
        }
        if (!Run)
            continue;
        printf("[%s]\n", Check.Name);
        Check.Run();
        fflush(stdout);
    }
    if (DcWindow != nullptr)
        DestroyWindow(DcWindow);
    return 0;
}

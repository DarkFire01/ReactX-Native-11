/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The environmental reverb behind CreateAudioReverb
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

#include <math.h>

#pragma fp_contract(off)

static const XAPO_REGISTRATION_PROPERTIES XaRvbRegistration =
{
    __uuidof(AudioReverb),
    L"Reverb",
    L"Copyright (c) Microsoft Corporation",
    1,
    0,
    XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH |
        XAPO_FLAG_BUFFERCOUNT_MUST_MATCH | XAPO_FLAG_INPLACE_SUPPORTED,
    1,
    1,
    1,
    1,
};

/* The 2.8 parameter block: the 2.9 one without the side delay */
#pragma pack(push, 1)
struct XaRvbParameters28
{
    float WetDryMix;
    UINT32 ReflectionsDelay;
    BYTE ReverbDelay;
    BYTE RearDelay;
    BYTE PositionLeft;
    BYTE PositionRight;
    BYTE PositionMatrixLeft;
    BYTE PositionMatrixRight;
    BYTE EarlyDiffusion;
    BYTE LateDiffusion;
    BYTE LowEQGain;
    BYTE LowEQCutoff;
    BYTE HighEQGain;
    BYTE HighEQCutoff;
    float RoomFilterFreq;
    float RoomFilterMain;
    float RoomFilterHF;
    float ReflectionsGain;
    float ReverbGain;
    float DecayTime;
    float Density;
    float RoomSize;
    BOOL DisableLateField;
};
#pragma pack(pop)

/* Silence is replaced by this level so the network never runs into denormals */
#define XA_RVB_QUIET_BITS       0x219392EFu

#define XA_RVB_PRE_LINE         16384
#define XA_RVB_LONGEST_GAP      4095

#include "reverbtables.hpp"

/* fcvtzu to 32 bits */
static
inline
UINT32
XaRvbCount(
    _In_ float Value)
{
    UINT64 Count = XaFloatToCount(Value);

    return Count > MAXUINT32 ? MAXUINT32 : static_cast<UINT32>(Count);
}

/* fmax and fmin as the hardware does them, NaN in gives NaN out */
static
inline
float
XaRvbLarger(
    _In_ float Left,
    _In_ float Right)
{
    if (Left != Left || Right != Right)
        return Left + Right;
    return Left > Right ? Left : Right;
}

static
inline
float
XaRvbSmaller(
    _In_ float Left,
    _In_ float Right)
{
    if (Left != Left || Right != Right)
        return Left + Right;
    return Left < Right ? Left : Right;
}

static
inline
UINT32
XaRvbAtLeastOne(
    _In_ UINT32 Value)
{
    return Value != 0 ? Value : 1;
}

/* Schroeder all pass with its output delayed by one sample */
template <UINT32 Size>
struct XaRvbAllPass
{
    float Line[Size];
    UINT32 Write;
    UINT32 Read;
    UINT32 Length;
    float Gain;
    float Back;
    float Out;

    VOID
    SetLength(
        _In_ UINT32 NewLength)
    {
        Length = NewLength;
        Read = (Write - NewLength) & (Size - 1);
    }

    VOID
    SetGain(
        _In_ float NewGain)
    {
        Gain = NewGain;
        Back = -NewGain;
    }

    float
    Tap(VOID) const
    {
        return Line[Read];
    }

    /* Stores what the caller fed in and returns the previous output */
    float
    Feed(
        _In_ float Fed,
        _In_ float Tapped)
    {
        float Previous = Out;

        Line[Write] = Fed;
        Out = fmaf(Back, Fed, Tapped);
        Read = (Read + 1) & (Size - 1);
        Write = (Write + 1) & (Size - 1);
        return Previous;
    }

    float
    Step(
        _In_ float Input)
    {
        float Tapped = Tap();

        return Feed(fmaf(Gain, Tapped, Input), Tapped);
    }

    VOID
    Clear(VOID)
    {
        memset(Line, 0, sizeof(Line));
        Write = 0;
        Read = (0 - Length) & (Size - 1);
        Out = 0.0f;
    }
};

/* All pass whose output weighs the input and the tap on their own */
template <UINT32 Size>
struct XaRvbLattice
{
    float Line[Size];
    UINT32 Write;
    UINT32 Read;
    UINT32 Length;
    float Gain;
    float Through;
    float Back;
    float Out;

    VOID
    SetLength(
        _In_ UINT32 NewLength)
    {
        Length = NewLength;
        Read = (Write - NewLength) & (Size - 1);
    }

    float
    Step(
        _In_ float Input)
    {
        float Tapped = Line[Read];
        float Previous = Out;

        Out = fmaf(Through, Input, Back * Tapped);
        Line[Write] = fmaf(Gain, Tapped, Input);
        Read = (Read + 1) & (Size - 1);
        Write = (Write + 1) & (Size - 1);
        return Previous;
    }

    VOID
    Clear(VOID)
    {
        memset(Line, 0, sizeof(Line));
        Write = 0;
        Read = (0 - Length) & (Size - 1);
        Out = 0.0f;
    }
};

/* Delay line read at fixed taps; each tap's value is handed out one sample later */
template <UINT32 Size, UINT32 Taps>
struct XaRvbTapLine
{
    float Line[Size];
    UINT32 Write;
    UINT32 Delay[Taps];
    float Level[Taps];
    float Out[Taps];

    float
    Read(
        _In_ UINT32 Tap) const
    {
        return Line[(Write - Delay[Tap]) & (Size - 1)] * Level[Tap];
    }

    VOID
    Push(
        _In_ float Input)
    {
        Line[Write] = Input;
        Write = (Write + 1) & (Size - 1);
    }

    VOID
    Clear(VOID)
    {
        memset(Line, 0, sizeof(Line));
        Write = 0;
        memset(Out, 0, sizeof(Out));
    }
};

/* Two sample history read at one of its two slots, chosen by the rate */
struct XaRvbPair
{
    float History[2];
    UINT32 Pick;
    float A;
    float B;
    float C;
    float Out;

    float
    Picked(VOID) const
    {
        return History[Pick];
    }

    VOID
    Shift(
        _In_ float Value)
    {
        History[1] = History[0];
        History[0] = Value;
    }

    VOID
    Clear(VOID)
    {
        History[0] = 0.0f;
        History[1] = 0.0f;
        Out = 0.0f;
    }
};

/* One pole low pass taken from the history, output one sample late */
static
inline
float
XaRvbPole(
    _Inout_ XaRvbPair* Pair,
    _In_ float Input)
{
    float Previous = Pair->Out;

    Pair->Out = fmaf(Pair->Picked(), Pair->A, Input);
    Pair->Shift(Input);
    return Previous;
}

/* First order shelf: A feeds the history back, B and C mix the result */
static
inline
float
XaRvbShelf(
    _Inout_ XaRvbPair* Pair,
    _In_ float Input)
{
    float Previous = Pair->Out;
    float Held = Pair->Picked();
    float Middle = fmaf(Pair->A, Held, Input);

    Pair->Out = fmaf(Pair->C, Held, Pair->B * Middle);
    Pair->Shift(Middle);
    return Previous;
}

/* Recursive damping with a gain on the way out */
static
inline
float
XaRvbDamp(
    _Inout_ XaRvbPair* Pair,
    _In_ float Input)
{
    float Previous = Pair->Out;
    float Middle = fmaf(Pair->Picked(), Pair->A, Input);

    Pair->Shift(Middle);
    Pair->Out = Pair->B * Middle;
    return Previous;
}

/* Scaled history only */
static
inline
float
XaRvbHold(
    _Inout_ XaRvbPair* Pair,
    _In_ float Input)
{
    float Previous = Pair->Out;

    Pair->Out = Pair->Picked() * Pair->A;
    Pair->Shift(Input);
    return Previous;
}

/* Input and history weighed on their own */
static
inline
float
XaRvbBlend(
    _Inout_ XaRvbPair* Pair,
    _In_ float Input)
{
    float Previous = Pair->Out;

    Pair->Out = fmaf(Pair->A, Input, Pair->Picked() * Pair->B);
    Pair->Shift(Input);
    return Previous;
}

/* Reflections delay that fades between the old and new length */
struct XaRvbPreDelay
{
    float Line[XA_RVB_PRE_LINE];
    UINT32 Write;
    UINT32 Delay;
    UINT32 Previous;
    UINT32 Target;
    float Rate;
    float Fade;
    float Last;

    VOID
    SetDelay(
        _In_ UINT32 Samples)
    {
        if (Previous == Delay)
        {
            Delay = Samples;
            Fade = 1.0f;
        }
        Target = Samples;
    }

    /* Output runs one sample behind and needs Frames + 1 slots */
    VOID
    Run(
        _In_reads_(Frames) const float* Input,
        _Out_writes_(Frames + 1) float* Output,
        _In_ UINT32 Frames)
    {
        Output[0] = Last;
        for (UINT32 Frame = 0; Frame < Frames; Frame++)
        {
            float Fresh = Line[(Write - Delay) & (XA_RVB_PRE_LINE - 1)] * (1.0f - Fade);
            float Value = fmaf(Line[(Write - Previous) & (XA_RVB_PRE_LINE - 1)], Fade, Fresh);

            Output[Frame + 1] = Value;
            Last = Value;
            if (Fade > 0.0f)
            {
                Fade = Fade - 0.000199999995f;
            }
            else
            {
                UINT32 Reached = Delay;

                Previous = Reached;
                Delay = Target;
                Fade = Target == Reached ? 0.0f : 1.0f;
            }
            Line[Write] = Input[Frame];
            Write = (Write + 1) & (XA_RVB_PRE_LINE - 1);
        }
    }
};

/* Surround feed delay with the same fade, its taps weighed the other way round */
template <UINT32 Size>
struct XaRvbRear
{
    float Line[Size];
    UINT32 Write;
    UINT32 Delay;
    UINT32 Previous;
    UINT32 Target;
    float Rate;
    float Fade;
    float Last;

    VOID
    SetDelay(
        _In_ UINT32 Samples)
    {
        if (Previous == Delay)
        {
            Delay = Samples;
            Fade = 1.0f;
        }
        Target = Samples;
    }

    VOID
    Retime(
        _In_ float NewRate)
    {
        float Ratio = NewRate / Rate;

        Rate = NewRate;
        Delay = XaRvbCount(fmaf(Ratio, static_cast<float>(Delay), 0.5f));
        Previous = Delay;
    }

    float
    Step(
        _In_ float Input)
    {
        float Before = Last;
        float Older = Line[(Write - Previous) & (Size - 1)] * Fade;

        Last = fmaf(Line[(Write - Delay) & (Size - 1)], 1.0f - Fade, Older);
        if (Fade > 0.0f)
        {
            Fade = Fade - 0.000199999995f;
        }
        else
        {
            UINT32 Reached = Delay;

            Previous = Reached;
            Delay = Target;
            Fade = Target == Reached ? 0.0f : 1.0f;
        }
        Line[Write] = Input;
        Write = (Write + 1) & (Size - 1);
        return Before;
    }

    VOID
    Clear(VOID)
    {
        memset(Line, 0, sizeof(Line));
        Write = 0;
        Last = 0.0f;
    }
};

/* Room filter: a gain and two matched one pole stages, also one sample late */
struct XaRvbRoom
{
    float Rate;
    float Frequency;
    float Main;
    float High;
    float StageA;
    float StageB;
    float Gain;
    float Pole;

    VOID
    Update(VOID)
    {
        float Total = High + Main;
        float Share = Total >= 0.0f || Total != Total ? 1.0f : Main / Total;
        float Upper = powf(10.0f, ((1.0f - Share) * Total) * 0.100000001f);
        float Root = sqrtf(Upper);

        Gain = sqrtf(powf(10.0f, (Share * Total) * 0.0500000007f));
        if (!(Root < 0.995000005f))
        {
            Pole = 0.0f;
        }
        else
        {
            float Cosine = cosf((Frequency * 6.28318548f) / Rate);
            float Spread = (Root + Root) * (1.0f - Cosine);
            float Inside = fmaf(-fmaf(-Cosine, Cosine, 1.0f), Upper, Spread);

            Pole = (fmaf(-Root, Cosine, 1.0f) - sqrtf(Inside)) / (1.0f - Root);
        }
    }

    VOID
    Run(
        _Inout_updates_(Frames) float* Samples,
        _In_ UINT32 Frames)
    {
        float Scale = (1.0f - Pole) * Gain;

        for (UINT32 Frame = 0; Frame < Frames; Frame++)
        {
            float Input = Samples[Frame];
            float NextA = fmaf(Input, Scale, StageA * Pole);
            float NextB = fmaf(StageA, Scale, StageB * Pole);

            Samples[Frame] = StageB;
            StageA = NextA;
            StageB = NextB;
        }
    }
};

/* Lengths taken from the room size and density */
struct XaRvbShape
{
    float Near;
    float Far;
    float Lengths[12];
};

static
VOID
XaRvbMeasure(
    _Out_ XaRvbShape* Shape,
    _In_ const XAUDIO2FX_REVERB_PARAMETERS* Parameters)
{
    float Size = Parameters->RoomSize;
    float Density = Parameters->Density;
    float Base = Size < 50.0f ? fmaf(Size, 0.00400000019f, 0.299999982f) : Size * 0.00999999978f;
    float Twice;
    float* Length = Shape->Lengths;

    Shape->Near = XaRvbLarger(Base, 0.300000012f);
    Twice = Shape->Near + Shape->Near;
    Shape->Far = 0.300000012f;
    if (!(Twice < 0.300000012f))
        Shape->Far = XaRvbSmaller(Twice, 1.0f);

    /* Each density band brings more of the late lines up to their full length */
    memset(Shape->Lengths, 0, sizeof(Shape->Lengths));
    if (Density >= 75.0f)
    {
        float Over = Density - 75.0f;

        Length[0] = Over * 16.3599987f;
        Length[1] = Over * 137.279999f;
        Length[2] = Over * 15.3199997f;
        Length[3] = 3820.0f;
        Length[4] = 1511.0f;
        Length[5] = 1061.0f;
        Length[6] = Over * 21.6399994f;
        Length[7] = 1510.0f;
        Length[8] = 1657.0f;
        Length[9] = 1103.0f;
        Length[10] = Over * 35.4799995f;
        Length[11] = Over * 19.6399994f;
    }
    else if (Density >= 50.0f)
    {
        float Over = Density - 50.0f;

        Length[3] = Over * 152.800003f;
        Length[4] = 1511.0f;
        Length[5] = Over * 42.4399986f;
        Length[7] = 1510.0f;
        Length[8] = 1657.0f;
        Length[9] = Over * 44.1199989f;
    }
    else if (Density >= 25.0f)
    {
        float Over = Density - 25.0f;

        Length[4] = Over * 60.4399986f;
        Length[7] = 1510.0f;
        Length[8] = Over * 66.2799988f;
    }
    else
    {
        Length[7] = Density * 60.3999977f;
    }
}

/* Delays are counted at 48 samples a millisecond whatever the rate; zero still gives one */
static
inline
UINT32
XaRvbMilliseconds(
    _In_ UINT32 Value)
{
    return Value != 0 ? XaRvbCount(static_cast<float>(Value) * 48.0f) : 1;
}

static
inline
UINT32
XaRvbPrimeOf(
    _In_ float Value)
{
    return XaReverbPrime(XaRvbCount(Value));
}

/* One late branch: diffusion, tone shaping and the recirculating line */
struct XaRvbBranch
{
    XaRvbAllPass<512> Smear;
    XaRvbPair Low;
    XaRvbPair Shelf;
    XaRvbPair Damp;
    XaRvbTapLine<4096, 2> Loop;
    XaRvbPair Tail;
};

/* Reflections and their diffusion for one side, and the two all passes it leaves through */
struct XaRvbEarly
{
    float Main;
    float Side;
    XaRvbTapLine<512, 3> Reflect;
    XaRvbAllPass<128> SpreadA;
    XaRvbTapLine<2048, 3> Echo;
    XaRvbAllPass<256> SpreadB;
    XaRvbAllPass<512> SpreadC;
    XaRvbTapLine<1024, 2> Feed;
    XaRvbAllPass<512> Outlet1;
    XaRvbAllPass<512> Outlet2;

    /* What one sample hands the late field and the outlets */
    float Sum;
    float EchoThird;
    float FeedFar;
    float FeedNear;

    VOID
    Step(
        _In_ float Input)
    {
        float Taps = Reflect.Out[0] + Reflect.Out[1];
        float Third = Reflect.Out[2];
        float First;
        float EchoTaps;
        float Second;

        Reflect.Out[1] = Reflect.Read(1);
        Reflect.Out[0] = Reflect.Read(0);
        Reflect.Out[2] = Reflect.Read(2);
        Reflect.Push(Input);
        First = SpreadA.Step(Taps);
        EchoTaps = Echo.Out[0] + Echo.Out[1];
        EchoThird = Echo.Out[2];
        Echo.Out[1] = Echo.Read(1);
        Echo.Out[0] = Echo.Read(0);
        Echo.Out[2] = Echo.Read(2);
        Echo.Push(First);
        Second = SpreadB.Step(EchoTaps);
        FeedFar = Feed.Out[1];
        FeedNear = Feed.Out[0];
        Feed.Out[0] = Feed.Read(0);
        Feed.Out[1] = Feed.Read(1);
        Feed.Push(SpreadC.Step(Second));
        Sum = fmaf(Side, Second, fmaf(Main, First, Third));
    }

    float
    Finish(
        _In_ float Tail,
        _In_ float Reflections)
    {
        return Outlet2.Step(Outlet1.Step(fmaf(Sum, Reflections + Reflections, Tail + Tail)));
    }

    VOID
    Clear(VOID)
    {
        Reflect.Clear();
        SpreadA.Clear();
        Echo.Clear();
        SpreadB.Clear();
        SpreadC.Clear();
        Feed.Clear();
        Outlet1.Clear();
        Outlet2.Clear();
    }
};

/* Interleaved stereo reflections delay; the counts are in floats, so twice the frames */
struct XaRvbPairDelay
{
    float Line[XA_RVB_PRE_LINE * 2];
    UINT32 Write;
    UINT32 Delay;
    UINT32 Previous;
    UINT32 Target;
    float Rate;
    float Fade;
    float Last[2];

    /* The stereo network rounds the fade with the new tap last, the surround ones with the old tap last */
    BOOLEAN NewLast;

    VOID
    SetDelay(
        _In_ UINT32 Floats)
    {
        if (Previous == Delay)
        {
            Delay = Floats;
            Fade = 1.0f;
        }
        Target = Floats;
    }

    VOID
    Run(
        _In_reads_(Frames * 2) const float* Input,
        _Out_writes_((Frames + 1) * 2) float* Output,
        _In_ UINT32 Frames)
    {
        const UINT32 Mask = XA_RVB_PRE_LINE * 2 - 1;

        Output[0] = Last[0];
        Output[1] = Last[1];
        for (UINT32 Frame = 0; Frame < Frames; Frame++)
        {
            float Keep = 1.0f - Fade;
            float NewLeft = Line[(Write - Delay) & Mask];
            float NewRight = Line[(Write - Delay - 1) & Mask];
            float OldLeft = Line[(Write - Previous) & Mask];
            float OldRight = Line[(Write - Previous - 1) & Mask];

            if (NewLast)
            {
                Last[0] = fmaf(Keep, NewLeft, OldLeft * Fade);
                Last[1] = fmaf(Keep, NewRight, OldRight * Fade);
            }
            else
            {
                Last[0] = fmaf(OldLeft, Fade, NewLeft * Keep);
                Last[1] = fmaf(OldRight, Fade, NewRight * Keep);
            }
            Output[Frame * 2 + 2] = Last[0];
            Output[Frame * 2 + 3] = Last[1];
            if (Fade > 0.0f)
            {
                Fade = Fade - 0.000199999995f;
            }
            else
            {
                UINT32 Reached = Delay;

                Previous = Reached;
                Delay = Target;
                Fade = Target == Reached ? 0.0f : 1.0f;
            }
            Line[Write] = Input[Frame * 2];
            Write = (Write + 1) & Mask;
            Line[Write] = Input[Frame * 2 + 1];
            Write = (Write + 1) & Mask;
        }
    }
};

/* Interleaved surround feed delay; the left tap fades like the reflections delay, the right like the rear one */
template <UINT32 Size>
struct XaRvbPairRear
{
    float Line[Size];
    UINT32 Write;
    UINT32 Delay;
    UINT32 Previous;
    UINT32 Target;
    float Rate;
    float Fade;
    float Last[2];

    VOID
    SetDelay(
        _In_ UINT32 Floats)
    {
        if (Previous == Delay)
        {
            Delay = Floats;
            Fade = 1.0f;
        }
        Target = Floats;
    }

    VOID
    Retime(
        _In_ float NewRate)
    {
        float Ratio = NewRate / Rate;

        Rate = NewRate;
        Delay = 2 * XaRvbCount(fmaf(Ratio, static_cast<float>(Delay), 0.5f));
        Previous = Delay;
    }

    VOID
    Step(
        _In_ float Left,
        _In_ float Right,
        _Out_writes_(2) float* Before)
    {
        float Keep = 1.0f - Fade;

        Before[0] = Last[0];
        Before[1] = Last[1];
        Last[0] = fmaf(Line[(Write - Previous) & (Size - 1)], Fade, Line[(Write - Delay) & (Size - 1)] * Keep);
        Last[1] = fmaf(Line[(Write - Delay - 1) & (Size - 1)], Keep, Line[(Write - Previous - 1) & (Size - 1)] * Fade);
        if (Fade > 0.0f)
        {
            Fade = Fade - 0.000199999995f;
        }
        else
        {
            UINT32 Reached = Delay;

            Previous = Reached;
            Delay = Target;
            Fade = Target == Reached ? 0.0f : 1.0f;
        }
        Line[Write] = Left;
        Write = (Write + 1) & (Size - 1);
        Line[Write] = Right;
        Write = (Write + 1) & (Size - 1);
    }

    VOID
    Clear(VOID)
    {
        memset(Line, 0, sizeof(Line));
        Write = 0;
        Last[0] = 0.0f;
        Last[1] = 0.0f;
    }
};

/* The room filter run on both channels of an interleaved buffer */
struct XaRvbPairRoom
{
    XaRvbRoom Shape;
    float StageA[2];
    float StageB[2];

    VOID
    Run(
        _Inout_updates_(Frames * 2) float* Samples,
        _In_ UINT32 Frames)
    {
        float Pole = Shape.Pole;
        float Scale = (1.0f - Pole) * Shape.Gain;

        for (UINT32 Frame = 0; Frame < Frames; Frame++)
        {
            for (UINT32 Channel = 0; Channel < 2; Channel++)
            {
                float Input = Samples[Frame * 2 + Channel];
                float NextA = fmaf(Input, Scale, StageA[Channel] * Pole);
                float NextB = fmaf(StageA[Channel], Scale, StageB[Channel] * Pole);

                Samples[Frame * 2 + Channel] = StageB[Channel];
                StageA[Channel] = NextA;
                StageB[Channel] = NextB;
            }
        }
    }
};

/*
 * The reverb network. Mono runs one early section into the late field; stereo adds a second
 * early section for the right channel with its own reflections delay and room filter, and the
 * late field then takes each side's taps where the mono one takes the single set.
 */
class XaRvbNetwork
{
public:
    VOID
    Construct(
        _In_ BOOLEAN NewStereo,
        _In_ UINT32 NewChannels);

    VOID
    Tune(
        _In_ float NewRate);

    VOID
    Configure(
        _In_ const XAUDIO2FX_REVERB_PARAMETERS* Parameters);

    VOID
    Clear(VOID);

    VOID
    Process(
        _In_ const float* Input,
        _Out_writes_(Frames * Channels) float* Output,
        _Inout_ float* Scratch,
        _In_ UINT32 Frames,
        _In_ float Step);

    BOOLEAN Stereo;
    UINT32 Channels;

private:
    XaRvbPreDelay Pre;
    XaRvbRoom Room;
    XaRvbEarly Left;

    XaRvbAllPass<512> InletA;
    XaRvbAllPass<512> InletB;
    XaRvbBranch BranchA;
    XaRvbBranch BranchB;
    XaRvbAllPass<2048> CrossA;
    XaRvbLattice<2048> ChainA1;
    XaRvbAllPass<2048> ChainA2;
    XaRvbAllPass<1024> ChainA3;
    XaRvbTapLine<4096, 2> ChainALoop;
    XaRvbPair ChainATail;
    XaRvbAllPass<2048> CrossB;
    XaRvbLattice<2048> ChainB1;
    XaRvbAllPass<2048> ChainB2;
    XaRvbAllPass<1024> ChainB3;
    XaRvbTapLine<4096, 2> ChainBLoop;
    XaRvbPair ChainBTail;

    float Wet;
    float Reflections;
    BOOL LateOff;
    XaRvbRear<256> Side;
    XaRvbRear<1024> Back;

    XaRvbPairDelay PairPre;
    XaRvbPairRoom PairRoom;
    XaRvbEarly Right;
    XaRvbAllPass<512> CenterA;
    XaRvbAllPass<512> CenterB;
    XaRvbPairRear<512> PairSide;
    XaRvbPairRear<2048> PairBack;
};

VOID
XaRvbNetwork::Construct(
    _In_ BOOLEAN NewStereo,
    _In_ UINT32 NewChannels)
{
    XaRvbBranch* Branches[2] = { &BranchA, &BranchB };

    memset(this, 0, sizeof(*this));
    Pre.Delay = 40;
    Pre.Previous = 40;
    Pre.Target = 40;
    Pre.Rate = 48000.0f;
    Room.Rate = 48000.0f;
    Room.Frequency = 24000.0f;
    Room.Update();

    /* Coefficients the parameters never touch */
    for (UINT32 Index = 0; Index < ARRAYSIZE(Branches); Index++)
        Branches[Index]->Loop.Level[0] = 1.0f;
    ChainALoop.Level[0] = 1.0f;
    ChainBLoop.Level[0] = 1.0f;
    CrossA.SetGain(0.617999971f);
    CrossB.SetGain(0.617999971f);
    ChainA1.Gain = 0.617999971f;
    ChainA1.Through = 0.617999971f;
    ChainA1.Back = -0.617999971f;
    ChainB1.Gain = 0.617999971f;
    ChainB1.Through = 0.617999971f;
    ChainB1.Back = -0.617999971f;
    ChainA2.SetGain(0.617999971f);
    ChainB2.SetGain(0.617999971f);
    Wet = 1.0f;
    Reflections = 1.0f;
    Stereo = NewStereo;
    Channels = NewChannels;
    Side.Delay = 20;
    Side.Previous = 20;
    Side.Target = 20;
    Side.Rate = 48000.0f;
    Back.Delay = 80;
    Back.Previous = 80;
    Back.Target = 80;
    Back.Rate = 48000.0f;
    PairPre.Delay = 80;
    PairPre.Previous = 80;
    PairPre.Target = 80;
    PairPre.Rate = 48000.0f;
    PairPre.NewLast = NewChannels == 2;
    PairRoom.Shape.Rate = 48000.0f;
    PairRoom.Shape.Frequency = 24000.0f;
    PairRoom.Shape.Update();
    PairSide.Delay = 40;
    PairSide.Previous = 40;
    PairSide.Target = 40;
    PairSide.Rate = 48000.0f;
    PairBack.Delay = 160;
    PairBack.Previous = 160;
    PairBack.Target = 160;
    PairBack.Rate = 48000.0f;
}

VOID
XaRvbNetwork::Tune(
    _In_ float NewRate)
{
    float Ratio = NewRate / Pre.Rate;
    UINT32 Pick = XaRvbCount(fmaf(NewRate, 2.08333331e-05f, 0.5f));
    XaRvbBranch* Branches[2] = { &BranchA, &BranchB };
    XaRvbTapLine<4096, 2>* Loops[4] = { &BranchA.Loop, &BranchB.Loop, &ChainALoop, &ChainBLoop };
    static const float LoopTaps[4] = { 0.0714999959f, 0.0795833319f, 0.0314583331f, 0.0299583338f };
    UINT32 Index;

    Pre.Rate = NewRate;
    Pre.Delay = XaRvbCount(fmaf(Ratio, static_cast<float>(Pre.Delay), 0.5f));
    Pre.Previous = Pre.Delay;
    Room.Rate = NewRate;
    Room.Update();

    Left.Reflect.Delay[0] = XaRvbPrimeOf(fmaf(NewRate, 0.0106041664f, 0.5f));
    Left.Reflect.Delay[1] = Left.Reflect.Delay[0] - 1;
    Left.Reflect.Delay[2] = Pick;
    Left.SpreadA.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0017291666f, 0.5f)));
    Left.Echo.Delay[0] = XaRvbPrimeOf(fmaf(NewRate, 0.0424791649f, 0.5f));
    Left.Echo.Delay[1] = Left.Echo.Delay[0] - 1;
    Left.Echo.Delay[2] = XaRvbPrimeOf(fmaf(NewRate, 0.0125000002f, 0.5f));
    Left.SpreadB.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00439583324f, 0.5f)));
    Left.SpreadC.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00647916645f, 0.5f)));
    Left.Feed.Delay[0] = XaRvbCount(fmaf(NewRate, 0.0212500002f, 0.5f));
    Left.Feed.Delay[1] = Pick;

    InletA.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00852083322f, 0.5f)));
    BranchA.Smear.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0053541665f, 0.5f)));
    InletB.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00797916669f, 0.5f)));
    BranchB.Smear.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00485416641f, 0.5f)));
    for (Index = 0; Index < ARRAYSIZE(Branches); Index++)
    {
        Branches[Index]->Low.Pick = Pick;
        Branches[Index]->Shelf.Pick = Pick;
        Branches[Index]->Damp.Pick = Pick;
        Branches[Index]->Tail.Pick = Pick;
    }

    /* The second loop tap keeps its length in time */
    for (Index = 0; Index < ARRAYSIZE(Loops); Index++)
    {
        float Held = static_cast<float>(Loops[Index]->Delay[1]) * NewRate;

        Loops[Index]->Delay[0] = XaRvbPrimeOf(fmaf(NewRate, LoopTaps[Index], 0.5f));
        Loops[Index]->Delay[1] = XaRvbPrimeOf(fmaf(Held, 2.08333331e-05f, 0.5f));
    }
    ChainATail.Pick = Pick;
    ChainBTail.Pick = Pick;

    CrossA.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.031479165f, 0.5f)));
    ChainA1.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0221041664f, 0.5f)));
    ChainA2.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0177708324f, 0.5f)));
    ChainA3.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0112708332f, 0.5f)));
    CrossB.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0345208347f, 0.5f)));
    ChainB1.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0229791664f, 0.5f)));
    ChainB2.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0184791666f, 0.5f)));
    ChainB3.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0102291666f, 0.5f)));
    Left.Outlet1.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00272916653f, 0.5f)));
    Left.Outlet2.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.0023541667f, 0.5f)));
    if (Channels > 2 && !Stereo)
        Side.Retime(NewRate);
    if (Channels == 8 && !Stereo)
        Back.Retime(NewRate);
    if (!Stereo)
        return;

    /* The stereo delay rescales its float count and doubles it once more */
    Ratio = NewRate / PairPre.Rate;
    PairPre.Rate = NewRate;
    PairPre.Delay = 2 * XaRvbCount(fmaf(Ratio, static_cast<float>(PairPre.Delay), 0.5f));
    PairPre.Previous = PairPre.Delay;
    PairRoom.Shape.Rate = NewRate;
    PairRoom.Shape.Update();
    Right.Reflect.Delay[0] = XaRvbPrimeOf(fmaf(NewRate, 0.0106041664f, 0.5f));
    Right.Reflect.Delay[1] = Right.Reflect.Delay[0] - 1;
    Right.Reflect.Delay[2] = Pick;
    Right.SpreadA.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00202083332f, 0.5f)));
    Right.Echo.Delay[0] = XaRvbPrimeOf(fmaf(NewRate, 0.0270208325f, 0.5f));
    Right.Echo.Delay[1] = Right.Echo.Delay[0] - 1;
    Right.Echo.Delay[2] = XaRvbPrimeOf(fmaf(NewRate, 0.0125000002f, 0.5f));
    Right.SpreadB.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00464583328f, 0.5f)));
    Right.SpreadC.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00610416662f, 0.5f)));
    Right.Feed.Delay[0] = XaRvbCount(fmaf(NewRate, 0.0212500002f, 0.5f));
    Right.Feed.Delay[1] = Pick;
    Right.Outlet1.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00222916668f, 0.5f)));
    Right.Outlet2.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00264583342f, 0.5f)));
    if (Channels > 2)
    {
        CenterA.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00214583334f, 0.5f)));
        CenterB.SetLength(XaRvbPrimeOf(fmaf(NewRate, 0.00202083332f, 0.5f)));
        PairSide.Retime(NewRate);
    }
    if (Channels == 8)
        PairBack.Retime(NewRate);
}

VOID
XaRvbNetwork::Configure(
    _In_ const XAUDIO2FX_REVERB_PARAMETERS* Parameters)
{
    XaRvbShape Shape;
    UINT32 Gap;
    UINT32 Total;
    UINT32 Low = Parameters->LowEQGain * 10u + Parameters->LowEQCutoff;
    UINT32 High = Parameters->HighEQGain * 15u + Parameters->HighEQCutoff;
    UINT32 MatrixLeft = Parameters->PositionMatrixLeft;
    float Loss;
    float Bright;
    float Dark;
    float Early;
    float Late;
    float Level;
    float FeedGap;
    XaRvbBranch* Branches[2] = { &BranchA, &BranchB };

    Gap = XaMin(XaReverbPrime(XaRvbMilliseconds(Parameters->ReverbDelay)), static_cast<UINT32>(XA_RVB_LONGEST_GAP));
    BranchA.Loop.Delay[1] = Gap;
    BranchB.Loop.Delay[1] = Gap;
    ChainALoop.Delay[1] = Gap;
    ChainBLoop.Delay[1] = Gap;

    XaRvbMeasure(&Shape, Parameters);
    FeedGap = Parameters->ReverbDelay != 0 ? 9.0f : 1020.0f;
    Left.Reflect.Delay[0] = XaRvbPrimeOf(Shape.Far * 509.0f);
    Left.Reflect.Delay[1] = Left.Reflect.Delay[0] - 1;
    Left.Echo.Delay[0] = XaRvbPrimeOf(Shape.Far * 2039.0f);
    Left.Echo.Delay[1] = Left.Echo.Delay[0] - 1;
    Left.Feed.Delay[0] = XaRvbPrimeOf(FeedGap * Shape.Far);
    InletA.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[0])));
    BranchA.Smear.SetLength(XaRvbPrimeOf(Shape.Far * 257.0f));
    BranchA.Loop.Delay[0] = XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Near * Shape.Lengths[1]));
    InletB.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[2])));
    BranchB.Smear.SetLength(XaRvbPrimeOf(Shape.Far * 233.0f));
    BranchB.Loop.Delay[0] = XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Near * Shape.Lengths[3]));
    CrossA.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[4])));
    ChainA1.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[5])) - 1);
    ChainA2.SetLength(XaRvbPrimeOf(Shape.Far * 853.0f));
    ChainA3.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[6])));
    ChainALoop.Delay[0] = XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Near * Shape.Lengths[7]));
    CrossB.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[8])));
    ChainB1.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[9])));
    ChainB2.SetLength(XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Far * Shape.Lengths[10])));
    ChainB3.SetLength(XaRvbPrimeOf(Shape.Far * Shape.Lengths[11]));
    ChainBLoop.Delay[0] = XaRvbAtLeastOne(XaRvbPrimeOf(Shape.Near * 1438.0f));

    /* The decay follows half the summed loop length, timed at 48 kHz whatever the rate */
    Total = BranchA.Loop.Delay[0] + BranchB.Loop.Delay[0] + ChainALoop.Delay[0] + ChainBLoop.Delay[0] +
            InletA.Length + BranchA.Smear.Length + InletB.Length + BranchB.Smear.Length +
            CrossA.Length + ChainA1.Length + ChainA2.Length + ChainA3.Length +
            CrossB.Length + ChainB1.Length + ChainB2.Length + ChainB3.Length;
    Loss = powf(10.0f, -3.0f / (Parameters->DecayTime * (48000.0f / static_cast<float>(Total >> 1))));
    Loss = sqrtf(Loss * 0.5f);
    Bright = fmaf(-Loss, 0.629999995f, 0.899999976f);
    Dark = fmaf(-Loss, -0.0787499994f, -0.112499997f);
    Early = XaRvbDiffusion[Parameters->EarlyDiffusion];
    Late = XaRvbDiffusion[Parameters->LateDiffusion];
    Pre.SetDelay(XaRvbMilliseconds(Parameters->ReflectionsDelay));

    Left.Reflect.Level[0] = 0.200000003f;
    Left.Reflect.Level[1] = 0.400000006f;
    Left.Reflect.Level[2] = XaRvbTapLevel[Parameters->PositionLeft];
    Left.SpreadA.SetGain(Early);
    Left.Echo.Level[0] = 0.200000003f;
    Left.Echo.Level[1] = 0.600000024f;
    Left.Echo.Level[2] = XaRvbMatrixLevel[MatrixLeft] * -0.125f;
    Left.SpreadB.SetGain(Early);
    Left.SpreadC.SetGain(Early);
    Left.Feed.Level[0] = Bright * XaRvbMatrixEcho[MatrixLeft];
    Left.Feed.Level[1] = Bright * XaRvbMatrixSpread[MatrixLeft];
    Left.Main = XaRvbEarlyMain[Parameters->PositionLeft];
    Left.Side = XaRvbEarlySide[Parameters->PositionLeft];

    InletA.SetGain(Late);
    InletB.SetGain(Late);
    for (UINT32 Index = 0; Index < ARRAYSIZE(Branches); Index++)
    {
        XaRvbBranch* Branch = Branches[Index];

        Branch->Smear.SetGain(Late);
        Branch->Low.A = XaRvbLowPole[Low];
        Branch->Shelf.B = XaRvbHighFeed[Parameters->HighEQCutoff];
        Branch->Shelf.A = XaRvbLowZero[Low];
        Branch->Shelf.C = XaRvbHighMix[High];
        Branch->Damp.B = XaRvbHighGain[High];
        Branch->Damp.A = XaRvbHighPole[Parameters->HighEQCutoff];
        Branch->Tail.A = Loss;
    }
    ChainA3.SetGain(Late);
    ChainATail.A = Loss * 0.5f;
    ChainATail.B = Loss * 0.5f;
    ChainB3.SetGain(Late);
    ChainBTail.A = Loss * -0.5f;
    ChainBTail.B = Loss * -0.5f;

    Level = powf(10.0f, Parameters->ReverbGain * 0.0500000007f);
    BranchA.Loop.Level[1] = Level * XaRvbMatrixLevel[MatrixLeft];
    BranchB.Loop.Level[1] = Level * XaRvbMatrixLevel[MatrixLeft];
    ChainALoop.Level[1] = Level * (Dark * XaRvbMatrixSpread[MatrixLeft]);
    ChainBLoop.Level[1] = Level * (Dark * XaRvbMatrixSpread[MatrixLeft]);
    Reflections = powf(10.0f, Parameters->ReflectionsGain * 0.0500000007f);
    Left.Outlet1.Gain = Late * 0.600000024f;
    Left.Outlet1.Back = Late * -0.600000024f;
    Left.Outlet2.Gain = Late * 0.600000024f;
    Left.Outlet2.Back = Late * -0.600000024f;

    Room.Frequency = Parameters->RoomFilterFreq;
    Room.Main = Parameters->RoomFilterMain;
    Room.High = Parameters->RoomFilterHF;
    Room.Update();
    Wet = Parameters->WetDryMix * 0.00999999978f;
    LateOff = Parameters->DisableLateField;

    /* 5.1 feeds the rear pair through the rear delay; 7.1 adds the side delay and moves the rear one */
    if (!Stereo && Channels == 6)
    {
        Side.SetDelay(XaRvbMilliseconds(Parameters->RearDelay));
    }
    else if (!Stereo && Channels == 8)
    {
        Side.SetDelay(XaRvbMilliseconds(Parameters->SideDelay));
        Back.SetDelay(XaRvbMilliseconds(Parameters->RearDelay));
    }
    if (!Stereo)
        return;

    /* The right side takes the right positions, and the left side's main early level moves with it */
    Right.Reflect.Delay[0] = XaRvbPrimeOf(Shape.Far * 509.0f);
    Right.Reflect.Delay[1] = Right.Reflect.Delay[0] - 1;
    Right.Echo.Delay[0] = XaRvbPrimeOf(Shape.Far * 1297.0f);
    Right.Echo.Delay[1] = Right.Echo.Delay[0] - 1;
    Right.Feed.Delay[0] = XaRvbPrimeOf(FeedGap * Shape.Far);
    PairPre.SetDelay(2 * XaRvbMilliseconds(Parameters->ReflectionsDelay));
    Left.Main = XaRvbEarlyMain[Parameters->PositionRight];
    Right.Main = XaRvbEarlyMain[Parameters->PositionLeft];
    Right.Side = XaRvbEarlySide[Parameters->PositionRight];
    Right.Reflect.Level[0] = 0.200000003f;
    Right.Reflect.Level[1] = 0.400000006f;
    Right.Reflect.Level[2] = XaRvbTapLevel[Parameters->PositionRight];
    Right.SpreadA.SetGain(Early);
    Right.Echo.Level[0] = 0.200000003f;
    Right.Echo.Level[1] = 0.600000024f;
    Right.Echo.Level[2] = XaRvbMatrixLevel[Parameters->PositionMatrixRight] * -0.125f;
    Right.SpreadB.SetGain(Early);
    Right.SpreadC.SetGain(Early);
    Right.Feed.Level[0] = Bright * XaRvbMatrixEcho[Parameters->PositionMatrixRight];
    Right.Feed.Level[1] = Bright * XaRvbMatrixSpread[Parameters->PositionMatrixRight];
    BranchB.Loop.Level[1] = Level * XaRvbMatrixLevel[Parameters->PositionMatrixRight];
    ChainBLoop.Level[1] = Level * (Dark * XaRvbMatrixSpread[Parameters->PositionMatrixRight]);
    Right.Outlet1.Gain = Late * 0.600000024f;
    Right.Outlet1.Back = Late * -0.600000024f;
    Right.Outlet2.Gain = Late * 0.600000024f;
    Right.Outlet2.Back = Late * -0.600000024f;
    PairRoom.Shape.Frequency = Parameters->RoomFilterFreq;
    PairRoom.Shape.Main = Parameters->RoomFilterMain;
    PairRoom.Shape.High = Parameters->RoomFilterHF;
    PairRoom.Shape.Update();
    if (Channels <= 2)
        return;

    /* 7.1 sends both its delays to the back line one after the other; the side line keeps its start */
    CenterA.Gain = Late * 0.600000024f;
    CenterA.Back = Late * -0.600000024f;
    CenterB.Gain = Late * 0.600000024f;
    CenterB.Back = Late * -0.600000024f;
    if (Channels == 6)
    {
        PairSide.SetDelay(2 * XaRvbMilliseconds(Parameters->RearDelay));
    }
    else
    {
        PairBack.SetDelay(2 * XaRvbMilliseconds(Parameters->SideDelay));
        PairBack.SetDelay(2 * XaRvbMilliseconds(Parameters->RearDelay));
    }
}

VOID
XaRvbNetwork::Clear(VOID)
{
    XaRvbBranch* Branches[2] = { &BranchA, &BranchB };

    memset(Pre.Line, 0, sizeof(Pre.Line));
    Pre.Write = 0;
    Pre.Last = 0.0f;
    Room.StageA = 0.0f;
    Room.StageB = 0.0f;
    Left.Clear();
    InletA.Clear();
    InletB.Clear();
    for (UINT32 Index = 0; Index < ARRAYSIZE(Branches); Index++)
    {
        Branches[Index]->Smear.Clear();
        Branches[Index]->Low.Clear();
        Branches[Index]->Shelf.Clear();
        Branches[Index]->Damp.Clear();
        Branches[Index]->Loop.Clear();
        Branches[Index]->Tail.Clear();
    }
    CrossA.Clear();
    ChainA1.Clear();
    ChainA2.Clear();
    ChainA3.Clear();
    ChainALoop.Clear();
    ChainATail.Clear();
    CrossB.Clear();
    ChainB1.Clear();
    ChainB2.Clear();
    ChainB3.Clear();
    ChainBLoop.Clear();
    ChainBTail.Clear();
    if (Channels > 2 && !Stereo)
        Side.Clear();
    if (Channels == 8 && !Stereo)
        Back.Clear();
    if (!Stereo)
        return;

    /* A reset leaves half of the stereo room filter's stages as they were */
    memset(PairPre.Line, 0, sizeof(PairPre.Line));
    PairPre.Write = 0;
    PairPre.Last[0] = 0.0f;
    PairPre.Last[1] = 0.0f;
    PairRoom.StageA[0] = 0.0f;
    PairRoom.StageB[1] = 0.0f;
    Right.Clear();
    if (Channels > 2)
    {
        CenterA.Clear();
        CenterB.Clear();
        PairSide.Clear();
    }
    if (Channels == 8)
        PairBack.Clear();
}

VOID
XaRvbNetwork::Process(
    _In_ const float* Input,
    _Out_writes_(Frames * Channels) float* Output,
    _Inout_ float* Scratch,
    _In_ UINT32 Frames,
    _In_ float Step)
{
    float Mix = Step > 0.0f ? 0.0f : Wet;
    float TailLeft = 0.0f;
    float TailRight = 0.0f;
    XaRvbEarly* Other = Stereo ? &Right : &Left;
    UINT32 Stride = Stereo ? 2 : 1;

    if (Stereo)
    {
        PairPre.Run(Input, Scratch, Frames);
        PairRoom.Run(Scratch, Frames);
    }
    else
    {
        Pre.Run(Input, Scratch, Frames);
        Room.Run(Scratch, Frames);
    }
    for (UINT32 Frame = 0; Frame < Frames; Frame++)
    {
        float Surround = 0.0f;
        float SurroundRight = 0.0f;
        float Wetted;

        Left.Step(Scratch[Frame * Stride]);
        if (Stereo)
            Right.Step(Scratch[Frame * 2 + 1]);

        /* Late field: branch and chain A follow the left taps, B the right ones */
        if (!LateOff)
        {
            float FirstA;
            float FirstB;
            float TailA;
            float TailB;
            float LoopA;
            float LoopB;
            float LoopC;
            float LoopD;
            float ChainA;
            float ChainB;
            float BlendA;
            float BlendB;
            float Through;
            float Fed;
            float Tapped;

            FirstA = BranchA.Smear.Step(InletA.Out);
            FirstA = XaRvbPole(&BranchA.Low, FirstA + Left.EchoThird);
            FirstA = XaRvbShelf(&BranchA.Shelf, FirstA);
            FirstA = XaRvbDamp(&BranchA.Damp, FirstA) * 8.0f;
            TailA = XaRvbHold(&BranchA.Tail, BranchA.Loop.Out[0]);
            LoopA = BranchA.Loop.Out[1];
            BranchA.Loop.Out[1] = BranchA.Loop.Read(1);
            BranchA.Loop.Out[0] = BranchA.Loop.Read(0);
            BranchA.Loop.Push(FirstA);

            FirstB = BranchB.Smear.Step(InletB.Out);
            FirstB = XaRvbPole(&BranchB.Low, FirstB + Other->EchoThird);
            FirstB = XaRvbShelf(&BranchB.Shelf, FirstB);
            FirstB = XaRvbDamp(&BranchB.Damp, FirstB) * 8.0f;
            Through = BranchB.Loop.Out[0];
            LoopB = BranchB.Loop.Out[1];
            BranchB.Loop.Out[1] = BranchB.Loop.Read(1);
            BranchB.Loop.Out[0] = BranchB.Loop.Read(0);
            BranchB.Loop.Push(FirstB);
            TailB = XaRvbHold(&BranchB.Tail, Through);

            ChainA = ChainA1.Step(CrossA.Out + Left.FeedFar);
            ChainA = ChainA2.Step(ChainA);
            ChainA = ChainA3.Step(ChainA + Left.FeedFar);
            Through = ChainALoop.Out[0];
            LoopC = ChainALoop.Out[1];
            ChainALoop.Out[1] = ChainALoop.Read(1);
            ChainALoop.Out[0] = ChainALoop.Read(0);
            ChainALoop.Push(ChainA + Left.EchoThird);
            BlendA = XaRvbBlend(&ChainATail, Through);

            ChainB = ChainB1.Step(CrossB.Out + Other->FeedFar);
            ChainB = ChainB2.Step(ChainB);
            Through = ChainB3.Out + Other->EchoThird;
            ChainB = ChainB3.Step(ChainB + Other->FeedFar);
            BlendB = XaRvbBlend(&ChainBTail, ChainBLoop.Out[0]);
            LoopD = ChainBLoop.Out[1];
            ChainBLoop.Out[0] = ChainBLoop.Read(0);
            ChainBLoop.Out[1] = ChainBLoop.Read(1);
            ChainBLoop.Push(Through);
            TailLeft = LoopD + LoopA;
            TailRight = LoopC + LoopB;
            Surround = LoopA;
            SurroundRight = LoopB;

            /* The four branches cross into the inlets of the next pass */
            Tapped = InletA.Tap();
            Fed = (BlendB + BlendA) + fmaf(InletA.Gain, Tapped, Left.FeedNear);
            InletA.Feed(Fed, Tapped);
            Tapped = InletB.Tap();
            Fed = fmaf(InletB.Gain, Tapped, (BlendB - BlendA) + Other->FeedNear);
            InletB.Feed(Fed, Tapped);
            Tapped = CrossA.Tap();
            Fed = (TailB + TailA) + fmaf(CrossA.Gain, Tapped, Other->FeedFar);
            CrossA.Feed(Fed, Tapped);
            Tapped = CrossB.Tap();
            Fed = fmaf(CrossB.Gain, Tapped, (TailB - TailA) + Left.FeedFar);
            CrossB.Feed(Fed, Tapped);
        }

        if (Stereo && Channels > 2)
        {
            float* Speakers = Output + Frame * Channels;
            float Dry = 1.0f - Mix;
            float Sides[2];
            float Center;

            /* The center takes both sides' sums ahead of the outlets, its dry part halved */
            Center = fmaf(Right.Sum, Reflections, TailRight) + fmaf(Left.Sum, Reflections, TailLeft);
            PairSide.Step(Surround, SurroundRight, Sides);
            Speakers[0] = fmaf(Left.Finish(TailLeft, Reflections), Mix, Dry * Input[Frame * 2]);
            Speakers[1] = fmaf(Right.Finish(TailRight, Reflections), Mix, Dry * Input[Frame * 2 + 1]);
            Center = CenterB.Step(CenterA.Step(Center));
            Speakers[2] = fmaf(Center, Mix, ((Input[Frame * 2 + 1] + Input[Frame * 2]) * Dry) * 0.5f);
            Speakers[3] = 0.0f;
            if (Channels == 6)
            {
                Speakers[4] = (Sides[0] + Sides[0]) * Mix;
                Speakers[5] = (Sides[1] + Sides[1]) * Mix;
            }
            else
            {
                float Backs[2];

                PairBack.Step(Surround, SurroundRight, Backs);
                Speakers[4] = -(Backs[0] * Mix);
                Speakers[5] = -(Backs[1] * Mix);
                Speakers[6] = (Sides[0] + Sides[0]) * Mix;
                Speakers[7] = (Sides[1] + Sides[1]) * Mix;
            }
            Mix = Mix + Step;
            continue;
        }
        Wetted = Left.Finish(TailLeft, Reflections);
        if (Stereo)
        {
            float WettedRight = Right.Finish(TailRight, Reflections);

            Output[Frame * 2] = fmaf(Wetted, Mix, (1.0f - Mix) * Input[Frame * 2]);
            Output[Frame * 2 + 1] = fmaf(WettedRight, Mix, (1.0f - Mix) * Input[Frame * 2 + 1]);
        }
        else if (Channels == 1)
        {
            Output[Frame] = fmaf(Wetted, Mix, (1.0f - Mix) * Input[Frame]);
        }
        else
        {
            float* Speakers = Output + Frame * Channels;
            float Sides = Side.Step(Surround);

            /* Front pair carries the mix, center and LFE stay silent, surrounds are wet only */
            Wetted = fmaf(Wetted, Mix, (1.0f - Mix) * Input[Frame]);
            Speakers[0] = Wetted;
            Speakers[1] = Wetted;
            Speakers[2] = 0.0f;
            Speakers[3] = 0.0f;
            if (Channels == 6)
            {
                Speakers[4] = (Sides + Sides) * Mix;
                Speakers[5] = (Sides + Sides) * Mix;
            }
            else
            {
                float Backs = Back.Step(Surround);

                Speakers[4] = -(Backs * Mix);
                Speakers[5] = -(Backs * Mix);
                Speakers[6] = (Sides + Sides) * Mix;
                Speakers[7] = (Sides + Sides) * Mix;
            }
        }
        Mix = Mix + Step;
    }
}

/* Every pending parameter set lands at the next Process; GetParameters reports what that picked */
class XaRvb final : public XaApo
{
public:
    XaRvb(
        _In_ BOOLEAN NewLegacy) : XaApo(&XaRvbRegistration)
    {
        InitializeSRWLock(&Lock);
        Legacy = NewLegacy;
        Ready = FALSE;
        Pending = FALSE;
        Picked = FALSE;
        WasEnabled = FALSE;
        InputChannels = 0;
        OutputChannels = 0;
        LockedRate = 0;
        LockedFrames = 0;
        Network = nullptr;
        Scratch = nullptr;
        Matrix = nullptr;
        memset(&Latest, 0, sizeof(Latest));
        memset(&Active, 0, sizeof(Active));
        memset(&Reported, 0, sizeof(Reported));
    }

    ~XaRvb() override
    {
        Discard();
    }

    HRESULT
    STDMETHODCALLTYPE
    IsInputFormatSupported(
        const WAVEFORMATEX* Other,
        const WAVEFORMATEX* Requested,
        _Outptr_opt_ WAVEFORMATEX** Supported) override
    {
        UNREFERENCED_PARAMETER(Other);
        UNREFERENCED_PARAMETER(Requested);
        UNREFERENCED_PARAMETER(Supported);
        return E_NOTIMPL;
    }

    HRESULT
    STDMETHODCALLTYPE
    IsOutputFormatSupported(
        const WAVEFORMATEX* Other,
        const WAVEFORMATEX* Requested,
        _Outptr_opt_ WAVEFORMATEX** Supported) override;

    void
    STDMETHODCALLTYPE
    Reset() override
    {
        if (Network != nullptr)
            Network->Clear();
    }

    HRESULT
    STDMETHODCALLTYPE
    LockForProcess(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
        UINT32 OutputCount,
        _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output) override;

    void
    STDMETHODCALLTYPE
    UnlockForProcess() override
    {
        Discard();
        WasEnabled = FALSE;
        XaApo::UnlockForProcess();
    }

    void
    STDMETHODCALLTYPE
    Process(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
        UINT32 OutputCount,
        _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
        BOOL Enabled) override;

    void
    STDMETHODCALLTYPE
    SetParameters(
        _In_reads_bytes_(Size) const void* Value,
        UINT32 Size) override;

    void
    STDMETHODCALLTYPE
    GetParameters(
        _Out_writes_bytes_(Size) void* Value,
        UINT32 Size) override;

private:
    VOID
    Discard(VOID)
    {
        delete Network;
        Network = nullptr;
        CoTaskMemFree(Scratch);
        Scratch = nullptr;
        CoTaskMemFree(Matrix);
        Matrix = nullptr;
    }

    HRESULT
    Build(
        _In_ UINT32 MaxFrames,
        _In_ UINT32 Rate);

    VOID
    PassThrough(
        _In_ const float* Source,
        _Out_ float* Target,
        _In_ UINT32 Frames);

    SRWLOCK Lock;
    BOOLEAN Legacy;
    BOOLEAN Ready;
    BOOLEAN Pending;
    BOOLEAN Picked;
    BOOL WasEnabled;
    UINT32 InputChannels;
    UINT32 OutputChannels;
    UINT32 LockedRate;
    UINT32 LockedFrames;
    XaRvbNetwork* Network;
    float* Scratch;
    float* Matrix;
    XAUDIO2FX_REVERB_PARAMETERS Latest;
    XAUDIO2FX_REVERB_PARAMETERS Active;
    XAUDIO2FX_REVERB_PARAMETERS Reported;
};

/* Output channels the reverb can produce for each input layout */
static
BOOLEAN
XaRvbLayoutSupported(
    _In_ UINT32 InputChannels,
    _In_ UINT32 OutputChannels)
{
    if (InputChannels == 1)
        return OutputChannels == 1 || OutputChannels == 6 || OutputChannels == 8;
    return OutputChannels == 2 || OutputChannels == 6 || OutputChannels == 8;
}

HRESULT
STDMETHODCALLTYPE
XaRvb::IsOutputFormatSupported(
    const WAVEFORMATEX* Other,
    const WAVEFORMATEX* Requested,
    _Outptr_opt_ WAVEFORMATEX** Supported)
{
    WAVEFORMATEX* Format = const_cast<WAVEFORMATEX*>(Requested);
    BOOLEAN Fix = Supported != nullptr;
    HRESULT hr;

    if (FAILED(ValidateFormat(const_cast<WAVEFORMATEX*>(Other), FALSE)) || !XaFormatValid(Requested))
        return E_INVALIDARG;
    if (Fix)
    {
        Format = XaApoCopyFormat(Requested);
        if (Format == nullptr)
            return E_OUTOFMEMORY;
    }

    hr = ValidateFormat(Format, Fix);
    if (SUCCEEDED(hr))
        hr = ValidatePair(Other, Format, Fix);
    if (SUCCEEDED(hr))
    {
        if (Other->nChannels != 1 && Other->nChannels != 2)
        {
            hr = E_INVALIDARG;
        }
        else if (!XaRvbLayoutSupported(Other->nChannels, Format->nChannels))
        {
            hr = XAPO_E_FORMAT_UNSUPPORTED;
            if (Fix)
                Format->nChannels = Other->nChannels;
        }

        /* The rate is judged even after a channel failure */
        if (Format->nSamplesPerSec < 20000 || Format->nSamplesPerSec > 48000)
        {
            hr = XAPO_E_FORMAT_UNSUPPORTED;
            if (Fix)
                Format->nSamplesPerSec = Format->nSamplesPerSec < 20000 ? 20000 : 48000;
        }
    }

    if (!Fix)
        return hr;
    if (hr == XAPO_E_FORMAT_UNSUPPORTED)
    {
        *Supported = Format;
        return hr;
    }
    CoTaskMemFree(Format);
    return hr;
}

HRESULT
XaRvb::Build(
    _In_ UINT32 MaxFrames,
    _In_ UINT32 Rate)
{
    BOOLEAN Stereo = OutputChannels == 2 || (OutputChannels > 2 && InputChannels != 1);
    UINT32 Layout;

    /* The output alone picks mono or stereo; surround outputs pick by the input */
    /* Any output other than mono, stereo or 5.1 gets the 7.1 network */
    Layout = OutputChannels <= 2 || OutputChannels == 6 ? OutputChannels : 8;
    Network = new (std::nothrow) XaRvbNetwork();
    Scratch = static_cast<float*>(CoTaskMemAlloc(sizeof(*Scratch) * (2 * (MaxFrames + 1) + MaxFrames * 8)));
    Matrix = static_cast<float*>(CoTaskMemAlloc(sizeof(*Matrix) * InputChannels * OutputChannels));
    if (Network == nullptr || Scratch == nullptr || Matrix == nullptr)
        return E_OUTOFMEMORY;
    if (FAILED(XaDefaultMatrix(InputChannels, OutputChannels, XaPseudoMask(InputChannels),
                               XaPseudoMask(OutputChannels), Matrix)))
    {
        return E_OUTOFMEMORY;
    }
    Network->Construct(Stereo, Layout);
    Network->Tune(static_cast<float>(Rate));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaRvb::LockForProcess(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
    UINT32 OutputCount,
    _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output)
{
    XAUDIO2FX_REVERB_PARAMETERS Defaults;
    UINT32 Rate;
    HRESULT hr;

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    hr = XaApo::LockForProcess(1, Input, 1, Output);
    if (FAILED(hr))
    {
        UnlockForProcess();
        return hr;
    }
    /* A lock on top of a lock keeps the running network and its settings */
    if (Network != nullptr && InputChannels == Input->pFormat->nChannels && OutputChannels == Output->pFormat->nChannels &&
        LockedRate == Input->pFormat->nSamplesPerSec && LockedFrames == Input->MaxFrameCount)
    {
        return S_OK;
    }
    InputChannels = Input->pFormat->nChannels;
    OutputChannels = Output->pFormat->nChannels;
    Rate = Input->pFormat->nSamplesPerSec;
    if (Input->MaxFrameCount != Output->MaxFrameCount || Input->MaxFrameCount == 0 || Rate < 20000 || Rate > 48000)
    {
        UnlockForProcess();
        return XAPO_E_FORMAT_UNSUPPORTED;
    }
    Discard();
    LockedRate = Rate;
    LockedFrames = Input->MaxFrameCount;
    hr = Build(Input->MaxFrameCount, Rate);
    if (FAILED(hr))
    {
        UnlockForProcess();
        return hr;
    }

    /* The first lock settles the defaults; later locks keep what was set */
    Defaults.WetDryMix = 100.0f;
    Defaults.ReflectionsDelay = 5;
    Defaults.ReverbDelay = 5;
    Defaults.RearDelay = OutputChannels == 8 ? 20 : 5;
    Defaults.SideDelay = 5;
    Defaults.PositionLeft = 6;
    Defaults.PositionRight = 6;
    Defaults.PositionMatrixLeft = 27;
    Defaults.PositionMatrixRight = 27;
    Defaults.EarlyDiffusion = 8;
    Defaults.LateDiffusion = 8;
    Defaults.LowEQGain = 8;
    Defaults.LowEQCutoff = 4;
    Defaults.HighEQGain = 8;
    Defaults.HighEQCutoff = 4;
    Defaults.RoomFilterFreq = 5000.0f;
    Defaults.RoomFilterMain = 0.0f;
    Defaults.RoomFilterHF = 0.0f;
    Defaults.ReflectionsGain = 0.0f;
    Defaults.ReverbGain = 0.0f;
    Defaults.DecayTime = 1.0f;
    Defaults.Density = 100.0f;
    Defaults.RoomSize = 100.0f;
    Defaults.DisableLateField = FALSE;
    Network->Configure(&Defaults);

    AcquireSRWLockExclusive(&Lock);
    if (!Ready)
    {
        Latest = Defaults;
        Active = Defaults;
        Reported = Defaults;
        Ready = TRUE;
    }
    ReleaseSRWLockExclusive(&Lock);
    return S_OK;
}

void
STDMETHODCALLTYPE
XaRvb::SetParameters(
    _In_reads_bytes_(Size) const void* Value,
    UINT32 Size)
{
    UNREFERENCED_PARAMETER(Size);
    AcquireSRWLockExclusive(&Lock);
    if (Legacy)
    {
        const XaRvbParameters28* Old = static_cast<const XaRvbParameters28*>(Value);

        /* The 2.8 mix level and right matrix position are never carried across */
        Latest.ReflectionsDelay = Old->ReflectionsDelay;
        Latest.ReverbDelay = Old->ReverbDelay;
        Latest.RearDelay = Old->RearDelay;
        Latest.SideDelay = 5;
        Latest.PositionLeft = Old->PositionLeft;
        Latest.PositionRight = Old->PositionRight;
        Latest.PositionMatrixLeft = Old->PositionMatrixLeft;
        Latest.EarlyDiffusion = Old->EarlyDiffusion;
        Latest.LateDiffusion = Old->LateDiffusion;
        Latest.LowEQGain = Old->LowEQGain;
        Latest.LowEQCutoff = Old->LowEQCutoff;
        Latest.HighEQGain = Old->HighEQGain;
        Latest.HighEQCutoff = Old->HighEQCutoff;
        Latest.RoomFilterFreq = Old->RoomFilterFreq;
        Latest.RoomFilterMain = Old->RoomFilterMain;
        Latest.RoomFilterHF = Old->RoomFilterHF;
        Latest.ReflectionsGain = Old->ReflectionsGain;
        Latest.ReverbGain = Old->ReverbGain;
        Latest.DecayTime = Old->DecayTime;
        Latest.Density = Old->Density;
        Latest.RoomSize = Old->RoomSize;
        memcpy(&Latest.DisableLateField, &Old->DisableLateField, sizeof(Latest.DisableLateField));
    }
    else
    {
        memcpy(&Latest, Value, sizeof(Latest));
    }
    Pending = TRUE;
    ReleaseSRWLockExclusive(&Lock);
}

void
STDMETHODCALLTYPE
XaRvb::GetParameters(
    _Out_writes_bytes_(Size) void* Value,
    UINT32 Size)
{
    UNREFERENCED_PARAMETER(Size);
    AcquireSRWLockExclusive(&Lock);
    if (Picked)
    {
        Reported = Active;
        Picked = FALSE;
    }
    if (Legacy)
    {
        XaRvbParameters28* Old = static_cast<XaRvbParameters28*>(Value);

        /* The mix level and right matrix position are left as the caller had them */
        Old->ReflectionsDelay = Reported.ReflectionsDelay;
        Old->ReverbDelay = Reported.ReverbDelay;
        Old->RearDelay = Reported.RearDelay;
        Old->PositionLeft = Reported.PositionLeft;
        Old->PositionRight = Reported.PositionRight;
        Old->PositionMatrixLeft = Reported.PositionMatrixLeft;
        Old->EarlyDiffusion = Reported.EarlyDiffusion;
        Old->LateDiffusion = Reported.LateDiffusion;
        Old->LowEQGain = Reported.LowEQGain;
        Old->LowEQCutoff = Reported.LowEQCutoff;
        Old->HighEQGain = Reported.HighEQGain;
        Old->HighEQCutoff = Reported.HighEQCutoff;
        Old->RoomFilterFreq = Reported.RoomFilterFreq;
        Old->RoomFilterMain = Reported.RoomFilterMain;
        Old->RoomFilterHF = Reported.RoomFilterHF;
        Old->ReflectionsGain = Reported.ReflectionsGain;
        Old->ReverbGain = Reported.ReverbGain;
        Old->DecayTime = Reported.DecayTime;
        Old->Density = Reported.Density;
        Old->RoomSize = Reported.RoomSize;
        Old->DisableLateField = Reported.DisableLateField;
    }
    else
    {
        memcpy(Value, &Reported, sizeof(Reported));
    }
    ReleaseSRWLockExclusive(&Lock);
}

VOID
XaRvb::PassThrough(
    _In_ const float* Source,
    _Out_ float* Target,
    _In_ UINT32 Frames)
{
    XaMixBlock Block;

    if (Source == Target)
        return;
    Block.Input = Source;
    Block.Output = Target;
    Block.Frames = Frames;
    Block.InputChannels = InputChannels;
    Block.OutputChannels = OutputChannels;
    Block.Matrix = Matrix;
    Block.Delta = nullptr;
    Block.Accumulate = FALSE;
    XaMatrixMix(&Block);
}

void
STDMETHODCALLTYPE
XaRvb::Process(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
    UINT32 OutputCount,
    _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
    BOOL Enabled)
{
    float* Source = static_cast<float*>(Input->pBuffer);
    float* Target = static_cast<float*>(Output->pBuffer);
    UINT32 Frames = Input->ValidFrameCount;
    float Wet;
    float Step = 0.0f;

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    AcquireSRWLockExclusive(&Lock);
    if (Pending)
    {
        Pending = FALSE;
        Picked = TRUE;
        Active = Latest;
        Network->Configure(&Latest);
    }
    Wet = Latest.WetDryMix;
    ReleaseSRWLockExclusive(&Lock);

    if (Input->BufferFlags == XAPO_BUFFER_SILENT)
    {
        if (!Enabled && !WasEnabled)
        {
            Output->BufferFlags = XAPO_BUFFER_SILENT;
            Output->ValidFrameCount = Frames;
            return;
        }

        /* A fade still has to run, so silence becomes a level just above zero */
        for (UINT32 Index = 0; Index < InputChannels * Frames; Index++)
        {
            UINT32 Bits = XA_RVB_QUIET_BITS;

            memcpy(&Source[Index], &Bits, sizeof(Bits));
        }
    }
    else if (!Enabled && !WasEnabled)
    {
        PassThrough(Source, Target, Frames);
        Output->BufferFlags = XAPO_BUFFER_VALID;
        Output->ValidFrameCount = Frames;
        WasEnabled = Enabled;
        return;
    }

    /* Turning on fades the reverb in over one buffer and turning off fades it out */
    if (Enabled && !WasEnabled)
        Step = (Wet / 100.0f) / static_cast<float>(Frames);
    else if (!Enabled)
        Step = -(Wet / 100.0f) / static_cast<float>(Frames);
    if (Network->Channels == OutputChannels)
    {
        Network->Process(Source, Target, Scratch, Frames, Step);
    }
    else
    {
        /* Odd layouts get the 7.1 frames laid end to end, cut at the buffer size */
        float* Wide = Scratch + 2 * (Frames + 1);

        Network->Process(Source, Wide, Scratch, Frames, Step);
        memcpy(Target, Wide, sizeof(*Target) * XaMin(OutputChannels, Network->Channels) * Frames);
    }
    Output->BufferFlags = XAPO_BUFFER_VALID;
    Output->ValidFrameCount = Frames;
    WasEnabled = Enabled;
}

HRESULT
XaCreateReverbObject(
    _Outptr_ IUnknown** Apo,
    _In_ BOOLEAN Legacy)
{
    XaRvb* Reverb = new (std::nothrow) XaRvb(Legacy);

    if (Reverb == nullptr)
        return E_OUTOFMEMORY;
    *Apo = static_cast<IXAPO*>(Reverb);
    return S_OK;
}

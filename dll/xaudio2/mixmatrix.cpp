/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Matrix mixing of one block through a send, with the gain ramp after a change
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

#include <math.h>

/* Any shape: each output is the dot product of the frame with its row */
static
VOID
XaMixGeneral(
    _In_ const XaMixBlock* Block)
{
    UINT32 Inputs = Block->InputChannels;
    UINT32 Outputs = Block->OutputChannels;

    for (UINT32 Frame = 0; Frame < Block->Frames; Frame++)
    {
        const float* In = Block->Input + static_cast<SIZE_T>(Frame) * Inputs;
        float* Out = Block->Output + static_cast<SIZE_T>(Frame) * Outputs;
        float Position = static_cast<float>(Frame);

        for (UINT32 Row = 0; Row < Outputs; Row++)
        {
            const float* Gains = Block->Matrix + static_cast<SIZE_T>(Row) * Inputs;
            float Sum;

            if (Block->Delta != nullptr)
            {
                const float* Steps = Block->Delta + static_cast<SIZE_T>(Row) * Inputs;

                Sum = fmaf(Position, Steps[0], Gains[0]) * In[0];
                for (UINT32 Column = 1; Column < Inputs; Column++)
                    Sum = fmaf(fmaf(Position, Steps[Column], Gains[Column]), In[Column], Sum);
            }
            else
            {
                UINT32 Column = 1;

                Sum = Gains[0] * In[0];
                if (Inputs - 1 >= 8)
                {
                    /* Two four lane accumulators, folded pairwise at the end */
                    float Lanes[8] = {};
                    UINT32 Vector = Inputs - ((Inputs - 1) & 7);

                    for (; Column < Vector; Column += 8)
                    {
                        for (UINT32 Lane = 0; Lane < 8; Lane++)
                            Lanes[Lane] = fmaf(Gains[Column + Lane], In[Column + Lane], Lanes[Lane]);
                    }
                    {
                        float Folded[4];

                        for (UINT32 Lane = 0; Lane < 4; Lane++)
                            Folded[Lane] = Lanes[Lane] + Lanes[Lane + 4];
                        Sum = ((Folded[0] + Folded[1]) + (Folded[2] + Folded[3])) + Sum;
                    }
                }
                for (; Column < Inputs; Column++)
                    Sum = fmaf(Gains[Column], In[Column], Sum);
            }
            Out[Row] = Block->Accumulate ? Out[Row] + Sum : Sum;
        }
    }
}

/* One channel into two, with the gains stepped once a frame while ramping */
static
VOID
XaMixMonoToStereo(
    _In_ const XaMixBlock* Block)
{
    float Left = Block->Matrix[0];
    float Right = Block->Matrix[1];

    for (UINT32 Frame = 0; Frame < Block->Frames; Frame++)
    {
        float Sample = Block->Input[Frame];
        float* Out = Block->Output + static_cast<SIZE_T>(Frame) * 2;
        float First = Left * Sample;
        float Second = Right * Sample;

        if (Block->Accumulate)
        {
            Out[0] = First + Out[0];
            Out[1] = Second + Out[1];
        }
        else
        {
            Out[0] = First;
            Out[1] = Second;
        }
        if (Block->Delta != nullptr)
        {
            Left = Block->Delta[0] + Left;
            Right = Block->Delta[1] + Right;
        }
    }
}

/* Channel for channel, sample by sample */
static
VOID
XaMixDiagonal(
    _In_ const XaMixBlock* Block)
{
    UINT32 Channels = Block->InputChannels;
    float Gains[XA_MAX_CHANNELS];
    SIZE_T Samples = static_cast<SIZE_T>(Block->Frames) * Channels;
    UINT32 Channel = 0;

    for (UINT32 Index = 0; Index < Channels; Index++)
        Gains[Index] = Block->Matrix[Index * Channels + Index];
    for (SIZE_T Sample = 0; Sample < Samples; Sample++)
    {
        float Gain = Gains[Channel];

        if (Block->Accumulate)
            Block->Output[Sample] = fmaf(Block->Input[Sample], Gain, Block->Output[Sample]);
        else
            Block->Output[Sample] = Block->Input[Sample] * Gain;
        if (Block->Delta != nullptr)
            Gains[Channel] = Gain + Block->Delta[Channel * Channels + Channel];
        Channel = Channel + 1 < Channels ? Channel + 1 : 0;
    }
}

/* Whether a square matrix only has gains on its diagonal */
static
BOOLEAN
XaMatrixDiagonal(
    _In_reads_(Channels * Channels) const float* Matrix,
    _In_ UINT32 Channels)
{
    for (UINT32 Row = 0; Row < Channels; Row++)
    {
        for (UINT32 Column = 0; Column < Channels; Column++)
        {
            if (Row != Column && Matrix[Row * Channels + Column] != 0.0f)
                return FALSE;
        }
    }
    return TRUE;
}

VOID
XaMatrixMix(
    _In_ const XaMixBlock* Block)
{
    if (Block->InputChannels == 1 && Block->OutputChannels == 2)
        XaMixMonoToStereo(Block);
    else if (Block->InputChannels == Block->OutputChannels && Block->InputChannels <= 8 &&
             XaMatrixDiagonal(Block->Matrix, Block->InputChannels))
        XaMixDiagonal(Block);
    else
        XaMixGeneral(Block);
}

/* Mixes a whole quantum through a send, ramping over the first millisecond after a change */
VOID
XaMixThroughSend(
    _Inout_ XaSend* Send,
    _In_reads_(Frames * InputChannels) const float* Input,
    _In_ UINT32 InputChannels,
    _Out_writes_(Frames * Send->Target->InputChannels) float* Output,
    _In_ UINT32 Frames,
    _In_ UINT32 Rate,
    _In_ BOOLEAN Accumulate)
{
    UINT32 OutputChannels = Send->Target->InputChannels;
    UINT32 Cells = InputChannels * OutputChannels;
    const float* Matrix = reinterpret_cast<const float*>(Send->Mix.Current) + 3;
    XaMixBlock Block;
    UINT32 RampFrames = 0;

    if (!Send->Primed)
    {
        memcpy(Send->MixState, Matrix, sizeof(float) * Cells);
        Send->Primed = TRUE;
        Send->Changed = FALSE;
    }
    Block.InputChannels = InputChannels;
    Block.OutputChannels = OutputChannels;
    Block.Accumulate = Accumulate;
    if (Send->Changed && Frames != 0)
    {
        float* Previous = Send->MixState;
        float* Steps = Send->MixState + Cells;
        UINT32 Length = (Rate / 1000) & ~3u;
        float Scale;

        RampFrames = XaMin(Length, Frames);
        if (Length != 0)
        {
            Scale = static_cast<float>(1.0 / static_cast<float>(Length));
            for (UINT32 Cell = 0; Cell < Cells; Cell++)
                Steps[Cell] = (Matrix[Cell] - Previous[Cell]) * Scale;
            Block.Input = Input;
            Block.Output = Output;
            Block.Frames = RampFrames;
            Block.Matrix = Previous;
            Block.Delta = Steps;
            XaMatrixMix(&Block);
        }
    }
    Block.Input = Input + static_cast<SIZE_T>(RampFrames) * InputChannels;
    Block.Output = Output + static_cast<SIZE_T>(RampFrames) * OutputChannels;
    Block.Frames = Frames - RampFrames;
    Block.Matrix = Matrix;
    Block.Delta = nullptr;
    if (Block.Frames != 0)
        XaMatrixMix(&Block);
    memcpy(Send->MixState, Matrix, sizeof(float) * Cells);
    Send->Changed = FALSE;
}

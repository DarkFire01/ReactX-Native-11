/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA bit reader: superframe packets, frames that run across them, Huffman symbols
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"

/* Bits the Huffman walk looks ahead */
#define XA_WMA_SYMBOL_PEEK      23

/* Tree levels that take two bits, then one bit, before the last pointer only step */
#define XA_WMA_TREE_PAIRS       7
#define XA_WMA_TREE_SINGLES     6

#define XA_WMA_LEAF             0x8000
#define XA_WMA_LONG_SYMBOL      0x3FC

static
UINT32
XaWmaHeaderBits(
    _In_ const XaWmaDecoder* Decoder)
{
    return Decoder->Standard.OffsetBits + 11;
}

static
UINT32
XaWmaStoreBit(
    _In_ const XaWmaReader* Reader,
    _In_ UINT32 Slot,
    _In_ UINT32 Bit)
{
    return (Reader->Store[Slot][Bit >> 3] >> (7 - (Bit & 7))) & 1;
}

/* Reads Count bits from a slot starting at Bit, the most significant first */
static
UINT32
XaWmaStoreField(
    _In_ const XaWmaReader* Reader,
    _In_ UINT32 Slot,
    _In_ UINT32 Bit,
    _In_ UINT32 Count)
{
    UINT32 Value = 0;

    for (UINT32 Index = 0; Index < Count; Index++)
        Value = (Value << 1) | XaWmaStoreBit(Reader, Slot, Bit + Index);
    return Value;
}

/* Pulls one packet into the next free slot and queues its header */
static
BOOLEAN
XaWmaLoadPacket(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaReader* Reader = &Decoder->Reader;
    UINT32 HeaderBits = XaWmaHeaderBits(Decoder);
    XaWmaInput Input;
    UINT32 Slot;
    UINT32 Bytes;

    if (Reader->Dry || Reader->Held >= ARRAYSIZE(Reader->Store))
        return FALSE;
    Decoder->Pull(Decoder->Context, &Input);
    if (Input.Last)
        Reader->Dry = TRUE;
    Bytes = XaMin(Input.Bytes, Decoder->Layout.PacketBytes);
    if (Input.Data == nullptr || Bytes * 8 < HeaderBits)
    {
        Reader->Dry = TRUE;
        return FALSE;
    }

    Slot = Reader->Held;
    memcpy(Reader->Store[Slot], Input.Data, Bytes);
    Reader->StoreBits[Slot] = Bytes * 8;
    if (Reader->HeadCount == ARRAYSIZE(Reader->Heads))
    {
        Reader->Heads[0] = Reader->Heads[1];
        Reader->HeadCount = 1;
    }
    Reader->Heads[Reader->HeadCount].Frames = XaWmaStoreField(Reader, Slot, 4, 4);
    Reader->Heads[Reader->HeadCount].Offset = XaWmaStoreField(Reader, Slot, 8, Decoder->Standard.OffsetBits + 3);
    Reader->HeadCount += 1;
    if (Slot == 0)
        Reader->Position = HeaderBits;
    Reader->Held += 1;
    return TRUE;
}

/* Moves reading onto the packet after the current one */
static
VOID
XaWmaDropPacket(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaReader* Reader = &Decoder->Reader;
    BYTE* Spare = Reader->Store[0];

    Reader->Store[0] = Reader->Store[1];
    Reader->Store[1] = Spare;
    Reader->StoreBits[0] = Reader->StoreBits[1];
    Reader->Held -= 1;
    Reader->Position = XaWmaHeaderBits(Decoder);
}

/* Makes the bit at Position readable, stepping over the next packet's header */
static
BOOLEAN
XaWmaReady(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaReader* Reader = &Decoder->Reader;

    while (Reader->Held == 0 || Reader->Position >= Reader->StoreBits[0])
    {
        if (Reader->Held == 2)
        {
            XaWmaDropPacket(Decoder);
            continue;
        }
        if (Reader->Held == 1)
        {
            if (!XaWmaLoadPacket(Decoder))
                return FALSE;
            XaWmaDropPacket(Decoder);
            continue;
        }
        if (!XaWmaLoadPacket(Decoder))
            return FALSE;
    }
    return TRUE;
}

/* Looks at the next Count bits without taking them; bits past the end of the stream read as zero */
static
UINT32
XaWmaPeekBits(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Count)
{
    XaWmaReader* Reader = &Decoder->Reader;
    UINT32 Value = 0;
    UINT32 Slot = 0;
    UINT32 Bit;

    if (!XaWmaReady(Decoder))
        return 0;
    Bit = Reader->Position;
    for (UINT32 Index = 0; Index < Count; Index++)
    {
        UINT32 Next = 0;

        if (Bit >= Reader->StoreBits[Slot] && Slot == 0)
        {
            if (Reader->Held == 2 || XaWmaLoadPacket(Decoder))
            {
                Slot = 1;
                Bit = XaWmaHeaderBits(Decoder);
            }
        }
        if (Bit < Reader->StoreBits[Slot])
        {
            Next = XaWmaStoreBit(Reader, Slot, Bit);
            Bit += 1;
        }
        Value = (Value << 1) | Next;
    }
    return Value;
}

HRESULT
XaWmaReaderCreate(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaReader* Reader = &Decoder->Reader;

    for (UINT32 Index = 0; Index < ARRAYSIZE(Reader->Store); Index++)
    {
        Reader->Store[Index] = new (std::nothrow) BYTE[Decoder->Layout.PacketBytes];
        if (Reader->Store[Index] == nullptr)
            return E_OUTOFMEMORY;
    }
    XaWmaReaderReset(Decoder);
    return S_OK;
}

VOID
XaWmaReaderFree(
    _Inout_ XaWmaDecoder* Decoder)
{
    for (UINT32 Index = 0; Index < ARRAYSIZE(Decoder->Reader.Store); Index++)
    {
        delete[] Decoder->Reader.Store[Index];
        Decoder->Reader.Store[Index] = nullptr;
    }
}

VOID
XaWmaReaderReset(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaReader* Reader = &Decoder->Reader;

    Reader->Held = 0;
    Reader->Position = 0;
    Reader->HeadCount = 0;
    Reader->Dry = FALSE;
}

/**
 * @brief Takes the next packet header, positioning reading at the first frame that starts in it.
 *
 * A frame left over from the previous packet carries on where it is unless the header says
 * none spills over; right after a reset the spilled bits are skipped.
 */
BOOLEAN
XaWmaNextPacket(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaReader* Reader = &Decoder->Reader;
    XaWmaPacketHead Head;

    if (Reader->HeadCount == 0)
    {
        Reader->Held = 0;
        if (!XaWmaLoadPacket(Decoder))
            return FALSE;
    }
    Head = Reader->Heads[0];
    Reader->Heads[0] = Reader->Heads[1];
    Reader->HeadCount -= 1;
    Decoder->FramesLeft = Head.Frames;

    /* The first frame of every packet restates the tile sizes around it */
    Decoder->ReadTileHeader = TRUE;

    if (Head.Offset == 0)
    {
        while (Reader->Held > 1)
            XaWmaDropPacket(Decoder);
        Reader->Position = XaWmaHeaderBits(Decoder);
    }
    if (Decoder->SkipOffset)
    {
        Decoder->SkipOffset = FALSE;
        return XaWmaSkipBits(Decoder, Head.Offset);
    }
    return TRUE;
}

BOOLEAN
XaWmaGetBits(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Count,
    _Out_ UINT32* Value)
{
    XaWmaReader* Reader = &Decoder->Reader;
    UINT32 Result = 0;

    for (UINT32 Index = 0; Index < Count; Index++)
    {
        if (!XaWmaReady(Decoder))
        {
            *Value = Result;
            return FALSE;
        }
        Result = (Result << 1) | XaWmaStoreBit(Reader, 0, Reader->Position);
        Reader->Position += 1;
    }
    *Value = Result;
    return TRUE;
}

BOOLEAN
XaWmaSkipBits(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Count)
{
    XaWmaReader* Reader = &Decoder->Reader;

    while (Count > 0)
    {
        UINT32 Step;

        if (!XaWmaReady(Decoder))
            return FALSE;
        Step = XaMin(Count, Reader->StoreBits[0] - Reader->Position);
        Reader->Position += Step;
        Count -= Step;
    }
    return TRUE;
}

/**
 * @brief Decodes one symbol from a table driven code tree.
 *
 * After receives the bit that follows the code, which carries the sign of a level.
 */
BOOLEAN
XaWmaGetSymbol(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ const UINT16* Tree,
    _Out_ UINT32* Symbol,
    _Out_ UINT32* After)
{
    UINT32 Window = XaWmaPeekBits(Decoder, XA_WMA_SYMBOL_PEEK) << (32 - XA_WMA_SYMBOL_PEEK);
    const UINT16* Root = Tree + (Window >> 30);
    const UINT16* Node = Root;
    UINT32 Entry = *Node;
    UINT32 Offset = 0;
    UINT32 Used = 2;
    UINT32 Length;

    for (UINT32 Level = 0; Level < XA_WMA_TREE_PAIRS + XA_WMA_TREE_SINGLES && (Entry & XA_WMA_LEAF) == 0; Level++)
    {
        UINT32 Width = Level < XA_WMA_TREE_PAIRS ? 2 : 1;

        Offset += Entry + ((Window << Used) >> (32 - Width));
        Used += Width;
        Node = Root + Offset;
        Entry = *Node;
    }

    /* A code longer than the walk keeps the last entry and only moves the pointer */
    if ((Entry & XA_WMA_LEAF) == 0)
        Node = Root + Offset + Entry;

    Length = (Entry >> 10) & 0x1F;
    *Symbol = Entry & 0x3FF;
    if (*Symbol >= XA_WMA_LONG_SYMBOL)
        *Symbol = Node[(Entry & 3) + 1];
    *After = Length < 32 ? (Window << Length) >> 31 : 0;
    return XaWmaSkipBits(Decoder, Length);
}

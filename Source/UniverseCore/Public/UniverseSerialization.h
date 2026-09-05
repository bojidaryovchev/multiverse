// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"

/**
 * UniverseSerialization.h
 *
 * An explicit, byte-exact binary format for canonical universe data.
 *
 * Why not FArchive? Because these bytes will eventually cross a network and
 * land in a persistence store shared between machines and engine versions.
 * FArchive's layout is an engine implementation detail; ours must be a
 * contract. Everything here is fixed-width, little-endian, and independent of
 * struct padding, so a byte stream produced by any build is readable by any
 * other.
 *
 * Unreal's FArchive operators are provided separately (in the engine-only
 * section of UniverseCoordinates.h) and are implemented on top of these, so
 * there is exactly one definition of the wire format.
 */

/** Sequential little-endian writer over a growable byte buffer. */
class UNIVERSECORE_API FUniverseByteWriter
{
public:
    void WriteUInt8(uint8 Value)
    {
        Bytes.Add(Value);
    }

    void WriteUInt32(uint32 Value)
    {
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Bytes.Add(static_cast<uint8>((Value >> (Index * 8)) & 0xFFu));
        }
    }

    void WriteUInt64(uint64 Value)
    {
        for (int32 Index = 0; Index < 8; ++Index)
        {
            Bytes.Add(static_cast<uint8>((Value >> (Index * 8)) & 0xFFull));
        }
    }

    void WriteInt32(int32 Value)
    {
        WriteUInt32(static_cast<uint32>(Value));
    }

    /** Two's complement, little-endian. Well defined for all int64 values. */
    void WriteInt64(int64 Value)
    {
        WriteUInt64(static_cast<uint64>(Value));
    }

    /**
     * IEEE-754 binary64, little-endian. Transported as raw bits rather than
     * text so the round trip is bit-exact including denormals and signed zero,
     * both of which a decimal round trip can lose.
     */
    void WriteDouble(double Value)
    {
        uint64 Bits = 0;
        FMemoryCopy(&Bits, &Value, sizeof(double));
        WriteUInt64(Bits);
    }

    const TArray<uint8>& GetBytes() const { return Bytes; }
    int32 Num() const { return Bytes.Num(); }
    void Reset() { Bytes.Reset(); }

private:
    static void FMemoryCopy(void* Dest, const void* Src, size_t Size)
    {
        // memcpy is the only standard-conformant way to reinterpret the bits of
        // a double; a reinterpret_cast through a uint64* would be a strict
        // aliasing violation and is miscompiled at high optimisation levels.
        unsigned char* D = static_cast<unsigned char*>(Dest);
        const unsigned char* S = static_cast<const unsigned char*>(Src);
        for (size_t Index = 0; Index < Size; ++Index)
        {
            D[Index] = S[Index];
        }
    }

    TArray<uint8> Bytes;
};

/** Sequential little-endian reader. Every read is bounds-checked. */
class UNIVERSECORE_API FUniverseByteReader
{
public:
    FUniverseByteReader(const uint8* InData, int32 InNum)
        : Data(InData)
        , Num(InNum)
    {
    }

    explicit FUniverseByteReader(const TArray<uint8>& InBytes)
        : Data(InBytes.GetData())
        , Num(InBytes.Num())
    {
    }

    bool IsValid() const { return !bError; }
    int32 Tell() const { return Offset; }
    int32 Remaining() const { return Num - Offset; }
    bool AtEnd() const { return Offset >= Num; }

    uint8 ReadUInt8()
    {
        if (!Require(1)) { return 0; }
        return Data[Offset++];
    }

    uint32 ReadUInt32()
    {
        if (!Require(4)) { return 0; }
        uint32 Value = 0;
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Value |= static_cast<uint32>(Data[Offset + Index]) << (Index * 8);
        }
        Offset += 4;
        return Value;
    }

    uint64 ReadUInt64()
    {
        if (!Require(8)) { return 0; }
        uint64 Value = 0;
        for (int32 Index = 0; Index < 8; ++Index)
        {
            Value |= static_cast<uint64>(Data[Offset + Index]) << (Index * 8);
        }
        Offset += 8;
        return Value;
    }

    int32 ReadInt32() { return static_cast<int32>(ReadUInt32()); }
    int64 ReadInt64() { return static_cast<int64>(ReadUInt64()); }

    double ReadDouble()
    {
        const uint64 Bits = ReadUInt64();
        double Value = 0.0;
        unsigned char* D = reinterpret_cast<unsigned char*>(&Value);
        const unsigned char* S = reinterpret_cast<const unsigned char*>(&Bits);
        for (size_t Index = 0; Index < sizeof(double); ++Index)
        {
            D[Index] = S[Index];
        }
        return Value;
    }

private:
    /** Marks the reader failed on underflow so a truncated stream cannot be
     *  mistaken for valid data containing zeroes. */
    bool Require(int32 Count)
    {
        if (bError || Offset + Count > Num)
        {
            bError = true;
            return false;
        }
        return true;
    }

    const uint8* Data = nullptr;
    int32 Num = 0;
    int32 Offset = 0;
    bool bError = false;
};

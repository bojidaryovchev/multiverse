// Copyright Universe Project. All Rights Reserved.

#include "UniverseCoordinates.h"
#include "UniverseHash.h"

namespace
{
    /**
     * Normalises one axis.
     *
     * The exactness argument, which is the whole reason the cell size is a
     * power of two:
     *
     *   1. Local * 2^-40 is exact. Multiplying by a power of two only adjusts
     *      the exponent field; no mantissa bit is lost (barring underflow,
     *      which cannot occur for the magnitudes involved).
     *   2. floor() of an exact value is exact.
     *   3. Carry * 2^40 is exact for the same reason as (1).
     *   4. Local - Carry * 2^40 is exact whenever |Local| <= 2^53, because
     *      both operands are then integers below 2^53 scaled by a common
     *      power of two, and IEEE-754 subtraction of exactly representable
     *      values whose difference is also exactly representable is exact.
     *
     * So for any displacement up to 2^53 cm (~6000 AU, vastly more than any
     * single frame of travel), normalisation is a lossless re-encoding: the
     * position that comes out denotes precisely the point that went in. That
     * is what makes the representation canonical, and canonical is what makes
     * equality, hashing and determinism meaningful.
     *
     * Above 2^53 cm the subtraction may round. The result is still a valid
     * normalised position and still within the correct cell, but it is no
     * longer bit-exact - which is why the API steers large jumps through
     * TryOffsetByCells (pure integer arithmetic) instead.
     */
    inline void NormalizeAxis(int64& Cell, double& Local)
    {
        if (!FMath::IsFinite(Local))
        {
            // A NaN or infinity here means a physics or input bug upstream.
            // Collapsing to the cell corner keeps the position canonical so
            // the corruption cannot propagate into cell indices (where an
            // out-of-range double-to-int64 cast would be undefined behaviour).
            Local = 0.0;
            return;
        }

        const double Carry = FMath::FloorToDouble(Local * UniverseScale::InvCellSizeCmD);

        if (Carry != 0.0)
        {
            // Guard the cast: converting a double outside int64's range is
            // undefined behaviour, not a wrap. Anything this large is a bug,
            // so clamp to the cell and leave the index alone rather than
            // corrupting the address space.
            if (Carry >= 9.2233720368547758e18 || Carry <= -9.2233720368547758e18)
            {
                Local = 0.0;
                return;
            }

            int64 NewCell = 0;
            if (!UniverseScale::AddChecked(Cell, static_cast<int64>(Carry), NewCell))
            {
                // Saturate at the edge of the universe rather than wrapping
                // from +max to -max, which would teleport the player across
                // the entire universe on an overflow.
                Cell = (Carry > 0.0) ? UniverseScale::MaxCellIndex : UniverseScale::MinCellIndex;
                Local = 0.0;
                return;
            }

            Cell = NewCell;
            Local -= Carry * UniverseScale::CellSizeCmD;
        }

        // Safety net for the rounding regime described above: a value that
        // rounded to exactly +CellSize (or to a small negative) must still end
        // up canonical. At most one correction is ever needed.
        if (Local >= UniverseScale::CellSizeCmD)
        {
            Local -= UniverseScale::CellSizeCmD;
            if (!UniverseScale::AddChecked(Cell, 1, Cell)) { Cell = UniverseScale::MaxCellIndex; }
        }
        else if (Local < 0.0)
        {
            Local += UniverseScale::CellSizeCmD;
            if (!UniverseScale::SubtractChecked(Cell, 1, Cell)) { Cell = UniverseScale::MinCellIndex; }
        }
    }
}

void FUniversePosition::Normalize()
{
    NormalizeAxis(CellX, Local.X);
    NormalizeAxis(CellY, Local.Y);
    NormalizeAxis(CellZ, Local.Z);
}

bool FUniversePosition::IsNormalized() const
{
    const double Size = UniverseScale::CellSizeCmD;
    return Local.X >= 0.0 && Local.X < Size
        && Local.Y >= 0.0 && Local.Y < Size
        && Local.Z >= 0.0 && Local.Z < Size;
}

FUniversePosition FUniversePosition::OffsetByCm(const FVector3d& DeltaCm) const
{
    FUniversePosition Result = *this;
    Result.Local += DeltaCm;
    Result.Normalize();
    return Result;
}

bool FUniversePosition::TryOffsetByCells(int64 DeltaCellX, int64 DeltaCellY, int64 DeltaCellZ, FUniversePosition& Out) const
{
    int64 NewX = 0;
    int64 NewY = 0;
    int64 NewZ = 0;
    if (!UniverseScale::AddChecked(CellX, DeltaCellX, NewX)) { return false; }
    if (!UniverseScale::AddChecked(CellY, DeltaCellY, NewY)) { return false; }
    if (!UniverseScale::AddChecked(CellZ, DeltaCellZ, NewZ)) { return false; }

    Out.CellX = NewX;
    Out.CellY = NewY;
    Out.CellZ = NewZ;
    Out.Local = Local;   // already canonical; nothing to renormalise
    return true;
}

FVector3d FUniversePosition::GetRelativeCells(const FUniversePosition& From, const FUniversePosition& To)
{
    // Cell differences are taken in double rather than int64 on purpose: the
    // difference between two extreme cell indices overflows int64, but the
    // double conversion cannot overflow and only loses exactness beyond 2^53
    // cells (~10 billion light years), where a fractional cell is irrelevant
    // anyway. The local remainder is added as a fraction of a cell, which
    // preserves full precision for the near-field case that actually matters.
    const double DeltaCellX = static_cast<double>(To.CellX) - static_cast<double>(From.CellX);
    const double DeltaCellY = static_cast<double>(To.CellY) - static_cast<double>(From.CellY);
    const double DeltaCellZ = static_cast<double>(To.CellZ) - static_cast<double>(From.CellZ);

    const FVector3d LocalDelta = To.Local - From.Local;

    return FVector3d(
        DeltaCellX + LocalDelta.X * UniverseScale::InvCellSizeCmD,
        DeltaCellY + LocalDelta.Y * UniverseScale::InvCellSizeCmD,
        DeltaCellZ + LocalDelta.Z * UniverseScale::InvCellSizeCmD);
}

bool FUniversePosition::TryGetRelativeCm(
    const FUniversePosition& From,
    const FUniversePosition& To,
    FVector3d& OutCm,
    double MaxCm)
{
    // Exact integer cell delta. Unlike GetRelativeCells this must not lose a
    // single unit, because the result is going to be used as a real position.
    int64 DeltaCellX = 0;
    int64 DeltaCellY = 0;
    int64 DeltaCellZ = 0;
    if (!UniverseScale::SubtractChecked(To.CellX, From.CellX, DeltaCellX)) { return false; }
    if (!UniverseScale::SubtractChecked(To.CellY, From.CellY, DeltaCellY)) { return false; }
    if (!UniverseScale::SubtractChecked(To.CellZ, From.CellZ, DeltaCellZ)) { return false; }

    // Reject before converting, so an out-of-range separation can never
    // produce a plausible-looking but meaningless transform.
    const double MaxCells = MaxCm * UniverseScale::InvCellSizeCmD;
    if (FMath::Abs(static_cast<double>(DeltaCellX)) > MaxCells + 1.0
     || FMath::Abs(static_cast<double>(DeltaCellY)) > MaxCells + 1.0
     || FMath::Abs(static_cast<double>(DeltaCellZ)) > MaxCells + 1.0)
    {
        return false;
    }

    const FVector3d LocalDelta = To.Local - From.Local;
    const FVector3d Result(
        static_cast<double>(DeltaCellX) * UniverseScale::CellSizeCmD + LocalDelta.X,
        static_cast<double>(DeltaCellY) * UniverseScale::CellSizeCmD + LocalDelta.Y,
        static_cast<double>(DeltaCellZ) * UniverseScale::CellSizeCmD + LocalDelta.Z);

    if (FMath::Abs(Result.X) > MaxCm || FMath::Abs(Result.Y) > MaxCm || FMath::Abs(Result.Z) > MaxCm)
    {
        return false;
    }

    OutCm = Result;
    return true;
}

double FUniversePosition::DistanceCm(const FUniversePosition& A, const FUniversePosition& B)
{
    // Magnitude in cell units first, scaled to centimetres last. Squaring
    // centimetre values directly would overflow a double at interstellar
    // range (1 ly is 9.5e17 cm; its square is 9e35, and summing three such
    // terms across galactic distances runs out of exponent). In cell units
    // the same separation is only 8.6e5, so the intermediate never grows.
    const FVector3d DeltaCells = GetRelativeCells(A, B);
    return DeltaCells.Size() * UniverseScale::CellSizeCmD;
}

uint64 FUniversePosition::GetStableHash64() const
{
    // Hash the bit patterns, not the numeric values. Local is canonical, so
    // equal positions have identical bits and therefore identical hashes;
    // there is no rounding step that could make two equal positions hash
    // differently. Note that this is only sound because Normalize() gives a
    // unique representation - it would be wrong for an un-normalised form.
    uint64 LocalBitsX = 0;
    uint64 LocalBitsY = 0;
    uint64 LocalBitsZ = 0;
    const double LX = Local.X;
    const double LY = Local.Y;
    const double LZ = Local.Z;
    for (size_t Index = 0; Index < sizeof(double); ++Index)
    {
        LocalBitsX |= static_cast<uint64>(reinterpret_cast<const unsigned char*>(&LX)[Index]) << (Index * 8);
        LocalBitsY |= static_cast<uint64>(reinterpret_cast<const unsigned char*>(&LY)[Index]) << (Index * 8);
        LocalBitsZ |= static_cast<uint64>(reinterpret_cast<const unsigned char*>(&LZ)[Index]) << (Index * 8);
    }

    uint64 Hash = UniverseHash::Mix64(0x506F730000000001ull);  // "Pos" tag
    Hash = UniverseHash::Combine(Hash, CellX);
    Hash = UniverseHash::Combine(Hash, CellY);
    Hash = UniverseHash::Combine(Hash, CellZ);
    Hash = UniverseHash::Combine(Hash, LocalBitsX);
    Hash = UniverseHash::Combine(Hash, LocalBitsY);
    Hash = UniverseHash::Combine(Hash, LocalBitsZ);
    return Hash;
}

void FUniversePosition::Serialize(FUniverseByteWriter& Writer) const
{
    Writer.WriteInt64(CellX);
    Writer.WriteInt64(CellY);
    Writer.WriteInt64(CellZ);
    Writer.WriteDouble(Local.X);
    Writer.WriteDouble(Local.Y);
    Writer.WriteDouble(Local.Z);
}

bool FUniversePosition::Deserialize(FUniverseByteReader& Reader)
{
    const int64 InCellX = Reader.ReadInt64();
    const int64 InCellY = Reader.ReadInt64();
    const int64 InCellZ = Reader.ReadInt64();
    const double InLocalX = Reader.ReadDouble();
    const double InLocalY = Reader.ReadDouble();
    const double InLocalZ = Reader.ReadDouble();

    if (!Reader.IsValid())
    {
        return false;
    }

    FUniversePosition Candidate;
    Candidate.CellX = InCellX;
    Candidate.CellY = InCellY;
    Candidate.CellZ = InCellZ;
    Candidate.Local = FVector3d(InLocalX, InLocalY, InLocalZ);

    // Serialize only ever emits canonical positions, so a non-canonical value
    // arriving here means a corrupt file or a crafted packet. Reject it rather
    // than normalising, which would silently accept the tampered value.
    if (!Candidate.IsNormalized())
    {
        return false;
    }

    *this = Candidate;
    return true;
}

FString FUniversePosition::ToCompactString() const
{
    return FString::Printf(
        TEXT("C[%lld,%lld,%lld] L[%.3f,%.3f,%.3f]"),
        static_cast<long long>(CellX),
        static_cast<long long>(CellY),
        static_cast<long long>(CellZ),
        Local.X, Local.Y, Local.Z);
}

FString FUniversePosition::ToDebugString() const
{
    // Distance from the universe origin, in light years, for orientation.
    const FUniversePosition Origin;
    const double LightYears = DistanceLightYears(Origin, *this);

    int64 SectorX = 0;
    int64 SectorY = 0;
    int64 SectorZ = 0;
    GetSector(SectorX, SectorY, SectorZ);

    return FString::Printf(
        TEXT("Cell [%lld, %lld, %lld]  Local [%.2f, %.2f, %.2f] cm  Sector [%lld, %lld, %lld]  %.6f ly from origin"),
        static_cast<long long>(CellX),
        static_cast<long long>(CellY),
        static_cast<long long>(CellZ),
        Local.X, Local.Y, Local.Z,
        static_cast<long long>(SectorX),
        static_cast<long long>(SectorY),
        static_cast<long long>(SectorZ),
        LightYears);
}

#if !(defined(UNIVERSE_STANDALONE) && UNIVERSE_STANDALONE)
FArchive& operator<<(FArchive& Ar, FUniversePosition& Position)
{
    if (Ar.IsLoading())
    {
        uint8 Buffer[FUniversePosition::SerializedSizeBytes];
        Ar.Serialize(Buffer, FUniversePosition::SerializedSizeBytes);
        FUniverseByteReader Reader(Buffer, FUniversePosition::SerializedSizeBytes);
        if (!Position.Deserialize(Reader))
        {
            Ar.SetError();
        }
    }
    else
    {
        FUniverseByteWriter Writer;
        Position.Serialize(Writer);
        check(Writer.Num() == FUniversePosition::SerializedSizeBytes);
        Ar.Serialize(const_cast<uint8*>(Writer.GetBytes().GetData()), FUniversePosition::SerializedSizeBytes);
    }
    return Ar;
}
#endif

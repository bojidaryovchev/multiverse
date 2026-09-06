// Copyright Universe Project. All Rights Reserved.

#include "PlanetPatchId.h"

using CubeSphere::EFace;
using CubeSphere::EFaceEdge;

namespace
{
    /** The face edge a neighbour direction leaves through. */
    EFaceEdge NeighbourDirectionToEdge(EPlanetPatchNeighbour Direction)
    {
        switch (Direction)
        {
        case EPlanetPatchNeighbour::West:  return EFaceEdge::West;
        case EPlanetPatchNeighbour::East:  return EFaceEdge::East;
        case EPlanetPatchNeighbour::South: return EFaceEdge::South;
        default:                           return EFaceEdge::North;
        }
    }
}

bool FPlanetPatchId::Contains(const FPlanetPatchId& Other) const
{
    if (Face != Other.Face || Other.Level < Level)
    {
        return false;
    }

    // Shift the descendant back up to this level; it is contained exactly when
    // the shifted coordinates match.
    const uint32 Shift = static_cast<uint32>(Other.Level - Level);
    return (Other.X >> Shift) == X && (Other.Y >> Shift) == Y;
}

double FPlanetPatchId::GetAngularRadius() const
{
    const FVector3d Centre = GetCentreDirection();

    // The maximum over the four corners. Corners are the extremes because the
    // patch is a UV-space rectangle and the cube-to-sphere map is monotonic
    // along each axis.
    double SmallestDot = 1.0;
    for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
    {
        const double LocalU = (CornerIndex & 1) ? 1.0 : 0.0;
        const double LocalV = (CornerIndex & 2) ? 1.0 : 0.0;

        const FVector3d Corner = GetDirectionAt(LocalU, LocalV);
        const double Dot = Centre.X * Corner.X + Centre.Y * Corner.Y + Centre.Z * Corner.Z;
        SmallestDot = FMath::Min(SmallestDot, Dot);
    }

    // Guard the acos domain: rounding can push a dot product a hair outside
    // [-1, 1], which would otherwise produce a NaN angular radius and poison
    // every LOD decision downstream.
    SmallestDot = FMath::Clamp(SmallestDot, -1.0, 1.0);

    return FMath::Acos(SmallestDot);
}

bool FPlanetPatchId::TryGetNeighbour(EPlanetPatchNeighbour Direction, FPlanetPatchId& OutNeighbour) const
{
    if (!IsValid())
    {
        return false;
    }

    const uint32 GridSize = GetGridSize();

    // The easy case: the neighbour is on the same face.
    switch (Direction)
    {
    case EPlanetPatchNeighbour::West:
        if (X > 0) { OutNeighbour = FPlanetPatchId(GetFace(), Level, X - 1, Y); return true; }
        break;
    case EPlanetPatchNeighbour::East:
        if (X + 1 < GridSize) { OutNeighbour = FPlanetPatchId(GetFace(), Level, X + 1, Y); return true; }
        break;
    case EPlanetPatchNeighbour::South:
        if (Y > 0) { OutNeighbour = FPlanetPatchId(GetFace(), Level, X, Y - 1); return true; }
        break;
    case EPlanetPatchNeighbour::North:
        if (Y + 1 < GridSize) { OutNeighbour = FPlanetPatchId(GetFace(), Level, X, Y + 1); return true; }
        break;
    default:
        return false;
    }

    // Otherwise the neighbour is across a cube-face seam.
    //
    // Everything here is integer arithmetic on patch indices. The along-edge
    // position is expressed as an index rather than a UV so that the reversal
    // is an exact integer subtraction; going through doubles would work too but
    // would reintroduce the rounding question the dyadic rule exists to avoid.
    const EFaceEdge Edge = NeighbourDirectionToEdge(Direction);
    const CubeSphere::FEdgeAdjacency Adjacency = CubeSphere::GetEdgeAdjacency(GetFace(), Edge);

    // Index along this patch's outgoing edge. For a West/East crossing the
    // along-axis is V; for South/North it is U.
    const bool bAlongIsV = (Edge == EFaceEdge::West || Edge == EFaceEdge::East);
    const uint32 AlongIndex = bAlongIsV ? Y : X;

    // Reversal flips the index within the row: i -> (GridSize - 1) - i.
    const uint32 NeighbourAlongIndex =
        Adjacency.bReverse ? (GridSize - 1u - AlongIndex) : AlongIndex;

    // The neighbour sits against its own edge, so its perpendicular index is
    // pinned to whichever end that edge is.
    uint32 NeighbourX = 0;
    uint32 NeighbourY = 0;

    switch (Adjacency.NeighbourEdge)
    {
    case EFaceEdge::West:
        NeighbourX = 0;
        NeighbourY = NeighbourAlongIndex;
        break;
    case EFaceEdge::East:
        NeighbourX = GridSize - 1u;
        NeighbourY = NeighbourAlongIndex;
        break;
    case EFaceEdge::South:
        NeighbourX = NeighbourAlongIndex;
        NeighbourY = 0;
        break;
    case EFaceEdge::North:
        NeighbourX = NeighbourAlongIndex;
        NeighbourY = GridSize - 1u;
        break;
    default:
        return false;
    }

    OutNeighbour = FPlanetPatchId(Adjacency.NeighbourFace, Level, NeighbourX, NeighbourY);
    return true;
}

bool FPlanetPatchId::Deserialize(FUniverseByteReader& Reader)
{
    const uint8 InFace = Reader.ReadUInt8();
    const uint8 InLevel = Reader.ReadUInt8();
    const uint32 InX = Reader.ReadUInt32();
    const uint32 InY = Reader.ReadUInt32();

    if (!Reader.IsValid())
    {
        return false;
    }

    FPlanetPatchId Candidate;
    Candidate.Face = InFace;
    Candidate.Level = InLevel;
    Candidate.X = InX;
    Candidate.Y = InY;

    // Reject rather than clamp. A malformed patch ID means a corrupt file or a
    // crafted packet, and silently repairing it would attach persistent data to
    // the wrong piece of ground.
    if (!Candidate.IsValid())
    {
        return false;
    }

    *this = Candidate;
    return true;
}

FString FPlanetPatchId::ToDebugString() const
{
    return FString::Printf(
        TEXT("%s L%u (%u, %u)"),
        CubeSphere::ToString(GetFace()),
        static_cast<uint32>(Level),
        X,
        Y);
}

FPlanetPatchId FPlanetPatchId::FromDirection(const FVector3d& Direction, uint8 Level)
{
    CubeSphere::EFace Face = CubeSphere::EFace::PosX;
    double U = 0.0;
    double V = 0.0;
    CubeSphere::DirectionToFaceUV(Direction, Face, U, V);

    const uint8 ClampedLevel = static_cast<uint8>(FMath::Min<int32>(Level, MaxLevel));
    const int32 Span = 1 << ClampedLevel;

    // Clamped, not wrapped. A UV of exactly 1.0 - which happens on every face
    // boundary, and is the common case rather than an edge case - floors to
    // Span, one past the last valid index. Wrapping it to zero would put the
    // point on the opposite side of the face.
    const int32 X = FMath::Clamp(FMath::FloorToInt32(U * Span), 0, Span - 1);
    const int32 Y = FMath::Clamp(FMath::FloorToInt32(V * Span), 0, Span - 1);

    return FPlanetPatchId(Face, ClampedLevel, static_cast<uint32>(X), static_cast<uint32>(Y));
}

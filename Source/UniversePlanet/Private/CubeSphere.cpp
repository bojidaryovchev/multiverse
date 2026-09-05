// Copyright Universe Project. All Rights Reserved.

#include "CubeSphere.h"

namespace CubeSphere
{
    namespace
    {
        /**
         * The face basis, laid out so Right x Up == Forward for every face.
         *
         * Every component is exactly 0 or +/-1. That is what makes cube vectors
         * - and therefore shared face edges - exact rather than approximate.
         */
        struct FFaceBasis
        {
            FVector3d Forward;
            FVector3d Right;
            FVector3d Up;
        };

        const FFaceBasis FaceBases[FaceCount] =
        {
            // PosX
            { FVector3d( 1.0,  0.0,  0.0), FVector3d( 0.0,  1.0,  0.0), FVector3d( 0.0,  0.0,  1.0) },
            // NegX
            { FVector3d(-1.0,  0.0,  0.0), FVector3d( 0.0, -1.0,  0.0), FVector3d( 0.0,  0.0,  1.0) },
            // PosY
            { FVector3d( 0.0,  1.0,  0.0), FVector3d(-1.0,  0.0,  0.0), FVector3d( 0.0,  0.0,  1.0) },
            // NegY
            { FVector3d( 0.0, -1.0,  0.0), FVector3d( 1.0,  0.0,  0.0), FVector3d( 0.0,  0.0,  1.0) },
            // PosZ
            { FVector3d( 0.0,  0.0,  1.0), FVector3d( 0.0,  1.0,  0.0), FVector3d(-1.0,  0.0,  0.0) },
            // NegZ
            { FVector3d( 0.0,  0.0, -1.0), FVector3d( 0.0,  1.0,  0.0), FVector3d( 1.0,  0.0,  0.0) },
        };

        /**
         * Canonical edge correspondence, indexed [face][edge] with edge order
         * West, East, South, North.
         *
         * Each entry was derived by writing out the cube vector for the edge
         * on both faces and solving for the neighbour's (s, t). For example
         * PosX East is the set of points (1, 1, t); on PosY the cube vector is
         * (-s', 1, t'), so -s' = 1 gives s' = -1 (PosY's West edge) and t' = t
         * (no reversal). PosZ North is (-1, s, 1); on NegX the cube vector is
         * (-1, -s', t'), so s' = -s - the along-edge direction is reversed.
         *
         * Every one of the twelve cube edges appears twice here, once from each
         * side, and the two entries must agree. A test checks that, and checks
         * the whole table against the geometry, because a plausible-looking but
         * wrong entry produces terrain that is continuous on most seams and
         * mirrored on a few - a genuinely nasty bug to chase.
         */
        const FEdgeAdjacency EdgeAdjacencies[FaceCount][4] =
        {
            /* PosX */ {
                /* West  */ { EFace::NegY, EFaceEdge::East,  false },
                /* East  */ { EFace::PosY, EFaceEdge::West,  false },
                /* South */ { EFace::NegZ, EFaceEdge::North, false },
                /* North */ { EFace::PosZ, EFaceEdge::South, false },
            },
            /* NegX */ {
                /* West  */ { EFace::PosY, EFaceEdge::East,  false },
                /* East  */ { EFace::NegY, EFaceEdge::West,  false },
                /* South */ { EFace::NegZ, EFaceEdge::South, true  },
                /* North */ { EFace::PosZ, EFaceEdge::North, true  },
            },
            /* PosY */ {
                /* West  */ { EFace::PosX, EFaceEdge::East,  false },
                /* East  */ { EFace::NegX, EFaceEdge::West,  false },
                /* South */ { EFace::NegZ, EFaceEdge::East,  true  },
                /* North */ { EFace::PosZ, EFaceEdge::East,  false },
            },
            /* NegY */ {
                /* West  */ { EFace::NegX, EFaceEdge::East,  false },
                /* East  */ { EFace::PosX, EFaceEdge::West,  false },
                /* South */ { EFace::NegZ, EFaceEdge::West,  false },
                /* North */ { EFace::PosZ, EFaceEdge::West,  true  },
            },
            /* PosZ */ {
                /* West  */ { EFace::NegY, EFaceEdge::North, true  },
                /* East  */ { EFace::PosY, EFaceEdge::North, false },
                /* South */ { EFace::PosX, EFaceEdge::North, false },
                /* North */ { EFace::NegX, EFaceEdge::North, true  },
            },
            /* NegZ */ {
                /* West  */ { EFace::NegY, EFaceEdge::South, false },
                /* East  */ { EFace::PosY, EFaceEdge::South, true  },
                /* South */ { EFace::NegX, EFaceEdge::South, true  },
                /* North */ { EFace::PosX, EFaceEdge::South, false },
            },
        };
    }

    const TCHAR* ToString(EFace Face)
    {
        switch (Face)
        {
        case EFace::PosX: return TEXT("+X");
        case EFace::NegX: return TEXT("-X");
        case EFace::PosY: return TEXT("+Y");
        case EFace::NegY: return TEXT("-Y");
        case EFace::PosZ: return TEXT("+Z");
        case EFace::NegZ: return TEXT("-Z");
        default:          return TEXT("??");
        }
    }

    const TCHAR* ToString(EFaceEdge Edge)
    {
        switch (Edge)
        {
        case EFaceEdge::West:  return TEXT("West");
        case EFaceEdge::East:  return TEXT("East");
        case EFaceEdge::South: return TEXT("South");
        case EFaceEdge::North: return TEXT("North");
        default:               return TEXT("????");
        }
    }

    FVector3d GetFaceForward(EFace Face)
    {
        return FaceBases[static_cast<int32>(Face)].Forward;
    }

    FVector3d GetFaceRight(EFace Face)
    {
        return FaceBases[static_cast<int32>(Face)].Right;
    }

    FVector3d GetFaceUp(EFace Face)
    {
        return FaceBases[static_cast<int32>(Face)].Up;
    }

    void DirectionToFaceUV(const FVector3d& Direction, EFace& OutFace, double& OutU, double& OutV)
    {
        const double AbsX = FMath::Abs(Direction.X);
        const double AbsY = FMath::Abs(Direction.Y);
        const double AbsZ = FMath::Abs(Direction.Z);

        // Ties are broken by a fixed axis order rather than by whichever
        // comparison happens to win, so a direction sitting exactly on an edge
        // or corner always resolves to the same face.
        EFace Face;
        if (AbsX >= AbsY && AbsX >= AbsZ)
        {
            Face = (Direction.X >= 0.0) ? EFace::PosX : EFace::NegX;
        }
        else if (AbsY >= AbsZ)
        {
            Face = (Direction.Y >= 0.0) ? EFace::PosY : EFace::NegY;
        }
        else
        {
            Face = (Direction.Z >= 0.0) ? EFace::PosZ : EFace::NegZ;
        }

        const FFaceBasis& Basis = FaceBases[static_cast<int32>(Face)];

        const double Denominator =
            Basis.Forward.X * Direction.X + Basis.Forward.Y * Direction.Y + Basis.Forward.Z * Direction.Z;

        // Denominator is the component along the dominant axis, so it is at
        // least 1/sqrt(3) of the direction's length for any non-degenerate
        // input. A zero here means a zero-length direction was passed in.
        if (Denominator == 0.0)
        {
            OutFace = Face;
            OutU = 0.5;
            OutV = 0.5;
            return;
        }

        const double InvDenominator = 1.0 / Denominator;

        const double S =
            (Basis.Right.X * Direction.X + Basis.Right.Y * Direction.Y + Basis.Right.Z * Direction.Z) * InvDenominator;
        const double T =
            (Basis.Up.X * Direction.X + Basis.Up.Y * Direction.Y + Basis.Up.Z * Direction.Z) * InvDenominator;

        OutFace = Face;
        OutU = 0.5 * (S + 1.0);
        OutV = 0.5 * (T + 1.0);
    }

    void WrapFaceUV(EFace Face, double U, double V, EFace& OutFace, double& OutU, double& OutV)
    {
        // Already inside: nothing to do, and crucially the UV is returned
        // untouched so that in-range lookups are bit-exact.
        if (U >= 0.0 && U <= 1.0 && V >= 0.0 && V <= 1.0)
        {
            OutFace = Face;
            OutU = U;
            OutV = V;
            return;
        }

        // Build the cube vector from the out-of-range coordinate and ask which
        // face it actually lands on. Going through the shared arithmetic is
        // what makes edges and corners agree automatically.
        const FVector3d Cube = FaceUVToCubeVector(Face, U, V);
        DirectionToFaceUV(Cube, OutFace, OutU, OutV);
    }

    FEdgeAdjacency GetEdgeAdjacency(EFace Face, EFaceEdge Edge)
    {
        return EdgeAdjacencies[static_cast<int32>(Face)][static_cast<int32>(Edge)];
    }

    EFace GetEdgeNeighbour(EFace Face, EFaceEdge Edge)
    {
        return EdgeAdjacencies[static_cast<int32>(Face)][static_cast<int32>(Edge)].NeighbourFace;
    }
}

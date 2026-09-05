// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseHash.h"

/**
 * PlanetNoise.h
 *
 * Deterministic 3D gradient noise, sampled in the space the planet actually
 * lives in.
 *
 *
 * WHY 3D, AND WHY NOT A HEIGHTMAP
 *
 * Terrain height is a function of a direction on the unit sphere, evaluated by
 * sampling 3D noise at that direction. It is never a 2D heightmap wrapped onto
 * a sphere. A wrapped 2D map has to be cut somewhere, and every cut is a seam
 * that has to be stitched; sampling a genuinely 3D field has no cut at all, so
 * cube-face boundaries and the poles are unremarkable places with nothing
 * special about them.
 *
 *
 * WHY THIS IS WRITTEN OUT RATHER THAN PULLED IN
 *
 * Sprint 002 explicitly asks whether to take a noise dependency such as
 * FastNoise2. The answer here is no, for a reason specific to this project.
 *
 * Every value below is produced by integer hashing plus add, multiply and
 * floor. Not one transcendental function is involved - no sin, cos, pow or
 * log. All of those operations are exactly specified by IEEE-754, so terrain
 * is bit-identical on any conforming platform and any compiler, which is a
 * strictly stronger guarantee than Sprint 001 managed for star generation
 * (see ProceduralGeneration.md section 7, where libm leaves a real gap).
 *
 * A SIMD noise library would very likely be faster. It would also make
 * determinism dependent on which instruction set the machine happened to
 * select at runtime, which for terrain the player builds on is exactly the
 * wrong trade. The performance answer, if one is needed, is to reduce the
 * number of samples rather than to make each sample unverifiable.
 *
 * The one caveat is compiler contraction: an FMA would change the rounding of
 * a*b + c. The build uses /fp:strict, which forbids it.
 */
namespace PlanetNoise
{
    /**
     * The 12 edge-midpoint gradients of a cube, as used by improved Perlin
     * noise. Each component is 0 or +/-1, so the gradient dot product is a sum
     * of exact terms and needs no normalisation.
     */
    inline constexpr double GradientTable[12][3] =
    {
        { 1, 1, 0}, {-1, 1, 0}, { 1,-1, 0}, {-1,-1, 0},
        { 1, 0, 1}, {-1, 0, 1}, { 1, 0,-1}, {-1, 0,-1},
        { 0, 1, 1}, { 0,-1, 1}, { 0, 1,-1}, { 0,-1,-1},
    };

    /** Hashes an integer lattice point to one of the 12 gradients. */
    inline int32 GradientIndex(int64 X, int64 Y, int64 Z, uint64 Seed)
    {
        const uint64 Hash = UniverseHash::Hash(Seed, X, Y, Z);
        // Modulo 12 of a well-mixed 64-bit value; the bias from 2^64 not being
        // a multiple of 12 is around 1e-19 and irrelevant here.
        return static_cast<int32>(Hash % 12u);
    }

    /** Dot product of the lattice gradient with the offset to the sample. */
    inline double GradientDot(int64 X, int64 Y, int64 Z, uint64 Seed, double Dx, double Dy, double Dz)
    {
        const int32 Index = GradientIndex(X, Y, Z, Seed);
        return GradientTable[Index][0] * Dx
             + GradientTable[Index][1] * Dy
             + GradientTable[Index][2] * Dz;
    }

    /**
     * Quintic fade 6t^5 - 15t^4 + 10t^3.
     *
     * Perlin's improved curve rather than the cheaper cubic smoothstep: its
     * second derivative vanishes at both ends, which matters because terrain
     * normals are derived from height differences. A cubic fade leaves a
     * discontinuity in curvature at every lattice boundary, and that shows up
     * as a faint grid of lighting creases across the whole planet.
     */
    inline double Fade(double T)
    {
        return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
    }

    inline double Lerp(double A, double B, double T)
    {
        return A + T * (B - A);
    }

    /**
     * Gradient noise at a point. Output is in roughly [-1, 1].
     *
     * FloorToInt64 rather than a cast: casting truncates toward zero, which
     * mirrors the lattice about the origin and puts a visible discontinuity
     * through the middle of the planet.
     */
    inline double Noise3D(double X, double Y, double Z, uint64 Seed)
    {
        const double FloorX = FMath::FloorToDouble(X);
        const double FloorY = FMath::FloorToDouble(Y);
        const double FloorZ = FMath::FloorToDouble(Z);

        const int64 LatticeX = static_cast<int64>(FloorX);
        const int64 LatticeY = static_cast<int64>(FloorY);
        const int64 LatticeZ = static_cast<int64>(FloorZ);

        const double Dx = X - FloorX;
        const double Dy = Y - FloorY;
        const double Dz = Z - FloorZ;

        const double Fx = Fade(Dx);
        const double Fy = Fade(Dy);
        const double Fz = Fade(Dz);

        const double D000 = GradientDot(LatticeX,     LatticeY,     LatticeZ,     Seed, Dx,       Dy,       Dz);
        const double D100 = GradientDot(LatticeX + 1, LatticeY,     LatticeZ,     Seed, Dx - 1.0, Dy,       Dz);
        const double D010 = GradientDot(LatticeX,     LatticeY + 1, LatticeZ,     Seed, Dx,       Dy - 1.0, Dz);
        const double D110 = GradientDot(LatticeX + 1, LatticeY + 1, LatticeZ,     Seed, Dx - 1.0, Dy - 1.0, Dz);
        const double D001 = GradientDot(LatticeX,     LatticeY,     LatticeZ + 1, Seed, Dx,       Dy,       Dz - 1.0);
        const double D101 = GradientDot(LatticeX + 1, LatticeY,     LatticeZ + 1, Seed, Dx - 1.0, Dy,       Dz - 1.0);
        const double D011 = GradientDot(LatticeX,     LatticeY + 1, LatticeZ + 1, Seed, Dx,       Dy - 1.0, Dz - 1.0);
        const double D111 = GradientDot(LatticeX + 1, LatticeY + 1, LatticeZ + 1, Seed, Dx - 1.0, Dy - 1.0, Dz - 1.0);

        const double X00 = Lerp(D000, D100, Fx);
        const double X10 = Lerp(D010, D110, Fx);
        const double X01 = Lerp(D001, D101, Fx);
        const double X11 = Lerp(D011, D111, Fx);

        const double Y0 = Lerp(X00, X10, Fy);
        const double Y1 = Lerp(X01, X11, Fy);

        return Lerp(Y0, Y1, Fz);
    }

    /** Convenience overload taking a vector. */
    inline double Noise3D(const FVector3d& Point, uint64 Seed)
    {
        return Noise3D(Point.X, Point.Y, Point.Z, Seed);
    }

    /**
     * Fractal Brownian motion: octaves at doubling frequency and halving
     * amplitude, normalised so the result stays in roughly [-1, 1].
     *
     * Lacunarity is exactly 2 and gain exactly 0.5, both powers of two, so the
     * per-octave scaling introduces no rounding of its own.
     */
    inline double FBM(const FVector3d& Point, uint64 Seed, int32 Octaves, double Frequency)
    {
        double Sum = 0.0;
        double Amplitude = 1.0;
        double AmplitudeSum = 0.0;
        double CurrentFrequency = Frequency;

        for (int32 Octave = 0; Octave < Octaves; ++Octave)
        {
            const FVector3d Sample(
                Point.X * CurrentFrequency,
                Point.Y * CurrentFrequency,
                Point.Z * CurrentFrequency);

            // Each octave gets its own seed stream, so changing the octave
            // count alters how much detail is present without re-rolling the
            // detail that was already there.
            Sum += Noise3D(Sample, UniverseHash::Hash(Seed, Octave)) * Amplitude;

            AmplitudeSum += Amplitude;
            Amplitude *= 0.5;
            CurrentFrequency *= 2.0;
        }

        return (AmplitudeSum > 0.0) ? (Sum / AmplitudeSum) : 0.0;
    }

    /**
     * Ridged multifractal: 1 - |noise|, squared to sharpen.
     *
     * The absolute value creates creases where the underlying noise crosses
     * zero, which read as ridge lines. This is what makes mountains look like
     * mountain ranges rather than lumps - plain FBM gives rolling hills and no
     * amount of amplitude turns it into a ridge.
     */
    inline double RidgedFBM(const FVector3d& Point, uint64 Seed, int32 Octaves, double Frequency)
    {
        double Sum = 0.0;
        double Amplitude = 1.0;
        double AmplitudeSum = 0.0;
        double CurrentFrequency = Frequency;

        for (int32 Octave = 0; Octave < Octaves; ++Octave)
        {
            const FVector3d Sample(
                Point.X * CurrentFrequency,
                Point.Y * CurrentFrequency,
                Point.Z * CurrentFrequency);

            const double Raw = Noise3D(Sample, UniverseHash::Hash(Seed, Octave));
            double Ridge = 1.0 - FMath::Abs(Raw);
            Ridge = Ridge * Ridge;

            Sum += Ridge * Amplitude;
            AmplitudeSum += Amplitude;
            Amplitude *= 0.5;
            CurrentFrequency *= 2.0;
        }

        // Rescaled to [-1, 1] to match FBM, so the layers above can be mixed
        // without each one needing to know its own output range.
        const double Normalised = (AmplitudeSum > 0.0) ? (Sum / AmplitudeSum) : 0.0;
        return Normalised * 2.0 - 1.0;
    }
}

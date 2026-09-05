// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UniverseRenderSpace.generated.h"

/**
 * UniverseRenderSpace.h
 *
 * How a universe position becomes an Unreal transform.
 *
 * There are two render spaces, and the distinction is the reason the project
 * can show a solar system at all without pretending the universe is small.
 *
 *  - Local: one Unreal centimetre is one real centimetre. Everything the
 *    player physically interacts with lives here - the probe, and later the
 *    ship interior, terrain patches, characters and physics. Distances are
 *    honest, so speeds, collisions and precision all mean what they say.
 *
 *  - ScaledAstronomical: a uniform linear scale model of real space. Both
 *    positions and radii are multiplied by the SAME factor, so the model is
 *    geometrically exact - angular sizes and parallax are correct, and a star
 *    subtends precisely the angle it really would from that distance. It is a
 *    shrunken copy of reality, not a distorted one.
 *
 * The uniformity is the important part. Scaling distance and radius by
 * different factors would make the picture a lie that gets baked into level
 * design and camera work, and unpicking it later is exactly the trap
 * CLAUDE.md warns about: visual scale must never become universe scale. Here
 * the descriptors keep their true SI values (a gas giant really is 70,000 km
 * in radius) and the scale factor is applied at one point, at render time.
 *
 * Sprint 001 renders astronomical bodies only in scaled space, so they can be
 * seen and navigated toward but not yet approached and landed on. Bridging the
 * two spaces continuously - growing a body out of scaled space into local space
 * as the player closes in - is the Sprint 002 work that makes planets real.
 */
UENUM(BlueprintType)
enum class EUniverseRenderSpace : uint8
{
    /** 1:1 with reality. For anything the player touches. */
    Local UMETA(DisplayName = "Local (1:1)"),

    /** Uniform scale model. For stars and planets viewed from a distance. */
    ScaledAstronomical UMETA(DisplayName = "Scaled Astronomical"),
};

/**
 * Presentation tuning. Nothing here changes what the universe *is* - only how
 * much of it is squeezed into the volume Unreal renders comfortably.
 */
USTRUCT(BlueprintType)
struct UNIVERSE_API FUniversePresentationSettings
{
    GENERATED_BODY()

    /**
     * Multiplier from real centimetres to scaled-space Unreal centimetres.
     *
     * At the default 1e-7, one astronomical unit becomes 1.5e6 cm (15 km) and
     * a Sun-like star becomes ~70 m in radius. Those two numbers together give
     * the star an angular diameter of about half a degree - which is exactly
     * what the Sun subtends from Earth, because a uniform scale preserves
     * angles. That is the sanity check for any change to this value.
     */
    // UnrealHeaderTool parses Clamp metadata as a plain decimal; scientific
    // notation is rejected outright, so the bound is spelled out.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Presentation",
        meta = (ClampMin = "0.000000000001", ClampMax = "1.0"))
    double AstronomicalScale = 1.0e-7;

    /**
     * Optional debug inflation of body radii, breaking scale fidelity so that
     * distant planets are large enough to find by eye.
     *
     * Off by default and named so nobody mistakes it for a physical property.
     * If it is ever needed permanently, that is a sign the scaled-space camera
     * work has been deferred too long.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Presentation",
        meta = (ClampMin = "1.0", ClampMax = "10000.0"))
    double DebugBodyRadiusInflation = 1.0;

    /**
     * How far (Unreal cm) the tracked viewpoint may drift from the Unreal
     * origin before the render frame is rebased.
     *
     * Bounded by float precision in the rendering and physics paths rather
     * than by anything about the universe: at 1e6 cm a float ULP is ~0.06 cm,
     * comfortably below visible vertex jitter.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Presentation",
        meta = (ClampMin = "10000.0"))
    double RebaseRadiusCm = 1.0e6;
};

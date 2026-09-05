// Copyright Universe Project. All Rights Reserved.
#pragma once

// ---------------------------------------------------------------------------
// UnrealShim.h
//
// Minimal stand-ins for the Unreal types UniverseCore uses, so the core can be
// compiled and RUN by a plain C++20 compiler in Tools/StandaloneTests.
//
// This is a test harness, not an abstraction layer. It is compiled only when
// UNIVERSE_STANDALONE=1 and is never part of a game or editor build. If the
// core ever needs an Unreal facility that is awkward to shim, that is a signal
// the code belongs in UniverseGeneration or the game module instead - not a
// signal to grow this file.
//
// It lives outside Source/ precisely so that UnrealBuildTool and
// UnrealHeaderTool never see it, and so that nothing in a shipping build can
// accidentally depend on it.
// ---------------------------------------------------------------------------

#if !(defined(UNIVERSE_STANDALONE) && UNIVERSE_STANDALONE)
    #error "UnrealShim.h must only be compiled with UNIVERSE_STANDALONE=1"
#endif

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <cassert>

// --- Integer types ---------------------------------------------------------
using int8   = std::int8_t;
using int16  = std::int16_t;
using int32  = std::int32_t;
using int64  = std::int64_t;
using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// --- Module export macros --------------------------------------------------
// UnrealBuildTool defines these; standalone they are simply empty.
#define UNIVERSECORE_API
#define UNIVERSEGENERATION_API
#define UNIVERSEPLANET_API

// Note what is deliberately NOT defined here: USTRUCT, UCLASS, UENUM,
// UPROPERTY, GENERATED_BODY and friends.
//
// UniverseCore and UniverseGeneration contain no reflection at all, and
// stripping the macros here would let someone add a USTRUCT to those modules
// and have the standalone build keep working while the Unreal build failed on
// a missing .generated.h. Leaving them undefined makes that mistake a loud
// compile error in the fast harness instead of a confusing one in the engine.

// --- Text ------------------------------------------------------------------
// Two-level indirection so the argument is macro-expanded before the L is
// pasted on, matching Unreal's own TEXT(). Without it, TEXT(#Expr) pastes
// against the literal token "#Expr" instead of the stringified result.
#define UNIVERSE_TEXT_PASTE(x) L##x
#define TEXT(x) UNIVERSE_TEXT_PASTE(x)
using TCHAR = wchar_t;

/** Minimal FString: construction, Printf, append and comparison only. */
struct FString
{
    std::wstring Data;

    FString() = default;
    FString(const wchar_t* In) : Data(In ? In : L"") {}
    explicit FString(std::wstring In) : Data(std::move(In)) {}

    const wchar_t* operator*() const { return Data.c_str(); }
    int32 Len() const { return static_cast<int32>(Data.size()); }
    bool IsEmpty() const { return Data.empty(); }

    FString& operator+=(const FString& Other) { Data += Other.Data; return *this; }
    friend FString operator+(const FString& A, const FString& B) { return FString(A.Data + B.Data); }
    bool operator==(const FString& Other) const { return Data == Other.Data; }
    bool operator!=(const FString& Other) const { return Data != Other.Data; }

    template <typename... TArgs>
    static FString Printf(const wchar_t* Fmt, TArgs... Args)
    {
        wchar_t Buffer[2048];
        _snwprintf_s(Buffer, 2048, _TRUNCATE, Fmt, Args...);
        return FString(std::wstring(Buffer));
    }

    /** Harness convenience only; not part of the Unreal API. */
    std::string ToUtf8() const
    {
        std::string Out;
        Out.reserve(Data.size());
        for (wchar_t C : Data)
        {
            Out.push_back(C < 128 ? static_cast<char>(C) : 63);
        }
        return Out;
    }
};

// --- Containers ------------------------------------------------------------
template <typename T>
struct TArray
{
    std::vector<T> Items;

    int32 Num() const { return static_cast<int32>(Items.size()); }
    bool IsEmpty() const { return Items.empty(); }
    void Reset() { Items.clear(); }
    void Empty() { Items.clear(); }
    void Reserve(int32 N) { Items.reserve(static_cast<size_t>(N)); }
    void SetNum(int32 N) { Items.resize(static_cast<size_t>(N)); }
    void SetNumUninitialized(int32 N) { Items.resize(static_cast<size_t>(N)); }
    int32 Add(const T& Value) { Items.push_back(Value); return Num() - 1; }
    int32 Add(T&& Value) { Items.push_back(std::move(Value)); return Num() - 1; }

    template <typename... TArgs>
    int32 Emplace(TArgs&&... Args) { Items.emplace_back(std::forward<TArgs>(Args)...); return Num() - 1; }

    void Append(const T* Ptr, int32 Count) { Items.insert(Items.end(), Ptr, Ptr + Count); }
    void Append(const TArray<T>& Other) { Items.insert(Items.end(), Other.Items.begin(), Other.Items.end()); }
    bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < Num(); }

    T& operator[](int32 Index) { return Items[static_cast<size_t>(Index)]; }
    const T& operator[](int32 Index) const { return Items[static_cast<size_t>(Index)]; }
    T* GetData() { return Items.data(); }
    const T* GetData() const { return Items.data(); }

    auto begin() { return Items.begin(); }
    auto end() { return Items.end(); }
    auto begin() const { return Items.begin(); }
    auto end() const { return Items.end(); }

    bool operator==(const TArray<T>& Other) const { return Items == Other.Items; }
    bool operator!=(const TArray<T>& Other) const { return Items != Other.Items; }
};

// --- Vector ----------------------------------------------------------------
struct FVector3d
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;

    FVector3d() = default;
    FVector3d(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
    explicit FVector3d(double S) : X(S), Y(S), Z(S) {}

    static const FVector3d ZeroVector;

    FVector3d operator+(const FVector3d& V) const { return FVector3d(X + V.X, Y + V.Y, Z + V.Z); }
    FVector3d operator-(const FVector3d& V) const { return FVector3d(X - V.X, Y - V.Y, Z - V.Z); }
    FVector3d operator*(double S) const { return FVector3d(X * S, Y * S, Z * S); }
    FVector3d operator/(double S) const { return FVector3d(X / S, Y / S, Z / S); }
    FVector3d operator-() const { return FVector3d(-X, -Y, -Z); }
    FVector3d& operator+=(const FVector3d& V) { X += V.X; Y += V.Y; Z += V.Z; return *this; }
    FVector3d& operator-=(const FVector3d& V) { X -= V.X; Y -= V.Y; Z -= V.Z; return *this; }
    FVector3d& operator*=(double S) { X *= S; Y *= S; Z *= S; return *this; }
    bool operator==(const FVector3d& V) const { return X == V.X && Y == V.Y && Z == V.Z; }
    bool operator!=(const FVector3d& V) const { return !(*this == V); }

    double SizeSquared() const { return X * X + Y * Y + Z * Z; }
    double Size() const { return std::sqrt(SizeSquared()); }
    double Dot(const FVector3d& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
    bool IsZero() const { return X == 0.0 && Y == 0.0 && Z == 0.0; }

    FVector3d GetSafeNormal(double Tolerance = 1.e-8) const
    {
        const double Sq = SizeSquared();
        if (Sq < Tolerance * Tolerance)
        {
            return FVector3d(0.0, 0.0, 0.0);
        }
        return *this / std::sqrt(Sq);
    }
};

// C++17 inline variable: no separate translation unit needed for the harness.
inline const FVector3d FVector3d::ZeroVector(0.0, 0.0, 0.0);

inline FVector3d operator*(double S, const FVector3d& V) { return V * S; }

// --- FMath -----------------------------------------------------------------
struct FMath
{
    template <typename T> static T Abs(T V) { return V < T(0) ? -V : V; }
    template <typename T> static T Min(T A, T B) { return A < B ? A : B; }
    template <typename T> static T Max(T A, T B) { return A > B ? A : B; }
    template <typename T> static T Clamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
    template <typename T> static T Square(T V) { return V * V; }

    static double Sqrt(double V) { return std::sqrt(V); }
    static double Pow(double A, double B) { return std::pow(A, B); }
    static double Loge(double V) { return std::log(V); }
    static double Exp(double V) { return std::exp(V); }
    static double Sin(double V) { return std::sin(V); }
    static double Cos(double V) { return std::cos(V); }
    static double Tan(double V) { return std::tan(V); }
    static double Acos(double V) { return std::acos(V); }
    static double Asin(double V) { return std::asin(V); }
    static double Atan2(double Y, double X) { return std::atan2(Y, X); }
    static double FloorToDouble(double V) { return std::floor(V); }
    static double Fmod(double A, double B) { return std::fmod(A, B); }
    static int32 FloorToInt32(double V) { return static_cast<int32>(std::floor(V)); }
    static int64 FloorToInt64(double V) { return static_cast<int64>(std::floor(V)); }
    static double Lerp(double A, double B, double T) { return A + (B - A) * T; }
    static bool IsNearlyZero(double V, double Tol = 1.e-8) { return Abs(V) <= Tol; }
    static bool IsNearlyEqual(double A, double B, double Tol = 1.e-8) { return Abs(A - B) <= Tol; }
    static bool IsFinite(double V) { return std::isfinite(V) != 0; }
};

// --- Assertions ------------------------------------------------------------
#define check(Expr)            assert(Expr)
#define checkf(Expr, ...)      assert(Expr)
#define ensure(Expr)           (!!(Expr))
#define ensureMsgf(Expr, ...)  (!!(Expr))
#define verify(Expr)           ((void)(Expr))

// --- Logging (no-op in the harness) ---------------------------------------
#define UE_LOG(...)  do {} while (0)

// Coordinate conversion between the simulation core and Unreal.
//
//   core   : x = east, y = north, z = up, metres, right-handed; heading in radians, 0 = east, counter-clockwise.
//   Unreal : X = east, Y = south, Z = up, centimetres, left-handed; yaw in degrees, clockwise seen from above.
//
// Mirroring the north axis turns the right-handed core frame into Unreal's left-handed one without mirroring
// geometry, so meshes keep their chirality and the sun keeps rising in the east. The terrain exporter uses the
// same convention (heightmap row 0 = northern edge = most negative Unreal Y).
// Body attitude: core pitch > 0 = nose up and roll > 0 = left side up, which are Unreal's Pitch and Roll signs.
#pragma once

#include "CoreMinimal.h"

#include "Noctis/Core/Math.h"

struct FNoctisSpace
{
	static constexpr double CmPerM = 100.0;

	/** World offset (cm) added to every converted position. */
	FVector OffsetCm = FVector::ZeroVector;

	FVector ToUE(const noctis::Vec3& P) const
	{
		return FVector(double(P.x) * CmPerM, -double(P.y) * CmPerM, double(P.z) * CmPerM) + OffsetCm;
	}

	FVector ToUE(const noctis::Vec2& P, double ZMetres) const
	{
		return FVector(double(P.x) * CmPerM, -double(P.y) * CmPerM, ZMetres * CmPerM) + OffsetCm;
	}

	noctis::Vec3 FromUE(const FVector& P) const
	{
		const FVector L = P - OffsetCm;
		return noctis::Vec3(float(L.X / CmPerM), float(-L.Y / CmPerM), float(L.Z / CmPerM));
	}

	noctis::Vec2 FromUE2D(const FVector& P) const
	{
		const FVector L = P - OffsetCm;
		return noctis::Vec2(float(L.X / CmPerM), float(-L.Y / CmPerM));
	}

	/** Unit direction (no offset, no scale). */
	static FVector DirectionToUE(const noctis::Vec3& D) { return FVector(D.x, -D.y, D.z); }

	/** Horizontal velocity m/s -> cm/s. */
	static FVector VelocityToUE(const noctis::Vec2& V) { return FVector(double(V.x) * CmPerM, -double(V.y) * CmPerM, 0.0); }

	static noctis::Vec2 VelocityFromUE(const FVector& V) { return noctis::Vec2(float(V.X / CmPerM), float(-V.Y / CmPerM)); }

	static double HeadingToYawDeg(float HeadingRad) { return -FMath::RadiansToDegrees(double(HeadingRad)); }

	static float YawDegToHeading(double YawDeg) { return float(-FMath::DegreesToRadians(YawDeg)); }

	/** Core body attitude (heading, pitch, roll in radians) to an Unreal rotator. */
	static FRotator AttitudeToUE(float HeadingRad, float PitchRad, float RollRad)
	{
		return FRotator(FMath::RadiansToDegrees(double(PitchRad)), HeadingToYawDeg(HeadingRad), FMath::RadiansToDegrees(double(RollRad)));
	}

	static double MetresToCm(double M) { return M * CmPerM; }
	static double CmToMetres(double Cm) { return Cm / CmPerM; }
};

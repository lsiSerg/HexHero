// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexCoord.h"

namespace
{
	const FHexCoord DirectionOffsets[FHexCoord::NumDirections] = {
		FHexCoord(1, -1),	// north-east
		FHexCoord(1, 0),	// east
		FHexCoord(0, 1),	// south-east
		FHexCoord(-1, 1),	// south-west
		FHexCoord(-1, 0),	// west
		FHexCoord(0, -1)	// north-west
	};

	const float LayoutSqrt3 = FMath::Sqrt(3.0f);
}

FHexCoord FHexCoord::FromOffset(int32 Column, int32 Row)
{
	return FHexCoord(Column - (Row - (Row & 1)) / 2, Row);
}

int32 FHexCoord::GetColumn() const
{
	return Q + (R - (R & 1)) / 2;
}

FHexCoord FHexCoord::GetNeighbor(int32 Direction) const
{
	const FHexCoord& Offset = DirectionOffsets[((Direction % NumDirections) + NumDirections) % NumDirections];
	return FHexCoord(Q + Offset.Q, R + Offset.R);
}

int32 FHexCoord::DistanceTo(const FHexCoord& Other) const
{
	const int32 DeltaQ = Q - Other.Q;
	const int32 DeltaR = R - Other.R;
	return (FMath::Abs(DeltaQ) + FMath::Abs(DeltaR) + FMath::Abs(DeltaQ + DeltaR)) / 2;
}

FVector2D FHexLayout::HexToWorld(const FHexCoord& Coord) const
{
	return FVector2D(-1.5f * Radius * Coord.R, LayoutSqrt3 * Radius * (Coord.Q + Coord.R * 0.5f));
}

FHexCoord FHexLayout::WorldToHex(const FVector2D& Position) const
{
	const double FractionalR = -Position.X / (1.5 * Radius);
	const double FractionalQ = Position.Y / (LayoutSqrt3 * Radius) - FractionalR * 0.5;
	const double FractionalS = -FractionalQ - FractionalR;

	// round in cube coordinates and rebuild the axis with the largest rounding error
	double RoundedQ = FMath::RoundToDouble(FractionalQ);
	double RoundedR = FMath::RoundToDouble(FractionalR);
	const double RoundedS = FMath::RoundToDouble(FractionalS);

	const double ErrorQ = FMath::Abs(RoundedQ - FractionalQ);
	const double ErrorR = FMath::Abs(RoundedR - FractionalR);
	const double ErrorS = FMath::Abs(RoundedS - FractionalS);

	if (ErrorQ > ErrorR && ErrorQ > ErrorS)
	{
		RoundedQ = -RoundedR - RoundedS;
	}
	else if (ErrorR > ErrorS)
	{
		RoundedR = -RoundedQ - RoundedS;
	}

	return FHexCoord(FMath::RoundToInt32(RoundedQ), FMath::RoundToInt32(RoundedR));
}

FVector2D FHexLayout::GetCornerDirection(int32 Corner)
{
	const int32 WrappedCorner = ((Corner % FHexCoord::NumDirections) + FHexCoord::NumDirections) % FHexCoord::NumDirections;
	const double Angle = FMath::DegreesToRadians(60.0 * WrappedCorner);
	return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
}

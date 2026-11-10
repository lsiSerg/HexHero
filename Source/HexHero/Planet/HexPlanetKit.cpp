// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetKit.h"
#include "Engine/StaticMesh.h"
#include "HexSphereGrid.h"
#include "StaticMeshResources.h"

#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
	const double KitSqrt3 = FMath::Sqrt(3.0);
}

TArray<double> HexPlanetKit::FindCornerHeights(const FHexSphereGrid& Grid, TFunctionRef<double(int32)> GetTop, EHexEdgeRule Rule)
{
	// three cells meet at every corner
	const bool bHighest = Rule == EHexEdgeRule::HigherHolds;
	TArray<double> Heights;
	Heights.Init(bHighest ? -TNumericLimits<double>::Max() : 0.0, Grid.GetCorners().Num());

	for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
	{
		const double Top = GetTop(Cell);
		for (const int32 Corner : Grid.GetCells()[Cell].Corners)
		{
			Heights[Corner] = bHighest ? FMath::Max(Heights[Corner], Top) : Heights[Corner] + Top / 3.0;
		}
	}
	return Heights;
}

HexPlanetKit::FCellFrame HexPlanetKit::BuildFrame(const FHexSphereGrid& Grid, int32 Cell, TFunctionRef<double(int32)> GetTop, TFunctionRef<int32(int32)> GetLevel,
	TConstArrayView<double> CornerHeights, TConstArrayView<int32> MassifSteps, TConstArrayView<float> Relief, EHexEdgeRule Rule)
{
	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const int32 NumSides = Each.Neighbors.Num();
	const int32 Level = GetLevel(Cell);

	FCellFrame Frame;
	Frame.Top = GetTop(Cell);
	Frame.MassifCenter = MassifSteps[Cell];
	const auto GetRelief = [&Relief](int32 Other) { return Relief.IsEmpty() ? 1.0 : double(Relief[Other]); };
	Frame.ReliefCenter = GetRelief(Cell);

	// the plateau: two cells share it when they are on the same level and neither is a pentagon, and a cell has the share of its sides towards
	// such neighbors
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const auto ArePlateau = [&](int32 One, int32 Other) { return GetLevel(One) == GetLevel(Other) && !Cells[One].IsPentagon() && !Cells[Other].IsPentagon(); };
	const auto GetPlateauShare = [&](int32 Of)
	{
		int32 Count = 0;
		for (const int32 Next : Cells[Of].Neighbors)
		{
			Count += ArePlateau(Of, Next) ? 1 : 0;
		}
		return double(Count) / Cells[Of].Neighbors.Num();
	};
	Frame.PlateauCenter = GetPlateauShare(Cell);
	TArray<double, TInlineAllocator<6>> NeighborPlateau;
	for (const int32 Neighbor : Each.Neighbors)
	{
		NeighborPlateau.Add(ArePlateau(Cell, Neighbor) ? GetPlateauShare(Neighbor) : -1.0);
	}

	for (int32 Side = 0; Side < NumSides; ++Side)
	{
		const int32 Neighbor = Each.Neighbors[Side];
		const int32 NextNeighbor = Each.Neighbors[(Side + 1) % NumSides];
		const double NeighborSteps = GetLevel(Neighbor) == Level ? MassifSteps[Neighbor] : 0.0;
		const double NextSteps = GetLevel(NextNeighbor) == Level ? MassifSteps[NextNeighbor] : 0.0;

		Frame.SideMiddles.Add(GetSideMiddle(Frame.Top, GetTop(Neighbor), Rule));
		Frame.SameSides.Add(GetLevel(Neighbor) == Level);
		// corner K lies between neighbors K and K + 1, and its three cells are all on the plateau when both of them share it with this one
		const double SidePlateau = NeighborPlateau[Side];
		const double NextPlateau = NeighborPlateau[(Side + 1) % NumSides];
		Frame.PlateauSides.Add(SidePlateau >= 0.0 ? (Frame.PlateauCenter + SidePlateau) * 0.5 : 0.0);
		Frame.PlateauCorners.Add(SidePlateau >= 0.0 && NextPlateau >= 0.0 ? (Frame.PlateauCenter + SidePlateau + NextPlateau) / 3.0 : 0.0);
		Frame.CornerHeights.Add(CornerHeights[Each.Corners[Side]]);
		Frame.MassifSides.Add((MassifSteps[Cell] + NeighborSteps) * 0.5);
		// corner K lies between neighbors K and K + 1
		Frame.MassifCorners.Add((MassifSteps[Cell] + NeighborSteps + NextSteps) / 3.0);
		Frame.ReliefSides.Add((Frame.ReliefCenter + GetRelief(Neighbor)) * 0.5);
		Frame.ReliefCorners.Add((Frame.ReliefCenter + GetRelief(Neighbor) + GetRelief(NextNeighbor)) / 3.0);
	}
	return Frame;
}

double HexPlanetKit::GetSideHeight(double Middle, double From, double To, double S)
{
	return Middle + (From - Middle) * FMath::Max(0.0, 1.0 - 2.0 * S) + (To - Middle) * FMath::Max(0.0, 2.0 * S - 1.0);
}

void HexPlanetKit::FindSector(const FVector2d& Point, int32& OutSector, double& OutCenter, double& OutFrom, double& OutTo)
{
	// corner D of the regular hex lies at 60 * D degrees, and the triangle towards direction D lies between corners D and D + 1
	double Angle = FMath::Atan2(Point.Y, Point.X);
	if (Angle < 0.0)
	{
		Angle += UE_DOUBLE_TWO_PI;
	}
	OutSector = FMath::Clamp(FMath::FloorToInt32(Angle / (UE_DOUBLE_PI / 3.0)), 0, 5);

	const FVector2d From(FMath::Cos(OutSector * UE_DOUBLE_PI / 3.0), FMath::Sin(OutSector * UE_DOUBLE_PI / 3.0));
	const FVector2d To(FMath::Cos((OutSector + 1) * UE_DOUBLE_PI / 3.0), FMath::Sin((OutSector + 1) * UE_DOUBLE_PI / 3.0));
	const double Determinant = From.X * To.Y - From.Y * To.X;

	OutFrom = (Point.X * To.Y - Point.Y * To.X) / Determinant;
	OutTo = (From.X * Point.Y - From.Y * Point.X) / Determinant;
	OutCenter = 1.0 - OutFrom - OutTo;
}

FVector3d HexPlanetKit::PlaceVertex(const FHexSphereGrid& Grid, int32 Cell, const FCellFrame& Frame, const FPlacement& Placement,
	int32 Side, double WCenter, double WFrom, double WTo, double Z, double* OutCrevice)
{
	if (OutCrevice)
	{
		*OutCrevice = 0.0;
	}
	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const int32 NumCorners = Each.Corners.Num();
	const int32 FromCorner = (Side + NumCorners - 1) % NumCorners;
	const int32 ToCorner = Side % NumCorners;

	const double Toward = WFrom + WTo;
	const double S = Toward > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(WTo / Toward, 0.0, 1.0) : 0.5;
	const double FromHeight = Frame.CornerHeights[FromCorner];
	const double ToHeight = Frame.CornerHeights[ToCorner];

	// the line of the side, where the foot goes, and the top, which bends to the corners near the sides
	const double Ground = GetSideHeight(Frame.SideMiddles[Side], FromHeight, ToHeight, S);

	// a point on a side towards another level, or at a corner of such a side, lies on that line whatever the piece models there,
	// as a piece may model past it, such as the base of a mountain at a notch
	const bool bOnSide = WCenter < 1.0e-6 && WCenter > -1.0e-6;
	const bool bEdge = !Frame.SameSides[Side] || (S < 1.0e-6 && !Frame.SameSides[FromCorner]) || (S > 1.0 - 1.0e-6 && !Frame.SameSides[(Side + 1) % NumCorners]);
	const TArray<FVector3d>& Corners = Grid.GetCorners();
	FVector3d Direction = (Each.Center * WCenter + Corners[Each.Corners[FromCorner]] * WFrom + Corners[Each.Corners[ToCorner]] * WTo
		+ Frame.CenterShift * FMath::Max(WCenter, 0.0)).GetSafeNormal();

	// the inside turns around the center by an angle of its own, the more the nearer the center, and not at all on the sides
	if (Frame.Twist != 0.0 && WCenter > 0.0)
	{
		Direction = Direction.RotateAngleAxisRad(Frame.Twist * WCenter, Each.Center).GetSafeNormal();
	}
	if (bOnSide && bEdge)
	{
		return Direction * ApplyFloorRise(Placement, Direction, ApplyWobble(Placement, Direction, Ground));
	}

	// a value of the frame at this point: from the center towards the side, and along the side from its middle to its corners, which both cells
	// of a side find alike
	const double Inward = FMath::Min(Toward, 1.0);
	const auto Blend = [&](double Center, const TArray<double, TInlineAllocator<6>>& Sides, const TArray<double, TInlineAllocator<6>>& Ends)
	{
		const double OnSide = S < 0.5 ? FMath::Lerp(Ends[FromCorner], Sides[Side], S * 2.0) : FMath::Lerp(Sides[Side], Ends[ToCorner], S * 2.0 - 1.0);
		return FMath::Lerp(Center, OnSide, Inward);
	};

	// the inside of the cell, which its neighbors never see, fading out before the sides
	const double Inside = 1.0 - FMath::SmoothStep(0.5, 0.95, Toward);
	const double ShapeShare = Placement.ShapeHeight > 0.0 ? FMath::Clamp((Z - Placement.ScaleBase) / Placement.ShapeHeight, 0.0, 1.0) : 0.0;
	if (Placement.ShapeHeight > 0.0 && Frame.Sharpness != 1.0 && ShapeShare > 0.0 && ShapeShare < 1.0)
	{
		// a power of the share of the height: below 1 a blunt dome, above 1 a needle
		const double Shaped = Placement.ScaleBase + FMath::Pow(ShapeShare, Frame.Sharpness) * Placement.ShapeHeight;
		Z = FMath::Lerp(Z, Shaped, Inside);
	}

	// a lone cell lifts its whole height from the foot, so its low ground stands above the ground as a broad base
	const double PeakAboveFoot = Placement.ScaleBase + Placement.ShapeHeight + Placement.FootDepth;
	if (Frame.LoneDome > 0.0 && PeakAboveFoot > 0.0 && Inside > 0.0)
	{
		const double Share = FMath::Clamp((Z + Placement.FootDepth) / PeakAboveFoot, 0.0, 1.0);
		Z = FMath::Lerp(Z, -Placement.FootDepth + FMath::Pow(Share, Frame.LoneDome) * PeakAboveFoot, Inside);
	}

	double Top = Frame.Top;
	const double Follow = Placement.bTopFollowsCorners ? FMath::SmoothStep(0.4, 1.0, Toward) : 0.0;
	Top += (GetSideHeight(Frame.Top, FromHeight, ToHeight, S) - Frame.Top) * Follow;

	// between the foot and FadeHeight above it a vertex blends from the ground to the top, which is a straight blend when FadeHeight is the foot
	const double ToGround = FMath::Clamp((Placement.FadeHeight - Placement.FootDepth - Z) / Placement.FadeHeight, 0.0, 1.0);
	double Height = Top + Z + ToGround * (Ground - (Top - Placement.FootDepth));

	// the factor of the height, blended across the sides as the massif, which both cells of a side find alike
	const double CellRelief = Frame.ReliefSides.IsEmpty() ? 1.0 : Blend(Frame.ReliefCenter, Frame.ReliefSides, Frame.ReliefCorners);

	// the whole piece rises above its foot by a lift of its height on the piece alone, so its low ground, which the blend into the side line
	// flattens, stands as a full base towards the sides, while the foot stays on the line and a side shared with the same level gets it alike
	if (Placement.BulkLift > 0.0 && PeakAboveFoot > 0.0)
	{
		const double Share = FMath::Clamp((Z + Placement.FootDepth) / PeakAboveFoot, 0.0, 1.0);
		Height += Placement.BulkLift * FMath::Sqrt(FMath::Max(CellRelief, 0.0)) * FMath::SmoothStep(0.0, Placement.BulkReach, Share);
	}

	if (Placement.MassifStep > 0.0 || Placement.MassifRise > 0.0)
	{
		// as on the flat map, a piece deep in its group rises as a whole and stretches above its foot, while its foot stays on the ground
		const double Massif = Blend(Frame.MassifCenter, Frame.MassifSides, Frame.MassifCorners);
		const double AboveFoot = Z + Placement.FootDepth;
		Height += FMath::Max(AboveFoot, 0.0) * Massif * Placement.MassifStep + Massif * Placement.MassifRise * FMath::SmoothStep(0.0, 30.0 * Placement.Scale, AboveFoot);
	}

	if (Placement.bRelief && !Frame.ReliefSides.IsEmpty())
	{
		// each cell stretches its piece above ScaleBase by its own factor, blended across its sides. Land puts its sides on the side lines
		// whatever it models there, so it fades the stretch out where its top follows them
		const double Relief = Blend(Frame.ReliefCenter, Frame.ReliefSides, Frame.ReliefCorners);
		Height += FMath::Max(Z - Placement.ScaleBase, 0.0) * (Relief - 1.0) * (1.0 - Follow);
	}

	// a cell with neighbors on its level rises into a plateau with them, the more the more of them it has: on the sides towards them, at the
	// corners between two of them and in its middle, blended across the cell as the massif, so it ramps down to the line of a side towards
	// another level. Its bumps take only a part of the lift, so the ground between them comes up towards their tops rather than each cell
	// standing as a mound of its own. A point of a shared side lies on the top whatever the piece models there, so both cells lift it alike
	if (Placement.PlateauLift > 0.0 && Frame.PlateauCenter > 0.0 && !Frame.PlateauSides.IsEmpty())
	{
		// it sets in gently at a side towards another level, where the slope of the piece is steepest, so the edge of a plateau gets little rock
		const double Share = FMath::SmoothStep(0.0, 1.0, Blend(Frame.PlateauCenter, Frame.PlateauSides, Frame.PlateauCorners));

		// a bump keeps its top when it is twice as high as the lift, and the fill never folds the ground, as the height still grows with the bump
		const double Lift = Placement.PlateauLift * FMath::Max(CellRelief, 0.0);
		const double Bump = Height - Top;
		Height += Lift * Share * (1.0 - Placement.PlateauFill * FMath::SmoothStep(0.0, 2.0 * Lift, Bump));
	}

	// a lone cell never lies below a dome over the whole cell from the line of its sides, so its flat outer ground rises too and the mountain
	// fills its cell; heights low on a piece blend into that line, so the dome goes on the height and not on the piece
	if (Frame.LoneBase > 0.0)
	{
		const double Relief = Frame.ReliefSides.IsEmpty() ? 1.0 : Blend(Frame.ReliefCenter, Frame.ReliefSides, Frame.ReliefCorners);
		const double Dome = Ground + (Placement.ScaleBase + Placement.ShapeHeight + Placement.FootDepth) * Relief * Frame.LoneBase * (1.0 - FMath::SmoothStep(0.0, 1.0, Toward));
		// only inside, as a lone cell may still share a side with one neighbor on its level, which must meet it there
		Height = FMath::Lerp(Height, FMath::Max(Height, Dome), Inside);
	}

	// a cluster of summits of unlike heights over a lower piece, only inside, as the sides of a lone cell lie on the side lines
	if (!Frame.Summits.IsEmpty() && Inside > 0.0)
	{
		const double Relief = Frame.ReliefSides.IsEmpty() ? 1.0 : Blend(Frame.ReliefCenter, Frame.ReliefSides, Frame.ReliefCorners);
		// the factor of the height counts by its root, so a cell of a high factor does not turn its summits into a tower
		const double Peak = (Placement.ScaleBase + Placement.ShapeHeight + Placement.FootDepth) * FMath::Sqrt(FMath::Max(Relief, 0.0));
		double Summit = 0.0;
		for (const FCellFrame::FSummit& One : Frame.Summits)
		{
			const double Near = FMath::Max(0.0, 1.0 - FVector3d::Distance(Direction, One.Direction) / One.Width);
			// a peak that steepens towards its top, with a soft foot
			Summit = FMath::Max(Summit, One.Height * Peak * FMath::Pow(Near, 1.4) * (3.0 - 2.0 * Near) * 0.5 * (1.0 + Near));
		}
		const double Clustered = FMath::Max(Ground + (Height - Ground) * Frame.SummitBase, Ground + Summit);
		Height = FMath::Lerp(Height, Clustered, Inside);
	}

	// a ridge from the center to the middle of each side towards the same level, highest on the side, which both cells give alike as it
	// depends on the place along the side the same way both ways, and grows with the factor of the height
	if (Placement.RidgeLift > 0.0 && Frame.SameSides[Side])
	{
		const double Along = 1.0 - FMath::SmoothStep(0.0, Placement.RidgeWidth, FMath::Abs(S - 0.5));
		const double Relief = Frame.ReliefSides.IsEmpty() ? 1.0 : Blend(Frame.ReliefCenter, Frame.ReliefSides, Frame.ReliefCorners);
		const double Weight = Frame.RidgeWeights.IsValidIndex(Side) ? Frame.RidgeWeights[Side] : 1.0;
		Height += Placement.RidgeLift * Weight * Relief * Along * FMath::SmoothStep(0.1, 1.0, Toward);
	}

	// a crest split into two blades with a couloir between them, along the lines from the center to the middles of the sides towards the same
	// level, in the regular cell of GetLocalUV, only inside, so the crest joins again where the neighbors share a side
	if (Frame.Split > 0.0 && Inside > 0.0 && Placement.SplitWidth > 0.0 && PeakAboveFoot > 0.0)
	{
		const double Turn = UE_DOUBLE_TWO_PI / NumCorners;
		const auto CornerAt = [Turn](int32 Index) { return FVector2d(FMath::Cos((Index + 0.5) * Turn), FMath::Sin((Index + 0.5) * Turn)); };
		const FVector2d Local = CornerAt(FromCorner) * WFrom + CornerAt(ToCorner) * WTo;
		double Axis = TNumericLimits<double>::Max();
		for (int32 Other = 0; Other < NumCorners; ++Other)
		{
			if (!Frame.SameSides[Other])
			{
				continue;
			}
			const FVector2d Middle = (CornerAt(Other - 1 + NumCorners) + CornerAt(Other)) * 0.5;
			const double Along = FMath::Clamp(FVector2d::DotProduct(Local, Middle) / Middle.SquaredLength(), 0.0, 1.0);
			Axis = FMath::Min(Axis, FVector2d::Distance(Local, Middle * Along));
		}
		if (Axis < TNumericLimits<double>::Max())
		{
			// only the upper part of the piece, where the crest is
			const double Crest = FMath::SmoothStep(0.2, 0.7, FMath::Clamp((Z + Placement.FootDepth) / PeakAboveFoot, 0.0, 1.0));
			const double Width = Placement.SplitWidth;
			const double Couloir = 1.0 - FMath::SmoothStep(0.0, Width, Axis);
			const double Blade = FMath::Exp(-FMath::Square((Axis - Width) / (0.45 * Width)));
			const double Peak = PeakAboveFoot * FMath::Sqrt(FMath::Max(CellRelief, 0.0));
			Height += Frame.Split * Inside * Crest * Peak * (Placement.SplitBlade * Blade - Placement.SplitDepth * Couloir);
		}
	}

	// summits and notches inside, most at the top
	if (Placement.CragHeight > 0.0 && Inside > 0.0 && ShapeShare > 0.0)
	{
		const FVector Point(Direction * (Placement.WobbleTops[2] / Placement.CragScale) + FVector3d(41.3, 7.7, 23.9));
		Height += Placement.CragHeight * Inside * ShapeShare * FMath::PerlinNoise3D(Point) * 2.0;
	}

	// gullies down the fall line: channels of a ridged noise of the angle around the center, stretched along the way out, with spurs between
	if (Placement.GullyHeight > 0.0 && Inside > 0.0 && Toward > 0.0)
	{
		const double Angle = (Side + S) * UE_DOUBLE_PI / 3.0;
		const FVector3d Seed = Each.Center * 37.0;
		const FVector Point(FMath::Cos(Angle) * Placement.GullyCount / 6.2831853 * 2.0 + Seed.X, FMath::Sin(Angle) * Placement.GullyCount / 6.2831853 * 2.0 + Seed.Y,
			Toward * Placement.GullyLength + Seed.Z);
		const double Channel = FMath::Pow(1.0 - FMath::Min(FMath::Abs(FMath::PerlinNoise3D(Point) * 1.6 + FMath::PerlinNoise3D(Point * 2.3f + FVector(7.1f, 3.3f, 1.9f)) * 0.5), 1.0), 3.0);
		// most on the middle of the slopes, a little less at the foot, nothing at the top
		const double Weight = Inside * FMath::SmoothStep(0.08, 0.4, Toward) * (1.0 - 0.35 * Toward);
		Height -= Placement.GullyHeight * Weight * (Channel - 0.2);
		if (OutCrevice)
		{
			*OutCrevice = Weight * Channel;
		}
	}

	// high ground breaks into sharp faces by a ridged noise of the place and of its height, which cells that share a point agree on
	const double Facets = GetFacetShare(Placement, Height);

	// and steps in ledges, a function of the height and the place alone, so cells that share a point agree on it
	if (Placement.LedgeStep > 0.0 && Placement.LedgeStrength > 0.0 && Facets > 0.0)
	{
		const FVector Point(Direction * (Placement.WobbleTops[2] / (Placement.LedgeStep * 4.0)));
		const double Shifted = Height / Placement.LedgeStep + FMath::PerlinNoise3D(Point) * 1.5;
		const double Fraction = Shifted - FMath::FloorToDouble(Shifted);
		// a tread for most of the step and a riser at its end
		const double Stepped = FMath::FloorToDouble(Shifted) + FMath::Pow(Fraction, 3.0);
		Height += (Stepped - Shifted) * Placement.LedgeStep * Placement.LedgeStrength * Facets;
	}
	if (Placement.JaggedHeight > 0.0 && Facets > 0.0)
	{
		const FVector Point(Direction * (Placement.WobbleTops[2] / Placement.JaggedScale));
		const double Ridged = 1.0 - 2.0 * FMath::Abs(FMath::PerlinNoise3D(Point)) + (0.5 - FMath::Abs(FMath::PerlinNoise3D(Point * 2.3f + FVector(3.7f, 11.1f, 5.3f))));
		Height += Placement.JaggedHeight * Facets * Ridged;
	}

	return Direction * ApplyFloorRise(Placement, Direction, ApplyWobble(Placement, Direction, Height));
}

double HexPlanetKit::GetFacetShare(const FPlacement& Placement, double Height)
{
	return Placement.FacetHigh > Placement.FacetLow ? FMath::SmoothStep(Placement.FacetLow, Placement.FacetHigh, Height) : 0.0;
}

double HexPlanetKit::ApplyWobble(const FPlacement& Placement, const FVector3d& Direction, double Height)
{
	const double* Tops = Placement.WobbleTops;
	if (Placement.Wobble <= 0.0 || Height <= Tops[0] || Height >= Tops[2])
	{
		return Height;
	}

	// most halfway down each slope and nothing on the tops, so the levels keep their flat tops and the wobble never lifts the deep seabed
	const double Low = Height < Tops[1] ? Tops[0] : Tops[1];
	const double High = Height < Tops[1] ? Tops[1] : Tops[2];
	const double Weight = FMath::Sin(UE_DOUBLE_PI * (Height - Low) / FMath::Max(High - Low, UE_DOUBLE_SMALL_NUMBER));

	const FVector Point(Direction * (Tops[1] / Placement.WobbleScale));
	const double Noise = FMath::PerlinNoise3D(Point) + FMath::PerlinNoise3D(Point * 3.1f + FVector(17.3f, 5.1f, 9.7f)) * Placement.WobbleDetail;
	// it stays within its slope, so it never lifts a point above the top it rises to nor sinks one below the floor it falls from
	return FMath::Clamp(Height + Placement.Wobble * Weight * Noise, Low, High);
}

double HexPlanetKit::GetFloorRise(const FVector3d& Direction, double FloorTop, double Height, double Scale, double Detail)
{
	if (Height <= 0.0)
	{
		return 0.0;
	}
	// a noise of the place on the sphere of the floor, and a finer one, from 0 to Height, as the floor of the passages is the top of their level
	const FVector Point(Direction * (FloorTop / FMath::Max(Scale, 1.0)));
	const double Noise = FMath::PerlinNoise3D(Point + FVector(31.7f, 12.9f, 47.3f)) + FMath::PerlinNoise3D(Point * 2.7f + FVector(5.9f, 63.1f, 21.4f)) * Detail;
	return Height * FMath::Clamp(0.5 + 0.5 * Noise / (1.0 + Detail), 0.0, 1.0);
}

double HexPlanetKit::ApplyFloorRise(const FPlacement& Placement, const FVector3d& Direction, double Height)
{
	const double Low = Placement.FloorTop - FMath::Max(Placement.FloorRiseFade, 1.0);
	if (Placement.FloorRise <= 0.0 || Height <= Low)
	{
		return Height;
	}
	// all of it on the top of the floor, fading out below it, so the cliffs of the voids keep their feet; a pure function of the point, so the
	// cells that share a point raise it alike
	const double Weight = Height >= Placement.FloorTop ? 1.0 : FMath::SmoothStep(Low, Placement.FloorTop, Height);
	return Height + Weight * GetFloorRise(Direction, Placement.FloorTop, Placement.FloorRise, Placement.FloorRiseScale, Placement.FloorRiseDetail);
}

namespace
{
	/** A mesh in its own units, with vertices that share a position joined */
	struct FSourceMesh
	{
		TArray<FVector3d> Positions;
		TArray<FVector2f> UVs;
		TArray<int32> Triangles;
	};

	/** Samples a mesh as a height field on a triangle lattice, as generate_hex_mountains.py lays out its own: corner 0 lies along +X */
	FSourceMesh SampleLattice(const FSourceMesh& Mesh, double MeshRadius, int32 Subdivisions)
	{
		const double Step = MeshRadius / Subdivisions;
		const double RowStep = Step * KitSqrt3 * 0.5;

		// edges used by one triangle only are the outline, onto which lattice points just past it move
		TMap<TPair<int32, int32>, int32> EdgeUses;
		for (int32 Index = 0; Index < Mesh.Triangles.Num(); Index += 3)
		{
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 A = Mesh.Triangles[Index + Corner];
				const int32 B = Mesh.Triangles[Index + (Corner + 1) % 3];
				++EdgeUses.FindOrAdd(TPair<int32, int32>(FMath::Min(A, B), FMath::Max(A, B)));
			}
		}
		TArray<TPair<int32, int32>> Outline;
		for (const TPair<TPair<int32, int32>, int32>& Edge : EdgeUses)
		{
			if (Edge.Value == 1)
			{
				Outline.Add(Edge.Key);
			}
		}

		double Reach = 0.0;
		for (const FVector3d& Position : Mesh.Positions)
		{
			Reach = FMath::Max(Reach, FVector2d(Position.X, Position.Y).Length());
		}
		const int32 Extent = FMath::CeilToInt32(Reach / RowStep) + 1;

		FSourceMesh Sampled;
		TMap<FIntPoint, int32> Points;

		for (int32 B = -Extent; B <= Extent; ++B)
		{
			for (int32 A = -2 * Extent; A <= 2 * Extent; ++A)
			{
				const FVector2d Point((A + B * 0.5) * Step, B * RowStep);
				if (Point.Length() > Reach + Step)
				{
					continue;
				}

				// the triangle of the mesh that holds the point, allowing for the rounding at its edges
				bool bFound = false;
				FVector3d Position;
				FVector2f UV;
				double BestInside = -1.0e-4;

				for (int32 Index = 0; Index < Mesh.Triangles.Num(); Index += 3)
				{
					const FVector3d& P0 = Mesh.Positions[Mesh.Triangles[Index]];
					const FVector3d& P1 = Mesh.Positions[Mesh.Triangles[Index + 1]];
					const FVector3d& P2 = Mesh.Positions[Mesh.Triangles[Index + 2]];
					const double Determinant = (P1.X - P0.X) * (P2.Y - P0.Y) - (P2.X - P0.X) * (P1.Y - P0.Y);
					if (FMath::Abs(Determinant) < UE_DOUBLE_SMALL_NUMBER)
					{
						continue;
					}

					const double W1 = ((Point.X - P0.X) * (P2.Y - P0.Y) - (P2.X - P0.X) * (Point.Y - P0.Y)) / Determinant;
					const double W2 = ((P1.X - P0.X) * (Point.Y - P0.Y) - (Point.X - P0.X) * (P1.Y - P0.Y)) / Determinant;
					const double W0 = 1.0 - W1 - W2;
					const double Inside = FMath::Min3(W0, W1, W2);

					if (Inside > BestInside)
					{
						BestInside = Inside;
						bFound = true;
						Position = FVector3d(Point.X, Point.Y, P0.Z * W0 + P1.Z * W1 + P2.Z * W2);
						UV = Mesh.UVs[Mesh.Triangles[Index]] * float(W0) + Mesh.UVs[Mesh.Triangles[Index + 1]] * float(W1) + Mesh.UVs[Mesh.Triangles[Index + 2]] * float(W2);
						if (Inside >= 0.0)
						{
							break;
						}
					}
				}

				// a point a little past the outline moves onto it, so the outline keeps its shape where it does not follow the lattice
				if (!bFound)
				{
					double BestDistance = Step * 0.6;
					for (const TPair<int32, int32>& Edge : Outline)
					{
						const FVector3d& From = Mesh.Positions[Edge.Key];
						const FVector3d& To = Mesh.Positions[Edge.Value];
						const FVector2d Along = FVector2d(To.X - From.X, To.Y - From.Y);
						const double Fraction = FMath::Clamp(FVector2d::DotProduct(Point - FVector2d(From.X, From.Y), Along) / FMath::Max(Along.SquaredLength(), UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
						const FVector3d OnEdge = FMath::Lerp(From, To, Fraction);
						const double Distance = FVector2d::Distance(Point, FVector2d(OnEdge.X, OnEdge.Y));

						if (Distance < BestDistance)
						{
							BestDistance = Distance;
							bFound = true;
							Position = OnEdge;
							UV = FMath::Lerp(Mesh.UVs[Edge.Key], Mesh.UVs[Edge.Value], float(Fraction));
						}
					}
				}

				if (bFound)
				{
					Points.Add(FIntPoint(A, B), Sampled.Positions.Add(Position));
					Sampled.UVs.Add(UV);
				}
			}
		}

		// lattice triangles whose winding faces up: (C - A) x (B - A) points up
		for (const TPair<FIntPoint, int32>& Point : Points)
		{
			const FIntPoint& At = Point.Key;
			const int32* Right = Points.Find(At + FIntPoint(1, 0));
			const int32* Up = Points.Find(At + FIntPoint(0, 1));
			const int32* LeftUp = Points.Find(At + FIntPoint(-1, 1));

			if (Right && Up)
			{
				Sampled.Triangles.Append({Point.Value, *Up, *Right});
			}
			if (Up && LeftUp)
			{
				Sampled.Triangles.Append({Point.Value, *LeftUp, *Up});
			}
		}
		return Sampled;
	}
}

bool HexPlanetKit::ReadPiece(UStaticMesh* Mesh, double Scale, double MeshRadius, int32 Subdivisions, FPiece& OutPiece)
{
	OutPiece = FPiece();

	if (!Mesh)
	{
		return false;
	}

#if WITH_EDITOR
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif

	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.IsEmpty() || (!Mesh->bAllowCPUAccess && !GIsEditor))
	{
		return false;
	}

	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& Attributes = LOD.VertexBuffers.StaticMeshVertexBuffer;
	if (Positions.GetNumVertices() == 0 || !Positions.GetVertexData() || !LOD.IndexBuffer.GetArrayView().Num())
	{
		return false;
	}

	// vertices split by the mesh, such as at a seam of its UVs, are joined, so normals are shared there
	FSourceMesh Raw;
	TMap<FIntVector, int32> Joined;
	TArray<int32> Remap;
	Remap.SetNumUninitialized(Positions.GetNumVertices());

	for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
	{
		const FVector3f Position = Positions.VertexPosition(Index);
		const FIntVector Key(FMath::RoundToInt32(Position.X * 64.0f), FMath::RoundToInt32(Position.Y * 64.0f), FMath::RoundToInt32(Position.Z * 64.0f));

		if (const int32* Existing = Joined.Find(Key))
		{
			Remap[Index] = *Existing;
			continue;
		}
		Remap[Index] = Raw.Positions.Add(FVector3d(Position));
		Raw.UVs.Add(Attributes.GetNumTexCoords() > 0 ? Attributes.GetVertexUV(Index, 0) : FVector2f::ZeroVector);
		Joined.Add(Key, Remap[Index]);
	}

	const FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
	for (const FStaticMeshSection& Section : LOD.Sections)
	{
		for (uint32 Index = Section.FirstIndex; Index + 2 < Section.FirstIndex + Section.NumTriangles * 3; Index += 3)
		{
			const int32 A = Remap[Indices[Index]];
			const int32 B = Remap[Indices[Index + 1]];
			const int32 C = Remap[Indices[Index + 2]];

			if (A != B && B != C && A != C)
			{
				Raw.Triangles.Append({A, B, C});
			}
		}
	}

	if (Subdivisions > 0)
	{
		Raw = SampleLattice(Raw, MeshRadius, Subdivisions);
	}

	const double Apothem = KitSqrt3 * 0.5;
	for (int32 Index = 0; Index < Raw.Positions.Num(); ++Index)
	{
		const FVector3d& Position = Raw.Positions[Index];
		const FVector2d Point = FVector2d(Position.X, Position.Y) / MeshRadius;
		FPieceVertex Vertex;
		FindSector(Point, Vertex.Sector, Vertex.WCenter, Vertex.WFrom, Vertex.WTo);
		Vertex.Z = Position.Z * Scale;

		double Reach = 0.0;
		for (int32 Direction = 0; Direction < 6; ++Direction)
		{
			const double Angle = FMath::DegreesToRadians(30.0 + 60.0 * Direction);
			Reach = FMath::Max(Reach, (Point.X * FMath::Cos(Angle) + Point.Y * FMath::Sin(Angle)) / Apothem);
		}
		Vertex.bOnSide = FMath::Abs(Reach - 1.0) < 1.0e-3;
		Vertex.GridU = Reach > 1.0 + 1.0e-3 ? 0.0f : float(FMath::Min(Reach, 1.0));
		Vertex.MeshUV = Raw.UVs[Index];
		OutPiece.Vertices.Add(Vertex);
	}
	OutPiece.Triangles = MoveTemp(Raw.Triangles);
	return !OutPiece.Triangles.IsEmpty();
}

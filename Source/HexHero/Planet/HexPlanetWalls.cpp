// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetWalls.h"
#include "Async/ParallelFor.h"
#include "HexPlanetConfig.h"
#include "HexSphereGrid.h"
#include "Math/RandomStream.h"

namespace
{
	using namespace HexPlanetWalls;

	/** The rows of the profile of a wall: up its outer face from deep in the chasm, over the crest, down its inner face, round its foot and over
	 *  the floor before it to under the floor */
	enum ERow : int32
	{
		OuterDeep,
		OuterFloor,
		OuterLow,
		OuterHigh,
		CrestOuter,
		CrestTop,
		CrestInner,
		InnerLip,
		InnerUnder,
		InnerFirst,
		InnerLast = InnerFirst + 7,
		FootFirst,
		FootLast = FootFirst + 4,
		FloorStart,
		FloorEnd,
		Buried,
		Rows
	};

	/** The share of the height of a wall at the lip of its overhang, where the stalactites hang from */
	constexpr double LipShare = 0.87;

	/** The lowest a crest may dip, a share of the height of its wall, so the rows of the profile keep their order (GetRowHeight) */
	constexpr double MinCrest = 0.7;

	/** The offsets of the noises, from the seed, so the walls of another planet differ; small, as a large offset loses the precision of the noise */
	struct FOffsets
	{
		FVector Bulge, Detail, Height, Crest;
		uint32 Ribs = 0;
		uint32 OuterRibs = 0;

		explicit FOffsets(int32 Seed)
		{
			FRandomStream Random(Seed * 7919 + 17);
			const auto Next = [&Random]() { return FVector(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0)); };
			Bulge = Next();
			Detail = Next();
			Height = Next();
			Crest = Next();
			Ribs = uint32(Random.GetUnsignedInt());
			OuterRibs = uint32(Random.GetUnsignedInt());
		}
	};

	double Noise(const FVector3d& Point, double Scale, const FVector& Offset)
	{
		return double(FMath::PerlinNoise3D(FVector(Point / FMath::Max(Scale, 1.0)) + Offset));
	}

	/** The offset of the noise of the regions of a style, from the seed and the place of the style */
	FVector GetStyleOffset(int32 Seed, int32 Style)
	{
		FRandomStream Random(Seed * 104729 + Style * 7907 + 3);
		return FVector(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
	}

	uint32 Mix(uint32 Hash)
	{
		Hash ^= Hash >> 16;
		Hash *= 0x7feb352du;
		Hash ^= Hash >> 15;
		Hash *= 0x846ca68bu;
		Hash ^= Hash >> 16;
		return Hash;
	}

	double ToShare(uint32 Hash)
	{
		return double(Hash >> 8) / 16777216.0;
	}

	/** The cellular pattern of the stalactites at a point of the floor: how far it lies from the edge between the two nearest of points scattered
	 *  one to a cell of Size (0 on the edge, where a flute runs down the face, to 1 well inside), and a hash of the nearest, so every stalactite has
	 *  values of its own; of the place alone, so an edit of the maze leaves the stalactites of the walls it does not reach as they were */
	void GetCells(const FVector3d& Point, double Size, uint32 Seed, double& OutEdge, uint32& OutHash)
	{
		const FVector3d Scaled = Point / FMath::Max(Size, 1.0);
		const FIntVector Base(FMath::FloorToInt32(Scaled.X), FMath::FloorToInt32(Scaled.Y), FMath::FloorToInt32(Scaled.Z));
		double Nearest = TNumericLimits<double>::Max();
		double Second = TNumericLimits<double>::Max();
		uint32 NearestHash = 0;
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 X = -1; X <= 1; ++X)
				{
					const FIntVector Cell = Base + FIntVector(X, Y, Z);
					const uint32 Hash = Mix((uint32(Cell.X) * 73856093u) ^ (uint32(Cell.Y) * 19349663u) ^ (uint32(Cell.Z) * 83492791u) ^ Seed);
					const FVector3d Feature(Cell.X + ToShare(Hash), Cell.Y + ToShare(Mix(Hash + 1u)), Cell.Z + ToShare(Mix(Hash + 2u)));
					const double Distance = FVector3d::Distance(Feature, Scaled);
					if (Distance < Nearest)
					{
						Second = Nearest;
						Nearest = Distance;
						NearestHash = Hash;
					}
					else if (Distance < Second)
					{
						Second = Distance;
					}
				}
			}
		}
		OutEdge = FMath::Clamp((Second - Nearest) * 1.6, 0.0, 1.0);
		OutHash = NearestHash;
	}

	/** What a sample of a wall takes from the noises once, for all its rows */
	struct FWallFrame
	{
		/** Height of the wall, without the rise and fall of its crest */
		double Base = 0.0;
		double Crest = 0.0;
		/** The cellular pattern of the stalactites of the inner face (GetCells) and of the ribs of the outer face */
		double Edge = 0.0;
		double OuterEdge = 0.0;
		/** The share of the height where the stalactite here ends, 0 for a column down to the floor, and how far it stands out, a share of
		 *  RibDepth */
		double Tip = 1.0;
		double Strength = 0.0;

		/** The values of the walls here, by the styles that hold where the sample lies (FParams::Styles), and how much its stone is flowstone
		 *  and how much it shows its joints, for the material */
		double Thickness = 0.0;
		double Lean = 0.0;
		double Overhang = 0.0;
		double Bulge = 1.0;
		double RibDepth = 0.0;
		double FluteDepth = 0.0;
		double Flowstone = 0.0;
		double Fracture = 0.0;
	};

	/** The factors of the styles where a point lies, each weighed by its weight and by a noise of its regions (FParams::Styles); all 1 without
	 *  styles */
	FStyle GetStyle(const FVector3d& Point, const FParams& Params)
	{
		FStyle Mixed;
		if (Params.Styles.IsEmpty())
		{
			return Mixed;
		}
		double Total = 0.0;
		TArray<double, TInlineAllocator<8>> Weights;
		for (int32 Index = 0; Index < Params.Styles.Num(); ++Index)
		{
			const double Weight = Params.Styles[Index].Weight * FMath::Exp(Params.StyleSharpness * Noise(Point, Params.StyleScale, GetStyleOffset(Params.Seed, Index)));
			Weights.Add(Weight);
			Total += Weight;
		}
		if (Total <= 0.0)
		{
			return Mixed;
		}
		FStyle Sum;
		Sum.Weight = Sum.Height = Sum.Thickness = Sum.Lean = Sum.Overhang = Sum.Bulge = Sum.CrestJag = Sum.CrestNotch = Sum.RibSize = Sum.RibDepth = Sum.RibTip
			= Sum.ColumnShare = Sum.FluteDepth = Sum.Flowstone = Sum.Fracture = 0.0;
		for (int32 Index = 0; Index < Params.Styles.Num(); ++Index)
		{
			const FStyle& Style = Params.Styles[Index];
			const double Share = Weights[Index] / Total;
			Sum.Height += Style.Height * Share;
			Sum.Thickness += Style.Thickness * Share;
			Sum.Lean += Style.Lean * Share;
			Sum.Overhang += Style.Overhang * Share;
			Sum.Bulge += Style.Bulge * Share;
			Sum.CrestJag += Style.CrestJag * Share;
			Sum.CrestNotch += Style.CrestNotch * Share;
			Sum.RibSize += Style.RibSize * Share;
			Sum.RibDepth += Style.RibDepth * Share;
			Sum.RibTip += Style.RibTip * Share;
			Sum.ColumnShare += Style.ColumnShare * Share;
			Sum.FluteDepth += Style.FluteDepth * Share;
			Sum.Flowstone += Style.Flowstone * Share;
			Sum.Fracture += Style.Fracture * Share;
		}
		Sum.Weight = 1.0;
		return Sum;
	}

	FWallFrame GetFrame(const FSample& Sample, const FParams& Params, const FOffsets& Offsets)
	{
		FWallFrame Frame;
		const FStyle Style = GetStyle(Sample.Position, Params);
		Frame.Base = Params.Height * Style.Height * FMath::Max(1.0 + Params.HeightNoise * Noise(Sample.Position, Params.HeightNoiseScale, Offsets.Height), 0.2);
		uint32 Hash = 0;
		GetCells(Sample.Position, Params.RibSize * Style.RibSize, Offsets.Ribs, Frame.Edge, Hash);
		const double TipMin = FMath::Clamp(Params.RibTipMin * Style.RibTip, 0.05, 0.82);
		const double TipMax = FMath::Clamp(Params.RibTipMax * Style.RibTip, TipMin, 0.82);
		Frame.Tip = ToShare(Mix(Hash + 11u)) < Params.ColumnShare * Style.ColumnShare ? 0.0 : FMath::Lerp(TipMin, TipMax, ToShare(Mix(Hash + 12u)));
		Frame.Strength = 0.55 + 0.45 * ToShare(Mix(Hash + 13u));
		// the crest rises or falls over each stalactite by a hash of it and dips between them, so its line is ragged
		const double Rise = ToShare(Mix(Hash + 14u)) * 2.0 - 1.0;
		const double Rib = FMath::Sqrt(Frame.Edge);
		Frame.Crest = Frame.Base * FMath::Max(1.0 + Params.CrestNoise * Noise(Sample.Position, Params.BulgeScale * 0.7, Offsets.Crest)
			+ Params.CrestJag * Style.CrestJag * Rise * Rib - Params.CrestNotch * Style.CrestNotch * (1.0 - Rib), MinCrest);
		uint32 OuterHash = 0;
		GetCells(Sample.Position, Params.RibSize * Style.RibSize * 1.3, Offsets.OuterRibs, Frame.OuterEdge, OuterHash);
		Frame.Thickness = Params.Thickness * Style.Thickness;
		Frame.Lean = Params.Lean * Style.Lean;
		Frame.Overhang = Params.Overhang * Style.Overhang;
		Frame.Bulge = Style.Bulge;
		Frame.RibDepth = Params.RibDepth * Style.RibDepth;
		Frame.FluteDepth = Params.FluteDepth * Style.FluteDepth;
		Frame.Flowstone = FMath::Clamp(Style.Flowstone, 0.0, 1.0);
		Frame.Fracture = FMath::Clamp(Style.Fracture, 0.0, 1.0);
		return Frame;
	}

	/** The height of a row at a share of the wall: the low rows by the height of the wall, the high ones more and more by its crest, so its
	 *  crest rises and falls without shearing the face below, and the rows keep their order as long as the crest is above MinCrest of the height */
	double GetRowHeight(const FWallFrame& Frame, double Share)
	{
		return Share * (Frame.Base + (Frame.Crest - Frame.Base) * Share * Share);
	}

	/** How far the stalactite of a sample stands out at a share of the height of its wall, a share of RibDepth: from its tip, where it comes to a
	 *  point, it widens up to the lip of the overhang it hangs from, so it narrows towards its tip both across the wall and along it; a column
	 *  stands out all the way */
	double GetStalactite(const FWallFrame& Frame, double Share)
	{
		const double Taper = Frame.Tip <= 0.0 ? 1.0 : FMath::Clamp((Share - Frame.Tip) / FMath::Max(LipShare - Frame.Tip, 0.05), 0.0, 1.0);
		const double Narrow = (1.0 - Taper) * 0.8;
		const double Across = FMath::Clamp((Frame.Edge - Narrow) / FMath::Max(1.0 - Narrow, 1e-3), 0.0, 1.0);
		return FMath::Sqrt(Across) * FMath::Pow(Taper, 0.6) * Frame.Strength;
	}

	/** A row of a sample: across the line (onto the passage) and above the floor, from the line itself or, by Smooth, from the smoothed line, with
	 *  how deep in a hollow it lies, how much of a stalactite stands out there and how much it is floor */
	struct FLocal
	{
		double Across = 0.0;
		double Up = 0.0;
		double Smooth = 0.0;
		double Cavity = 0.0;
		double Rib = 0.0;
		double Floor = 0.0;
	};

	FLocal GetLocal(const FSample& Sample, const FWallFrame& Frame, int32 Row, const FParams& Params, const FOffsets& Offsets)
	{
		const double Height = FMath::Max(Frame.Base, 1.0);
		const double Fillet = Params.Fillet;
		const double Foot = Params.FootReach;
		// the inner face rises from the top of the rounded foot, leaning back as it goes up
		const double FaceBase = Foot - Fillet;
		const auto Face = [&](double Up) { return FaceBase - Frame.Lean * Up; };
		const double Thickness = Frame.Thickness;
		const double Flare = Params.OuterFlare;
		const auto At = [&Frame](double Share) { return GetRowHeight(Frame, Share); };

		FLocal Local;
		// the share of the height of the wall the row stands at, by which the stalactites taper and the flutes fade
		double Share = 0.0;
		enum class EKind : uint8 { Outer, Crest, Inner, Foot, Floor } Kind = EKind::Inner;
		switch (Row)
		{
		case OuterDeep: Local = {FaceBase - Thickness - Flare, -Params.OuterDepth, 1.0}; Kind = EKind::Outer; break;
		case OuterFloor: Local = {FaceBase - Thickness - Flare * 0.25, 0.0, 1.0}; Kind = EKind::Outer; break;
		case OuterLow: Share = 0.38; Local = {FaceBase - Thickness * 0.95, At(Share), 1.0}; Kind = EKind::Outer; break;
		case OuterHigh: Share = 0.7; Local = {FaceBase - Thickness * 0.82, At(Share), 1.0}; Kind = EKind::Outer; break;
		// the top of the crest lies between its shoulders (GetRing), its offset only for its noise
		case CrestOuter: Share = 0.91; Local = {FaceBase - Thickness * 0.66, At(Share), 1.0}; Kind = EKind::Crest; break;
		case CrestTop: Share = 1.0; Local = {FaceBase - Thickness * 0.42 - Frame.Lean * Frame.Crest, At(Share), 0.5}; Kind = EKind::Crest; break;
		case CrestInner: Share = 0.96; Local = {Face(At(Share)) + Frame.Overhang * 0.45, At(Share), 0.0}; Kind = EKind::Crest; break;
		case InnerLip: Share = LipShare; Local = {Face(At(Share)) + Frame.Overhang, At(Share), 0.0}; break;
		case InnerUnder: Share = 0.8; Local = {Face(At(Share)) + Frame.Overhang * 0.3, At(Share), 0.0}; Local.Cavity = 0.45; break;
		case FloorStart: Local = {Foot, 0.12, 0.0}; Kind = EKind::Floor; break;
		case FloorEnd: Local = {FMath::Max(Foot, Sample.Gap) + 2.0, 0.12, 0.0}; Kind = EKind::Floor; break;
		case Buried: Local = {FMath::Max(Foot, Sample.Gap) + 6.0, -2.5, 0.0}; Kind = EKind::Floor; break;
		default:
			if (Row >= InnerFirst && Row <= InnerLast)
			{
				Share = FMath::Lerp(0.73, FMath::Min(Fillet * 1.1 / Height, 0.6), double(Row - InnerFirst) / (InnerLast - InnerFirst));
				Local = {Face(At(Share)), At(Share), 0.0};
				Local.Cavity = Row == InnerFirst ? 0.2 : 0.0;
			}
			else
			{
				// the foot is a quarter of a circle of Fillet from the inner face, upright, to the floor, flat, every 15 degrees from 195 to 255
				const int32 Step = Row - FootFirst;
				const double Angle = FMath::DegreesToRadians(195.0 + 15.0 * Step);
				Local = {Foot + Fillet * FMath::Cos(Angle), Fillet * (1.0 + FMath::Sin(Angle)), 0.0};
				const double Floors[] = {0.08, 0.25, 0.5, 0.75, 0.92};
				Local.Floor = Floors[FMath::Clamp(Step, 0, 4)];
				Local.Cavity = 0.18 - 0.04 * Step;
				Kind = EKind::Foot;
			}
			break;
		}

		// lumps of a 3D noise where the row lies, so the wall is no extruded profile
		const FVector3d Place = Sample.Position + Sample.Inward * Local.Across + Sample.Up * Local.Up;
		const double Bulge = Params.Bulge * Frame.Bulge * Noise(Place, Params.BulgeScale, Offsets.Bulge) + Params.Detail * Noise(Place, Params.DetailScale, Offsets.Detail);
		const double OuterRib = Frame.RibDepth * 0.6 * FMath::Sqrt(Frame.OuterEdge);
		switch (Kind)
		{
		case EKind::Outer:
			// ribs down the outer face too, of a pattern of their own, fading deep in the chasm
			Local.Across -= (Row == OuterDeep ? 0.5 : 0.8) * Bulge + OuterRib * (Row == OuterDeep ? 0.3 : Row == OuterFloor ? 0.5 : 1.0);
			break;
		case EKind::Crest:
			Local.Up += Bulge * 0.3;
			if (Row == CrestTop)
			{
				// a ragged top of sharp knobs rather than a rounded ridge
				const double Ridge = 1.0 - FMath::Abs(Noise(Sample.Position, Params.DetailScale, Offsets.Crest));
				Local.Up += Frame.Base * Params.CrestNoise * (Ridge * Ridge - 0.5);
			}
			Local.Across += Bulge * 0.3 + (Row == CrestOuter ? -OuterRib * 0.5 : Row == CrestInner ? Frame.RibDepth * 0.5 * GetStalactite(Frame, Share) : 0.0);
			break;
		case EKind::Inner:
		{
			// the stalactites hanging from the overhang, and flutes scooped down the face between them, fading out above the foot and below the
			// crest
			const double Stalactite = GetStalactite(Frame, Share);
			const double Flute = 1.0 - FMath::SmoothStep(0.0, FMath::Max(Params.FluteWidth, 1e-3), Frame.Edge);
			const double Fade = FMath::SmoothStep(Fillet, Fillet * 2.0 + 1.0, Local.Up) * (1.0 - FMath::SmoothStep(0.82, 0.95, Share));
			Local.Across += Bulge + Frame.RibDepth * Stalactite - Frame.FluteDepth * Flute * Fade;
			// between the stalactites, from the lowest of their tips up to the lip, the face lies in their shade
			const double Zone = FMath::SmoothStep(Params.RibTipMin - 0.06, Params.RibTipMin + 0.06, Share) * (1.0 - FMath::SmoothStep(LipShare - 0.03, LipShare, Share));
			Local.Rib = FMath::Clamp(Stalactite, 0.0, 1.0);
			Local.Cavity = FMath::Clamp(Local.Cavity + Flute * Fade + 0.35 * (1.0 - Local.Rib) * Zone, 0.0, 1.0);
			break;
		}
		case EKind::Foot:
		{
			const double Flute = 1.0 - FMath::SmoothStep(0.0, FMath::Max(Params.FluteWidth, 1e-3), Frame.Edge);
			Local.Across += Bulge * 0.35 - Frame.FluteDepth * Flute * (Row == FootFirst ? 0.4 : 0.0);
			break;
		}
		case EKind::Floor:
			Local.Floor = 1.0;
			break;
		}
		return Local;
	}

	/** Eases an offset towards a reach: unchanged up to most of it, then bending smoothly towards it */
	double Ease(double Offset, double Reach)
	{
		if (Reach >= 1e9)
		{
			return Offset;
		}
		const double Magnitude = FMath::Abs(Offset);
		const double Knee = Reach * 0.6;
		if (Magnitude <= Knee)
		{
			return Offset;
		}
		return FMath::Sign(Offset) * (Knee + (Reach - Knee) * (1.0 - FMath::Exp(-(Magnitude - Knee) / (Reach - Knee))));
	}

	/** Where a row of a sample lies in the world, its offset eased towards the reach of the turn on its side of the line it stands from */
	FVector3d ToWorld(const FSample& Sample, const FLocal& Local, const FParams& Params)
	{
		const bool bSmooth = Local.Smooth >= 0.5;
		const double Reach = Local.Across > 0.0 ? (bSmooth ? Sample.SmoothInnerReach : Sample.InnerReach) : (bSmooth ? Sample.SmoothOuterReach : Sample.OuterReach);
		const FVector3d Base = FMath::Lerp(Sample.Position, Sample.OuterPosition, Local.Smooth);
		const FVector3d Inward = FMath::Lerp(Sample.Inward, Sample.OuterInward, Local.Smooth).GetSafeNormal();
		return (Base + Inward * Ease(Local.Across, Reach)).GetSafeNormal() * (Params.FloorRadius + Local.Up);
	}

	/** The way out of the wall at a row, by which a normal is turned */
	FVector3d GetRowOutside(const FSample& Sample, int32 Row)
	{
		if (Row <= OuterHigh)
		{
			return -Sample.Inward;
		}
		switch (Row)
		{
		case CrestOuter: return (Sample.Up - Sample.Inward).GetSafeNormal();
		case CrestTop: return Sample.Up;
		case CrestInner: return (Sample.Up + Sample.Inward).GetSafeNormal();
		default: break;
		}
		if (Row <= InnerLast)
		{
			return Sample.Inward;
		}
		return Row <= FootLast ? (Sample.Inward + Sample.Up).GetSafeNormal() : Sample.Up;
	}

	/** Where the line across the floor through a sample, onto its passage, first reaches the floor of a passage: the nearest point onto the
	 *  passage where it goes into the polygon of a cell that CanHold within two rings of the cell of the sample, 0 when the sample lies on such
	 *  a polygon */
	double FindGap(const FHexSphereGrid& Grid, TFunctionRef<bool(int32)> CanHold, const FSample& Sample, double Radius)
	{
		const TArray<FHexSphereCell>& Cells = Grid.GetCells();
		const TArray<FVector3d>& Corners = Grid.GetCorners();
		// how far onto the passage the line goes into the polygon of a cell, 0 when the sample lies on it, and a large number when it misses it
		const auto Cross = [&](int32 Cell)
		{
			const auto& Ring = Cells[Cell].Corners;
			double Low = TNumericLimits<double>::Max();
			double High = -TNumericLimits<double>::Max();
			for (int32 Side = 0; Side < Ring.Num(); ++Side)
			{
				const FVector3d A = Corners[Ring[Side]] * Radius - Sample.Position;
				const FVector3d B = Corners[Ring[(Side + 1) % Ring.Num()]] * Radius - Sample.Position;
				const double AlongA = FVector3d::DotProduct(A, Sample.Along);
				const double AlongB = FVector3d::DotProduct(B, Sample.Along);
				if ((AlongA < 0.0) == (AlongB < 0.0))
				{
					continue;
				}
				const double AcrossA = FVector3d::DotProduct(A, Sample.Inward);
				const double AcrossB = FVector3d::DotProduct(B, Sample.Inward);
				const double Across = AcrossA + (AcrossB - AcrossA) * (-AlongA / (AlongB - AlongA));
				Low = FMath::Min(Low, Across);
				High = FMath::Max(High, Across);
			}
			return High < Low || High < 0.0 ? TNumericLimits<double>::Max() : FMath::Max(Low, 0.0);
		};
		// most samples lie on the floor of their own passage or on its side
		double Entry = CanHold(Sample.Cell) ? Cross(Sample.Cell) : TNumericLimits<double>::Max();
		if (Entry <= 0.0)
		{
			return 0.0;
		}
		TArray<int32, TInlineAllocator<32>> Near;
		Near.Add(Sample.Cell);
		int32 Begin = 0;
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			const int32 End = Near.Num();
			for (int32 Index = Begin; Index < End; ++Index)
			{
				for (const int32 Neighbor : Cells[Near[Index]].Neighbors)
				{
					Near.AddUnique(Neighbor);
				}
			}
			Begin = End;
		}
		for (int32 Index = 1; Index < Near.Num() && Entry > 0.0; ++Index)
		{
			if (CanHold(Near[Index]))
			{
				Entry = FMath::Min(Entry, Cross(Near[Index]));
			}
		}
		return Entry < TNumericLimits<double>::Max() ? Entry : 0.0;
	}

	/** The turn of a closed line, the line itself or the smoothed one, at each sample, onto the passage when positive, averaged over a few
	 *  samples each way, into the reach a wall standing from that line may have on each side there */
	void SetReaches(TArray<FSample>& Line, bool bSmooth)
	{
		const int32 Count = Line.Num();
		const auto Position = [&Line, bSmooth, Count](int32 Index) -> const FVector3d&
		{
			const FSample& Sample = Line[(Index % Count + Count) % Count];
			return bSmooth ? Sample.OuterPosition : Sample.Position;
		};
		TArray<FVector3d> Alongs;
		Alongs.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Alongs[Index] = (Position(Index + 1) - Position(Index - 1)).GetSafeNormal();
		}
		TArray<double> Turns;
		Turns.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Span = FMath::Max(FVector3d::Distance(Position(Index + 1), Position(Index - 1)), 1e-6);
			const FVector3d& Inward = bSmooth ? Line[Index].OuterInward : Line[Index].Inward;
			Turns[Index] = FVector3d::DotProduct(Alongs[(Index + 1) % Count] - Alongs[(Index + Count - 1) % Count], Inward) / Span;
		}
		// the sharpest turn each way within a couple of samples, as an offset folds where it reaches past the radius of the turn there, which an
		// average over more samples misses at a rounded corner
		constexpr int32 TurnWindow = 2;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			double Inner = 0.0;
			double Outer = 0.0;
			for (int32 Offset = -TurnWindow; Offset <= TurnWindow; ++Offset)
			{
				const double Turn = Turns[(Index + Offset + Count * 2) % Count];
				Inner = FMath::Max(Inner, Turn);
				Outer = FMath::Max(Outer, -Turn);
			}
			FSample& Sample = Line[Index];
			if (Inner > 1e-6)
			{
				(bSmooth ? Sample.SmoothInnerReach : Sample.InnerReach) = 0.85 / Inner;
			}
			if (Outer > 1e-6)
			{
				(bSmooth ? Sample.SmoothOuterReach : Sample.OuterReach) = 0.85 / Outer;
			}
		}
	}

	/** The points of every row of a sample: each from its line, but the top of the crest, which lies between its inner shoulder, from the line,
	 *  and its outer shoulder, from the smoothed line, as an offset of a line between the two would fold where they part at a corner */
	void GetRing(const FSample& Sample, const FWallFrame& Frame, const FParams& Params, const FOffsets& Offsets, FLocal (&OutLocals)[Rows], FVector3d (&OutPoints)[Rows])
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			OutLocals[Row] = GetLocal(Sample, Frame, Row, Params, Offsets);
			if (Row != CrestTop)
			{
				OutPoints[Row] = ToWorld(Sample, OutLocals[Row], Params);
			}
		}
		// never below its shoulders, so the top does not fold in where its knobs dip
		OutLocals[CrestTop].Up = FMath::Max(OutLocals[CrestTop].Up, FMath::Max(OutLocals[CrestInner].Up, OutLocals[CrestOuter].Up) + 1.0);
		OutPoints[CrestTop] = FMath::Lerp(OutPoints[CrestInner], OutPoints[CrestOuter], 0.45).GetSafeNormal() * (Params.FloorRadius + OutLocals[CrestTop].Up);
	}
}

HexPlanetWalls::FParams HexPlanetWalls::FParams::FromConfig(const UHexPlanetConfig& Config, double FloorRadius, int32 Seed)
{
	FParams Params;
	Params.FloorRadius = FloorRadius;
	Params.Seed = Seed;
	Params.Step = FMath::Max(Config.WallStep, 1.0f);
	Params.CornerCut = FMath::Clamp(Config.WallCornerCut, 0.0f, 1.0f);
	Params.CornerRound = FMath::Max(Config.WallCornerRound, 0.0f);
	Params.OuterSmooth = FMath::Max(Config.WallOuterSmooth, 0.0f);
	Params.Height = Config.WallHeight;
	Params.HeightNoise = Config.WallHeightNoise;
	Params.HeightNoiseScale = Config.WallHeightNoiseScale;
	Params.Thickness = Config.WallThickness;
	Params.FootReach = Config.WallFootReach;
	Params.Fillet = Config.WallFillet;
	Params.Lean = Config.WallLean;
	Params.Overhang = Config.WallOverhang;
	Params.OuterDepth = Config.WallOuterDepth;
	Params.OuterFlare = Config.WallOuterFlare;
	Params.Bulge = Config.WallBulge;
	Params.BulgeScale = Config.WallBulgeScale;
	Params.Detail = Config.WallDetail;
	Params.DetailScale = Config.WallDetailScale;
	Params.CrestNoise = Config.WallCrestNoise;
	Params.CrestJag = Config.WallCrestJag;
	Params.CrestNotch = Config.WallCrestNotch;
	Params.RibSize = FMath::Max(Config.WallRibSize, 4.0f);
	Params.RibDepth = Config.WallRibDepth;
	Params.RibTipMin = Config.WallRibTipMin;
	Params.RibTipMax = Config.WallRibTipMax;
	Params.ColumnShare = Config.WallColumnShare;
	Params.FluteDepth = Config.WallFluteDepth;
	Params.FluteWidth = Config.WallFluteWidth;
	Params.StyleScale = FMath::Max(Config.WallStyleScale, 10.0f);
	Params.StyleSharpness = FMath::Max(Config.WallStyleSharpness, 0.0f);
	for (const FHexWallStyle& Style : Config.WallStyles)
	{
		FStyle& Each = Params.Styles.AddDefaulted_GetRef();
		Each.Weight = FMath::Max(Style.Weight, 0.0f);
		Each.Height = Style.Height;
		Each.Thickness = Style.Thickness;
		Each.Lean = Style.Lean;
		Each.Overhang = Style.Overhang;
		Each.Bulge = Style.Bulge;
		Each.CrestJag = Style.CrestJag;
		Each.CrestNotch = Style.CrestNotch;
		Each.RibSize = Style.RibSize;
		Each.RibDepth = Style.RibDepth;
		Each.RibTip = Style.RibTip;
		Each.ColumnShare = Style.ColumnShare;
		Each.FluteDepth = Style.FluteDepth;
		Each.Flowstone = Style.Flowstone;
		Each.Fracture = Style.Fracture;
	}
	return Params;
}

double HexPlanetWalls::GetHeight(const FSample& Sample, const FParams& Params)
{
	const FOffsets Offsets(Params.Seed);
	return Params.Height * GetStyle(Sample.Position, Params).Height * FMath::Max(1.0 + Params.HeightNoise * Noise(Sample.Position, Params.HeightNoiseScale, Offsets.Height), 0.2);
}

double HexPlanetWalls::GetFootReach(const FParams& Params)
{
	return Params.FootReach;
}

void HexPlanetWalls::Build(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TFunctionRef<bool(int32)> CanHold, const FParams& Params, FWalls& Out)
{
	Out = FWalls();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const TArray<FVector3d>& Corners = Grid.GetCorners();
	Out.ChunkSamples.SetNum(Grid.GetNumChunks());
	Out.CellSamples.SetNum(Cells.Num());
	if (Levels.Num() != Cells.Num() || Params.FloorRadius <= 0.0)
	{
		return;
	}
	const double Radius = Params.FloorRadius;

	// the sides of the passages towards a void, each from corner K - 1 to corner K of its passage, which lies on its left seen from outside, so
	// along the edge of the passages each side ends where the next begins
	struct FEdge
	{
		int32 Cell;
		int32 From;
		int32 To;
	};
	TArray<FEdge> Edges;
	TMap<int32, int32> EdgeFrom;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (!CanHold(Cell))
		{
			continue;
		}
		const FHexSphereCell& Each = Cells[Cell];
		const int32 Count = Each.Neighbors.Num();
		for (int32 Side = 0; Side < Count; ++Side)
		{
			if (int32(Levels[Each.Neighbors[Side]]) <= int32(Levels[Cell]) - Params.MinDrop)
			{
				const int32 Index = Edges.Add({Cell, Each.Corners[(Side + Count - 1) % Count], Each.Corners[Side]});
				EdgeFrom.FindOrAdd(Edges[Index].From, Index);
			}
		}
	}

	TBitArray<> Used(false, Edges.Num());
	for (int32 First = 0; First < Edges.Num(); ++First)
	{
		if (Used[First])
		{
			continue;
		}
		TArray<int32> Loop;
		for (int32 Edge = First; Edge != INDEX_NONE && !Used[Edge];)
		{
			Used[Edge] = true;
			Loop.Add(Edge);
			const int32* Next = EdgeFrom.Find(Edges[Edge].To);
			Edge = Next ? *Next : INDEX_NONE;
		}
		const int32 NumEdges = Loop.Num();
		if (NumEdges < 3)
		{
			continue;
		}
		const auto Wrap = [NumEdges](int32 Index) { return (Index % NumEdges + NumEdges) % NumEdges; };
		const auto CornerAt = [&](int32 Index) { return Corners[Edges[Loop[Wrap(Index)]].From] * Radius; };
		const auto CellAt = [&](int32 Index) { return Edges[Loop[Wrap(Index)]].Cell; };
		// where the sides of two passages meet, the corner of the void between them pokes into the floor; where two sides of one passage meet, it
		// is the tip of that hex
		const auto IsVoidCorner = [&](int32 Index) { return CellAt(Index - 1) != CellAt(Index); };

		// the sides, keeping the tips of the hexes, each corner of a void cut across from CornerCut of each of its sides, or of half a side that
		// ends at another such corner, as if the floor went on into the void, so the walls still show the hexes they stand on
		TArray<FVector3d> Points;
		TArray<int32> PointCells;
		for (int32 Index = 0; Index < NumEdges; ++Index)
		{
			const FVector3d Here = CornerAt(Index);
			if (Params.CornerCut <= 0.0 || !IsVoidCorner(Index))
			{
				Points.Add(Here);
				PointCells.Add(CellAt(Index));
				continue;
			}
			const double Back = Params.CornerCut * (IsVoidCorner(Index - 1) ? 0.5 : 1.0);
			const double Ahead = Params.CornerCut * (IsVoidCorner(Index + 1) ? 0.5 : 1.0);
			Points.Add(FMath::Lerp(Here, CornerAt(Index - 1), Back));
			PointCells.Add(CellAt(Index - 1));
			Points.Add(FMath::Lerp(Here, CornerAt(Index + 1), Ahead));
			PointCells.Add(CellAt(Index));
		}
		// points that fall together, as where a full cut reaches the tip of a hex, are one
		for (int32 Index = Points.Num() - 1; Index >= 0 && Points.Num() > 3; --Index)
		{
			if (FVector3d::DistSquared(Points[Index], Points[(Index + 1) % Points.Num()]) < 1e-4)
			{
				Points.RemoveAt(Index);
				PointCells.RemoveAt(Index);
			}
		}

		// every corner rounded by a quadratic curve within CornerRound of it
		TArray<FVector3d> Rounded;
		TArray<int32> RoundedCells;
		const int32 NumPoints = Points.Num();
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FVector3d& Here = Points[Index];
			const FVector3d& Before = Points[(Index + NumPoints - 1) % NumPoints];
			const FVector3d& After = Points[(Index + 1) % NumPoints];
			const double In = FVector3d::Distance(Before, Here);
			const double Onward = FVector3d::Distance(Here, After);
			const FVector3d InWay = (Here - Before) / FMath::Max(In, 1e-9);
			const FVector3d OnWay = (After - Here) / FMath::Max(Onward, 1e-9);
			const double Turn = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(InWay, OnWay), -1.0, 1.0));
			const double Tangent = FMath::Min(Params.CornerRound * FMath::Tan(FMath::Min(Turn, 2.6) * 0.5), 0.45 * FMath::Min(In, Onward));
			if (Params.CornerRound <= 0.0 || Turn < 0.02 || Tangent < 0.05)
			{
				Rounded.Add(Here);
				RoundedCells.Add(PointCells[Index]);
				continue;
			}
			const FVector3d Start = Here - InWay * Tangent;
			const FVector3d End = Here + OnWay * Tangent;
			const int32 Steps = FMath::Clamp(FMath::CeilToInt32(Tangent * 4.0 / FMath::Max(Params.Step, 1.0)), 2, 16);
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double U = double(Step) / Steps;
				Rounded.Add(Start * FMath::Square(1.0 - U) + Here * (2.0 * U * (1.0 - U)) + End * FMath::Square(U));
				RoundedCells.Add(PointCells[Index]);
			}
		}

		// samples evenly spaced along the closed loop on the floor
		const int32 NumRounded = Rounded.Num();
		TArray<double> Lengths;
		Lengths.SetNum(NumRounded + 1);
		Lengths[0] = 0.0;
		for (int32 Index = 0; Index < NumRounded; ++Index)
		{
			Lengths[Index + 1] = Lengths[Index] + FVector3d::Distance(Rounded[Index], Rounded[(Index + 1) % NumRounded]);
		}
		const double Total = Lengths[NumRounded];
		const int32 Count = FMath::Max(8, FMath::RoundToInt32(Total / Params.Step));
		const double Spacing = Total / Count;
		TArray<FSample>& Line = Out.Lines.AddDefaulted_GetRef();
		Line.SetNum(Count);
		int32 Segment = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double S = Index * Spacing;
			while (Segment < NumRounded - 1 && Lengths[Segment + 1] < S)
			{
				++Segment;
			}
			const double Share = (S - Lengths[Segment]) / FMath::Max(Lengths[Segment + 1] - Lengths[Segment], 1e-9);
			FSample& Sample = Line[Index];
			Sample.Up = FMath::Lerp(Rounded[Segment], Rounded[(Segment + 1) % NumRounded], Share).GetSafeNormal();
			Sample.Position = Sample.Up * Radius;
			Sample.S = S;
			Sample.Cell = RoundedCells[Share < 0.5 ? Segment : (Segment + 1) % NumRounded];
		}

		// the line smoothed over OuterSmooth each way, twice, from which the outer face and the crest stand
		TArray<FVector3d> Smooth;
		Smooth.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Smooth[Index] = Line[Index].Position;
		}
		const int32 Half = FMath::Clamp(FMath::RoundToInt32(Params.OuterSmooth / Spacing), 0, (Count - 1) / 2);
		if (Half > 0)
		{
			TArray<FVector3d> Sums;
			Sums.SetNum(Count + 1);
			TArray<FVector3d> Next;
			Next.SetNum(Count);
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				Sums[0] = FVector3d::ZeroVector;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					Sums[Index + 1] = Sums[Index] + Smooth[Index];
				}
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const int32 Low = Index - Half;
					const int32 High = Index + Half;
					const FVector3d Window = Low < 0 ? Sums[Count] - Sums[Low + Count] + Sums[High + 1]
						: High >= Count ? Sums[Count] - Sums[Low] + Sums[High + 1 - Count]
						: Sums[High + 1] - Sums[Low];
					Next[Index] = Window / double(2 * Half + 1);
				}
				Swap(Smooth, Next);
			}
		}

		// along the line and onto the passage, the same way over the whole loop, by which side most of its passages lie on
		int32 Votes = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSample& Sample = Line[Index];
			const FVector3d Ahead = Line[(Index + 1) % Count].Position - Line[(Index + Count - 1) % Count].Position;
			Sample.Along = (Ahead - Sample.Up * FVector3d::DotProduct(Ahead, Sample.Up)).GetSafeNormal();
			Sample.Inward = FVector3d::CrossProduct(Sample.Up, Sample.Along);
			Votes += FVector3d::DotProduct(Sample.Inward, Cells[Sample.Cell].Center * Radius - Sample.Position) > 0.0 ? 1 : -1;

			Sample.OuterPosition = Smooth[Index].GetSafeNormal() * Radius;
			const FVector3d SmoothUp = Smooth[Index].GetSafeNormal();
			const FVector3d SmoothAhead = Smooth[(Index + 1) % Count] - Smooth[(Index + Count - 1) % Count];
			const FVector3d SmoothAlong = SmoothAhead - SmoothUp * FVector3d::DotProduct(SmoothAhead, SmoothUp);
			// a loop small enough to shrink to nearly a point keeps the way of the line itself
			Sample.OuterInward = SmoothAlong.SizeSquared() > 1e-6 ? FVector3d::CrossProduct(SmoothUp, SmoothAlong.GetSafeNormal()) : Sample.Inward;
		}
		if (Votes < 0)
		{
			for (FSample& Sample : Line)
			{
				Sample.Inward = -Sample.Inward;
				Sample.OuterInward = -Sample.OuterInward;
			}
		}

		// inside a turn the wall reaches at most about its radius from the line it stands from, so its faces do not cross over themselves
		SetReaches(Line, false);
		SetReaches(Line, true);

		// where the line cuts across the corner of a void, the floor begins further onto the passage
		ParallelFor(Count, [&](int32 Index) { Line[Index].Gap = FindGap(Grid, CanHold, Line[Index], Radius); }, Count < 512 ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

		const int32 LineIndex = Out.Lines.Num() - 1;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Cell = Line[Index].Cell;
			Out.ChunkSamples[Cells[Cell].Chunk].Add(FIntPoint(LineIndex, Index));
			Out.CellSamples[Cell].Add(FIntPoint(LineIndex, Index));
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				Out.CellSamples[Neighbor].Add(FIntPoint(LineIndex, Index));
			}
		}
	}
}

void HexPlanetWalls::GetInnerFace(const FWalls& Walls, const FIntPoint& At, double Height, const FParams& Params, FVector3d& OutPosition, FVector3d& OutNormal)
{
	// between the rows of the inner face that hold the height, and the way out of it across them and along the line
	const FOffsets Offsets(Params.Seed);
	const TArray<FSample>& Line = Walls.Lines[At.X];
	const FSample& Sample = Walls.Get(At);
	const FSample& Ahead = Line[(At.Y + 1) % Line.Num()];
	const FWallFrame Frame = GetFrame(Sample, Params, Offsets);
	const FWallFrame AheadFrame = GetFrame(Ahead, Params, Offsets);
	int32 Upper = CrestInner;
	FLocal Top = GetLocal(Sample, Frame, Upper, Params, Offsets);
	FLocal Bottom = Top;
	for (int32 Row = CrestInner + 1; Row <= FootLast; ++Row)
	{
		Bottom = GetLocal(Sample, Frame, Row, Params, Offsets);
		if (Bottom.Up <= Height || Row == FootLast)
		{
			Upper = Row - 1;
			break;
		}
		Top = Bottom;
	}
	const double Share = FMath::Clamp((Top.Up - Height) / FMath::Max(Top.Up - Bottom.Up, 1e-3), 0.0, 1.0);
	const FVector3d TopPoint = ToWorld(Sample, Top, Params);
	const FVector3d BottomPoint = ToWorld(Sample, Bottom, Params);
	OutPosition = FMath::Lerp(TopPoint, BottomPoint, Share);
	const FVector3d AheadPoint = FMath::Lerp(ToWorld(Ahead, GetLocal(Ahead, AheadFrame, Upper, Params, Offsets), Params),
		ToWorld(Ahead, GetLocal(Ahead, AheadFrame, Upper + 1, Params, Offsets), Params), Share);
	OutNormal = FVector3d::CrossProduct(TopPoint - BottomPoint, AheadPoint - OutPosition).GetSafeNormal();
	if (FVector3d::DotProduct(OutNormal, Sample.Inward) < 0.0)
	{
		OutNormal = -OutNormal;
	}
}

double HexPlanetWalls::GetDistanceToLine(const FWalls& Walls, int32 Cell, const FVector3d& Point)
{
	double Nearest = TNumericLimits<double>::Max();
	if (!Walls.CellSamples.IsValidIndex(Cell))
	{
		return Nearest;
	}
	bool bOutside = false;
	for (const FIntPoint& At : Walls.CellSamples[Cell])
	{
		const FSample& Sample = Walls.Get(At);
		const FVector3d Offset = Point - Sample.Position;
		const double Distance = (Offset - Sample.Up * FVector3d::DotProduct(Offset, Sample.Up)).Size();
		if (Distance < Nearest)
		{
			Nearest = Distance;
			bOutside = FVector3d::DotProduct(Offset, Sample.Inward) < 0.0;
		}
	}
	// a point beyond the line, between it and the chasm, lies in the wall
	return bOutside ? 0.0 : Nearest;
}

namespace
{
	/** The mesh of the stretches from each of some samples to the next */
	void BuildSamples(const FWalls& Walls, TConstArrayView<FIntPoint> Samples, const FParams& Params, FMesh& Out)
	{
		const FOffsets Offsets(Params.Seed);

		// the rings of the samples, of the next sample of each and, for the normals, of the samples around them
		struct FRing
		{
			FVector3d Points[Rows];
			FLocal Locals[Rows];
			double Height = 0.0;
			FVector2f Stone = FVector2f::ZeroVector;
		};
		TMap<FIntPoint, FRing> Rings;
		const auto FindRing = [&](const FIntPoint& At) -> const FRing&
		{
			if (const FRing* Found = Rings.Find(At))
			{
				return *Found;
			}
			FRing Ring;
			const FSample& Sample = Walls.Get(At);
			const FWallFrame Frame = GetFrame(Sample, Params, Offsets);
			Ring.Height = Frame.Base;
			Ring.Stone = FVector2f(float(Frame.Flowstone), float(Frame.Fracture));
			GetRing(Sample, Frame, Params, Offsets, Ring.Locals, Ring.Points);
			return Rings.Add(At, Ring);
		};
		const auto Offset = [&Walls](const FIntPoint& At, int32 By)
		{
			const int32 Count = Walls.Lines[At.X].Num();
			return FIntPoint(At.X, ((At.Y + By) % Count + Count) % Count);
		};

		TMap<FIntPoint, int32> RingVertices;
		const auto AddRing = [&](const FIntPoint& At) -> int32
		{
			if (const int32* Found = RingVertices.Find(At))
			{
				return *Found;
			}
			const FSample& Sample = Walls.Get(At);
			// copies, as the map may grow and move the rings while the neighbors are built
			const FRing Ring = FindRing(At);
			const FRing Before = FindRing(Offset(At, -1));
			const FRing After = FindRing(Offset(At, 1));
			const int32 First = Out.Positions.Num();
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const FVector3d AcrossLine = After.Points[Row] - Before.Points[Row];
				const FVector3d DownProfile = Ring.Points[FMath::Min(Row + 1, Rows - 1)] - Ring.Points[FMath::Max(Row - 1, 0)];
				FVector3d Normal = FVector3d::CrossProduct(DownProfile, AcrossLine).GetSafeNormal();
				if (FVector3d::DotProduct(Normal, GetRowOutside(Sample, Row)) < 0.0)
				{
					Normal = -Normal;
				}
				const FLocal& Local = Ring.Locals[Row];
				Out.Positions.Add(FVector3f(Ring.Points[Row]));
				Out.Normals.Add(FVector3f(Normal));
				Out.UVs.Add(FVector2f(float(Local.Cavity), float(Local.Up / FMath::Max(Ring.Height, 1.0))));
				Out.UV1s.Add(FVector2f(float(Local.Floor), float(Local.Rib)));
				Out.UV2s.Add(Ring.Stone);
			}
			RingVertices.Add(At, First);
			return First;
		};

		for (const FIntPoint& At : Samples)
		{
			const int32 A = AddRing(At);
			const int32 B = AddRing(Offset(At, 1));
			for (int32 Row = 0; Row < Rows - 1; ++Row)
			{
				const int32 Quad[4] = {A + Row, B + Row, B + Row + 1, A + Row + 1};
				// a triangle shows on the side where (C - A) x (B - A) points, which must be out of the wall
				const FVector3f Outside = Out.Normals[Quad[0]] + Out.Normals[Quad[1]] + Out.Normals[Quad[2]] + Out.Normals[Quad[3]];
				const FVector3f Face = FVector3f::CrossProduct(Out.Positions[Quad[2]] - Out.Positions[Quad[0]], Out.Positions[Quad[1]] - Out.Positions[Quad[0]]);
				if (FVector3f::DotProduct(Face, Outside) >= 0.0f)
				{
					Out.Triangles.Append({Quad[0], Quad[1], Quad[2], Quad[0], Quad[2], Quad[3]});
				}
				else
				{
					Out.Triangles.Append({Quad[0], Quad[2], Quad[1], Quad[0], Quad[3], Quad[2]});
				}
			}
		}
	}
}

void HexPlanetWalls::BuildChunk(const FWalls& Walls, int32 Chunk, const FParams& Params, FMesh& Out)
{
	Out = FMesh();
	if (Walls.ChunkSamples.IsValidIndex(Chunk) && !Walls.ChunkSamples[Chunk].IsEmpty())
	{
		BuildSamples(Walls, Walls.ChunkSamples[Chunk], Params, Out);
	}
}

void HexPlanetWalls::BuildNear(const FWalls& Walls, int32 Cell, const FVector3d& Middle, double Reach, const FParams& Params, FMesh& Out)
{
	Out = FMesh();
	if (!Walls.CellSamples.IsValidIndex(Cell))
	{
		return;
	}
	// the samples listed for a cell are those of the passages it and its neighbors are, so a wall along any of its sides is among them
	TArray<FIntPoint> Samples;
	for (const FIntPoint& At : Walls.CellSamples[Cell])
	{
		if (FVector3d::DistSquared(Walls.Get(At).Position, Middle) <= Reach * Reach)
		{
			Samples.AddUnique(At);
		}
	}
	if (!Samples.IsEmpty())
	{
		BuildSamples(Walls, Samples, Params, Out);
	}
}

uint32 HexPlanetWalls::HashChunk(const FWalls& Walls, int32 Chunk)
{
	if (!Walls.ChunkSamples.IsValidIndex(Chunk))
	{
		return 0;
	}
	const auto Round = [](const FVector3d& Point)
	{
		return FIntVector(FMath::RoundToInt32(Point.X * 16.0), FMath::RoundToInt32(Point.Y * 16.0), FMath::RoundToInt32(Point.Z * 16.0));
	};
	uint32 Hash = GetTypeHash(Walls.ChunkSamples[Chunk].Num());
	for (const FIntPoint& At : Walls.ChunkSamples[Chunk])
	{
		const TArray<FSample>& Line = Walls.Lines[At.X];
		// the normals of a ring take the samples on both sides of it
		for (const int32 Index : {(At.Y + Line.Num() - 1) % Line.Num(), At.Y, (At.Y + 1) % Line.Num(), (At.Y + 2) % Line.Num()})
		{
			const FSample& Sample = Line[Index];
			Hash = HashCombine(Hash, GetTypeHash(Round(Sample.Position)));
			Hash = HashCombine(Hash, GetTypeHash(Round(Sample.OuterPosition)));
			Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt32(Sample.Gap * 16.0)));
			for (const double Reach : {Sample.InnerReach, Sample.OuterReach, Sample.SmoothInnerReach, Sample.SmoothOuterReach})
			{
				Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt32(FMath::Min(Reach, 1e6) * 4.0)));
			}
		}
	}
	return Hash;
}

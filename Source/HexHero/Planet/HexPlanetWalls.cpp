// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetWalls.h"
#include "Async/ParallelFor.h"
#include "HexPlanetConfig.h"
#include "HexPlanetKit.h"
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

	/** The share of the height of a wall at the lip of its overhang, where the stalactites hang from, and under the overhang, where the underside
	 *  of a canopy across a hollow begins (FParams::BridgeUnder) */
	constexpr double LipShare = 0.87;
	constexpr double UnderShare = 0.8;

	/** The lowest a crest may dip, a share of the height of its wall, so the rows of the profile keep their order (GetRowHeight) */
	constexpr double MinCrest = 0.7;

	/** The offsets of the noises, from the seed, so the walls of another planet differ; small, as a large offset loses the precision of the noise */
	struct FOffsets
	{
		FVector Bulge, Detail, Height, Crest;
		uint32 Ribs = 0;
		uint32 OuterRibs = 0;
		/** The runs of the lip, of the width of the stalactites and of the thickness (FParams::LipScale), drawn after the others, so they keep
		 *  their offsets */
		FVector Lip, RibWidth, Thickness;

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
			Lip = Next();
			RibWidth = Next();
			Thickness = Next();
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

		/** How much of its width the wall keeps, 1 down to a parapet, less as a lower wall narrows to its line (GetPresenceOf) */
		double Presence = 1.0;

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

	/** How far a wall of a height (FSample::Height) has gone towards a parapet: 0 at its full height and above, 1 at PoolHeight and below */
	double GetParapet(double Height, const FParams& Params)
	{
		return FMath::Clamp((1.0 - Height) / FMath::Max(1.0 - Params.PoolHeight, 0.05), 0.0, 1.0);
	}

	/** How much of its width a wall of a height keeps: 1 down to PoolHeight, then less as it narrows to its line, and nothing just above 0, as
	 *  the heights eased along a line stay a little above it in the middle of a single void without a wall */
	double GetPresenceOf(double Height, const FParams& Params)
	{
		constexpr double Gone = 0.03;
		return FMath::Clamp((Height - Gone) / FMath::Max(Params.PoolHeight - Gone, 0.02), 0.0, 1.0);
	}

	FWallFrame GetFrame(const FSample& Sample, const FParams& Params, const FOffsets& Offsets)
	{
		FWallFrame Frame;
		const FStyle Style = GetStyle(Sample.Position, Params);
		Frame.Base = Params.Height * Style.Height * FMath::Max(1.0 + Params.HeightNoise * Noise(Sample.Position, Params.HeightNoiseScale, Offsets.Height), 0.2);

		// the lip changes in runs along the wall, each of a few cells, by a noise of the place (FParams::LipScale): plain, its stalactites, flutes
		// and overhang nearly gone, bold, deeper and jutting out as a ledge even on a parapet, or broken between them, its stalactites coming and
		// going at random; and its stalactites are wider or narrower in runs of their own
		const double LipNoise = Noise(Sample.Position, Params.LipScale, Offsets.Lip);
		const double Plain = 1.0 - FMath::SmoothStep(Params.LipPlain - Params.LipBlend, Params.LipPlain + Params.LipBlend, LipNoise);
		const double Bold = FMath::SmoothStep(Params.LipBold - Params.LipBlend, Params.LipBold + Params.LipBlend, LipNoise);
		const double Broken = FMath::Clamp(1.0 - Plain - Bold, 0.0, 1.0);
		const double RibWidth = FMath::Max(1.0 + Params.RibWidthVariation * FMath::Clamp(2.0 * Noise(Sample.Position, Params.LipScale * 0.7, Offsets.RibWidth), -1.0, 1.0), 0.3);
		// narrower stalactites are shallower and rise and fall less on the crest, so the faces of a wall keep their slopes and do not fold
		const double Narrow = FMath::Min(RibWidth, 1.0);
		const double PlainKeep = FMath::Lerp(1.0, 0.1, Plain);
		const double RunRelief = PlainKeep * FMath::Lerp(1.0, Params.LipBoldDepth, Bold) * Narrow;

		uint32 Hash = 0;
		GetCells(Sample.Position, Params.RibSize * Style.RibSize * RibWidth, Offsets.Ribs, Frame.Edge, Hash);
		const double TipMin = FMath::Clamp(Params.RibTipMin * Style.RibTip, 0.05, 0.82);
		const double TipMax = FMath::Clamp(Params.RibTipMax * Style.RibTip, TipMin, 0.82);
		Frame.Tip = ToShare(Mix(Hash + 11u)) < Params.ColumnShare * Style.ColumnShare ? 0.0 : FMath::Lerp(TipMin, TipMax, ToShare(Mix(Hash + 12u)));
		// a stalactite of a broken run is gone by a hash of its own; both its shape and its rise of the crest come to nothing at its edge, so the
		// wall does not step there
		const double RibKeep = ToShare(Mix(Hash + 15u)) < Params.LipBrokenShare ? 1.0 - 0.9 * Broken : 1.0;
		Frame.Strength = (0.55 + 0.45 * ToShare(Mix(Hash + 13u))) * RibKeep;
		// the crest rises or falls over each stalactite by a hash of it and dips between them, so its line is ragged
		const double Rise = ToShare(Mix(Hash + 14u)) * 2.0 - 1.0;
		const double Rib = FMath::Sqrt(Frame.Edge);
		Frame.Crest = Frame.Base * FMath::Max(1.0 + Params.CrestNoise * Noise(Sample.Position, Params.BulgeScale * 0.7, Offsets.Crest)
			+ Params.CrestJag * Style.CrestJag * Rise * Rib * PlainKeep * Narrow * RibKeep - Params.CrestNotch * Style.CrestNotch * (1.0 - Rib) * PlainKeep * Narrow, MinCrest);
		uint32 OuterHash = 0;
		GetCells(Sample.Position, Params.RibSize * Style.RibSize * 1.3, Offsets.OuterRibs, Frame.OuterEdge, OuterHash);
		// a lower wall, as along lava and lakes, is thinner, with less of an overhang and smaller stalactites, a parapet at PoolHeight, so they
		// show over it, and below that it narrows to its line until nothing of it is left
		const double Height = FMath::Max(Sample.Height, 0.0);
		const double Parapet = GetParapet(Height, Params);
		Frame.Presence = GetPresenceOf(Height, Params);
		// the walls of neighboring voids a little higher or lower than each other, which leaves a parapet one
		Frame.Base *= Height * Sample.Jitter;
		Frame.Crest *= Height * Sample.Jitter;
		// the wall thicker or thinner in runs of its own, so its top does not run alike along a passage seen from above
		const double ThicknessRun = 1.0 + Params.ThicknessVariation * FMath::Clamp(2.0 * Noise(Sample.Position, Params.LipScale * 1.3, Offsets.Thickness), -1.0, 1.0);
		Frame.Thickness = Params.Thickness * Style.Thickness * FMath::Lerp(1.0, Params.PoolThickness, Parapet) * Frame.Presence * FMath::Max(ThicknessRun, 0.3);
		Frame.Lean = Params.Lean * Style.Lean;
		Frame.Overhang = (Params.Overhang * Style.Overhang * (1.0 - Parapet) * FMath::Lerp(1.0, 0.3, Plain) + Params.LipBoldOverhang * Bold) * Frame.Presence;
		Frame.Bulge = Style.Bulge * Frame.Presence;
		// the stalactites and the flutes between them by the run of the lip
		Frame.RibDepth = Params.RibDepth * Style.RibDepth * FMath::Lerp(1.0, 0.5, Parapet) * Frame.Presence * RunRelief;
		Frame.FluteDepth = Params.FluteDepth * Style.FluteDepth * Frame.Presence * RunRelief;
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
	 *  how deep in a hollow it lies, how much of a stalactite stands out there and how much it is floor. FromSmooth is how far towards the smoothed
	 *  line it stands: Smooth, less where a wall lower than a parapet narrows to the line itself, while Smooth still marks the outer face for the
	 *  material; FromTop how far towards the line of the top of the inner face (FSample::TopPosition), which no row of the outer face has */
	struct FLocal
	{
		double Across = 0.0;
		double Up = 0.0;
		double Smooth = 0.0;
		double Cavity = 0.0;
		double Rib = 0.0;
		double Floor = 0.0;
		double FromSmooth = 0.0;
		double FromTop = 0.0;
	};

	FLocal GetLocal(const FSample& Sample, const FWallFrame& Frame, int32 Row, const FParams& Params, const FOffsets& Offsets)
	{
		const double Height = FMath::Max(Frame.Base, 1.0);
		// a wall lower than a parapet narrows to its line, its foot too
		const double Fillet = Params.Fillet * Frame.Presence;
		const double Foot = Params.FootReach * Frame.Presence;
		// the inner face rises from the top of the rounded foot, leaning back as it goes up
		const double FaceBase = Foot - Fillet;
		const auto Face = [&](double Up) { return FaceBase - Frame.Lean * Up; };
		const double Thickness = Frame.Thickness;
		const double Flare = Params.OuterFlare * Frame.Presence;
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
		case InnerUnder: Share = UnderShare; Local = {Face(At(Share)) + Frame.Overhang * 0.3, At(Share), 0.0}; Local.Cavity = 0.45; break;
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
		const double Bulge = Params.Bulge * Frame.Bulge * Noise(Place, Params.BulgeScale, Offsets.Bulge) + Params.Detail * Frame.Presence * Noise(Place, Params.DetailScale, Offsets.Detail);
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
		Local.FromSmooth = Local.Smooth * Frame.Presence;
		// a canopy across a hollow: the top of the inner face stands from the line of the top, which reaches across it, and its underside goes back
		// to the line itself from under the overhang down to BridgeUnder of the height, less as a lower wall narrows to its line
		if (Kind == EKind::Inner || Row == CrestInner)
		{
			Local.FromTop = FMath::SmoothStep(Params.BridgeUnder, UnderShare, Share) * Frame.Presence;
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

	/** Where a row of a sample lies in the world: from the line itself, towards the smoothed line by FromSmooth or, across a hollow with a canopy,
	 *  towards the line of the top by FromTop, its offset eased towards the reach of the turn on its side of the line it stands from most */
	FVector3d ToWorld(const FSample& Sample, const FLocal& Local, const FParams& Params)
	{
		const bool bTop = Local.FromTop > 0.0 && Sample.Bridge > 1e-3;
		const double Share = bTop ? Local.FromTop : Local.FromSmooth;
		const bool bOther = Share >= 0.5;
		const double Reach = Local.Across > 0.0
			? (!bOther ? Sample.InnerReach : bTop ? Sample.TopInnerReach : Sample.SmoothInnerReach)
			: (!bOther ? Sample.OuterReach : bTop ? Sample.TopOuterReach : Sample.SmoothOuterReach);
		const FVector3d Base = FMath::Lerp(Sample.Position, bTop ? Sample.TopPosition : Sample.OuterPosition, Share);
		const FVector3d Inward = FMath::Lerp(Sample.Inward, bTop ? Sample.TopInward : Sample.OuterInward, Share).GetSafeNormal();
		const FVector3d Direction = (Base + Inward * Ease(Local.Across, Reach)).GetSafeNormal();
		return Direction * (GetFloorRadius(Params, Direction) + Local.Up);
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

	/** The lines the rows of a wall stand from: the line itself, the line smoothed for the outer face and the crest, and the line of the top of
	 *  the inner face, which reaches across the hollows with a canopy */
	enum class EBase : uint8
	{
		Line,
		Smooth,
		Top
	};

	/** The turn of a closed line, one of EBase, at each sample, onto the passage when positive, averaged over a few samples each way, into the
	 *  reach a wall standing from that line may have on each side there */
	void SetReaches(TArray<FSample>& Line, EBase Base)
	{
		const int32 Count = Line.Num();
		const auto Position = [&Line, Base, Count](int32 Index) -> const FVector3d&
		{
			const FSample& Sample = Line[(Index % Count + Count) % Count];
			return Base == EBase::Line ? Sample.Position : Base == EBase::Smooth ? Sample.OuterPosition : Sample.TopPosition;
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
			const FSample& Sample = Line[Index];
			const FVector3d& Inward = Base == EBase::Line ? Sample.Inward : Base == EBase::Smooth ? Sample.OuterInward : Sample.TopInward;
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
				(Base == EBase::Line ? Sample.InnerReach : Base == EBase::Smooth ? Sample.SmoothInnerReach : Sample.TopInnerReach) = 0.85 / Inner;
			}
			if (Outer > 1e-6)
			{
				(Base == EBase::Line ? Sample.OuterReach : Base == EBase::Smooth ? Sample.SmoothOuterReach : Sample.TopOuterReach) = 0.85 / Outer;
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
		// never below its shoulders, so the top does not fold in where its knobs dip, nor above the floor where the wall is gone
		OutLocals[CrestTop].Up = FMath::Max(OutLocals[CrestTop].Up, FMath::Max(OutLocals[CrestInner].Up, OutLocals[CrestOuter].Up) + Frame.Presence);
		const FVector3d TopDirection = FMath::Lerp(OutPoints[CrestInner], OutPoints[CrestOuter], 0.45).GetSafeNormal();
		OutPoints[CrestTop] = TopDirection * (GetFloorRadius(Params, TopDirection) + OutLocals[CrestTop].Up);
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
	Params.CornerCutVariation = FMath::Clamp(Config.WallCornerCutVariation, 0.0f, 0.5f);
	Params.BridgeShare = FMath::Clamp(Config.WallBridgeShare, 0.0f, 1.0f);
	Params.BridgeFillMin = FMath::Clamp(Config.WallBridgeFillMin, 0.0f, 1.0f);
	Params.BridgeUnder = FMath::Clamp(Config.WallBridgeUnder, 0.3f, 0.78f);
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
	Params.PoolHeight = FMath::Clamp(Config.WallPoolHeight, 0.1f, 1.0f);
	Params.PoolThickness = FMath::Clamp(Config.WallPoolThickness, 0.1f, 1.0f);
	Params.HeightBlend = FMath::Max(Config.WallHeightBlend, 0.0f);
	Params.HeightJitter = FMath::Clamp(Config.WallHeightJitter, 0.0f, 0.5f);
	Params.HeightJitterShare = FMath::Clamp(Config.WallHeightJitterShare, 0.0f, 1.0f);
	Params.LipScale = FMath::Max(Config.WallLipScale, 10.0f);
	Params.LipPlain = Config.WallLipPlain;
	Params.LipBold = FMath::Max(Config.WallLipBold, Config.WallLipPlain);
	Params.LipBlend = FMath::Max(Config.WallLipBlend, 0.001f);
	Params.LipBoldDepth = FMath::Max(Config.WallLipBoldDepth, 0.0f);
	Params.LipBoldOverhang = FMath::Max(Config.WallLipBoldOverhang, 0.0f);
	Params.LipBrokenShare = FMath::Clamp(Config.WallLipBrokenShare, 0.0f, 1.0f);
	Params.RibWidthVariation = FMath::Clamp(Config.WallRibWidthVariation, 0.0f, 0.7f);
	Params.ThicknessVariation = FMath::Clamp(Config.WallThicknessVariation, 0.0f, 0.7f);
	Params.FloorRise = FMath::Max(Config.FloorNoise, 0.0f);
	Params.FloorRiseScale = FMath::Max(Config.FloorNoiseScale, 10.0f);
	Params.FloorRiseDetail = FMath::Clamp(Config.FloorNoiseDetail, 0.0f, 1.0f);
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
	return Params.Height * GetStyle(Sample.Position, Params).Height * FMath::Max(1.0 + Params.HeightNoise * Noise(Sample.Position, Params.HeightNoiseScale, Offsets.Height), 0.2)
		* FMath::Max(Sample.Height, 0.0) * Sample.Jitter;
}

double HexPlanetWalls::GetPresence(const FSample& Sample, const FParams& Params)
{
	return GetPresenceOf(Sample.Height, Params);
}

double HexPlanetWalls::GetFootReach(const FParams& Params)
{
	return Params.FootReach;
}

double HexPlanetWalls::GetFloorRadius(const FParams& Params, const FVector3d& Direction)
{
	return Params.FloorRadius + HexPlanetKit::GetFloorRise(Direction, Params.FloorRadius, Params.FloorRise, Params.FloorRiseScale, Params.FloorRiseDetail);
}

void HexPlanetWalls::Build(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TFunctionRef<bool(int32)> CanHold, const FParams& Params, FWalls& Out,
	TConstArrayView<uint8> VoidStyles, TConstArrayView<float> VoidHeights)
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
		/** The void across the side */
		int32 Void;
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
				const int32 Index = Edges.Add({Cell, Each.Neighbors[Side], Each.Corners[(Side + Count - 1) % Count], Each.Corners[Side]});
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
		const auto VoidAt = [&](int32 Index) { return Edges[Loop[Wrap(Index)]].Void; };
		// where the sides of two passages meet, the corner of the void between them pokes into the floor; where two sides of one passage meet, it
		// is the tip of that hex
		const auto IsVoidCorner = [&](int32 Index) { return CellAt(Index - 1) != CellAt(Index); };

		// a hash of the corner of the grid at a point of the loop, for what the wall does there by chance; of the place alone, so an edit of the
		// maze leaves the walls it does not reach as they were
		const auto CornerHash = [&](int32 Index, uint32 Salt)
		{
			return Mix(uint32(Edges[Loop[Wrap(Index)]].From) * 0x9E3779B1u ^ (uint32(Params.Seed) * 0x85EBCA77u + Salt));
		};
		// the cut of each corner of a void, within CornerCutVariation of CornerCut by its hash, so the protrusions of a straight wall differ, and
		// never past half a side unless CornerCut is, so every side over a void keeps the wall at its middle
		const double CutLow = FMath::Max(Params.CornerCut - Params.CornerCutVariation, 0.0);
		const double CutHigh = FMath::Max(FMath::Min(Params.CornerCut + Params.CornerCutVariation, FMath::Max(Params.CornerCut, 0.5)), CutLow);
		const auto CutAt = [&](int32 Index)
		{
			return Params.CornerCut <= 0.0 || !IsVoidCorner(Index) ? 0.0 : FMath::Lerp(CutLow, CutHigh, ToShare(CornerHash(Index, 0x2545F491u)));
		};

		// the sides, keeping the tips of the hexes, each corner of a void cut across from its cut of each of its sides, or of half a side that
		// ends at another such corner, as if the floor went on into the void, so the walls still show the hexes they stand on. A tip between two
		// cut corners is a hollow of the wall, and BridgeShare of them carry a canopy, whose fill is how far it reaches across (PointFills)
		TArray<FVector3d> Points;
		TArray<int32> PointCells;
		TArray<int32> PointVoids;
		TArray<double> PointFills;
		// whether a point is an end of the cut across a corner of a void, which a canopy may reach to
		TArray<bool> PointCuts;
		for (int32 Index = 0; Index < NumEdges; ++Index)
		{
			const FVector3d Here = CornerAt(Index);
			const double Cut = CutAt(Index);
			if (Cut <= 0.0)
			{
				double Fill = 0.0;
				if (!IsVoidCorner(Index) && CutAt(Index - 1) > 0.0 && CutAt(Index + 1) > 0.0 && ToShare(CornerHash(Index, 0x68E31DA4u)) < Params.BridgeShare)
				{
					Fill = FMath::Lerp(Params.BridgeFillMin, 1.0, ToShare(CornerHash(Index, 0xB5297A4Du)));
				}
				Points.Add(Here);
				PointCells.Add(CellAt(Index));
				PointVoids.Add(VoidAt(Index));
				PointFills.Add(Fill);
				PointCuts.Add(false);
				continue;
			}
			const double Back = Cut * (IsVoidCorner(Index - 1) ? 0.5 : 1.0);
			const double Ahead = Cut * (IsVoidCorner(Index + 1) ? 0.5 : 1.0);
			Points.Add(FMath::Lerp(Here, CornerAt(Index - 1), Back));
			PointCells.Add(CellAt(Index - 1));
			PointVoids.Add(VoidAt(Index - 1));
			Points.Add(FMath::Lerp(Here, CornerAt(Index + 1), Ahead));
			PointCells.Add(CellAt(Index));
			PointVoids.Add(VoidAt(Index));
			PointFills.Append({0.0, 0.0});
			PointCuts.Append({true, true});
		}
		// points that fall together, as where a full cut reaches the tip of a hex, are one
		for (int32 Index = Points.Num() - 1; Index >= 0 && Points.Num() > 3; --Index)
		{
			if (FVector3d::DistSquared(Points[Index], Points[(Index + 1) % Points.Num()]) < 1e-4)
			{
				Points.RemoveAt(Index);
				PointCells.RemoveAt(Index);
				PointVoids.RemoveAt(Index);
				PointFills.RemoveAt(Index);
				PointCuts.RemoveAt(Index);
			}
		}

		// every corner rounded by a quadratic curve within CornerRound of it. Each point holds the passage and void of the stretch that starts at
		// it, so the first half of a curve, still on the stretch that comes in, holds those of the point before; RoundFirst and RoundLast are the
		// first and last points of the curve of each corner
		TArray<FVector3d> Rounded;
		TArray<int32> RoundedCells;
		TArray<int32> RoundedVoids;
		const int32 NumPoints = Points.Num();
		TArray<int32> RoundFirst;
		TArray<int32> RoundLast;
		RoundFirst.SetNum(NumPoints);
		RoundLast.SetNum(NumPoints);
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FVector3d& Here = Points[Index];
			const int32 Previous = (Index + NumPoints - 1) % NumPoints;
			const FVector3d& Before = Points[Previous];
			const FVector3d& After = Points[(Index + 1) % NumPoints];
			const double In = FVector3d::Distance(Before, Here);
			const double Onward = FVector3d::Distance(Here, After);
			const FVector3d InWay = (Here - Before) / FMath::Max(In, 1e-9);
			const FVector3d OnWay = (After - Here) / FMath::Max(Onward, 1e-9);
			const double Turn = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(InWay, OnWay), -1.0, 1.0));
			const double Tangent = FMath::Min(Params.CornerRound * FMath::Tan(FMath::Min(Turn, 2.6) * 0.5), 0.45 * FMath::Min(In, Onward));
			RoundFirst[Index] = Rounded.Num();
			if (Params.CornerRound <= 0.0 || Turn < 0.02 || Tangent < 0.05)
			{
				RoundLast[Index] = Rounded.Num();
				Rounded.Add(Here);
				RoundedCells.Add(PointCells[Index]);
				RoundedVoids.Add(PointVoids[Index]);
				continue;
			}
			const FVector3d Start = Here - InWay * Tangent;
			const FVector3d End = Here + OnWay * Tangent;
			const int32 Steps = FMath::Clamp(FMath::CeilToInt32(Tangent * 4.0 / FMath::Max(Params.Step, 1.0)), 2, 16);
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double U = double(Step) / Steps;
				Rounded.Add(Start * FMath::Square(1.0 - U) + Here * (2.0 * U * (1.0 - U)) + End * FMath::Square(U));
				RoundedCells.Add(PointCells[U < 0.5 ? Previous : Index]);
				RoundedVoids.Add(PointVoids[U < 0.5 ? Previous : Index]);
			}
			RoundLast[Index] = Rounded.Num() - 1;
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
			// the passage and void of the stretch the sample lies on, which starts at the point before it; the nearer point held the next side
			// for the second half of each side, which moved the parapets along lava and lakes half a side on
			Sample.Cell = RoundedCells[Segment];
			// how much of the void beyond is lava or a lake and how high its wall is, before they are eased along the line
			const int32 Void = RoundedVoids[Segment];
			const uint8 Style = VoidStyles.IsValidIndex(Void) ? VoidStyles[Void] : 0;
			Sample.Lava = Style == 1 ? 1.0 : 0.0;
			Sample.Lake = Style == 2 ? 1.0 : 0.0;
			Sample.Height = VoidHeights.IsValidIndex(Void) ? FMath::Max(double(VoidHeights[Void]), 0.0) : 1.0;
			// HeightJitterShare of the voids, by a hash of the void, so an edit leaves the others as they were
			const uint32 VoidHash = Mix(uint32(Void) * 0x9E3779B1u ^ (uint32(Params.Seed) * 0x85EBCA77u + 0x27D4EB2Fu));
			Sample.Jitter = ToShare(VoidHash) < Params.HeightJitterShare ? 1.0 + Params.HeightJitter * (2.0 * ToShare(Mix(VoidHash + 1u)) - 1.0) : 1.0;
		}

		// the line of the top of the inner face: the line itself, but across a hollow with a canopy, from where the curve of the cut corner
		// before it begins to where that of the one after it ends, it reaches towards the chord between them by the fill of the hollow, a sample
		// taking the point of the chord as far along it as the sample lies along the line, so the rows of the canopy keep their order
		TArray<FVector3d> Top;
		Top.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Top[Index] = Line[Index].Position;
		}
		bool bBridged = false;
		for (int32 Point = 0; Point < NumPoints; ++Point)
		{
			const int32 Before = (Point + NumPoints - 1) % NumPoints;
			const int32 After = (Point + 1) % NumPoints;
			if (PointFills[Point] <= 0.0 || !PointCuts[Before] || !PointCuts[After])
			{
				continue;
			}
			const double From = Lengths[RoundFirst[Before]];
			double To = Lengths[RoundLast[After]];
			// a span over the start of the loop
			To += To <= From ? Total : 0.0;
			const FVector3d& FromPoint = Rounded[RoundFirst[Before]];
			const FVector3d& ToPoint = Rounded[RoundLast[After]];
			const int32 Last = FMath::FloorToInt32(To / Spacing);
			for (int32 Each = FMath::CeilToInt32(From / Spacing); Each <= Last; ++Each)
			{
				const int32 Index = Each % Count;
				const FVector3d Chord = FMath::Lerp(FromPoint, ToPoint, (Each * Spacing - From) / FMath::Max(To - From, 1e-9)).GetSafeNormal() * Radius;
				Top[Index] = FMath::Lerp(Line[Index].Position, Chord, PointFills[Point]).GetSafeNormal() * Radius;
				bBridged = true;
			}
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

		// the line of the top and onto the passage from there, the same way as the line
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSample& Sample = Line[Index];
			Sample.TopPosition = Top[Index];
			Sample.TopInward = Sample.Inward;
			Sample.Bridge = FVector3d::Distance(Top[Index], Sample.Position);
			const FVector3d TopUp = Top[Index].GetSafeNormal();
			const FVector3d TopAhead = Top[(Index + 1) % Count] - Top[(Index + Count - 1) % Count];
			const FVector3d TopAlong = TopAhead - TopUp * FVector3d::DotProduct(TopAhead, TopUp);
			if (bBridged && TopAlong.SizeSquared() > 1e-6)
			{
				Sample.TopInward = FVector3d::CrossProduct(TopUp, TopAlong.GetSafeNormal()) * (Votes < 0 ? -1.0 : 1.0);
			}
		}

		// inside a turn the wall reaches at most about its radius from the line it stands from, so its faces do not cross over themselves
		SetReaches(Line, EBase::Line);
		SetReaches(Line, EBase::Smooth);
		if (bBridged)
		{
			SetReaches(Line, EBase::Top);
		}

		// the heights of the walls and the lava and lakes that light them ease from one void to the next over HeightBlend each way, as an S
		// between the lowest and highest of them around, so a wall still sinks to nothing in the middle of a single void without one
		const int32 BlendHalf = FMath::Clamp(FMath::RoundToInt32(Params.HeightBlend / Spacing), 0, (Count - 1) / 2);
		if (BlendHalf > 0)
		{
			TArray<FVector3d> Raw;
			Raw.SetNum(Count);
			TArray<double> RawJitter;
			RawJitter.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Raw[Index] = FVector3d(Line[Index].Lava, Line[Index].Lake, Line[Index].Height);
				RawJitter[Index] = Line[Index].Jitter;
			}
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FVector3d Sum = FVector3d::ZeroVector;
				double JitterSum = 0.0;
				double Weights = 0.0;
				double Low = TNumericLimits<double>::Max();
				double High = -TNumericLimits<double>::Max();
				for (int32 Offset = -BlendHalf; Offset <= BlendHalf; ++Offset)
				{
					const int32 Each = (Index + Offset + Count * 2) % Count;
					const double Weight = 1.0 - FMath::Abs(double(Offset)) / (BlendHalf + 1);
					Sum += Raw[Each] * Weight;
					JitterSum += RawJitter[Each] * Weight;
					Weights += Weight;
					Low = FMath::Min(Low, Raw[Each].Z);
					High = FMath::Max(High, Raw[Each].Z);
				}
				Line[Index].Lava = FMath::SmoothStep(0.0, 1.0, Sum.X / Weights);
				Line[Index].Lake = FMath::SmoothStep(0.0, 1.0, Sum.Y / Weights);
				Line[Index].Height = High - Low > 1e-6 ? Low + (High - Low) * FMath::SmoothStep(0.0, 1.0, (Sum.Z / Weights - Low) / (High - Low)) : Low;
				Line[Index].Jitter = JitterSum / Weights;
			}
		}

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

	// the index of each sample among all of them, line after line, by which it has its pixel in the texture of the pools of light on the walls;
	// each line starts a row of its own, so a line whose length an edit changed moves the pixels of the lines after it only when it takes a row
	// more or less (an edit already moves every sample of its own line, which is sampled evenly from its start)
	Out.LineStarts.SetNum(Out.Lines.Num());
	for (int32 Line = 0; Line < Out.Lines.Num(); ++Line)
	{
		Out.LineStarts[Line] = Out.NumSamples;
		Out.NumSamples += Out.Lines[Line].Num();
		Out.NumSamples = Line + 1 < Out.Lines.Num() ? FMath::DivideAndRoundUp(Out.NumSamples, SampleColumns) * SampleColumns : Out.NumSamples;
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
	/** The rows a coarse mesh of the walls keeps, from deep down the outer face over the crest to under the floor, every CoarseStride sample:
	 *  about a fifth of the triangles, for the walls further than WallLodDistance */
	constexpr int32 CoarseRows[] = {OuterDeep, OuterFloor, OuterLow, OuterHigh, CrestOuter, CrestTop, CrestInner, InnerLip, InnerFirst + 2, InnerFirst + 5,
		FootFirst + 2, FootLast, FloorEnd, Buried};
	constexpr int32 CoarseStride = 3;

	/** The mesh of the stretches from each of some samples to the sample Stride further, of the rows of RowSet in its order (every row when it
	 *  is empty); a sample whose index is no multiple of Stride starts none */
	void BuildSamples(const FWalls& Walls, TConstArrayView<FIntPoint> Samples, const FParams& Params, FMesh& Out, int32 Stride = 1, TConstArrayView<int32> RowSet = {})
	{
		const FOffsets Offsets(Params.Seed);
		TArray<int32, TInlineAllocator<Rows>> Kept(RowSet.GetData(), RowSet.Num());
		if (Kept.IsEmpty())
		{
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				Kept.Add(Row);
			}
		}
		const int32 NumKept = Kept.Num();

		// the rings of the samples, of the next sample of each and, for the normals, of the samples around them
		struct FRing
		{
			FVector3d Points[Rows];
			FLocal Locals[Rows];
			double Height = 0.0;
			FVector2f Stone = FVector2f::ZeroVector;
			FVector2f Pools = FVector2f::ZeroVector;
			FVector2f Texel = FVector2f::ZeroVector;
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
			Ring.Pools = FVector2f(float(Sample.Lava), float(Sample.Lake));
			Ring.Texel = GetSampleTexel(Walls, At);
			GetRing(Sample, Frame, Params, Offsets, Ring.Locals, Ring.Points);
			return Rings.Add(At, Ring);
		};
		// the sample Stride along, the last stretch of a loop ending at its first sample
		const auto Step = [&Walls, Stride](const FIntPoint& At, int32 Way)
		{
			const int32 Count = Walls.Lines[At.X].Num();
			if (Way > 0)
			{
				return FIntPoint(At.X, At.Y + Stride >= Count ? 0 : At.Y + Stride);
			}
			const int32 Last = ((Count - 1) / Stride) * Stride;
			return FIntPoint(At.X, At.Y == 0 ? Last : At.Y - Stride);
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
			const FRing Before = FindRing(Step(At, -1));
			const FRing After = FindRing(Step(At, 1));
			const int32 First = Out.Positions.Num();
			for (int32 Index = 0; Index < NumKept; ++Index)
			{
				const int32 Row = Kept[Index];
				const FVector3d AcrossLine = After.Points[Row] - Before.Points[Row];
				const FVector3d DownProfile = Ring.Points[Kept[FMath::Min(Index + 1, NumKept - 1)]] - Ring.Points[Kept[FMath::Max(Index - 1, 0)]];
				FVector3d Normal = FVector3d::CrossProduct(DownProfile, AcrossLine).GetSafeNormal();
				// where a wall is gone its rows fall together, so the way out of the row stands in
				if (Normal.IsNearlyZero())
				{
					Normal = GetRowOutside(Sample, Row);
				}
				if (FVector3d::DotProduct(Normal, GetRowOutside(Sample, Row)) < 0.0)
				{
					Normal = -Normal;
				}
				const FLocal& Local = Ring.Locals[Row];
				Out.Positions.Add(FVector3f(Ring.Points[Row]));
				Out.Normals.Add(FVector3f(Normal));
				Out.UVs.Add(FVector2f(float(Local.Cavity), float(Local.Up / FMath::Max(Ring.Height, 1.0))));
				// the floor of the foot and the outer face, which stands from the smoothed line, never share a row, so one channel holds both
				Out.UV1s.Add(FVector2f(float(Local.Floor - Local.Smooth), float(Local.Rib)));
				Out.UV2s.Add(Ring.Stone);
				Out.UV3s.Add(Ring.Pools);
				Out.UV4s.Add(Ring.Texel);
			}
			RingVertices.Add(At, First);
			return First;
		};

		for (const FIntPoint& At : Samples)
		{
			if (At.Y % Stride != 0)
			{
				continue;
			}
			const int32 A = AddRing(At);
			const int32 B = AddRing(Step(At, 1));
			for (int32 Index = 0; Index < NumKept - 1; ++Index)
			{
				const int32 Quad[4] = {A + Index, B + Index, B + Index + 1, A + Index + 1};
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

void HexPlanetWalls::BuildChunk(const FWalls& Walls, int32 Chunk, const FParams& Params, FMesh& Out, bool bCoarse)
{
	Out = FMesh();
	if (Walls.ChunkSamples.IsValidIndex(Chunk) && !Walls.ChunkSamples[Chunk].IsEmpty())
	{
		if (bCoarse)
		{
			BuildSamples(Walls, Walls.ChunkSamples[Chunk], Params, Out, CoarseStride, CoarseRows);
		}
		else
		{
			BuildSamples(Walls, Walls.ChunkSamples[Chunk], Params, Out);
		}
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
		// the normals of a ring take the samples on both sides of it, a coarse ring those three samples away
		for (int32 Step = -3; Step <= 4; ++Step)
		{
			const int32 Index = ((At.Y + Step) % Line.Num() + Line.Num()) % Line.Num();
			const FSample& Sample = Line[Index];
			// and its pixel of the pools of light, which its vertices carry
			Hash = HashCombine(Hash, GetTypeHash(Walls.GetIndex(FIntPoint(At.X, Index))));
			Hash = HashCombine(Hash, GetTypeHash(Round(Sample.Position)));
			Hash = HashCombine(Hash, GetTypeHash(Round(Sample.OuterPosition)));
			Hash = HashCombine(Hash, GetTypeHash(Round(Sample.TopPosition)));
			Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt32(Sample.Gap * 16.0)));
			Hash = HashCombine(Hash, GetTypeHash(FIntVector(FMath::RoundToInt32(Sample.Lava * 256.0), FMath::RoundToInt32(Sample.Lake * 256.0), FMath::RoundToInt32(Sample.Height * 1024.0))));
			Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt32(Sample.Jitter * 1024.0)));
			for (const double Reach : {Sample.InnerReach, Sample.OuterReach, Sample.SmoothInnerReach, Sample.SmoothOuterReach, Sample.TopInnerReach, Sample.TopOuterReach})
			{
				Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt32(FMath::Min(Reach, 1e6) * 4.0)));
			}
		}
	}
	return Hash;
}

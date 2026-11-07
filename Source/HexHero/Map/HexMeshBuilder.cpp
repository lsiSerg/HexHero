// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexMeshBuilder.h"
#include "HexCoord.h"
#include "HexMapData.h"
#include "Async/ParallelFor.h"

namespace
{
	/** Lattice steps from the center of a hex towards each corner */
	const FIntPoint CornerSteps[FHexCoord::NumDirections] = {
		FIntPoint(1, 0),
		FIntPoint(0, 1),
		FIntPoint(-1, 1),
		FIntPoint(-1, 0),
		FIntPoint(0, -1),
		FIntPoint(1, -1)
	};

	const double Sqrt3 = FMath::Sqrt(3.0);

	/** Rough noise from about -1 to 1 */
	double GetBumpNoise(const FVector2D& Position, const FHexTerrainTuning& Tuning)
	{
		double Result = 0.0;
		double Amplitude = 0.6;
		FVector2D Sample = Position;

		for (int32 Octave = 0; Octave < Tuning.NoiseOctaves; ++Octave)
		{
			Result += FMath::PerlinNoise2D(Sample) * Amplitude;
			Amplitude *= 0.5;
			Sample *= Tuning.NoiseFrequencyStep;
		}

		return Result;
	}

	/** Sharp crests from about 0 to 1 */
	double GetRidgeNoise(const FVector2D& Position, const FHexTerrainTuning& Tuning)
	{
		double Result = 0.0;
		double Amplitude = 0.55;
		FVector2D Sample = Position + FVector2D(37.3, 11.7);

		for (int32 Octave = 0; Octave < Tuning.NoiseOctaves; ++Octave)
		{
			const double Crest = 1.0 - FMath::Abs(FMath::PerlinNoise2D(Sample));
			Result += Crest * Crest * Amplitude;
			Amplitude *= 0.5;
			Sample *= Tuning.NoiseFrequencyStep;
		}

		return Result;
	}

	/** A point on the line of a coast while it is being built */
	struct FCoastPoint
	{
		FVector2D Position = FVector2D::ZeroVector;
		double Top = 0.0;
		double Ground = 0.0;
		FIntPoint Cell = FIntPoint::ZeroValue;
	};

	/** Cuts each corner of a line, which leaves it round after a few passes */
	TArray<FCoastPoint> SmoothCoast(const TArray<FCoastPoint>& Points, bool bClosed)
	{
		TArray<FCoastPoint> Result;
		const int32 NumSegments = bClosed ? Points.Num() : Points.Num() - 1;

		if (!bClosed)
		{
			Result.Add(Points[0]);
		}

		for (int32 Index = 0; Index < NumSegments; ++Index)
		{
			const FCoastPoint& A = Points[Index];
			const FCoastPoint& B = Points[(Index + 1) % Points.Num()];

			FCoastPoint First = A;
			First.Position = FMath::Lerp(A.Position, B.Position, 0.25);
			First.Top = FMath::Lerp(A.Top, B.Top, 0.25);
			First.Ground = FMath::Lerp(A.Ground, B.Ground, 0.25);

			FCoastPoint Second = B;
			Second.Position = FMath::Lerp(A.Position, B.Position, 0.75);
			Second.Top = FMath::Lerp(A.Top, B.Top, 0.75);
			Second.Ground = FMath::Lerp(A.Ground, B.Ground, 0.75);

			Result.Add(First);
			Result.Add(Second);
		}

		if (!bClosed)
		{
			Result.Add(Points.Last());
		}

		return Result;
	}

	/** Places points at equal distances along a line */
	TArray<FCoastPoint> ResampleCoast(const TArray<FCoastPoint>& Points, bool bClosed, const FHexTerrainTuning& Tuning)
	{
		const int32 NumSegments = bClosed ? Points.Num() : Points.Num() - 1;
		double Length = 0.0;

		for (int32 Index = 0; Index < NumSegments; ++Index)
		{
			Length += FVector2D::Distance(Points[Index].Position, Points[(Index + 1) % Points.Num()].Position);
		}

		const int32 NumSteps = FMath::Max(2, FMath::RoundToInt32(Length / Tuning.CliffRingSpacing));
		const double Spacing = Length / NumSteps;

		TArray<FCoastPoint> Result;
		int32 Segment = 0;
		double SegmentStart = 0.0;

		for (int32 Step = 0; Step < (bClosed ? NumSteps : NumSteps + 1); ++Step)
		{
			const double Along = Step * Spacing;
			double SegmentLength = FVector2D::Distance(Points[Segment].Position, Points[(Segment + 1) % Points.Num()].Position);

			while (Along > SegmentStart + SegmentLength && Segment < NumSegments - 1)
			{
				SegmentStart += SegmentLength;
				++Segment;
				SegmentLength = FVector2D::Distance(Points[Segment].Position, Points[(Segment + 1) % Points.Num()].Position);
			}

			const double Alpha = FMath::Clamp((Along - SegmentStart) / FMath::Max(SegmentLength, UE_KINDA_SMALL_NUMBER), 0.0, 1.0);
			const FCoastPoint& A = Points[Segment];
			const FCoastPoint& B = Points[(Segment + 1) % Points.Num()];

			FCoastPoint Point = Alpha < 0.5 ? A : B;
			Point.Position = FMath::Lerp(A.Position, B.Position, Alpha);
			Point.Top = FMath::Lerp(A.Top, B.Top, Alpha);
			Point.Ground = FMath::Lerp(A.Ground, B.Ground, Alpha);
			Result.Add(Point);
		}

		return Result;
	}

	/** Returns the distance from a cut towards the water of a row of the face, from 0 at the top to 1 at the bottom, for ledges, a wall and a slope */
	double GetCliffReach(double Depth, double Ledges, double Slope, const FHexTerrainTuning& Tuning)
	{
		// it leans back, as a wall straight down would hardly show from above
		const double Wall = Tuning.CliffWallTop + Depth * Tuning.CliffWallLean;

		// shelves of rock with short walls between them
		const double Steps = Depth * Tuning.CliffLedges;
		const double Step = FMath::FloorToDouble(Steps);
		const double Stair = Tuning.CliffWallTop + (Step + FMath::SmoothStep(0.45, 0.8, Steps - Step)) * Tuning.CliffLedgeStep;

		const double Gentle = Tuning.CliffSlopeTop + FMath::Pow(Depth, double(Tuning.CliffSlopePower)) * Tuning.CliffSlopeReach;

		return FMath::Lerp(FMath::Lerp(Wall, Stair, Ledges), Gentle, Slope);
	}

	/** Fills in the vertices of a chain from its rings */
	void BuildCliffMesh(FHexCliffChain& Chain, double BottomHeight, const FHexTerrainTuning& Tuning)
	{
		const int32 Rows = FHexCliffData::RowsPerRing;
		const int32 NumRings = Chain.Rings.Num();
		const int32 NumFaceRows = Rows - 3;

		Chain.Vertices.Reserve(NumRings * Rows);
		Chain.Colors.Reserve(NumRings * Rows);

		// the top reaches inland only until another part of the coast is closer, as in a tight bend it would otherwise cross itself and fold the mesh over
		const double FullLipReach = Tuning.CliffInlandRamp + Tuning.CliffLipWidth + 6.0;
		TArray<double> LipReach;
		LipReach.Init(FullLipReach, NumRings);

		for (int32 Index = 0; Index < NumRings; ++Index)
		{
			const FHexCliffRing& Ring = Chain.Rings[Index];

			for (double& Reach = LipReach[Index]; Reach > 6.0; Reach -= 2.0)
			{
				const FVector2D Inner = Ring.Position - Ring.Outward * Reach;
				bool bCloser = false;

				for (int32 Offset = -Tuning.CliffLipBendRings; Offset <= Tuning.CliffLipBendRings && !bCloser; ++Offset)
				{
					const int32 Other = Chain.bClosed ? (Index + Offset + NumRings) % NumRings : Index + Offset;
					bCloser = Chain.Rings.IsValidIndex(Other) && FVector2D::Distance(Inner, Chain.Rings[Other].Position) < Reach - 1.0;
				}

				if (!bCloser)
				{
					break;
				}
			}
		}

		for (int32 RingIndex = 0; RingIndex < NumRings; ++RingIndex)
		{
			const FHexCliffRing& Ring = Chain.Rings[RingIndex];
			const FVector2D Sample = Ring.Position / Tuning.CliffKindScale;
			const double Kind = FMath::PerlinNoise2D(Sample + FVector2D(13.1, 4.7)) + FMath::PerlinNoise2D(Sample * 2.3 + FVector2D(3.3, 8.9)) * 0.4;
			const double Ledges = FMath::SmoothStep(double(Tuning.CliffKindStart), double(Tuning.CliffKindEnd), Kind);
			const double Slope = FMath::SmoothStep(-double(Tuning.CliffKindStart), -double(Tuning.CliffKindEnd), Kind) * (1.0 - Ledges);
			const double Height = FMath::Max(Ring.Top - BottomHeight, 1.0);

			// the top lies flat over the place where the terrain falls away, and takes the color of the land
			const auto AddRow = [&Chain, &Ring](double Reach, double Z, float TopAmount)
			{
				const FVector2D Position = Ring.Position + Ring.Outward * Reach;
				Chain.Vertices.Add(FVector(Position.X, Position.Y, Z));
				Chain.Colors.Add(FLinearColor(TopAmount, 0.0f, 0.0f, 1.0f));
			};

			// more than the terrain can fall as the cliff fades out, so the terrain always covers it
			const double Sunk = (1.0 - Ring.Strength) * Tuning.CliffFadeSink;
			const auto AddTopRow = [&Chain](const FVector2D& Position, double Z)
			{
				Chain.Vertices.Add(FVector(Position.X, Position.Y, Z));
				Chain.Colors.Add(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
			};

			const FVector2D LipEnd = Ring.Position - Ring.Outward * Tuning.CliffLipOverhang;
			const FVector2D InnerEdge = Ring.Position - Ring.Outward * LipReach[RingIndex];
			// its inner edge goes under the terrain, however far it reaches, as an edge above the terrain shows as a dark crack
			AddTopRow(InnerEdge, Ring.Top - 0.6 - (FullLipReach - LipReach[RingIndex]) * 0.25 - Sunk);
			AddTopRow(FMath::Lerp(InnerEdge, LipEnd, 0.2), Ring.Top + Tuning.CliffLipRise - Sunk);
			AddTopRow(LipEnd, Ring.Top + Tuning.CliffLipRise - Sunk);

			for (int32 FaceRow = 0; FaceRow < NumFaceRows; ++FaceRow)
			{
				const double Depth = static_cast<double>(FaceRow) / (NumFaceRows - 1);
				const FVector2D RockSample = Ring.Position / Tuning.CliffRockScale + FVector2D(Depth * 3.0, Depth * 2.0);
				const FVector2D FineSample = Ring.Position / Tuning.CliffRockFineScale + FVector2D(Depth * 7.0, -Depth * 5.0);
				const double Rock = FMath::PerlinNoise2D(RockSample) * Tuning.CliffRockHeight + FMath::PerlinNoise2D(FineSample) * Tuning.CliffRockFineHeight;
				const double Reach = GetCliffReach(Depth, Ledges, Slope, Tuning) * Ring.Strength + Rock * FMath::Min(1.0, Depth * 4.0 + 0.2) * Ring.Strength;
				const double Strata = Slope * FMath::PerlinNoise2D(Ring.Position / Tuning.CliffStrataScale + FVector2D(Depth * 5.0, 0.0)) * Tuning.CliffStrataHeight;

				AddRow(Reach, Ring.Top - 0.6 - Depth * Height + Strata - Sunk, FaceRow == 0 ? 0.5f : 0.0f);
			}
		}

		TArray<FVector> Totals;
		Totals.SetNumZeroed(Chain.Vertices.Num());
		const int32 NumSegments = Chain.bClosed ? NumRings : NumRings - 1;

		for (int32 Index = 0; Index < NumSegments; ++Index)
		{
			const int32 Next = (Index + 1) % NumRings;

			for (int32 Row = 0; Row < Rows - 1; ++Row)
			{
				const int32 A = Index * Rows + Row;
				const int32 B = Next * Rows + Row;
				const int32 C = Next * Rows + Row + 1;
				const int32 D = Index * Rows + Row + 1;

				const FVector First = FVector::CrossProduct(Chain.Vertices[C] - Chain.Vertices[A], Chain.Vertices[B] - Chain.Vertices[A]);
				const FVector Second = FVector::CrossProduct(Chain.Vertices[D] - Chain.Vertices[A], Chain.Vertices[C] - Chain.Vertices[A]);

				Totals[A] += First + Second;
				Totals[B] += First;
				Totals[C] += First + Second;
				Totals[D] += Second;
			}
		}

		Chain.Normals.Reserve(Totals.Num());

		for (const FVector& Total : Totals)
		{
			Chain.Normals.Add(Total.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
		}
	}

	/** The joined mesh that a position of the terrain has to meet */
	struct FHexMeshFoot
	{
		const FHexLevelSurface* Surface = nullptr;

		/** Height to meet */
		double Height = 0.0;

		/** Distance to the edge of the mesh */
		double Distance = 0.0;

		/** If true, the position is under the mesh */
		bool bInside = false;
	};

	/** A minimum of two values that rounds off over Width where they come close, so a profile made of them has no sharp corner */
	double SmoothMin(double A, double B, double Width)
	{
		const double Blend = FMath::Max(Width - FMath::Abs(A - B), 0.0) / Width;
		return FMath::Min(A, B) - Blend * Blend * Width * 0.25;
	}

	/** Builds the terrain of one map */
	class FTerrainWriter
	{
	public:

		FTerrainWriter(const FHexMapData& InMap, const FHexMeshParams& InParams, const TMap<int8, FHexLevelSurface>& InLevelSurfaces, FHexMeshData& InMesh, const FHexCliffData* InCliffs,
			const FHexRiverData* InRivers = nullptr)
			: Map(InMap)
			, Params(InParams)
			, LevelSurfaces(InLevelSurfaces)
			, Mesh(InMesh)
			, Cliffs(InCliffs)
			, Rivers(InRivers)
			, LatticeStep(static_cast<double>(InParams.HexRadius) / InParams.Subdivisions)
		{
			for (const TPair<int8, FHexLevelSurface>& Pair : LevelSurfaces)
			{
				bAnyJoinedLevel |= Pair.Value.bHasJoinedMesh;
			}
		}

		void AddHex(const FHexCoord& Hex)
		{
			if (IsTerrainHidden(Hex))
			{
				return;
			}

			const int32 Subdivisions = Params.Subdivisions;

			// the center of a hex in lattice steps
			const FIntPoint Center(-Subdivisions * (2 * Hex.R + Hex.Q), Subdivisions * (2 * Hex.Q + Hex.R));

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FIntPoint StepA = CornerSteps[Direction];
				const FIntPoint StepB = CornerSteps[(Direction + 1) % FHexCoord::NumDirections];

				// a joined mesh covers its whole hex, and terrain under it could only poke through
				if (!FindJoinedSurface(Hex))
				{
					AddSector(Center, StepA, StepB);
				}

				// close the sides of the terrain along the edge of the map
				if (!Map.Contains(Hex.GetNeighbor(Direction)))
				{
					AddWall(Center + StepA * Subdivisions, StepB - StepA);
				}
			}
		}

		/** Returns the height of the surface at a point of the lattice, the same as the vertex of the terrain there */
		float GetLatticeHeight(const FIntPoint& Point) const
		{
			return static_cast<float>(GetSurface(GetLatticePosition(Point)));
		}

		/** Returns the height of the surface at any position */
		double GetHeight(const FVector2D& Position) const
		{
			return GetSurface(Position);
		}

		/** The river a position belongs to: the one it lies deepest inside, or nearest the bank of */
		struct FRiverPlace
		{
			bool bFound = false;

			/** Distance from the middle of the river, and the place in its middle nearest the position */
			double Distance = 0.0;
			FVector2D Middle = FVector2D::ZeroVector;

			/** Horizontal direction from the middle towards the position, so towards the land */
			FVector2D Landward = FVector2D::ZeroVector;

			/** Distance from the middle to the foot of its bank, under the water, and to the top of its bank */
			double Foot = 0.0;
			double Top = 0.0;

			/** Height of its water there */
			double Surface = 0.0;
		};

		FRiverPlace FindRiver(const FVector2D& Position) const
		{
			FRiverPlace Place;
			const TArray<FIntPoint>* Parts = Rivers ? Rivers->Nearby.Find(FHexLayout(Params.HexRadius).WorldToHex(Position)) : nullptr;
			double Inside = TNumericLimits<double>::Max();

			for (int32 Index = 0; Parts && Index < Parts->Num(); ++Index)
			{
				const FIntPoint& Part = (*Parts)[Index];
				const TArray<FHexRiverPoint>& Points = Rivers->Lines[Part.X].Points;
				const FHexRiverPoint& A = Points[Part.Y];
				const FHexRiverPoint& B = Points[Part.Y + 1];
				const FVector2D Along = B.Position - A.Position;
				const double Alpha = FMath::Clamp(FVector2D::DotProduct(Position - A.Position, Along) / FMath::Max(Along.SizeSquared(), UE_KINDA_SMALL_NUMBER), 0.0, 1.0);
				const FVector2D Middle = A.Position + Along * Alpha;
				const double Distance = FVector2D::Distance(Position, Middle);
				const double Half = FMath::Lerp(A.Width, B.Width, Alpha) * 0.5;

				if (Distance - Half < Inside)
				{
					Inside = Distance - Half;
					Place.bFound = true;
					Place.Distance = Distance;
					Place.Middle = Middle;
					Place.Landward = Distance > UE_KINDA_SMALL_NUMBER ? (Position - Middle) / Distance : FVector2D(-Along.Y, Along.X).GetSafeNormal();
					Place.Foot = Half - Params.Tuning.RiverBankFoot;
					Place.Top = Half + Params.RiverBankLean;
					Place.Surface = FMath::Lerp(A.Surface, B.Surface, Alpha);
				}
			}

			return Place;
		}

		/** Returns true if a position stands above the water */
		bool IsAboveWater(const FVector2D& Position) const
		{
			const FHexCoord Hex = FHexLayout(Params.HexRadius).WorldToHex(Position);

			if (!Map.Contains(Hex))
			{
				return false;
			}

			// the base of two meshes reaches into the notch between them, where it comes out of the water short of its edge
			if (IsInJoinedNotch(Position, Params.Tuning.JoinedWaterInset))
			{
				return true;
			}

			if (IsTerrainHidden(Hex))
			{
				return false;
			}

			// the terrain sinks under a joined mesh, which stands above the water itself, except near a side towards water,
			// where its edge follows the seabed down and the water covers it
			if (FindJoinedSurface(Hex))
			{
				const FVector2D FromCenter = Position - FHexLayout(Params.HexRadius).HexToWorld(Hex);

				for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
				{
					const FHexCoord Neighbor = Hex.GetNeighbor(Direction);

					if (Map.Contains(Neighbor) && Map.GetLevel(Neighbor) < 0)
					{
						const FVector2D SideNormal = (FHexLayout::GetCornerDirection(Direction) + FHexLayout::GetCornerDirection(Direction + 1)).GetSafeNormal();

						if (Params.HexRadius * UE_HALF_SQRT_3 - FVector2D::DotProduct(FromCenter, SideNormal) < Params.Tuning.JoinedWaterInset)
						{
							return false;
						}
					}
				}

				return true;
			}

			return GetSurface(Position) > Params.WaterHeight;
		}

		/** Returns true if a position lies on a joined mesh within Reach of a side towards water, where the water breaks against it as against a cliff */
		bool IsJoinedShore(const FVector2D& Position, double Reach) const
		{
			const FHexCoord Hex = FHexLayout(Params.HexRadius).WorldToHex(Position);

			if (!FindJoinedSurface(Hex))
			{
				return Map.Contains(Hex) && Map.GetLevel(Hex) < 0 && IsInJoinedNotch(Position, Params.Tuning.JoinedWaterInset);
			}

			const FVector2D FromCenter = Position - FHexLayout(Params.HexRadius).HexToWorld(Hex);

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FHexCoord Neighbor = Hex.GetNeighbor(Direction);

				if (Map.Contains(Neighbor) && Map.GetLevel(Neighbor) < 0)
				{
					const FVector2D SideNormal = (FHexLayout::GetCornerDirection(Direction) + FHexLayout::GetCornerDirection(Direction + 1)).GetSafeNormal();

					if (Params.HexRadius * UE_HALF_SQRT_3 - FVector2D::DotProduct(FromCenter, SideNormal) < Reach)
					{
						return true;
					}
				}
			}

			return false;
		}

		/** Returns how far the ground lies above the water, below it when negative. A joined mesh only counts as above or below, as it is not terrain */
		double GetHeightAboveWater(const FVector2D& Position) const
		{
			const FHexCoord Hex = FHexLayout(Params.HexRadius).WorldToHex(Position);

			if (!Map.Contains(Hex))
			{
				return -FHexShoreField::HeightRange;
			}

			if (FindJoinedSurface(Hex) || IsInJoinedNotch(Position, 0.0))
			{
				return IsAboveWater(Position) ? FHexShoreField::HeightRange : -Params.Tuning.JoinedShoreDepth;
			}

			return GetSurface(Position) - Params.WaterHeight;
		}

		/** Returns true if a position lies in a hex without a joined mesh, in the notch between two neighbors with the same one, whose base covers it, at least Inset inside its far edge */
		bool IsInJoinedNotch(const FVector2D& Position, double Inset) const
		{
			const FHexCoord Hex = FHexLayout(Params.HexRadius).WorldToHex(Position);

			if (!Map.Contains(Hex) || FindJoinedSurface(Hex))
			{
				return false;
			}

			const FVector2D Center = FHexLayout(Params.HexRadius).HexToWorld(Hex);

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FHexLevelSurface* Surface = FindJoinedSurface(Hex.GetNeighbor(Direction));

				if (Surface && Surface == FindJoinedSurface(Hex.GetNeighbor(Direction + 1)))
				{
					const FVector2D CornerDirection = FHexLayout::GetCornerDirection(Direction + 1);

					if (FVector2D::DotProduct(Center + CornerDirection * Params.HexRadius - Position, CornerDirection) <= Params.HexRadius * Params.Tuning.NotchDepth - Inset)
					{
						return true;
					}
				}
			}

			return false;
		}

		/** Returns how far the seabed lies below the water, or zero on land. Under a joined mesh near water it is as deep as the seabed at its side */
		double GetWaterDepth(const FVector2D& Position) const
		{
			if (IsAboveWater(Position))
			{
				return 0.0;
			}

			const FHexCoord Hex = FHexLayout(Params.HexRadius).WorldToHex(Position);
			return FindJoinedSurface(Hex) ? Params.Tuning.JoinedShoreDepth : FMath::Max(Params.WaterHeight - GetSurface(Position), 0.0);
		}

	private:

		/** Returns true if a hex needs no terrain, as it and all its neighbors on the map are under water too deep to see through */
		bool IsTerrainHidden(const FHexCoord& Hex) const
		{
			const FHexLevelSurface* Surface = LevelSurfaces.Find(Map.GetLevel(Hex));

			if (!Surface || !Surface->bHidesTerrain)
			{
				return false;
			}

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FHexCoord Neighbor = Hex.GetNeighbor(Direction);
				const FHexLevelSurface* NeighborSurface = Map.Contains(Neighbor) ? LevelSurfaces.Find(Map.GetLevel(Neighbor)) : Surface;

				if (!NeighborSurface || !NeighborSurface->bHidesTerrain)
				{
					return false;
				}
			}

			return true;
		}

		/** Returns a position on the lattice. Whole numbers make it exactly the same for every hex that touches it */
		FVector2D GetLatticePosition(const FIntPoint& Point) const
		{
			return FVector2D((Point.X + Point.Y * 0.5) * LatticeStep, Point.Y * Sqrt3 * 0.5 * LatticeStep);
		}

		/** Returns the level of a hex, or of the closest hex on the map if it is off the map */
		int8 GetClampedLevel(const FHexCoord& Hex) const
		{
			return Map.GetLevel(FMath::Clamp(Hex.GetColumn(), 0, Map.Width - 1), FMath::Clamp(Hex.GetRow(), 0, Map.Height - 1));
		}

		/** Returns the height of the surface and optionally its color */
		double GetSurface(const FVector2D& Position, FLinearColor* OutColor = nullptr) const
		{
			const double HexRadius = Params.HexRadius;
			FVector2D Warped = Position;

			// near a joined mesh the surface loses its noise, as the edge of the mesh follows it and must be smooth
			FHexMeshFoot Foot;
			const bool bHasFoot = FindMeshFoot(Position, Foot);
			const double NoiseAmount = bHasFoot ? (Foot.bInside ? 0.0 : FMath::SmoothStep(0.0, static_cast<double>(Params.FootBlendDistance), Foot.Distance)) : 1.0;

			// the warp bends the borders between levels, so they do not follow the straight sides of the hexes
			if (Params.WarpDistance > 0.0f)
			{
				const FVector2D WarpSample = Position / (HexRadius * Params.Tuning.WarpWavelength);
				Warped.X += FMath::PerlinNoise2D(WarpSample + FVector2D(5.2, 1.3)) * Params.WarpDistance * NoiseAmount;
				Warped.Y += FMath::PerlinNoise2D(WarpSample + FVector2D(9.7, 2.8)) * Params.WarpDistance * NoiseAmount;
			}

			// find the triangle of hex centers around the position
			const double FractionalR = -Warped.X / (1.5 * HexRadius);
			const double FractionalQ = Warped.Y / (Sqrt3 * HexRadius) - FractionalR * 0.5;
			const int32 FloorQ = FMath::FloorToInt32(FractionalQ);
			const int32 FloorR = FMath::FloorToInt32(FractionalR);
			const double U = FractionalQ - FloorQ;
			const double V = FractionalR - FloorR;

			FHexCoord Hexes[3];
			double Weights[3];

			if (U + V < 1.0)
			{
				Hexes[0] = FHexCoord(FloorQ, FloorR);
				Hexes[1] = FHexCoord(FloorQ + 1, FloorR);
				Hexes[2] = FHexCoord(FloorQ, FloorR + 1);
				Weights[0] = 1.0 - U - V;
				Weights[1] = U;
				Weights[2] = V;
			}
			else
			{
				Hexes[0] = FHexCoord(FloorQ + 1, FloorR + 1);
				Hexes[1] = FHexCoord(FloorQ, FloorR + 1);
				Hexes[2] = FHexCoord(FloorQ + 1, FloorR);
				Weights[0] = U + V - 1.0;
				Weights[1] = 1.0 - U;
				Weights[2] = 1.0 - V;
			}

			// a power above one flattens the surface around hex centers and keeps it smooth across the triangles
			// Land next to a joined mesh is blended without that mesh, which a high power would squeeze into a steep step towards its center
			double JoinedShare = 0.0;

			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FHexLevelSurface* Surface = LevelSurfaces.Find(GetClampedLevel(Hexes[Index]));
				JoinedShare += Surface && Surface->bHasJoinedMesh ? Weights[Index] : 0.0;
			}

			const double BlendPower = FMath::Lerp(static_cast<double>(Params.BlendPower), 1.0, FMath::SmoothStep(0.0, 1.0 / 3.0, JoinedShare));
			double TotalWeight = 0.0;

			for (double& Weight : Weights)
			{
				Weight = FMath::Pow(Weight, BlendPower);
				TotalWeight += Weight;
			}

			double LandWeight = 0.0;
			double Height = 0.0;
			double BumpHeight = 0.0;
			double RidgeHeight = 0.0;
			FLinearColor Color = FLinearColor::Transparent;
			double JoinedWeight = 0.0;
			// the nearest hex without a joined mesh decides whether a mesh here stands by the water. It is not the hex of the position,
			// as a point on the side of a mesh can belong to the mesh, and the seabed there must rise just as much
			int8 NearestOtherLevel = 0;
			double NearestOtherWeight = -1.0;

			for (int32 Index = 0; Index < 3; ++Index)
			{
				const int8 Level = GetClampedLevel(Hexes[Index]);
				const FHexLevelSurface* Surface = LevelSurfaces.Find(Level);

				if (!(Surface && Surface->bHasJoinedMesh) && Weights[Index] > NearestOtherWeight)
				{
					NearestOtherLevel = Level;
					NearestOtherWeight = Weights[Index];
				}
			}

			for (int32 Index = 0; Index < 3; ++Index)
			{
				const double Weight = Weights[Index] / TotalWeight;
				const int8 Level = GetClampedLevel(Hexes[Index]);
				const FHexLevelSurface* Surface = LevelSurfaces.Find(Level);

				// the mesh of such a level brings its own height, so it adds nothing here and the land next to it keeps its height up to the mesh
				if (Surface && Surface->bHasJoinedMesh)
				{
					Color += Surface->Color * static_cast<float>(Weight);
					JoinedWeight += Weight;
					continue;
				}

				LandWeight += Weight;
				Height += Weight * Level * Params.LevelHeight;

				if (Surface)
				{
					BumpHeight += Weight * Surface->BumpHeight;
					RidgeHeight += Weight * Surface->RidgeHeight;
					Color += Surface->Color * static_cast<float>(Weight);
				}
			}

			if (LandWeight > UE_KINDA_SMALL_NUMBER)
			{
				Height /= LandWeight;
				BumpHeight /= LandWeight;
				RidgeHeight /= LandWeight;
			}
			else
			{
				// the warp can reach only meshes from land between them, which must not fall to their foot
				const FHexCoord OwnHex = FHexLayout(HexRadius).WorldToHex(Position);
				const FHexLevelSurface* Joined = FindJoinedSurface(OwnHex);
				Height = Joined || !Map.Contains(OwnHex) ? (Joined ? Joined->FootHeight : 0.0) : Map.GetLevel(OwnHex) * Params.LevelHeight;
			}

			// the seabed rises to just under the water at the side of a joined mesh, whose slope then goes on down under the water,
			// and falls to its own depth by the middle of the hex, so the water shows how deep it gets
			if (JoinedWeight > 0.0 && NearestOtherWeight >= 0.0 && NearestOtherLevel < 0)
			{
				const double TowardsMesh = FMath::Pow(FMath::Min(JoinedWeight * 2.0, 1.0), Params.Tuning.JoinedSeabedCurve);
				Height = FMath::Lerp(Height, static_cast<double>(Params.WaterHeight) - Params.Tuning.JoinedShoreDepth, TowardsMesh);
			}

			const FVector2D ReliefSample = Position / (HexRadius * Params.Tuning.ReliefWavelength);

			if (BumpHeight > 0.0)
			{
				Height += BumpHeight * GetBumpNoise(ReliefSample, Params.Tuning) * NoiseAmount;
			}

			if (RidgeHeight > 0.0)
			{
				Height += RidgeHeight * GetRidgeNoise(ReliefSample, Params.Tuning) * NoiseAmount;
			}

			const double RiverWater = ApplyRivers(Position, Height);

			// land keeps its own height right up to a joined mesh, whose edge comes to it. A cliff has the last word, as its mesh lies on this
			ApplyCoast(Position, Height);

			// alpha is how much of a river covers the place, which M_HexTerrain draws
			if (OutColor)
			{
				*OutColor = FLinearColor(Color.R, Color.G, Color.B, static_cast<float>(RiverWater));
			}

			return Height;
		}

		/**
		 *  Lays the land under a river into a flat floor below its water, and its bank straight up from the foot to its top, RiverBankHeight above
		 *  the water on both sides, from where the land goes back to its own height over RiverBankSettle, so the bank is as high everywhere.
		 *  The seabed past a mouth keeps its height and color, as the sea covers the river there. Returns how much of the bed a place is,
		 *  from 1 under the water to RiverBankTopColor at the top of the bank, and falling on to 0 over RiverBankColorReach past it
		 */
		double ApplyRivers(const FVector2D& Position, double& Height) const
		{
			const FRiverPlace Place = FindRiver(Position);

			if (!Place.bFound || Place.Distance >= Place.Top + Params.Tuning.RiverBankSettle)
			{
				return 0.0;
			}

			// how far the ground stands out of the sea, from the seabed past a mouth, which the river leaves alone, to land
			const double Dry = FMath::Clamp((Height - Params.WaterHeight + Params.RiverBankHeight) / FMath::Max(Params.RiverBankHeight, 0.1), 0.0, 1.0);

			if (Dry <= 0.0)
			{
				return 0.0;
			}

			// land at the top of the bank lies at one height above the water, also where the ground falls faster than the water down a step
			const double Bank = Place.Surface + Params.RiverBankHeight;
			const double Land = FMath::Lerp(Height, Bank, Dry);

			// a flat floor out to the foot of the bank, and its face straight up from there to the land at its top, which AddFineTriangle splits the terrain at,
			// both rounded where they meet the face (RiverBankRounding), so the bank does not break at a hard angle
			const double Floor = Place.Surface - Params.RiverDepth;
			const double Face = Floor + (Place.Distance - Place.Foot) / (Place.Top - Place.Foot) * (Land - Floor);
			const double Top = SmoothMin(Face, Land, Params.Tuning.RiverBankRounding);

			if (Place.Distance >= Place.Top)
			{
				const double Settle = FMath::SmoothStep(0.0, 1.0, (Place.Distance - Place.Top) / Params.Tuning.RiverBankSettle);
				Height = FMath::Lerp(Top, Height, Settle);

				// the color goes on falling with the distance past the top, evenly enough for any triangle to draw it, and M_HexTerrain ends the sand
				// on the land at a winding line from it
				return FMath::Max(0.0, 1.0 - (Place.Distance - Place.Top) / Params.Tuning.RiverBankColorReach) * Params.Tuning.RiverBankTopColor * Dry;
			}

			const double Rise = Place.Distance <= Place.Foot ? 0.0 : (Place.Distance - Place.Foot) / (Place.Top - Place.Foot);
			const double Cut = FMath::Min(-SmoothMin(-Floor, -Top, Params.Tuning.RiverBankRounding), FMath::Max(Height, Land));
			Height = FMath::Lerp(Height, Cut, Dry);
			return FMath::Lerp(1.0, Params.Tuning.RiverBankTopColor, Rise) * Dry;
		}

		/** Makes the terrain follow the nearest cliff: flat towards the top of it, then falling behind its mesh to the seabed */
		void ApplyCoast(const FVector2D& Position, double& Height) const
		{
			if (!Cliffs)
			{
				return;
			}

			const TArray<FIntPoint>* Parts = Cliffs->Nearby.Find(FHexLayout(Params.HexRadius).WorldToHex(Position));

			if (!Parts)
			{
				return;
			}

			double Nearest = TNumericLimits<double>::Max();
			double Side = 0.0;
			double Top = 0.0;
			double Ground = 0.0;
			double Strength = 0.0;

			for (const FIntPoint& Part : *Parts)
			{
				const FHexCliffChain& Chain = Cliffs->Chains[Part.X];
				const FHexCliffRing& A = Chain.Rings[Part.Y];
				const FHexCliffRing& B = Chain.Rings[(Part.Y + 1) % Chain.Rings.Num()];
				const FVector2D Along = B.Position - A.Position;
				const double Alpha = FMath::Clamp(FVector2D::DotProduct(Position - A.Position, Along) / FMath::Max(Along.SizeSquared(), UE_KINDA_SMALL_NUMBER), 0.0, 1.0);
				const FVector2D Closest = A.Position + Along * Alpha;
				const double Distance = FVector2D::Distance(Position, Closest);

				if (Distance < Nearest)
				{
					Nearest = Distance;
					Side = FVector2D::DotProduct(Position - Closest, FMath::Lerp(A.Outward, B.Outward, Alpha));
					Top = FMath::Lerp(A.Top, B.Top, Alpha);
					Ground = FMath::Lerp(A.Ground, B.Ground, Alpha);
					Strength = FMath::Lerp(A.Strength, B.Strength, Alpha);
				}
			}

			const double Signed = Side < 0.0 ? -Nearest : Nearest;
			const double Seabed = static_cast<double>(Params.WaterHeight) - Params.CliffSeabedDepth;
			double Shaped;

			if (Signed < -Params.Tuning.CliffInlandRamp)
			{
				// level with the top of the mesh until a whole triangle past its inner edge, or it would poke through the mesh.
				// It is back at its own height before FHexCliffData::Nearby ends, which would otherwise leave a step
				const double Past = -Signed - Params.Tuning.CliffInlandRamp - Params.Tuning.CliffLipWidth - 6.0 - LatticeStep;
				Shaped = FMath::Lerp(Top, FMath::Max(Height, Ground), FMath::SmoothStep(0.0, Params.Tuning.CliffLandBlend, Past));

				// under the top of the mesh it stays a little lower, or it shows through as shards
				Shaped -= Params.Tuning.CliffLipClearance * FMath::SmoothStep(0.0, Params.Tuning.CliffLipWidth * 0.8, Signed + Params.Tuning.CliffInlandRamp + Params.Tuning.CliffLipWidth);
			}
			else if (Signed < 0.0)
			{
				Shaped = FMath::Lerp(Top - Params.Tuning.CliffLipClearance, Seabed, (Signed + Params.Tuning.CliffInlandRamp) / Params.Tuning.CliffInlandRamp);
			}
			else
			{
				// other land can lie on the water side of a coast, past the end of a cliff or across a strait, and it keeps its height
				const FHexCoord OwnHex = FHexLayout(Params.HexRadius).WorldToHex(Position);
				const bool bOverLand = Map.Contains(OwnHex) && Map.GetLevel(OwnHex) >= 0;
				Shaped = bOverLand ? Height : FMath::Min(Height, Seabed);
			}

			Height = FMath::Lerp(Height, Shaped, Strength);
		}

		/** Returns the surface of a level with a joined mesh at this hex, if there is one */
		const FHexLevelSurface* FindJoinedSurface(const FHexCoord& Hex) const
		{
			if (!Map.Contains(Hex))
			{
				return nullptr;
			}

			const FHexLevelSurface* Surface = LevelSurfaces.Find(Map.GetLevel(Hex));
			return Surface && Surface->bHasJoinedMesh ? Surface : nullptr;
		}

		/** Returns the distance from a position to one side of a hex */
		double GetDistanceToSide(const FVector2D& Position, const FVector2D& HexCenter, int32 Direction) const
		{
			const FVector2D CornerA = HexCenter + FHexLayout::GetCornerDirection(Direction) * Params.HexRadius;
			const FVector2D CornerB = HexCenter + FHexLayout::GetCornerDirection(Direction + 1) * Params.HexRadius;
			return FVector2D::Distance(Position, FMath::ClosestPointOnSegment2D(Position, CornerA, CornerB));
		}

		/** Finds the joined mesh that affects a position. Uses the true position, as the edge of the mesh does not follow the warp */
		bool FindMeshFoot(const FVector2D& Position, FHexMeshFoot& OutFoot) const
		{
			if (Params.FootBlendDistance <= 0.0f || !bAnyJoinedLevel)
			{
				return false;
			}

			const FHexLayout Layout(Params.HexRadius);
			const FHexCoord Hex = Layout.WorldToHex(Position);
			const FVector2D Center = Layout.HexToWorld(Hex);
			OutFoot.bInside = false;
			OutFoot.Surface = FindJoinedSurface(Hex);

			// inside a hex with a mesh the nearest side counts, whatever is behind it
			if (OutFoot.Surface)
			{
				OutFoot.bInside = true;
				OutFoot.Height = OutFoot.Surface->FootHeight;
				OutFoot.Distance = TNumericLimits<double>::Max();

				for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
				{
					OutFoot.Distance = FMath::Min(OutFoot.Distance, GetDistanceToSide(Position, Center, Direction));
				}

				return true;
			}

			OutFoot.Distance = Params.FootBlendDistance;
			bool bFound = false;

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				if (const FHexLevelSurface* Surface = FindJoinedSurface(Hex.GetNeighbor(Direction)))
				{
					const double Distance = GetDistanceToSide(Position, Center, Direction);

					if (Distance < OutFoot.Distance)
					{
						OutFoot.Distance = Distance;
						OutFoot.Surface = Surface;
						OutFoot.Height = Surface->FootHeight;
						bFound = true;
					}
				}
			}

			// where two neighbors with meshes meet, a notch mesh covers the corner of this hex
			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FHexLevelSurface* Surface = FindJoinedSurface(Hex.GetNeighbor(Direction));

				if (!Surface || Surface != FindJoinedSurface(Hex.GetNeighbor(Direction + 1)))
				{
					continue;
				}

				const FVector2D CornerDirection = FHexLayout::GetCornerDirection(Direction + 1);
				const FVector2D Corner = Center + CornerDirection * Params.HexRadius;
				const FVector2D EdgeA = Corner + (FHexLayout::GetCornerDirection(Direction) - CornerDirection) * Params.HexRadius * 0.5;
				const FVector2D EdgeB = Corner + (FHexLayout::GetCornerDirection(Direction + 2) - CornerDirection) * Params.HexRadius * 0.5;

				const double Along = FVector2D::DotProduct(Position - Corner, -CornerDirection);
				const double Distance = Along <= Params.HexRadius * Params.Tuning.NotchDepth ? 0.0 : FVector2D::Distance(Position, FMath::ClosestPointOnSegment2D(Position, EdgeA, EdgeB));

				if (Distance < OutFoot.Distance)
				{
					OutFoot.Distance = Distance;
					OutFoot.Surface = Surface;
					OutFoot.Height = Surface->FootHeight;
					bFound = true;
				}
			}

			return bFound;
		}

		/** Returns the index of a surface vertex, and creates it the first time */
		/** Returns a position on the fine lattice, which is RiverSplit times finer than the lattice and holds the points of split triangles */
		FVector2D GetFinePosition(const FIntPoint& Fine) const
		{
			const double FineStep = LatticeStep / Params.Tuning.RiverSplit;
			return FVector2D((Fine.X + Fine.Y * 0.5) * FineStep, Fine.Y * Sqrt3 * 0.5 * FineStep);
		}

		/** Returns the index of a surface vertex at a point of the fine lattice, and creates it the first time */
		int32 GetFineVertex(const FIntPoint& Fine)
		{
			if (const int32* Existing = SurfaceVertices.Find(Fine))
			{
				return *Existing;
			}

			const bool bLattice = Fine.X % Params.Tuning.RiverSplit == 0 && Fine.Y % Params.Tuning.RiverSplit == 0;
			FVector2D Position = GetFinePosition(Fine);

			// a point near the top or the foot of a bank, also of the lattice, which every chunk moves alike, moves onto it, so the edge of the bank runs
			// along the river and not in steps of the fine lattice. It moves less than half a fine step, so the triangles around it keep their order
			const bool bSnapped = SnapToBank(Position);

			// the slope comes from the height itself, so it does not depend on which triangles surround the vertex. A point of the lattice measures it over
			// a lattice step, as every chunk does, and a point on a bank over a tiny step, which takes the rounded slope of the bank right where it lies
			const double Step = bLattice && !bSnapped ? LatticeStep : Params.Tuning.RiverBankNormalStep;
			FLinearColor Color;
			const double Height = GetSurface(Position, &Color);
			const double SlopeX = GetSurface(Position + FVector2D(Step, 0.0)) - GetSurface(Position - FVector2D(Step, 0.0));
			const double SlopeY = GetSurface(Position + FVector2D(0.0, Step)) - GetSurface(Position - FVector2D(0.0, Step));
			const FVector Normal = FVector(-SlopeX, -SlopeY, 2.0 * Step).GetSafeNormal();

			const int32 Index = AddVertex(FVector(Position.X, Position.Y, Height), Normal, Color);
			SurfaceVertices.Add(Fine, Index);
			return Index;
		}

		/** Moves a position near the top or the foot of a bank onto it, and returns whether it moved */
		bool SnapToBank(FVector2D& Position) const
		{
			const FRiverPlace Place = FindRiver(Position);

			if (!Place.bFound)
			{
				return false;
			}

			const double Reach = LatticeStep / Params.Tuning.RiverSplit * Params.Tuning.RiverBankSnap;

			for (const double Line : { Place.Top, Place.Foot })
			{
				if (FMath::Abs(Place.Distance - Line) < Reach)
				{
					Position = Place.Middle + Place.Landward * Line;
					return true;
				}
			}

			return false;
		}

		/** Returns the index of a surface vertex at a point of the lattice */
		int32 GetSurfaceVertex(const FIntPoint& Point)
		{
			return GetFineVertex(Point * Params.Tuning.RiverSplit);
		}

		/** Returns a vertex in the middle of an edge that is not split, which lies on that edge, so the triangle on its other side meets it without a crack */
		int32 GetEdgeMiddleVertex(const FIntPoint& Fine, int32 A, int32 B)
		{
			if (const int32* Existing = SurfaceVertices.Find(Fine))
			{
				return *Existing;
			}

			const int32 Index = AddVertex((Mesh.Vertices[A] + Mesh.Vertices[B]) * 0.5, (Mesh.Normals[A] + Mesh.Normals[B]).GetSafeNormal(), (Mesh.Colors[A] + Mesh.Colors[B]) * 0.5f);
			SurfaceVertices.Add(Fine, Index);
			return Index;
		}

		/** Returns which part of a river a point of the fine lattice lies in: 0 on its floor, 1 on the face of its bank and 2 on land */
		int32 GetRiverPart(const FIntPoint& Fine) const
		{
			if (const int32* Known = RiverParts.Find(Fine))
			{
				return *Known;
			}

			const FRiverPlace Place = FindRiver(GetFinePosition(Fine));
			const int32 Part = !Place.bFound || Place.Distance >= Place.Top ? 2 : Place.Distance > Place.Foot ? 1 : 0;
			RiverParts.Add(Fine, Part);
			return Part;
		}

		/**
		 *  Returns true if an edge crosses the top or the foot of a bank or lies on its face, where the terrain cannot follow the river with a straight edge.
		 *  The answer depends only on the edge, so the triangles on both sides of it split it alike
		 */
		bool NeedsSplit(const FIntPoint& A, const FIntPoint& B) const
		{
			if (!Rivers || (FMath::Abs(A.X - B.X) <= 1 && FMath::Abs(A.Y - B.Y) <= 1))
			{
				return false;
			}

			const int32 PartA = GetRiverPart(A);
			const int32 PartB = GetRiverPart(B);
			const int32 PartMiddle = GetRiverPart((A + B) / 2);
			return PartA != PartB || PartA != PartMiddle || PartA == 1;
		}

		/**
		 *  Adds a triangle of the fine lattice, split into four as long as a river passes through it, so the terrain falls sharply at the line of a bank
		 *  and stays coarse everywhere else. An edge that is not split keeps its middle on the edge and its halves are never split, so they meet a triangle
		 *  that is not split without a crack. Locked edges are halves of such an edge
		 */
		void AddFineTriangle(const FIntPoint& A, const FIntPoint& B, const FIntPoint& C, int32 VertexA, int32 VertexB, int32 VertexC, bool bLockedAB, bool bLockedBC, bool bLockedCA)
		{
			const bool bSplitAB = !bLockedAB && NeedsSplit(A, B);
			const bool bSplitBC = !bLockedBC && NeedsSplit(B, C);
			const bool bSplitCA = !bLockedCA && NeedsSplit(C, A);

			if (!bSplitAB && !bSplitBC && !bSplitCA)
			{
				AddTriangle(VertexA, VertexB, VertexC, FVector::UpVector);
				return;
			}

			const FIntPoint AB = (A + B) / 2;
			const FIntPoint BC = (B + C) / 2;
			const FIntPoint CA = (C + A) / 2;
			const int32 VertexAB = bSplitAB ? GetFineVertex(AB) : GetEdgeMiddleVertex(AB, VertexA, VertexB);
			const int32 VertexBC = bSplitBC ? GetFineVertex(BC) : GetEdgeMiddleVertex(BC, VertexB, VertexC);
			const int32 VertexCA = bSplitCA ? GetFineVertex(CA) : GetEdgeMiddleVertex(CA, VertexC, VertexA);

			AddFineTriangle(A, AB, CA, VertexA, VertexAB, VertexCA, !bSplitAB, false, !bSplitCA);
			AddFineTriangle(AB, B, BC, VertexAB, VertexB, VertexBC, !bSplitAB, !bSplitBC, false);
			AddFineTriangle(CA, BC, C, VertexCA, VertexBC, VertexC, false, !bSplitBC, !bSplitCA);
			AddFineTriangle(AB, BC, CA, VertexAB, VertexBC, VertexCA, false, false, false);
		}

		/** Adds a triangle of the lattice, which is split where a river passes through it */
		void AddLatticeTriangle(const FIntPoint& A, const FIntPoint& B, const FIntPoint& C)
		{
			AddFineTriangle(A * Params.Tuning.RiverSplit, B * Params.Tuning.RiverSplit, C * Params.Tuning.RiverSplit, GetSurfaceVertex(A), GetSurfaceVertex(B), GetSurfaceVertex(C), false, false, false);
		}

		/** Fills the triangle between the center of a hex and two of its corners */
		void AddSector(const FIntPoint& Center, const FIntPoint& StepA, const FIntPoint& StepB)
		{
			const int32 Subdivisions = Params.Subdivisions;

			for (int32 A = 0; A < Subdivisions; ++A)
			{
				for (int32 B = 0; B < Subdivisions - A; ++B)
				{
					const FIntPoint Corner = Center + StepA * A + StepB * B;
					AddLatticeTriangle(Corner, Corner + StepA, Corner + StepB);

					if (A + B < Subdivisions - 1)
					{
						AddLatticeTriangle(Corner + StepA, Corner + StepA + StepB, Corner + StepB);
					}
				}
			}
		}

		/** Adds the side of the terrain below one edge of a hex */
		void AddWall(const FIntPoint& Start, const FIntPoint& Step)
		{
			for (int32 Segment = 0; Segment < Params.Subdivisions; ++Segment)
			{
				const FVector2D PositionA = GetLatticePosition(Start + Step * Segment);
				const FVector2D PositionB = GetLatticePosition(Start + Step * (Segment + 1));

				FLinearColor ColorA;
				FLinearColor ColorB;
				const double HeightA = GetSurface(PositionA, &ColorA);
				const double HeightB = GetSurface(PositionB, &ColorB);

				// corners go clockwise as seen from above, so the side away from the hex is to the left of the edge
				const FVector2D Along = PositionB - PositionA;
				const FVector Outward = FVector(Along.Y, -Along.X, 0.0).GetSafeNormal();

				const int32 TopA = AddVertex(FVector(PositionA.X, PositionA.Y, HeightA), Outward, ColorA);
				const int32 TopB = AddVertex(FVector(PositionB.X, PositionB.Y, HeightB), Outward, ColorB);
				const int32 BottomB = AddVertex(FVector(PositionB.X, PositionB.Y, Params.BottomHeight), Outward, ColorB);
				const int32 BottomA = AddVertex(FVector(PositionA.X, PositionA.Y, Params.BottomHeight), Outward, ColorA);

				AddTriangle(TopA, TopB, BottomB, Outward);
				AddTriangle(TopA, BottomB, BottomA, Outward);
			}
		}

		void AddTriangle(int32 A, int32 B, int32 C, const FVector& FacingHint)
		{
			const FVector& PositionA = Mesh.Vertices[A];
			const FVector& PositionB = Mesh.Vertices[B];
			const FVector& PositionC = Mesh.Vertices[C];

			// the renderer shows the side where (C - A) x (B - A) points, so swap the winding if it would face away
			const FVector Normal = FVector::CrossProduct(PositionC - PositionA, PositionB - PositionA);
			const bool bFlip = FVector::DotProduct(Normal, FacingHint) < 0.0;

			Mesh.Triangles.Add(A);
			Mesh.Triangles.Add(bFlip ? C : B);
			Mesh.Triangles.Add(bFlip ? B : C);
		}

		int32 AddVertex(const FVector& Position, const FVector& Normal, const FLinearColor& Color)
		{
			Mesh.Normals.Add(Normal);
			Mesh.Colors.Add(Color);
			return Mesh.Vertices.Add(Position);
		}

		const FHexMapData& Map;
		const FHexMeshParams& Params;
		const TMap<int8, FHexLevelSurface>& LevelSurfaces;
		FHexMeshData& Mesh;
		const FHexCliffData* Cliffs;
		const FHexRiverData* Rivers;
		double LatticeStep;
		bool bAnyJoinedLevel = false;

		/** Surface vertices of this part of the terrain by their position on the fine lattice */
		TMap<FIntPoint, int32> SurfaceVertices;

		/** Which part of a river each point of the fine lattice that was asked about lies in, see GetRiverPart */
		mutable TMap<FIntPoint, int32> RiverParts;	};
}

FTransform HexMeshBuilder::GetFeatureTransform(const FHexCoord& Hex, const FBox& MeshBounds, float HexRadius, float Width, float Height, double BottomZ)
{
	const FVector MeshSize = MeshBounds.GetSize();
	const FVector Scale(
		Width * HexRadius / FMath::Max(MeshSize.X, UE_KINDA_SMALL_NUMBER),
		Width * HexRadius / FMath::Max(MeshSize.Y, UE_KINDA_SMALL_NUMBER),
		Height / FMath::Max(MeshSize.Z, UE_KINDA_SMALL_NUMBER));

	// turns of 60 degrees keep the mesh lined up with the sides of the hex
	const FQuat Rotation(FVector::UpVector, FMath::DegreesToRadians(60.0 * (GetTypeHash(Hex) % FHexCoord::NumDirections)));

	const FVector2D Center = FHexLayout(HexRadius).HexToWorld(Hex);
	const FVector MeshBase(MeshBounds.GetCenter().X, MeshBounds.GetCenter().Y, MeshBounds.Min.Z);

	return FTransform(Rotation, FVector(Center.X, Center.Y, BottomZ) - Rotation.RotateVector(MeshBase * Scale), Scale);
}

int32 HexMeshBuilder::GetJoinedExtraTurns(int32 NeighborMask, const FHexCoord& Hex)
{
	int32 Period = FHexCoord::NumDirections;

	for (int32 Turns = 1; Turns < FHexCoord::NumDirections; ++Turns)
	{
		if ((((NeighborMask << Turns) | (NeighborMask >> (FHexCoord::NumDirections - Turns))) & 63) == NeighborMask)
		{
			Period = Turns;
			break;
		}
	}

	return HashCombine(GetTypeHash(Hex.Q * 31), GetTypeHash(Hex.R * 17)) % (FHexCoord::NumDirections / Period) * Period;
}

FHexJoinedPiece HexMeshBuilder::GetJoinedPiece(int32 NeighborMask)
{
	const int32 AllDirections = (1 << FHexCoord::NumDirections) - 1;
	const int32 Mask = NeighborMask & AllDirections;

	FHexJoinedPiece Piece;
	Piece.MeshMask = Mask;

	// turning the mask back by some amount gives the lowest value, which is the name of the mesh
	for (int32 Turns = 1; Turns < FHexCoord::NumDirections; ++Turns)
	{
		const int32 TurnedBack = ((Mask >> Turns) | (Mask << (FHexCoord::NumDirections - Turns))) & AllDirections;

		if (TurnedBack < Piece.MeshMask)
		{
			Piece.MeshMask = TurnedBack;
			Piece.Turns = Turns;
		}
	}

	return Piece;
}

FTransform HexMeshBuilder::GetJoinedTransform(const FHexCoord& Hex, int32 Turns, float HexRadius, float MeshRadius, double BaseZ)
{
	const FQuat Rotation(FVector::UpVector, FMath::DegreesToRadians(60.0 * Turns));
	const FVector2D Center = FHexLayout(HexRadius).HexToWorld(Hex);

	return FTransform(Rotation, FVector(Center.X, Center.Y, BaseZ), FVector(HexRadius / MeshRadius));
}

bool HexMeshBuilder::IsCliffSide(int32 LevelA, int32 LevelB, int32 MinDifference)
{
	return MinDifference > 0 && (LevelA < 0) != (LevelB < 0) && FMath::Abs(LevelA - LevelB) >= MinDifference;
}

void HexMeshBuilder::BuildCliffs(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, FHexCliffData& OutCliffs)
{
	if (Params.CliffMinDifference <= 0)
	{
		return;
	}

	// every side with a cliff, walking around its land, so the water is on the left
	struct FCoastEdge
	{
		FCoastPoint Start;
		FIntPoint EndKey = FIntPoint::ZeroValue;
		FVector2D End = FVector2D::ZeroVector;
		bool bVisited = false;
	};

	const FHexLayout Layout(Params.HexRadius);
	const auto GetKey = [](const FVector2D& Position)
	{
		return FIntPoint(FMath::RoundToInt32(Position.X), FMath::RoundToInt32(Position.Y));
	};

	TArray<FCoastEdge> Edges;
	TMap<FIntPoint, int32> EdgeByStart;
	TSet<FIntPoint> Ends;

	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		for (int32 Column = 0; Column < Map.Width; ++Column)
		{
			const FHexCoord Hex = FHexCoord::FromOffset(Column, Row);
			const int32 Level = Map.GetLevel(Hex);

			const FHexLevelSurface* Surface = LevelSurfaces.Find(static_cast<int8>(Level));

			// a joined mesh has no cliff and runs on under the water, as its edge follows the seabed of the hex next to it
			if (Level < 0 || (Surface && Surface->bHasJoinedMesh))
			{
				continue;
			}

			const FVector2D Center = Layout.HexToWorld(Hex);

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FHexCoord Neighbor = Hex.GetNeighbor(Direction);

				if (Map.Contains(Neighbor) && IsCliffSide(Level, Map.GetLevel(Neighbor), Params.CliffMinDifference))
				{
					FCoastEdge Edge;
					Edge.Start.Position = Center + FHexLayout::GetCornerDirection(Direction) * Params.HexRadius;
					Edge.Start.Top = Surface ? Surface->CliffTop : 0.0;
					Edge.Start.Ground = Level * Params.LevelHeight;
					Edge.Start.Cell = FIntPoint(Column, Row);
					Edge.End = Center + FHexLayout::GetCornerDirection(Direction + 1) * Params.HexRadius;
					Edge.EndKey = GetKey(Edge.End);
					EdgeByStart.Add(GetKey(Edge.Start.Position), Edges.Num());
					Ends.Add(Edge.EndKey);
					Edges.Add(Edge);
				}
			}
		}
	}

	const auto TraceChain = [&](int32 First)
	{
		TArray<FCoastPoint> Points;
		FVector2D LastEnd = FVector2D::ZeroVector;
		double LastTop = 0.0;
		double LastGround = 0.0;
		FIntPoint LastCell = FIntPoint::ZeroValue;
		bool bClosed = false;

		for (int32 Index = First; ; )
		{
			FCoastEdge& Edge = Edges[Index];
			Edge.bVisited = true;
			Points.Add(Edge.Start);
			LastEnd = Edge.End;
			LastTop = Edge.Start.Top;
			LastGround = Edge.Start.Ground;
			LastCell = Edge.Start.Cell;

			const int32* Next = EdgeByStart.Find(Edge.EndKey);

			if (!Next)
			{
				break;
			}

			if (*Next == First)
			{
				bClosed = true;
				break;
			}

			if (Edges[*Next].bVisited)
			{
				break;
			}

			Index = *Next;
		}

		if (!bClosed)
		{
			FCoastPoint Last;
			Last.Position = LastEnd;
			Last.Top = LastTop;
			Last.Ground = LastGround;
			Last.Cell = LastCell;
			Points.Add(Last);
		}

		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			Points = SmoothCoast(Points, bClosed);
		}

		TArray<FCoastPoint> Coast = ResampleCoast(Points, bClosed, Params.Tuning);
		const int32 Num = Coast.Num();

		const auto GetOutward = [&Coast, Num, bClosed](int32 Index)
		{
			const FVector2D Before = Coast[bClosed ? (Index + Num - 1) % Num : FMath::Max(Index - 1, 0)].Position;
			const FVector2D After = Coast[bClosed ? (Index + 1) % Num : FMath::Min(Index + 1, Num - 1)].Position;
			const FVector2D Along = (After - Before).GetSafeNormal();
			return FVector2D(Along.Y, -Along.X);
		};

		FHexCliffChain Chain;
		Chain.bClosed = bClosed;
		Chain.Rings.SetNum(Num);

		for (int32 Index = 0; Index < Num; ++Index)
		{
			const double FromEnd = bClosed ? Params.Tuning.CliffEndLength : FMath::Min(Index, Num - 1 - Index) * Params.Tuning.CliffRingSpacing;
			FHexCliffRing& Ring = Chain.Rings[Index];
			Ring.Strength = FMath::SmoothStep(0.0, Params.Tuning.CliffEndLength, FromEnd);
			Ring.Cell = Coast[Index].Cell;

			// the top changes slowly along the coast, where it goes from one level to another
			double TopSum = 0.0;
			const int32 Window = 6;
			int32 TopCount = 0;

			for (int32 Offset = -Window; Offset <= Window; ++Offset)
			{
				const int32 Other = bClosed ? (Index + Offset + Num) % Num : Index + Offset;

				if (Other >= 0 && Other < Num)
				{
					TopSum += Coast[Other].Top;
					++TopCount;
				}
			}

			Ring.Top = TopSum / TopCount;
			Ring.Ground = Coast[Index].Ground;

			const FVector2D Position = Coast[Index].Position;
			const double Noise = FMath::PerlinNoise2D(Position / Params.Tuning.CoastNoiseScale + FVector2D(2.2, 7.1))
				+ FMath::PerlinNoise2D(Position / Params.Tuning.CoastNoiseFineScale + FVector2D(6.6, 1.9)) * Params.Tuning.CoastNoiseFine;
			const double Bay = FMath::Clamp(FMath::PerlinNoise2D(Position / Params.Tuning.CoastBayScale + FVector2D(11.3, 3.1)) * 2.0 + 0.5, 0.0, 1.0);
			const double Offset = (Params.Tuning.CoastMinOffset + FMath::Clamp(Noise + 0.5, 0.0, 1.0) * Params.Tuning.CoastOffsetNoise + Bay * Params.Tuning.CoastBayDepth) * Ring.Strength;
			Ring.Position = Position + GetOutward(Index) * Offset;
		}

		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector2D Before = Chain.Rings[bClosed ? (Index + Num - 1) % Num : FMath::Max(Index - 1, 0)].Position;
			const FVector2D After = Chain.Rings[bClosed ? (Index + 1) % Num : FMath::Min(Index + 1, Num - 1)].Position;
			const FVector2D Along = (After - Before).GetSafeNormal();
			Chain.Rings[Index].Outward = FVector2D(Along.Y, -Along.X);
		}

		BuildCliffMesh(Chain, Params.CliffBottomHeight, Params.Tuning);

		const int32 ChainIndex = OutCliffs.Chains.Num();
		const int32 NumParts = bClosed ? Num : Num - 1;

		for (int32 Index = 0; Index < NumParts; ++Index)
		{
			const FVector2D Middle = (Chain.Rings[Index].Position + Chain.Rings[(Index + 1) % Num].Position) * 0.5;
			const FHexCoord Hex = Layout.WorldToHex(Middle);
			OutCliffs.Nearby.FindOrAdd(Hex).Add(FIntPoint(ChainIndex, Index));

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				OutCliffs.Nearby.FindOrAdd(Hex.GetNeighbor(Direction)).Add(FIntPoint(ChainIndex, Index));
			}
		}

		OutCliffs.Chains.Add(MoveTemp(Chain));
	};

	// a line that ends in a beach starts where no other side leads to it, and whatever is left goes around in a loop
	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		if (!Ends.Contains(GetKey(Edges[Index].Start.Position)))
		{
			TraceChain(Index);
		}
	}

	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		if (!Edges[Index].bVisited)
		{
			TraceChain(Index);
		}
	}
}

void HexMeshBuilder::BuildRivers(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs, FHexRiverData& OutRivers)
{
	OutRivers = FHexRiverData();

	if (Map.RiverPaths.IsEmpty())
	{
		return;
	}

	const FHexLayout Layout(Params.HexRadius);
	const double SideDistance = Params.HexRadius * UE_HALF_SQRT_3;
	const double Width = Params.RiverWidth;

	const auto GetSideDirection = [](int32 Direction)
	{
		return (FHexLayout::GetCornerDirection(Direction) + FHexLayout::GetCornerDirection(Direction + 1)).GetSafeNormal();
	};

	for (const FHexRiverPath& Path : Map.RiverPaths)
	{
		FHexRiverLine& Line = OutRivers.Lines.AddDefaulted_GetRef();
		Line.bEndsAtMouth = Path.Joins == INDEX_NONE;

		// windings come from noise of the position, moved by the seed of the river and scaled by its own amount
		const FVector2D SeedOffset(Path.Seed * 17.31, Path.Seed * 5.77);
		const auto Meander = [&Params, &Path, &SeedOffset](const FVector2D& Position)
		{
			const FVector2D Sample = Position / (Params.HexRadius * Params.Tuning.RiverMeanderWavelength) + SeedOffset;
			return Position + FVector2D(FMath::PerlinNoise2D(Sample + FVector2D(3.1, 7.7)), FMath::PerlinNoise2D(Sample + FVector2D(8.3, 4.6))) * Params.RiverMeander * Path.Meander;
		};

		// the centers of its hexes and the middles of the sides between them
		TArray<FHexRiverPoint> Raw;

		for (int32 Index = 0; Index < Path.Hexes.Num(); ++Index)
		{
			const FHexCoord& Hex = Path.Hexes[Index];
			const FHexRiver River = Map.GetRiver(Hex);
			const bool bLast = Index + 1 == Path.Hexes.Num();
			const FVector2D Center = Layout.HexToWorld(Hex);
			Raw.Add({ Center, Width, 0.0 });

			if (!bLast || Line.bEndsAtMouth)
			{
				Raw.Add({ Center + GetSideDirection(River.Outflow) * SideDistance, Width, 0.0 });
			}

			// the mouth goes straight out into the water
			if (bLast && Line.bEndsAtMouth)
			{
				Raw.Add({ Center + GetSideDirection(River.Outflow) * (SideDistance + Params.HexRadius * Params.Tuning.RiverMouthReach), Width, 0.0 });
			}
		}

		for (int32 Index = 0; Index < Raw.Num() - (Line.bEndsAtMouth ? 2 : 0); ++Index)
		{
			Raw[Index].Position = Meander(Raw[Index].Position);
		}

		for (int32 Pass = 0; Pass < Params.Tuning.RiverSmoothPasses && Raw.Num() > 2; ++Pass)
		{
			TArray<FHexRiverPoint> Smooth = { Raw[0] };

			for (int32 Index = 0; Index + 1 < Raw.Num(); ++Index)
			{
				const FHexRiverPoint& A = Raw[Index];
				const FHexRiverPoint& B = Raw[Index + 1];
				Smooth.Add({ FMath::Lerp(A.Position, B.Position, 0.25), FMath::Lerp(A.Width, B.Width, 0.25), 0.0 });
				Smooth.Add({ FMath::Lerp(A.Position, B.Position, 0.75), FMath::Lerp(A.Width, B.Width, 0.75), 0.0 });
			}

			Smooth.Add(Raw.Last());
			Raw = MoveTemp(Smooth);
		}

		// points at equal distances
		double Length = 0.0;

		for (int32 Index = 0; Index + 1 < Raw.Num(); ++Index)
		{
			Length += FVector2D::Distance(Raw[Index].Position, Raw[Index + 1].Position);
		}

		const int32 NumSteps = FMath::Max(1, FMath::RoundToInt32(Length / Params.Tuning.RiverPointSpacing));
		int32 Segment = 0;
		double SegmentStart = 0.0;

		for (int32 Step = 0; Step <= NumSteps; ++Step)
		{
			const double Along = Length * Step / NumSteps;
			double SegmentLength = FVector2D::Distance(Raw[Segment].Position, Raw[Segment + 1].Position);

			while (Along > SegmentStart + SegmentLength && Segment < Raw.Num() - 2)
			{
				SegmentStart += SegmentLength;
				++Segment;
				SegmentLength = FVector2D::Distance(Raw[Segment].Position, Raw[Segment + 1].Position);
			}

			const double Alpha = FMath::Clamp((Along - SegmentStart) / FMath::Max(SegmentLength, UE_KINDA_SMALL_NUMBER), 0.0, 1.0);
			Line.Points.Add({ FMath::Lerp(Raw[Segment].Position, Raw[Segment + 1].Position, Alpha), FMath::Lerp(Raw[Segment].Width, Raw[Segment + 1].Width, Alpha), 0.0 });
		}

		// every river has one width, which narrows only at its source and where it ends in another river
		const double Taper = FMath::Min(Params.Tuning.RiverTaperLength, Length * 0.4);

		for (int32 Step = 0; Step <= NumSteps; ++Step)
		{
			const double Along = Length * Step / NumSteps;
			const double Ends = FMath::Min(FMath::SmoothStep(0.0, Taper, Along), Line.bEndsAtMouth ? 1.0 : FMath::SmoothStep(0.0, Taper, Length - Along));
			Line.Points[Step].Width = Params.RiverWidth * FMath::Lerp(Params.Tuning.RiverTaperWidth, 1.0, Ends);
		}
	}

	// smoothing moves the larger river away from the center where a tributary meets it, so the tributary ends on its line
	struct FRiverJoin
	{
		int32 Line = INDEX_NONE;
		int32 Segment = 0;
		double Alpha = 0.0;
	};

	TArray<FRiverJoin> Joins;
	Joins.SetNum(OutRivers.Lines.Num());

	for (int32 LineIndex = 0; LineIndex < OutRivers.Lines.Num(); ++LineIndex)
	{
		const int32 Target = Map.RiverPaths[LineIndex].Joins;

		if (Target == INDEX_NONE)
		{
			continue;
		}

		FHexRiverLine& Line = OutRivers.Lines[LineIndex];
		const TArray<FHexRiverPoint>& Points = OutRivers.Lines[Target].Points;
		FVector2D& End = Line.Points.Last().Position;
		double Nearest = TNumericLimits<double>::Max();
		FRiverJoin& Join = Joins[LineIndex];
		Join.Line = Target;

		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const FVector2D Along = Points[Index + 1].Position - Points[Index].Position;
			const double Alpha = FMath::Clamp(FVector2D::DotProduct(End - Points[Index].Position, Along) / FMath::Max(Along.SizeSquared(), UE_KINDA_SMALL_NUMBER), 0.0, 1.0);
			const double Distance = FVector2D::Distance(End, Points[Index].Position + Along * Alpha);

			if (Distance < Nearest)
			{
				Nearest = Distance;
				Join.Segment = Index;
				Join.Alpha = Alpha;
			}
		}

		End = FMath::Lerp(Points[Join.Segment].Position, Points[Join.Segment + 1].Position, Join.Alpha);
	}

	// the water follows the terrain down and never rises, and not below the sea
	FHexMeshData UnusedMesh;
	const FTerrainWriter Writer(Map, Params, LevelSurfaces, UnusedMesh, Cliffs);

	// The ground is averaged along the river first, as each bump would otherwise drag the water down, and the water lies just below it
	const int32 SmoothingPoints = FMath::Max(1, FMath::RoundToInt32(Params.Tuning.RiverSurfaceSmoothing / Params.Tuning.RiverPointSpacing));

	for (FHexRiverLine& Line : OutRivers.Lines)
	{
		TArray<double> Ground;

		for (const FHexRiverPoint& Point : Line.Points)
		{
			Ground.Add(Writer.GetHeight(Point.Position));
		}

		double Surface = TNumericLimits<double>::Max();

		for (int32 Index = 0; Index < Line.Points.Num(); ++Index)
		{
			double Total = 0.0;
			int32 Count = 0;

			for (int32 Other = FMath::Max(0, Index - SmoothingPoints); Other <= FMath::Min(Ground.Num() - 1, Index + SmoothingPoints); ++Other)
			{
				Total += Ground[Other];
				++Count;
			}

			Surface = FMath::Min(Surface, Total / Count - Params.RiverBankHeight);
			Line.Points[Index].Surface = FMath::Max(Surface, static_cast<double>(Params.WaterHeight));
		}
	}

	// a tributary meets the water of the larger river, which it may not lie below. Raising one can raise its own tributaries, so repeat until nothing changes
	for (bool bChanged = true; bChanged;)
	{
		bChanged = false;

		for (int32 LineIndex = 0; LineIndex < OutRivers.Lines.Num(); ++LineIndex)
		{
			const FRiverJoin& Join = Joins[LineIndex];

			if (Join.Line == INDEX_NONE)
			{
				continue;
			}

			const TArray<FHexRiverPoint>& Target = OutRivers.Lines[Join.Line].Points;
			const double JoinSurface = FMath::Lerp(Target[Join.Segment].Surface, Target[Join.Segment + 1].Surface, Join.Alpha);

			for (FHexRiverPoint& Point : OutRivers.Lines[LineIndex].Points)
			{
				if (Point.Surface < JoinSurface - UE_KINDA_SMALL_NUMBER)
				{
					Point.Surface = JoinSurface;
					bChanged = true;
				}
			}
		}
	}

	// the hexes whose terrain each segment can reach
	const double LatticeStep = static_cast<double>(Params.HexRadius) / Params.Subdivisions;
	const double Reach = Params.RiverWidth * 0.5 + Params.RiverBankLean + Params.Tuning.RiverBankSettle + LatticeStep + Params.HexRadius;

	for (int32 LineIndex = 0; LineIndex < OutRivers.Lines.Num(); ++LineIndex)
	{
		const TArray<FHexRiverPoint>& Points = OutRivers.Lines[LineIndex].Points;

		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const FHexCoord Start = Layout.WorldToHex(Points[Index].Position);

			for (int32 Q = -2; Q <= 2; ++Q)
			{
				for (int32 R = FMath::Max(-2, -Q - 2); R <= FMath::Min(2, -Q + 2); ++R)
				{
					const FHexCoord Hex(Start.Q + Q, Start.R + R);
					const FVector2D Center = Layout.HexToWorld(Hex);

					if (FVector2D::Distance(Center, FMath::ClosestPointOnSegment2D(Center, Points[Index].Position, Points[Index + 1].Position)) < Reach)
					{
						OutRivers.Nearby.FindOrAdd(Hex).Add(FIntPoint(LineIndex, Index));
					}
				}
			}
		}
	}
}

void HexMeshBuilder::BuildRiverWater(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs,
	const FHexRiverData& Rivers, FHexMeshData& OutMesh)
{
	const double SeaHeight = Params.WaterHeight;

	for (const FHexRiverLine& Line : Rivers.Lines)
	{
		const TArray<FHexRiverPoint>& Points = Line.Points;

		if (Points.Num() < 2)
		{
			continue;
		}

		double Length = 0.0;

		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			Length += FVector2D::Distance(Points[Index].Position, Points[Index + 1].Position);
		}

		// a tributary fades out inside the river it meets, whose water is already there
		const double EndFade = Line.bEndsAtMouth ? Params.Tuning.RiverWaterFade : Points.Last().Width * 0.5 + Params.Tuning.RiverWaterFade;
		const int32 FirstVertex = OutMesh.Vertices.Num();
		double Along = 0.0;

		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FHexRiverPoint& Point = Points[Index];
			const FHexRiverPoint& Before = Points[FMath::Max(Index - 1, 0)];
			const FHexRiverPoint& After = Points[FMath::Min(Index + 1, Points.Num() - 1)];

			if (Index > 0)
			{
				Along += FVector2D::Distance(Before.Position, Point.Position);
			}

			const FVector2D Tangent = (After.Position - Before.Position).GetSafeNormal();
			const FVector2D Side(-Tangent.Y, Tangent.X);
			const double Fall = (Before.Surface - After.Surface) / FMath::Max(FVector2D::Distance(Before.Position, After.Position), UE_KINDA_SMALL_NUMBER);
			const bool bUnderSea = Point.Surface <= SeaHeight + 0.5;

			FLinearColor Color;
			Color.R = static_cast<float>(FMath::SmoothStep(Params.Tuning.RiverRapidsStart, Params.Tuning.RiverRapidsFull, Fall));
			Color.G = Line.bEndsAtMouth ? static_cast<float>(1.0 - FMath::SmoothStep(0.0, Params.Tuning.RiverMouthFoamHeight, Point.Surface - SeaHeight)) : 0.0f;
			Color.B = 0.0f;
			Color.A = static_cast<float>(FMath::Min(FMath::SmoothStep(0.0, Params.Tuning.RiverWaterFade, Along), FMath::SmoothStep(0.0, EndFade, Length - Along)));

			// a little wider than the river, so its edge goes under the face of the bank, where the water meets it
			const double HalfWidth = Point.Width * 0.5 + Params.RiverBankLean * 0.7;

			for (int32 Strip = 0; Strip <= Params.Tuning.RiverWaterStrips; ++Strip)
			{
				const double Across = Strip * 2.0 / Params.Tuning.RiverWaterStrips - 1.0;
				const FVector2D Position = Point.Position + Side * (Across * HalfWidth);
				const double Height = bUnderSea ? SeaHeight - Params.Tuning.RiverUnderSea : Point.Surface;

				OutMesh.Vertices.Add(FVector(Position.X, Position.Y, Height));
				OutMesh.Normals.Add(FVector::UpVector);
				OutMesh.Colors.Add(Color);
				OutMesh.UVs.Add(FVector2D(Across, Along));
			}
		}

		const int32 Columns = Params.Tuning.RiverWaterStrips + 1;

		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			for (int32 Strip = 0; Strip < Params.Tuning.RiverWaterStrips; ++Strip)
			{
				const int32 A = FirstVertex + Index * Columns + Strip;
				const int32 B = A + 1;
				const int32 C = A + Columns;
				const int32 D = C + 1;

				// the renderer shows the side where (C - A) x (B - A) points, which must be up
				for (const FIntVector& Triangle : { FIntVector(A, B, C), FIntVector(B, D, C) })
				{
					const FVector Normal = FVector::CrossProduct(OutMesh.Vertices[Triangle.Z] - OutMesh.Vertices[Triangle.X], OutMesh.Vertices[Triangle.Y] - OutMesh.Vertices[Triangle.X]);
					const bool bFlip = Normal.Z < 0.0;
					OutMesh.Triangles.Add(Triangle.X);
					OutMesh.Triangles.Add(bFlip ? Triangle.Z : Triangle.Y);
					OutMesh.Triangles.Add(bFlip ? Triangle.Y : Triangle.Z);
				}
			}
		}
	}
}

TArray<uint8> HexMeshBuilder::BuildMassifDepths(const FHexMapData& Map, const TMap<int8, FHexLevelSurface>& LevelSurfaces)
{
	TArray<uint8> Depths;
	Depths.Init(0, Map.Width * Map.Height);
	TArray<FHexCoord> Front;

	const auto IsJoined = [&Map, &LevelSurfaces](const FHexCoord& Hex)
	{
		const FHexLevelSurface* Surface = Map.Contains(Hex) ? LevelSurfaces.Find(Map.GetLevel(Hex)) : nullptr;
		return Surface && Surface->bHasJoinedMesh;
	};

	// a joined hex next to anything else is on the edge of its group, and the rest are counted inwards from there
	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		for (int32 Column = 0; Column < Map.Width; ++Column)
		{
			const FHexCoord Hex = FHexCoord::FromOffset(Column, Row);
			bool bOnEdge = true;

			if (IsJoined(Hex))
			{
				bOnEdge = false;

				for (int32 Direction = 0; Direction < FHexCoord::NumDirections && !bOnEdge; ++Direction)
				{
					bOnEdge = !IsJoined(Hex.GetNeighbor(Direction));
				}

				Depths[Row * Map.Width + Column] = bOnEdge ? 0 : MAX_uint8;

				if (bOnEdge)
				{
					Front.Add(Hex);
				}
			}
		}
	}

	while (!Front.IsEmpty())
	{
		TArray<FHexCoord> Next;

		for (const FHexCoord& Hex : Front)
		{
			const uint8 Depth = Depths[Hex.GetRow() * Map.Width + Hex.GetColumn()];

			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FHexCoord Neighbor = Hex.GetNeighbor(Direction);

				if (IsJoined(Neighbor))
				{
					uint8& NeighborDepth = Depths[Neighbor.GetRow() * Map.Width + Neighbor.GetColumn()];

					if (NeighborDepth > Depth + 1)
					{
						NeighborDepth = static_cast<uint8>(FMath::Min(Depth + 1, 254));
						Next.Add(Neighbor);
					}
				}
			}
		}

		Front = MoveTemp(Next);
	}

	return Depths;
}

void HexMeshBuilder::BuildTerrainHeights(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs, FHexTerrainHeights& OutHeights, const FHexRiverData* Rivers)
{
	const int32 Subdivisions = Params.Subdivisions;
	FIntPoint Min(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint Max(TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Lowest());
	TSet<FHexCoord> Wanted;

	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		for (int32 Column = 0; Column < Map.Width; ++Column)
		{
			const FHexCoord Hex = FHexCoord::FromOffset(Column, Row);
			const FIntPoint Center(-Subdivisions * (2 * Hex.R + Hex.Q), Subdivisions * (2 * Hex.Q + Hex.R));
			const int32 Y = Center.Y;
			const int32 StorageX = Center.X + FHexTerrainHeights::GetRowShift(Y);

			// a whole hex and a margin past the edge of the map
			Min.X = FMath::Min(Min.X, StorageX - 3 * Subdivisions);
			Min.Y = FMath::Min(Min.Y, Y - 3 * Subdivisions);
			Max.X = FMath::Max(Max.X, StorageX + 3 * Subdivisions);
			Max.Y = FMath::Max(Max.Y, Y + 3 * Subdivisions);

			const FHexLevelSurface* Surface = LevelSurfaces.Find(Map.GetLevel(Hex));

			if (Surface && Surface->bHasJoinedMesh)
			{
				Wanted.Add(Hex);

				for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
				{
					Wanted.Add(Hex.GetNeighbor(Direction));
				}
			}
		}
	}

	OutHeights.Width = Max.X - Min.X + 1;
	OutHeights.Height = Max.Y - Min.Y + 1;
	OutHeights.Origin = Min;
	OutHeights.Heights.Init(0.0f, OutHeights.Width * OutHeights.Height);

	FHexMeshData UnusedMesh;
	const FTerrainWriter Writer(Map, Params, LevelSurfaces, UnusedMesh, Cliffs, Rivers);
	const TArray<FHexCoord> Hexes = Wanted.Array();

	// only what a mesh can stand on, as a large map has millions of these
	ParallelFor(Hexes.Num(), [&](int32 Index)
	{
		const FHexCoord& Hex = Hexes[Index];
		const FIntPoint Center(-Subdivisions * (2 * Hex.R + Hex.Q), Subdivisions * (2 * Hex.Q + Hex.R));

		for (int32 A = -Subdivisions; A <= Subdivisions; ++A)
		{
			for (int32 B = -Subdivisions; B <= Subdivisions; ++B)
			{
				if (FMath::Max3(FMath::Abs(A), FMath::Abs(B), FMath::Abs(A + B)) <= Subdivisions)
				{
					const FIntPoint Point = Center + FIntPoint(A, B);
					OutHeights.Heights[OutHeights.GetIndex(Point)] = Writer.GetLatticeHeight(Point);
				}
			}
		}
	});
}

void HexMeshBuilder::BuildShoreField(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs, const FBox2D& Bounds, FHexShoreField& OutField, const FHexRiverData* Rivers)
{
	const FVector2D Size = Bounds.GetSize();
	const double Step = FMath::Max(Params.Tuning.ShoreFieldMinStep, FMath::Max(Size.X, Size.Y) / Params.Tuning.ShoreFieldMaxSize);
	const int32 Width = FMath::CeilToInt32(Size.X / Step);
	const int32 Height = FMath::CeilToInt32(Size.Y / Step);
	const int32 Total = Width * Height;

	OutField.Width = Width;
	OutField.Height = Height;
	OutField.Step = Step;
	OutField.Origin = Bounds.Min;

	TArray<uint8> Land;
	Land.SetNumZeroed(Total);

	FHexMeshData UnusedMesh;
	const FTerrainWriter Writer(Map, Params, LevelSurfaces, UnusedMesh, Cliffs, Rivers);

	TArray<float> CliffNear;
	CliffNear.SetNumZeroed(Total);
	OutField.HeightPixels.SetNumUninitialized(Total);

	// water breaks against a mountain that stands in it as it does against a cliff
	ParallelFor(Height, [&](int32 Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const FVector2D Position = Bounds.Min + FVector2D(X + 0.5, Y + 0.5) * Step;
			const double AboveWater = FMath::Clamp(Writer.GetHeightAboveWater(Position), -FHexShoreField::HeightRange, FHexShoreField::HeightRange);
			OutField.HeightPixels[Y * Width + X] = static_cast<uint16>(FMath::RoundToInt32((AboveWater / FHexShoreField::HeightRange * 0.5 + 0.5) * 65535.0));
			Land[Y * Width + X] = Writer.IsAboveWater(Position) ? 1 : 0;
			CliffNear[Y * Width + X] = Land[Y * Width + X] && Writer.IsJoinedShore(Position, Params.Tuning.JoinedShoreFoamReach) ? 1.0f : 0.0f;
		}
	});

	// a cliff stands further out than the terrain behind it

	if (Cliffs)
	{
		for (const FHexCliffChain& Chain : Cliffs->Chains)
		{
			for (const FVector& Vertex : Chain.Vertices)
			{
				const int32 X = FMath::FloorToInt32((Vertex.X - Bounds.Min.X) / Step);
				const int32 Y = FMath::FloorToInt32((Vertex.Y - Bounds.Min.Y) / Step);

				if (Vertex.Z > Params.WaterHeight && X >= 0 && Y >= 0 && X < Width && Y < Height)
				{
					Land[Y * Width + X] = 1;
					CliffNear[Y * Width + X] = 1.0f;
				}
			}
		}
	}

	// every pixel takes the nearest land of its neighbors, in one sweep each way across the map
	const FIntPoint Far(100000, 100000);
	TArray<FIntPoint> ToLand;
	ToLand.SetNumUninitialized(Total);

	for (int32 Index = 0; Index < Total; ++Index)
	{
		ToLand[Index] = Land[Index] ? FIntPoint::ZeroValue : Far;
	}

	const auto Take = [&](int32 X, int32 Y, int32 OffsetX, int32 OffsetY)
	{
		const int32 OtherX = X + OffsetX;
		const int32 OtherY = Y + OffsetY;

		if (OtherX < 0 || OtherY < 0 || OtherX >= Width || OtherY >= Height)
		{
			return;
		}

		const FIntPoint Candidate = ToLand[OtherY * Width + OtherX] + FIntPoint(OffsetX, OffsetY);
		FIntPoint& Current = ToLand[Y * Width + X];

		if (static_cast<int64>(Candidate.X) * Candidate.X + static_cast<int64>(Candidate.Y) * Candidate.Y < static_cast<int64>(Current.X) * Current.X + static_cast<int64>(Current.Y) * Current.Y)
		{
			Current = Candidate;
		}
	};

	const auto Sweep = [&]()
	{
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Take(X, Y, -1, 0);
				Take(X, Y, 0, -1);
				Take(X, Y, -1, -1);
				Take(X, Y, 1, -1);
			}

			for (int32 X = Width - 1; X >= 0; --X)
			{
				Take(X, Y, 1, 0);
			}
		}

		for (int32 Y = Height - 1; Y >= 0; --Y)
		{
			for (int32 X = Width - 1; X >= 0; --X)
			{
				Take(X, Y, 1, 0);
				Take(X, Y, 0, 1);
				Take(X, Y, 1, 1);
				Take(X, Y, -1, 1);
			}

			for (int32 X = 0; X < Width; ++X)
			{
				Take(X, Y, -1, 0);
			}
		}
	};

	Sweep();

	TArray<float> Near;
	Near.SetNumUninitialized(Total);

	for (int32 Index = 0; Index < Total; ++Index)
	{
		Near[Index] = FMath::Min(FVector2D(ToLand[Index]).Size() * Step, 2.0 * FHexShoreField::FarRange);
	}

	const auto Blur = [Width, Height, Total](TArray<float>& Values, int32 Radius, int32 Passes)
	{
		TArray<float> Source;

		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				Source = Values;
				const int32 Stride = Axis == 0 ? 1 : Width;
				const int32 Length = Axis == 0 ? Width : Height;
				const int32 NumLines = Axis == 0 ? Height : Width;
				const int32 LineStride = Axis == 0 ? Width : 1;

				ParallelFor(NumLines, [&](int32 Line)
				{
					for (int32 Along = 0; Along < Length; ++Along)
					{
						float Sum = 0.0f;

						for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
						{
							Sum += Source[Line * LineStride + FMath::Clamp(Along + Offset, 0, Length - 1) * Stride];
						}

						Values[Line * LineStride + Along * Stride] = Sum / (2 * Radius + 1);
					}
				});
			}
		}
	};

	// how deep the seabed lies, from which the water takes its opacity, so the slope of the land shows going on down under it
	TArray<float> Deep;
	Deep.SetNumUninitialized(Total);

	ParallelFor(Height, [&](int32 Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			Deep[Y * Width + X] = static_cast<float>(Writer.GetWaterDepth(Bounds.Min + FVector2D(X + 0.5, Y + 0.5) * Step));
		}
	});

	TArray<float> Soft = Near;
	Blur(Soft, FMath::Max(1, FMath::RoundToInt32(Params.Tuning.ShoreFieldSoftBlur / Step)), 2);
	Blur(Near, 1, 1);
	Blur(Deep, FMath::Max(1, FMath::RoundToInt32(Params.Tuning.ShoreFieldDeepBlur / Step)), 3);
	Blur(CliffNear, FMath::Max(1, FMath::RoundToInt32(Params.Tuning.ShoreFieldSoftBlur / Step)), 2);

	OutField.Pixels.SetNumUninitialized(Total);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = Y * Width + X;
			const auto ToByte = [](double Value)
			{
				return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Value * 255.0), 0, 255));
			};

			OutField.Pixels[Index] = FColor(ToByte(Near[Index] / FHexShoreField::NearRange), ToByte(Soft[Index] / FHexShoreField::FarRange), ToByte(CliffNear[Index] * 4.0), ToByte(Deep[Index] / FHexShoreField::DepthRange));
		}
	}
}

void HexMeshBuilder::AddCliffRange(const FHexCliffData& Cliffs,int32 MinColumn, int32 MinRow, int32 MaxColumn, int32 MaxRow, FHexMeshData& OutMesh)
{
	const int32 Rows = FHexCliffData::RowsPerRing;

	for (const FHexCliffChain& Chain : Cliffs.Chains)
	{
		const int32 NumRings = Chain.Rings.Num();
		const int32 NumParts = Chain.bClosed ? NumRings : NumRings - 1;

		for (int32 Index = 0; Index < NumParts; ++Index)
		{
			const FIntPoint& Cell = Chain.Rings[Index].Cell;

			if (Cell.X < MinColumn || Cell.X >= MaxColumn || Cell.Y < MinRow || Cell.Y >= MaxRow)
			{
				continue;
			}

			const int32 Next = (Index + 1) % NumRings;
			const int32 First = OutMesh.Vertices.Num();

			for (const int32 Ring : { Index, Next })
			{
				for (int32 Row = 0; Row < Rows; ++Row)
				{
					const int32 Source = Ring * Rows + Row;
					OutMesh.Vertices.Add(Chain.Vertices[Source]);
					OutMesh.Normals.Add(Chain.Normals[Source]);
					OutMesh.Colors.Add(Chain.Colors[Source]);
				}
			}

			for (int32 Row = 0; Row < Rows - 1; ++Row)
			{
				const int32 A = First + Row;
				const int32 B = First + Rows + Row;
				const int32 C = First + Rows + Row + 1;
				const int32 D = First + Row + 1;
				OutMesh.Triangles.Append({ A, B, C, A, C, D });
			}
		}
	}
}

bool HexMeshBuilder::HasJoinedNotch(int32 NeighborMask, int32 Direction)
{
	const int32 Next = (Direction + 1) % FHexCoord::NumDirections;
	return (NeighborMask >> Direction & 1) != 0 && (NeighborMask >> Next & 1) == 0;
}

FTransform HexMeshBuilder::GetJoinedNotchTransform(const FHexCoord& Hex, int32 Direction, float HexRadius, float MeshRadius, double BaseZ)
{
	const FVector2D Corner = FHexLayout(HexRadius).HexToWorld(Hex) + FHexLayout::GetCornerDirection(Direction + 1) * HexRadius;

	// the center of the lower hex lies from the shared corner in the direction of the next corner of this hex
	const FQuat Rotation(FVector::UpVector, FMath::DegreesToRadians(60.0 * (Direction + 2)));

	return FTransform(Rotation, FVector(Corner.X, Corner.Y, BaseZ), FVector(HexRadius / MeshRadius));
}

bool HexMeshBuilder::FindHexAlongRay(const FHexMapData& Map, float HexRadius, float LevelHeight, float WaterHeight, double TopHeight,
	const FVector& RayOrigin, const FVector& RayDirection, FHexCoord& OutHex)
{
	if (RayDirection.Z >= -UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FHexLayout Layout(HexRadius);
	const FVector Direction = RayDirection.GetSafeNormal();

	// no hex is lower than the water, so a ray below it has missed the map
	const double StartDistance = FMath::Max(0.0, (RayOrigin.Z - TopHeight) / -Direction.Z);
	const double EndDistance = (RayOrigin.Z - WaterHeight) / -Direction.Z;
	const double Step = HexRadius * 0.25;

	for (double Distance = StartDistance; Distance <= EndDistance + Step; Distance += Step)
	{
		const FVector Point = RayOrigin + Direction * Distance;
		const FHexCoord Hex = Layout.WorldToHex(FVector2D(Point.X, Point.Y));

		if (Map.Contains(Hex) && Point.Z <= FMath::Max(static_cast<double>(Map.GetLevel(Hex) * LevelHeight), static_cast<double>(WaterHeight)))
		{
			OutHex = Hex;
			return true;
		}
	}

	return false;
}

void HexMeshBuilder::BuildRange(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces,
	int32 MinColumn, int32 MinRow, int32 MaxColumn, int32 MaxRow, FHexMeshData& OutMesh, const FHexCliffData* Cliffs, const FHexRiverData* Rivers)
{
	// a hex without map edges adds three vertices per square of its subdivision and six triangles
	const int32 NumHexes = FMath::Max(0, MaxColumn - MinColumn) * FMath::Max(0, MaxRow - MinRow);
	const int32 SquaredSubdivisions = Params.Subdivisions * Params.Subdivisions;
	const int32 ExpectedVertices = NumHexes * 3 * SquaredSubdivisions;
	OutMesh.Vertices.Reserve(OutMesh.Vertices.Num() + ExpectedVertices);
	OutMesh.Normals.Reserve(OutMesh.Normals.Num() + ExpectedVertices);
	OutMesh.Colors.Reserve(OutMesh.Colors.Num() + ExpectedVertices);
	OutMesh.Triangles.Reserve(OutMesh.Triangles.Num() + NumHexes * 18 * SquaredSubdivisions);

	FTerrainWriter Writer(Map, Params, LevelSurfaces, OutMesh, Cliffs, Rivers);

	for (int32 Row = MinRow; Row < MaxRow; ++Row)
	{
		for (int32 Column = MinColumn; Column < MaxColumn; ++Column)
		{
			Writer.AddHex(FHexCoord::FromOffset(Column, Row));
		}
	}
}

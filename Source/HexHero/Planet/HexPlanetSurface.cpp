// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetSurface.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "HexHero.h"
#include "HexMeshBuilder.h"
#include "HexPlanetConfig.h"
#include "HexPlanetKitConfig.h"
#include "HexPlanetRivers.h"
#include "HexSphereGrid.h"

namespace
{
	/** Spreads the bits of a number, so neighboring cells choose unlike versions and turns */
	uint32 HashCell(int32 Cell, uint32 Salt)
	{
		uint32 Hash = uint32(Cell) * 2654435761u ^ Salt * 40503u;
		Hash ^= Hash >> 16;
		Hash *= 0x85EBCA6Bu;
		Hash ^= Hash >> 13;
		Hash *= 0xC2B2AE35u;
		return Hash ^ (Hash >> 16);
	}

	/** A section while it is built, with the normals still summed */
	struct FRawSection
	{
		TArray<FVector> Positions;
		TArray<int32> Triangles;
		TArray<FVector3d> NormalSums;
		TArray<FVector2D> GridUVs;
		TArray<FVector2D> MeshUVs;
		TArray<FVector2D> CellUVs;
		TArray<FVector2D> LocalUVs;
		/** Level + 2 of the cell of each vertex, plus 8 on a pentagon, which chooses its colors */
		TArray<uint8> Looks;

	};

	/** A vertex on the side of a cell, whose normal is summed with those of the neighbors at the same place */
	struct FSharedVertex
	{
		int32 Section;
		int32 Vertex;
		FIntVector Key;
	};

	FIntVector GetKey(const FVector3d& Position)
	{
		return FIntVector(FMath::RoundToInt32(Position.X * 16.0), FMath::RoundToInt32(Position.Y * 16.0), FMath::RoundToInt32(Position.Z * 16.0));
	}

	void AddTriangle(FRawSection& Section, int32 A, int32 B, int32 C, const FVector3d& Outward)
	{
		const FVector3d PositionA(Section.Positions[A]);
		FVector3d Facing = FVector3d::CrossProduct(FVector3d(Section.Positions[C]) - PositionA, FVector3d(Section.Positions[B]) - PositionA);

		if (FVector3d::DotProduct(Facing, Outward) < 0.0)
		{
			Swap(B, C);
			Facing = -Facing;
		}
		Section.Triangles.Append({A, B, C});
		Section.NormalSums[A] += Facing;
		Section.NormalSums[B] += Facing;
		Section.NormalSums[C] += Facing;
	}

	/** What every cell of a build reads */
	struct FContext
	{
		const FHexSphereGrid& Grid;
		const HexPlanetSurface::FKit& Kit;
		const HexPlanetSurface::FParams& Params;
		TConstArrayView<int8> Levels;
		TConstArrayView<int32> MassifSteps;
		TArray<double> CornerLow;
		TArray<float> Relief;
		const HexPlanetSurface::FReliefOverrides* Overrides = nullptr;
		/** The line of the rivers, null for none */
		const HexPlanetRivers::FCurve* Rivers = nullptr;
	};

	/** Lowers a point of a cell into the valley of the nearest river, a function of the point and the line of the rivers alone, so the cells that
	 *  share a point lower it alike */
	FVector3d CarveRiver(const FContext& Context, int32 Cell, const FVector3d& Position)
	{
		const HexPlanetSurface::FParams& Params = Context.Params;
		const FVector3d Up = Position.GetSafeNormal();
		HexPlanetRivers::FCurveHit Hit;
		if (!Context.Rivers || !HexPlanetRivers::FindNearest(*Context.Rivers, Cell, Up, Hit))
		{
			return Position;
		}
		const double Share = 1.0 - FMath::SmoothStep(Params.RiverValleyBottom, Params.RiverValleyBottom + FMath::Max(Params.RiverValleySlope, 1.0), Hit.Distance);
		return Share > 0.0 ? Position - Up * (Params.RiverValleyDepth * Share) : Position;
	}

	/** The mask of a piece mirrored across the line through its corners 0 and 3: direction N goes to 5 - N */
	int32 MirrorMask(int32 Mask)
	{
		int32 Mirrored = 0;
		for (int32 Bit = 0; Bit < 6; ++Bit)
		{
			Mirrored |= (Mask >> Bit & 1) << (5 - Bit);
		}
		return Mirrored;
	}

	/** What a cell built */
	struct FCellResult
	{
		HexPlanetSurface::FCellMesh Mesh;
		int32 Piece = INDEX_NONE;
		int8 Turns = 0;
		bool bMirrored = false;
		bool bFlat = false;
		FIntPoint Version = FIntPoint::ZeroValue;
		FIntPoint TurnChoice = FIntPoint::ZeroValue;
	};

	/** Where a cell goes in a build: a section, its index, and the list of its vertices on sides, whose normals are summed */
	struct FTarget
	{
		FRawSection* Section = nullptr;
		int32 Tag = INDEX_NONE;
		TArray<FSharedVertex>* Shared = nullptr;
	};

	/**
	 *  Adds a fan of the center, the corners and the middles of the sides of a cell, placed as a piece at the heights of Heights, by sector of
	 *  the piece turned by Turns, or at height 0 without them, which is a flat tile whose points on sides towards other levels lie on the side
	 *  lines, so it slopes to them from its center
	 */
	void AddTile(const FContext& Context, int32 Cell, const HexPlanetKit::FCellFrame& Frame, const HexPlanetKit::FPlacement& Placement,
		const HexPlanetSurface::FCoarsePiece* Heights, int32 Turns, bool bMirrored, uint8 Look, const FTarget& Target)
	{
		const FHexSphereGrid& Grid = Context.Grid;
		const FHexSphereCell& Each = Grid.GetCells()[Cell];
		const int32 NumSides = Each.Neighbors.Num();
		FRawSection& Section = *Target.Section;

		const auto AddVertex = [&](int32 Side, double WCenter, double WFrom, double WTo, double Z, float GridU, bool bShared)
		{
			const FVector3d Position = CarveRiver(Context, Cell, HexPlanetKit::PlaceVertex(Grid, Cell, Frame, Placement, Side, WCenter, WFrom, WTo, Z));
			if (bShared)
			{
				Target.Shared->Add({Target.Tag, Section.Positions.Num(), GetKey(Position)});
			}
			Section.NormalSums.Add(FVector3d::ZeroVector);
			Section.GridUVs.Add(FVector2D(GridU, 0.0));
			Section.MeshUVs.Add(FVector2D::ZeroVector);
			Section.CellUVs.Add(HexPlanetSurface::GetCellUV(Cell));
			Section.LocalUVs.Add(HexPlanetSurface::GetLocalUV(NumSides, Side, WFrom, WTo));
			Section.Looks.Add(Look);
			return Section.Positions.Add(FVector(Position));
		};

		// side K of the cell is sector K - Turns of the piece, or 5 - K - Turns mirrored, and corner K, the end of side K, is the end of
		// sector K - Turns, or of 4 - K - Turns mirrored
		const auto Wrap = [](int32 Value) { return (Value % 6 + 6) % 6; };
		const auto PieceSector = [&](int32 Side) { return Wrap(bMirrored ? 5 - Side - Turns : Side - Turns); };
		const auto PieceEnd = [&](int32 Side) { return Wrap(bMirrored ? 4 - Side - Turns : Side - Turns); };

		const int32 Center = AddVertex(0, 1.0, 0.0, 0.0, Heights ? Heights->Center : 0.0, 0.0f, false);
		TArray<int32, TInlineAllocator<6>> CornerVertices;
		for (int32 Side = 0; Side < NumSides; ++Side)
		{
			// corner K is the end of side K
			CornerVertices.Add(AddVertex(Side, 0.0, 0.0, 1.0, Heights ? Heights->Ends[PieceEnd(Side)] : 0.0, 1.0f, true));
		}

		for (int32 Side = 0; Side < NumSides; ++Side)
		{
			const int32 From = CornerVertices[(Side + NumSides - 1) % NumSides];
			const int32 To = CornerVertices[Side];
			const int32 Middle = AddVertex(Side, 0.0, 0.5, 0.5, Heights ? Heights->Middles[PieceSector(Side)] : 0.0, 1.0f, true);
			AddTriangle(Section, Center, From, Middle, Each.Center);
			AddTriangle(Section, Center, Middle, To, Each.Center);
		}
	}

	/** Returns the heights of a piece at the points of the coarse mesh */
	HexPlanetSurface::FCoarsePiece MakeCoarsePiece(const HexPlanetKit::FPiece& Piece)
	{
		// corner D of the regular hex lies at 60 * D degrees, and sector D lies between corners D and D + 1
		const auto Corner = [](int32 Index) { return FVector2d(FMath::Cos(Index * UE_DOUBLE_PI / 3.0), FMath::Sin(Index * UE_DOUBLE_PI / 3.0)); };
		const auto Nearest = [&](const FVector2d& Point)
		{
			double Best = TNumericLimits<double>::Max();
			double Z = 0.0;
			for (const HexPlanetKit::FPieceVertex& Vertex : Piece.Vertices)
			{
				if (Vertex.WCenter < -1.0e-6)
				{
					continue;
				}
				const double Distance = FVector2d::DistSquared(Corner(Vertex.Sector) * Vertex.WFrom + Corner(Vertex.Sector + 1) * Vertex.WTo, Point);
				if (Distance < Best)
				{
					Best = Distance;
					Z = Vertex.Z;
				}
			}
			return Z;
		};

		HexPlanetSurface::FCoarsePiece Result;
		Result.Center = Nearest(FVector2d::ZeroVector);
		for (int32 Sector = 0; Sector < 6; ++Sector)
		{
			Result.Middles[Sector] = Nearest((Corner(Sector) + Corner(Sector + 1)) * 0.5);
			Result.Ends[Sector] = Nearest(Corner(Sector + 1));
		}
		return Result;
	}

	/**
	 *  Adds the mesh of a cell to a section, and its vertices on sides to Shared with the index of the section SectionTag, and its coarse mesh
	 *  to Coarse when that has a section
	 */
	FCellResult BuildCell(const FContext& Context, int32 Cell, FRawSection& Section, int32 SectionTag, TArray<FSharedVertex>& Shared, const FTarget& Coarse,
		TArray<FIntVector>* RidgeKeys = nullptr)
	{
		const FHexSphereGrid& Grid = Context.Grid;
		const FHexSphereCell& Each = Grid.GetCells()[Cell];
		const int32 NumSides = Each.Neighbors.Num();
		const int32 Level = Context.Levels[Cell];

		const auto GetLevel = [&Context](int32 Other) { return int32(Context.Levels[Other]); };
		const auto GetTop = [&Context](int32 Other) { return Context.Params.GetTop(Context.Levels[Other]); };

		// the vertices and triangles of a cell follow one another, so its mesh is a range of each
		FCellResult Result;
		Result.Mesh.Section = HexPlanetSurface::GetSection(Each.Chunk, Level);
		Result.Mesh.FirstVertex = Section.Positions.Num();
		Result.Mesh.FirstIndex = Section.Triangles.Num();
		const auto Finish = [&]()
		{
			Result.Mesh.NumVertices = Section.Positions.Num() - Result.Mesh.FirstVertex;
			Result.Mesh.NumIndices = Section.Triangles.Num() - Result.Mesh.FirstIndex;
			return Result;
		};
		const uint8 Look = uint8(Level + 2 + (Each.IsPentagon() ? 8 : 0));

		HexPlanetKit::FCellFrame Frame = HexPlanetKit::BuildFrame(Grid, Cell, GetTop, GetLevel, Context.CornerLow, Context.MassifSteps, Context.Relief);

		// the piece by the neighbors on the same level, with direction D of the piece towards neighbor D
		const HexPlanetSurface::FKitLevel* Loaded = Each.IsPentagon() ? nullptr : Context.Kit.Levels.Find(Level);
		const TArray<int32>* Choices = nullptr;
		int32 Turns = 0;
		bool bMirrored = false;
		int32 SameCount = 0;
		const HexPlanetSurface::FReliefOverride* Override = Context.Overrides ? Context.Overrides->Find(Cell) : nullptr;

		if (Loaded)
		{
			int32 Mask = 0;
			for (int32 Side = 0; Side < NumSides; ++Side)
			{
				Mask |= Context.Levels[Each.Neighbors[Side]] == Level ? 1 << Side : 0;
			}

			// a mirrored piece fits the mirrored mask, so it looks the piece up by that and turns it back the other way
			SameCount = FMath::CountBits(uint64(Mask));
			bMirrored = Override && Override->Mirror >= 0 ? Override->Mirror != 0 : Loaded->bMirror && (HashCell(Cell, 3) & 1) != 0;
			if (bMirrored)
			{
				Mask = MirrorMask(Mask);
			}

			const FHexJoinedPiece Piece = HexMeshBuilder::GetJoinedPiece(Mask);
			Choices = Loaded->PiecesByMask.Find(Piece.MeshMask);
			Turns = Piece.Turns;

			// a set of neighbors that looks the same turned can take any of those turns, as on the flat map
			int32 Period = 6;
			for (int32 Turn = 1; Turn < 6; ++Turn)
			{
				if ((((Mask << Turn) | (Mask >> (6 - Turn))) & 63) == Mask)
				{
					Period = Turn;
					break;
				}
			}
			// a turn set by hand is one of these, so the sides stay
			const int32 NumTurns = 6 / Period;
			const int32 TurnChoice = Override && Override->Turn >= 0 ? Override->Turn % NumTurns : int32(HashCell(Cell, 1) % uint32(NumTurns));
			Turns += TurnChoice * Period;
			Result.TurnChoice = FIntPoint(TurnChoice, NumTurns);
		}

		if (Choices && !Choices->IsEmpty())
		{
			// a version set by hand is one of the pieces with the same sides, and a cell with no neighbors on its level also has the styles of
			// summits, which go over its first piece
			const int32 NumStyles = SameCount == 0 ? Loaded->LoneSummitStyles : 0;
			const int32 NumVersions = Choices->Num() + NumStyles;
			const int32 Version = Override && Override->Version >= 0 ? Override->Version % NumVersions : int32(HashCell(Cell, 2) % uint32(NumVersions));
			const int32 SummitStyle = Version >= Choices->Num() ? Version - Choices->Num() : INDEX_NONE;
			const int32 PieceIndex = (*Choices)[SummitStyle == INDEX_NONE ? Version : 0];
			Result.Version = FIntPoint(Version, NumVersions);
			const HexPlanetKit::FPiece& Piece = Context.Kit.Pieces[PieceIndex];
			Result.Piece = PieceIndex;
			Result.Turns = int8(Turns % 6);
			Result.bMirrored = bMirrored;

			HexPlanetKit::FPlacement Placement = Loaded->Placement;
			Context.Params.SetWobble(Placement);

			// the center moves in a direction and by a share of its own, within the cell, which only the cell sees
			if (Loaded->CenterShift > 0.0f)
			{
				const double Angle = (HashCell(Cell, 6) & 0xFFFF) / 65536.0 * UE_DOUBLE_TWO_PI;
				const double Share = Loaded->CenterShift * FMath::Sqrt((HashCell(Cell, 7) & 0xFFFF) / 65535.0);
				const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Each.Center).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitX());
				const FVector3d North = FVector3d::CrossProduct(Each.Center, East);
				const double CellRadius = FVector3d::Distance(Each.Center, Grid.GetCorners()[Each.Corners[0]]);
				Frame.CenterShift = (East * FMath::Cos(Angle) + North * FMath::Sin(Angle)) * Share * CellRadius;
			}

			// the shape of the inside, a dome or a needle and turned, by a hash of its own
			Frame.Sharpness = FMath::Lerp(double(Loaded->SharpnessMin), double(Loaded->SharpnessMax), (HashCell(Cell, 8) & 0xFFFF) / 65535.0);
			Frame.Twist = Loaded->Twist * ((HashCell(Cell, 9) & 0xFFFF) / 65535.0 * 2.0 - 1.0);

			// a lone cell is a broad dome that stands in the middle of its cell, as a needle there takes little of it
			if (SameCount <= Loaded->LoneNeighbors)
			{
				Frame.LoneBase = Loaded->LoneBase;
				Frame.CenterShift *= Loaded->LoneShift;
				// a spur, the end of a chain, keeps only the dome over its cell, as the lifted dome on its peak, which stands off its center,
				// leans as a tower
				if (SameCount == 0)
				{
					Frame.Sharpness = 1.0;
					Frame.LoneDome = Loaded->LoneSharpness;
				}
				else
				{
					Frame.Sharpness = FMath::Min(Frame.Sharpness, 1.0);
				}
			}

			// a crest of a cell with neighbors on its level splits into two blades where a noise along the ranges says so, so chains vary along
			// their length
			if (SameCount > 0 && (Loaded->Placement.SplitDepth > 0.0 || Loaded->Placement.SplitBlade > 0.0))
			{
				const double Spacing = Context.Params.HexRadius * FMath::Sqrt(3.0) * FMath::Max(Loaded->SplitNoiseCells, 0.1f);
				const FVector Point(Each.Center * (Context.Params.Radius / Spacing) + FVector3d(5.3, 71.9, 19.1));
				Frame.Split = FMath::SmoothStep(double(Loaded->SplitThreshold), Loaded->SplitThreshold + 0.2, double(FMath::PerlinNoise3D(Point)));
			}

			// a style of summits: two of unlike heights, three in an uneven cluster, or a main peak with a lower shoulder, turned by a hash
			if (SummitStyle != INDEX_NONE)
			{
				// (distance from the center as a share of the radius of the cell, angle from the turn of the cell, height, width)
				static const FVector4d Styles[3][3] = {
					{FVector4d(0.26, 0.0, 1.0, 0.62), FVector4d(0.4, 3.55, 0.62, 0.5), FVector4d(0.0, 0.0, 0.0, 0.0)},
					{FVector4d(0.3, 0.0, 1.0, 0.5), FVector4d(0.38, 2.3, 0.74, 0.44), FVector4d(0.42, 4.1, 0.52, 0.4)},
					{FVector4d(0.1, 0.0, 1.0, 0.72), FVector4d(0.5, 0.65, 0.5, 0.34), FVector4d(0.0, 0.0, 0.0, 0.0)},
				};
				const double Turn = (HashCell(Cell, 11) & 0xFFFF) / 65536.0 * UE_DOUBLE_TWO_PI;
				const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Each.Center).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitX());
				const FVector3d North = FVector3d::CrossProduct(Each.Center, East);
				const double CellRadius = FVector3d::Distance(Each.Center, Grid.GetCorners()[Each.Corners[0]]);
				for (const FVector4d& Style : Styles[SummitStyle])
				{
					if (Style.Z <= 0.0)
					{
						continue;
					}
					HexPlanetKit::FCellFrame::FSummit& Summit = Frame.Summits.AddDefaulted_GetRef();
					const double Angle = Turn + Style.Y;
					Summit.Direction = (Each.Center + (East * FMath::Cos(Angle) + North * FMath::Sin(Angle)) * Style.X * CellRadius).GetSafeNormal();
					Summit.Height = Style.Z * Loaded->SummitHeight;
					Summit.Width = Style.W * Loaded->SummitWidth * CellRadius;
				}
				Frame.SummitBase = Loaded->SummitBase;
				Frame.CenterShift = FVector3d::ZeroVector;
				Frame.LoneDome = 0.0;
			}

			// a narrow chain keeps all its ridges, a massif only some, chosen by the pair of cells, so both give a side alike
			if (Loaded->Placement.RidgeLift > 0.0)
			{
				const auto CountSame = [&Context](int32 Other)
				{
					const int32 OtherLevel = Context.Levels[Other];
					return Context.Grid.GetCells()[Other].Neighbors.FilterByPredicate([&](int32 Next) { return Context.Levels[Next] == OtherLevel; }).Num();
				};
				const bool bChain = CountSame(Cell) <= 2;
				for (int32 Side = 0; Side < NumSides; ++Side)
				{
					const int32 Neighbor = Each.Neighbors[Side];
					const uint32 Pair = HashCell(FMath::Min(Cell, Neighbor) * 7 + 3, uint32(FMath::Max(Cell, Neighbor)));
					const bool bWhole = bChain || CountSame(Neighbor) <= 2 || (Pair & 0xFFFF) / 65535.0 < Loaded->RidgeMassifShare;
					Frame.RidgeWeights.Add(bWhole ? 1.0 : Loaded->RidgeMassifLow);
				}
			}

			const int32 First = Section.Positions.Num();
			for (const HexPlanetKit::FPieceVertex& Vertex : Piece.Vertices)
			{
				// mirrored, sector D faces direction 5 - D and its corners swap
				const int32 Side = bMirrored ? ((5 - Vertex.Sector - Turns) % 6 + 6) % 6 : (Vertex.Sector + Turns) % 6;
				const double WFrom = bMirrored ? Vertex.WTo : Vertex.WFrom;
				const double WTo = bMirrored ? Vertex.WFrom : Vertex.WTo;
				double Crevice = 0.0;
				const FVector3d Position = CarveRiver(Context, Cell, HexPlanetKit::PlaceVertex(Grid, Cell, Frame, Placement, Side, Vertex.WCenter, WFrom, WTo, Vertex.Z, &Crevice));

				if (Vertex.bOnSide)
				{
					Shared.Add({SectionTag, Section.Positions.Num(), GetKey(Position)});

					// a piece that models its sides towards its own level itself must meet its neighbor there exactly, mirrored or not.
					// Its corners at another level go on the side line anyway
					const bool bAtOpenCorner = (WTo < 1.0e-6 && !Frame.SameSides[(Side + NumSides - 1) % NumSides]) || (WFrom < 1.0e-6 && !Frame.SameSides[(Side + 1) % NumSides]);
					if (RidgeKeys && !Loaded->Placement.bTopFollowsCorners && Frame.SameSides[Side] && !bAtOpenCorner)
					{
						RidgeKeys->Add(FIntVector(FMath::RoundToInt32(Position.X * 4.0), FMath::RoundToInt32(Position.Y * 4.0), FMath::RoundToInt32(Position.Z * 4.0)));					}
				}
				Section.Positions.Add(FVector(Position));
				Section.NormalSums.Add(FVector3d::ZeroVector);
				Section.GridUVs.Add(FVector2D(Vertex.GridU, 0.0));
				// Y of the second UV: how much of a gully the vertex is, X the faces, from its height below
				Section.MeshUVs.Add(FVector2D(0.0, Crevice));
				Section.CellUVs.Add(HexPlanetSurface::GetCellUV(Cell));
				Section.LocalUVs.Add(HexPlanetSurface::GetLocalUV(NumSides, Side, WFrom, WTo));
				Section.Looks.Add(Look);
			}

			// the pieces face up and the bending keeps their winding, so the triangles keep their order. What a piece models past its cell,
			// such as the base of a mountain at a notch, is left out, as the neighbor rises to the side there
			for (int32 Index = 0; Index < Piece.Triangles.Num(); Index += 3)
			{
				if (Piece.Vertices[Piece.Triangles[Index]].WCenter < -1.0e-6 || Piece.Vertices[Piece.Triangles[Index + 1]].WCenter < -1.0e-6
					|| Piece.Vertices[Piece.Triangles[Index + 2]].WCenter < -1.0e-6)
				{
					continue;
				}
				AddTriangle(Section, First + Piece.Triangles[Index], First + Piece.Triangles[Index + 1], First + Piece.Triangles[Index + 2], Each.Center);
			}

			if (Coarse.Section)
			{
				AddTile(Context, Cell, Frame, Placement, &Context.Kit.CoarsePieces[PieceIndex], Turns, bMirrored, Look, Coarse);
			}
			return Finish();
		}

		// a flat tile, the same in the coarse mesh
		Result.bFlat = true;
		HexPlanetKit::FPlacement Flat;
		Flat.FootDepth = Context.Params.LevelHeight;
		Flat.FadeHeight = Context.Params.LevelHeight;
		Context.Params.SetWobble(Flat);
		AddTile(Context, Cell, Frame, Flat, nullptr, 0, false, Look, {&Section, SectionTag, &Shared});

		if (Coarse.Section)
		{
			AddTile(Context, Cell, Frame, Flat, nullptr, 0, false, Look, Coarse);
		}
		return Finish();
	}
}

TSharedRef<const HexPlanetSurface::FKit> HexPlanetSurface::LoadKit(const UHexPlanetKitConfig* Config, double HexRadius)
{
	check(IsInGameThread());
	TSharedRef<FKit> Result = MakeShared<FKit>();

	if (!Config)
	{
		return Result;
	}

	const FHexPlanetBoulders& Boulders = Config->Boulders;
	Result->BoulderMeshes = Boulders.Meshes.Num();
	Result->BoulderMaxPerSide = Boulders.MaxPerSide;
	Result->BoulderMinLevel = Boulders.MinLevel;
	Result->BoulderSizeMin = Boulders.SizeMin;
	Result->BoulderSizeMax = FMath::Max(Boulders.SizeMax, Boulders.SizeMin);
	Result->BoulderSpread = Boulders.Spread;
	Result->BoulderSink = Boulders.Sink;
	Result->BoulderTilt = Boulders.Tilt;

	for (const FHexPlanetKit& Kit : Config->Kits)
	{
		FKitLevel& Loaded = Result->Levels.Add(Kit.Level);
		Loaded.Placement.Scale = HexRadius / Kit.MeshRadius;
		Loaded.Placement.FootDepth = Kit.FootDepth * Loaded.Placement.Scale;
		Loaded.Placement.FadeHeight = Kit.FadeHeight * Loaded.Placement.Scale;
		Loaded.Placement.bTopFollowsCorners = Kit.bTopFollowsCorners;
		Loaded.Placement.MassifStep = Kit.MassifStep;
		Loaded.Placement.MassifRise = Kit.MassifRise * Loaded.Placement.Scale;
		Loaded.MassifMaxSteps = Kit.MassifMaxSteps;
		Loaded.bMirror = Kit.bMirror;
		Loaded.HeightScaleMin = Kit.HeightScaleMin;
		Loaded.HeightScaleMax = Kit.HeightScaleMax;
		Loaded.HeightNoiseCells = Kit.HeightNoiseCells;
		Loaded.HeightJitter = Kit.HeightJitter;
		Loaded.PeakChance = Kit.PeakChance;
		Loaded.PeakScale = Kit.PeakScale;
		Loaded.CenterShift = Kit.CenterShift;
		Loaded.HeightScaleFloor = Kit.HeightScaleFloor;
		Loaded.LoneNeighbors = Kit.LoneNeighbors;
		Loaded.LoneHeightMin = Kit.LoneHeightMin;
		Loaded.LoneSharpness = Kit.LoneSharpness;
		Loaded.LoneShift = Kit.LoneShift;
		Loaded.LoneBase = Kit.LoneBase;
		Loaded.SpurHeightScale = Kit.SpurHeightScale;
		Loaded.LoneSummitStyles = FMath::Clamp(Kit.LoneSummitStyles, 0, 3);
		Loaded.SplitNoiseCells = Kit.SplitNoiseCells;
		Loaded.SplitThreshold = Kit.SplitThreshold;
		Loaded.Placement.BulkLift = Kit.BulkLift * Loaded.Placement.Scale;
		Loaded.Placement.BulkReach = FMath::Max(Kit.BulkReach, 0.01f);
		Loaded.Placement.SplitDepth = Kit.SplitDepth;
		Loaded.Placement.SplitBlade = Kit.SplitBlade;
		Loaded.Placement.SplitWidth = Kit.SplitWidth;
		Loaded.SummitBase = Kit.SummitBase;
		Loaded.SummitHeight = Kit.SummitHeight;
		Loaded.SummitWidth = Kit.SummitWidth;
		Loaded.SharpnessMin = Kit.SharpnessMin;
		Loaded.SharpnessMax = Kit.SharpnessMax;
		Loaded.Twist = FMath::DegreesToRadians(Kit.Twist);
		Loaded.RidgeMassifShare = Kit.RidgeMassifShare;
		Loaded.RidgeMassifLow = Kit.RidgeMassifLow;
		Loaded.Placement.ShapeHeight = Kit.ShapeHeight * Loaded.Placement.Scale;
		Loaded.Placement.CragHeight = Kit.CragHeight * Loaded.Placement.Scale;
		Loaded.Placement.CragScale = FMath::Max(Kit.CragScale, 1.0f) * Loaded.Placement.Scale;
		Loaded.Placement.RidgeLift = Kit.RidgeLift * Loaded.Placement.Scale;
		Loaded.Placement.RidgeWidth = FMath::Clamp(Kit.RidgeWidth, 0.01f, 0.49f);
		Loaded.Placement.JaggedHeight = Kit.JaggedHeight * Loaded.Placement.Scale;
		Loaded.Placement.JaggedScale = FMath::Max(Kit.JaggedScale, 1.0f) * Loaded.Placement.Scale;
		// heights above the sea, which FParams::SetWobble makes radii
		Loaded.Placement.GullyHeight = Kit.GullyHeight * Loaded.Placement.Scale;
		Loaded.Placement.GullyCount = Kit.GullyCount;
		Loaded.Placement.GullyLength = Kit.GullyLength;
		Loaded.Placement.LedgeStep = Kit.LedgeStep * Loaded.Placement.Scale;
		Loaded.Placement.LedgeStrength = Kit.LedgeStrength;
		Loaded.Placement.FacetLow = Kit.FacetHeightStart;
		Loaded.Placement.FacetHigh = Kit.FacetHeightEnd;
		Loaded.Placement.bRelief = Kit.HeightScaleMin != 1.0f || Kit.HeightScaleMax != 1.0f || Kit.HeightJitter > 0.0f || (Kit.PeakChance > 0.0f && Kit.PeakScale > 0.0f);
		Loaded.Placement.ScaleBase = Kit.ScaleBase * Loaded.Placement.Scale;
		Loaded.Color = Kit.Color;
		Loaded.SlopeColor = Kit.SlopeColor;

		const auto AddPiece = [&](int32 Mask, const TSoftObjectPtr<UStaticMesh>& Mesh)
		{
			HexPlanetKit::FPiece Piece;
			if (HexPlanetKit::ReadPiece(Mesh.LoadSynchronous(), Loaded.Placement.Scale, Kit.MeshRadius, Kit.Subdivisions, Piece))
			{
				Result->CoarsePieces.Add(MakeCoarsePiece(Piece));
				Loaded.PiecesByMask.FindOrAdd(Mask).Add(Result->Pieces.Add(MoveTemp(Piece)));
				Result->PieceNames.Add(Mesh.GetAssetName());
			}
			else
			{
				UE_LOG(LogHexHero, Warning, TEXT("Planet: cannot read %s, which needs CPU access, run Tools/HexPlanetKit/import_planet_kit.py"), *Mesh.ToString());
				++Result->UnreadMeshes;
			}
		};

		for (const TPair<int32, TSoftObjectPtr<UStaticMesh>>& Mesh : Kit.Meshes)
		{
			AddPiece(Mesh.Key, Mesh.Value);
		}
		for (const TPair<int32, FHexJoinedVariants>& Variants : Kit.Variants)
		{
			for (const TSoftObjectPtr<UStaticMesh>& Mesh : Variants.Value.Meshes)
			{
				AddPiece(Variants.Key, Mesh);
			}
		}
	}
	return Result;
}

HexPlanetSurface::FParams HexPlanetSurface::FParams::Make(const UHexPlanetConfig& Config, const FKit& Kit, double Radius)
{
	FParams Params;
	Params.Radius = Radius;
	Params.LevelHeight = Config.LevelHeight;
	Params.ShallowDepth = Config.ShallowDepth;
	Params.DeepDepth = Config.DeepDepth;

	// colors by level, slope and height: sand at the sea, the seabed darkening with depth and snow high on mountains
	for (int32 Level = -2; Level <= 2; ++Level)
	{
		const FKitLevel* Loaded = Kit.Levels.Find(Level);
		const FLinearColor LevelColor = Config.LevelColors.IsValidIndex(Level + 2) ? Config.LevelColors[Level + 2] : FLinearColor::Gray;
		Params.FlatColors[Level + 2] = Loaded ? Loaded->Color : LevelColor;
		Params.SlopeColors[Level + 2] = Loaded ? Loaded->SlopeColor : LevelColor * Config.SlopeDarkening;
	}

	Params.RiverValleyDepth = Config.RiverValleyDepth;
	Params.RiverValleyBottom = Config.RiverValleyBottom;
	Params.RiverValleySlope = Config.RiverValleySlope;
	Params.SandColor = Config.SandColor;
	Params.SnowColor = Config.SnowColor;
	Params.SlopeStart = Config.SlopeStart;
	Params.SlopeEnd = Config.SlopeEnd;
	Params.SnowHeight = Config.SnowHeight;
	Params.SnowFade = Config.SnowFade;
	Params.SnowFlatStart = Config.SnowFlatStart;
	Params.SnowFlatEnd = Config.SnowFlatEnd;
	Params.BeachHeight = Config.BeachHeight;
	Params.BeachFade = Config.BeachFade;
	Params.HexRadius = Config.HexRadius;
	Params.CoastWobble = Config.CoastWobble;
	Params.CoastWobbleScale = Config.CoastWobbleScale;
	Params.CoastWobbleDetail = Config.CoastWobbleDetail;
	for (int32 Level = -2; Level <= 2; ++Level)
	{
		const FKitLevel* Loaded = Kit.Levels.Find(Level);
		Params.FacetLow[Level + 2] = Loaded ? Radius + Loaded->Placement.FacetLow : 0.0;
		Params.FacetHigh[Level + 2] = Loaded ? Radius + Loaded->Placement.FacetHigh : 0.0;
	}
	return Params;
}

void HexPlanetSurface::FParams::SetWobble(HexPlanetKit::FPlacement& Placement) const
{
	Placement.Wobble = CoastWobble;
	Placement.WobbleScale = FMath::Max(CoastWobbleScale, 1.0);
	Placement.WobbleDetail = CoastWobbleDetail;
	Placement.WobbleTops[0] = GetTop(-2);
	Placement.WobbleTops[1] = GetTop(-1);
	Placement.WobbleTops[2] = GetTop(0);
	// the faces of high ground, from heights above the sea to radii
	if (Placement.FacetHigh > Placement.FacetLow && Placement.FacetHigh < Radius * 0.5)
	{
		Placement.FacetLow += Radius;
		Placement.FacetHigh += Radius;
	}
}

TArray<float> HexPlanetSurface::FindRelief(const FHexSphereGrid& Grid, const FKit& Kit, const FParams& Params, TConstArrayView<int8> Levels, const FReliefOverrides* Overrides)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<float> Relief;
	Relief.Init(1.0f, Cells.Num());

	ParallelFor(Cells.Num(), [&](int32 Cell)
	{
		const FKitLevel* Loaded = Cells[Cell].IsPentagon() ? nullptr : Kit.Levels.Find(Levels[Cell]);
		if (!Loaded || !Loaded->Placement.bRelief)
		{
			return;
		}

		// a noise of the place with cells of about HeightNoiseCells, the distance between cell centers being a radius times the root of 3,
		// moved by level so hills and mountains do not rise together
		const double Spacing = Params.HexRadius * FMath::Sqrt(3.0) * FMath::Max(Loaded->HeightNoiseCells, 0.1f);
		const FVector Point(Cells[Cell].Center * (Params.Radius / Spacing) + FVector3d(31.7 * Levels[Cell], 13.3, 47.9));
		const double Rise = FMath::SmoothStep(-0.45, 0.45, double(FMath::PerlinNoise3D(Point)));

		const double Jitter = (HashCell(Cell, 4) & 0xFFFF) / 65535.0 * 2.0 - 1.0;
		const double PeakRoll = (HashCell(Cell, 5) & 0xFFFF) / 65535.0;
		double Value = FMath::Lerp(double(Loaded->HeightScaleMin), double(Loaded->HeightScaleMax), Rise) + Jitter * Loaded->HeightJitter;
		// peaks come more where the chain rises
		if (PeakRoll < Loaded->PeakChance * (0.4 + 1.2 * Rise))
		{
			Value += Loaded->PeakScale;
		}
		// never so low that a mountain reads as a hill, and a lone one stands taller
		Value = FMath::Max(Value, double(Loaded->HeightScaleFloor));
		const int32 SameCount = Cells[Cell].Neighbors.FilterByPredicate([&](int32 Neighbor) { return Levels[Neighbor] == Levels[Cell]; }).Num();
		if (SameCount == 0 && Loaded->LoneNeighbors >= 0)
		{
			Value = FMath::Max(Value, double(Loaded->LoneHeightMin));
		}
		// the end of a chain steps down, but never below the floor, so it still reads as a mountain
		else if (SameCount == 1 && SameCount <= Loaded->LoneNeighbors)
		{
			Value = FMath::Max(Value * Loaded->SpurHeightScale, double(Loaded->HeightScaleFloor));
		}

		// and a factor set by hand on top
		const FReliefOverride* Override = Overrides ? Overrides->Find(Cell) : nullptr;
		if (Override)
		{
			Value *= Override->Scale;
		}
		Relief[Cell] = float(FMath::Max(Value, 0.1));
	});
	return Relief;
}

double HexPlanetSurface::FParams::GetTop(int32 Level) const
{
	return Radius + (Level == -2 ? -DeepDepth : Level == -1 ? -ShallowDepth : (Level + 1) * LevelHeight);
}

TArray<int32> HexPlanetSurface::FindMassifSteps(const FHexSphereGrid& Grid, const FKit& Kit, TConstArrayView<int8> Levels)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<int32> MassifSteps;
	MassifSteps.Init(0, Cells.Num());

	for (const TPair<int32, FKitLevel>& Loaded : Kit.Levels)
	{
		if (Loaded.Value.MassifMaxSteps <= 0)
		{
			continue;
		}

		TArray<int32> Front;
		TArray<bool> Reached;
		Reached.Init(false, Cells.Num());

		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			if (Levels[Cell] == Loaded.Key && Cells[Cell].Neighbors.ContainsByPredicate([&](int32 Neighbor) { return Levels[Neighbor] != Loaded.Key; }))
			{
				Reached[Cell] = true;
				Front.Add(Cell);
			}
		}

		for (int32 Index = 0; Index < Front.Num(); ++Index)
		{
			for (const int32 Neighbor : Cells[Front[Index]].Neighbors)
			{
				if (!Reached[Neighbor] && Levels[Neighbor] == Loaded.Key)
				{
					Reached[Neighbor] = true;
					MassifSteps[Neighbor] = FMath::Min(MassifSteps[Front[Index]] + 1, Loaded.Value.MassifMaxSteps);
					Front.Add(Neighbor);
				}
			}
		}
	}
	return MassifSteps;
}

TArray<int32> HexPlanetSurface::GetAllChunks(const FHexSphereGrid& Grid)
{
	TArray<int32> Chunks;
	for (int32 Chunk = 0; Chunk < Grid.GetNumChunks(); ++Chunk)
	{
		Chunks.Add(Chunk);
	}
	return Chunks;
}

TArray<int32> HexPlanetSurface::FindAffectedChunks(const FHexSphereGrid& Grid, TConstArrayView<int8> OldLevels, TConstArrayView<int8> NewLevels,
	TConstArrayView<int32> OldMassifSteps, TConstArrayView<int32> NewMassifSteps, TConstArrayView<int32> ChangedCells)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	check(OldLevels.Num() == Cells.Num() && NewLevels.Num() == Cells.Num() && OldMassifSteps.Num() == Cells.Num() && NewMassifSteps.Num() == Cells.Num());

	const auto MarkRing = [&Cells](TBitArray<>& Marks, int32 Cell)
	{
		Marks[Cell] = true;
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			Marks[Neighbor] = true;
		}
	};

	// the mesh of a cell changes with the levels and massif steps of itself and its neighbors: the piece, the frame of corners and sides,
	// and the rise of the massif
	TBitArray<> MeshChanged(false, Cells.Num());
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (OldLevels[Cell] != NewLevels[Cell] || OldMassifSteps[Cell] != NewMassifSteps[Cell])
		{
			MarkRing(MeshChanged, Cell);
		}
	}
	// a relief set by hand changes the piece of its cell and, through its factor, the meshes of its neighbors, as a level does
	for (const int32 Cell : ChangedCells)
	{
		if (Cells.IsValidIndex(Cell))
		{
			MarkRing(MeshChanged, Cell);
		}
	}

	// and the normals on its sides and corners with the meshes of the cells that meet there, its neighbors
	TBitArray<> NormalsChanged(false, Cells.Num());
	for (TConstSetBitIterator<> It(MeshChanged); It; ++It)
	{
		MarkRing(NormalsChanged, It.GetIndex());
	}

	TBitArray<> bChunks(false, Grid.GetNumChunks());
	for (TConstSetBitIterator<> It(NormalsChanged); It; ++It)
	{
		bChunks[Cells[It.GetIndex()].Chunk] = true;
	}

	TArray<int32> Chunks;
	for (TConstSetBitIterator<> It(bChunks); It; ++It)
	{
		Chunks.Add(It.GetIndex());
	}
	return Chunks;
}

TArray<int32> HexPlanetSurface::FindChangedOverrides(const FReliefOverrides& Old, const FReliefOverrides& New)
{
	TArray<int32> Changed;
	for (const TPair<int32, FReliefOverride>& Each : New)
	{
		const FReliefOverride* Before = Old.Find(Each.Key);
		if (!Before || !(*Before == Each.Value))
		{
			Changed.Add(Each.Key);
		}
	}
	for (const TPair<int32, FReliefOverride>& Each : Old)
	{
		if (!New.Contains(Each.Key))
		{
			Changed.Add(Each.Key);
		}
	}
	return Changed;
}

HexPlanetSurface::FBuild HexPlanetSurface::Build(const FHexSphereGrid& Grid, const FKit& Kit, const FParams& Params, TConstArrayView<int8> Levels,
	TConstArrayView<int32> MassifSteps, TConstArrayView<int32> Chunks, double MeanPeak, const FReliefOverrides* Overrides, const HexPlanetRivers::FCurve* Rivers)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();

	FBuild Result;
	Result.Chunks = TArray<int32>(Chunks.GetData(), Chunks.Num());
	Result.Chunks.Sort();
	Result.CellMeshes.Init(FCellMesh(), Cells.Num());
	Result.CellPieces.Init(INDEX_NONE, Cells.Num());
	Result.CellTurns.Init(0, Cells.Num());
	Result.CellMirrored.Init(false, Cells.Num());
	Result.CellVersions.Init(FIntPoint::ZeroValue, Cells.Num());
	Result.CellTurnChoices.Init(FIntPoint::ZeroValue, Cells.Num());

	FContext Context{Grid, Kit, Params, Levels, MassifSteps, {}, {}};
	Context.CornerLow = HexPlanetKit::FindCornerHeights(Grid, [&](int32 Cell) { return Params.GetTop(Levels[Cell]); });
	Context.Overrides = Overrides;
	Context.Rivers = Rivers && Rivers->CellSegments.Num() == Cells.Num() ? Rivers : nullptr;
	Context.Relief = FindRelief(Grid, Kit, Params, Levels, Overrides);

	// the slot of each chunk built, whose sections are Slot * 2 and Slot * 2 + 1 of the result
	TArray<int32> ChunkSlots;
	ChunkSlots.Init(INDEX_NONE, Grid.GetNumChunks());
	for (int32 Slot = 0; Slot < Result.Chunks.Num(); ++Slot)
	{
		ChunkSlots[Result.Chunks[Slot]] = Slot;
	}

	// the cells of each chunk in the order of the grid, and the halo: the cells of other chunks next to them, whose meshes are built only for
	// the normals of the sides they share
	TArray<TArray<int32>> CellsBySlot;
	CellsBySlot.SetNum(Result.Chunks.Num());
	TArray<int32> Halo;

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (ChunkSlots[Cells[Cell].Chunk] != INDEX_NONE)
		{
			CellsBySlot[ChunkSlots[Cells[Cell].Chunk]].Add(Cell);
		}
		else if (Cells[Cell].Neighbors.ContainsByPredicate([&](int32 Neighbor) { return ChunkSlots[Cells[Neighbor].Chunk] != INDEX_NONE; }))
		{
			Halo.Add(Cell);
		}
	}

	// the two sections of each chunk, then its coarse section, then the halo of each
	const int32 NumSlots = Result.Chunks.Num();
	const int32 CoarseFirst = NumSlots * 2;
	const int32 HaloSection = NumSlots * 3;
	const int32 CoarseHaloSection = HaloSection + 1;
	TArray<FRawSection> RawSections;
	RawSections.SetNum(CoarseHaloSection + 1);
	TArray<TArray<FSharedVertex>> SharedBySlot;
	SharedBySlot.SetNum(NumSlots + 1);
	TArray<TArray<FSharedVertex>> CoarseSharedBySlot;
	CoarseSharedBySlot.SetNum(NumSlots + 1);
	TArray<int32> FlatTilesBySlot;
	FlatTilesBySlot.Init(0, NumSlots);
	TArray<TArray<FIntVector>> RidgeKeysBySlot;
	RidgeKeysBySlot.SetNum(NumSlots);

	ParallelFor(NumSlots + 1, [&](int32 Slot)
	{
		if (Slot == NumSlots)
		{
			const FTarget CoarseHalo{&RawSections[CoarseHaloSection], CoarseHaloSection, &CoarseSharedBySlot[Slot]};
			for (const int32 Cell : Halo)
			{
				BuildCell(Context, Cell, RawSections[HaloSection], HaloSection, SharedBySlot[Slot], CoarseHalo);
			}
			return;
		}

		const FTarget Coarse{&RawSections[CoarseFirst + Slot], CoarseFirst + Slot, &CoarseSharedBySlot[Slot]};
		for (const int32 Cell : CellsBySlot[Slot])
		{
			const int32 SectionIndex = Slot * 2 + (Levels[Cell] == MountainLevel ? 1 : 0);
			const FCellResult Built = BuildCell(Context, Cell, RawSections[SectionIndex], SectionIndex, SharedBySlot[Slot], Coarse, &RidgeKeysBySlot[Slot]);
			Result.CellMeshes[Cell] = Built.Mesh;
			Result.CellPieces[Cell] = Built.Piece;
			Result.CellTurns[Cell] = Built.Turns;
			Result.CellMirrored[Cell] = Built.bMirrored;
			Result.CellVersions[Cell] = Built.Version;
			Result.CellTurnChoices[Cell] = Built.TurnChoice;
			FlatTilesBySlot[Slot] += Built.bFlat ? 1 : 0;
		}
	});

	for (const int32 FlatTiles : FlatTilesBySlot)
	{
		Result.FlatTiles += FlatTiles;
	}

	// boulders at the foot of the mountains on the edge of a range, on the ground of the neighbor near the side, as a flat tile of it would lie
	Result.Boulders.SetNum(NumSlots);
	if (Kit.BoulderMeshes > 0 && Kit.BoulderMaxPerSide > 0)
	{
		ParallelFor(NumSlots, [&](int32 Slot)
		{
			for (const int32 Cell : CellsBySlot[Slot])
			{
				if (Levels[Cell] != MountainLevel || Cells[Cell].IsPentagon())
				{
					continue;
				}
				const FHexSphereCell& Each = Cells[Cell];
				for (int32 Side = 0; Side < Each.Neighbors.Num(); ++Side)
				{
					const int32 Neighbor = Each.Neighbors[Side];
					if (Levels[Neighbor] >= MountainLevel || Levels[Neighbor] < Kit.BoulderMinLevel)
					{
						continue;
					}

					const int32 NeighborSide = Cells[Neighbor].Neighbors.IndexOfByKey(Cell);
					const auto GetLevel = [&Levels](int32 Other) { return int32(Levels[Other]); };
					const auto GetTop = [&Params, &Levels](int32 Other) { return Params.GetTop(Levels[Other]); };
					const HexPlanetKit::FCellFrame NeighborFrame = HexPlanetKit::BuildFrame(Grid, Neighbor, GetTop, GetLevel, Context.CornerLow, MassifSteps, Context.Relief);
					HexPlanetKit::FPlacement Ground;
					Ground.FootDepth = Params.LevelHeight;
					Ground.FadeHeight = Params.LevelHeight;
					Params.SetWobble(Ground);

					FRandomStream Random(int32(HashCell(Cell * 7 + Side, 10)));
					const int32 Count = Random.RandRange(0, Kit.BoulderMaxPerSide);
					for (int32 Index = 0; Index < Count; ++Index)
					{
						// along the side as the mountain walks it, which the neighbor walks the other way, and a little into the neighbor
						const double S = Random.FRandRange(0.12, 0.88);
						const double Out = Random.FRand() * Kit.BoulderSpread;
						const FVector3d Position = HexPlanetKit::PlaceVertex(Grid, Neighbor, NeighborFrame, Ground, NeighborSide, Out, (1.0 - Out) * S, (1.0 - Out) * (1.0 - S), 0.0);

						// not in the valley of a river
						HexPlanetRivers::FCurveHit Hit;
						if (Context.Rivers && HexPlanetRivers::FindNearest(*Context.Rivers, Neighbor, Position.GetSafeNormal(), Hit)
							&& Hit.Distance < Params.RiverValleyBottom + Params.RiverValleySlope)
						{
							continue;
						}

						FBoulder Boulder;
						Boulder.Mesh = Random.RandRange(0, Kit.BoulderMeshes - 1);
						Boulder.Cell = Cell;
						// the small ones come more often
						Boulder.Scale = float(FMath::Lerp(double(Kit.BoulderSizeMin), double(Kit.BoulderSizeMax), FMath::Square(Random.FRand())));
						const FVector3d Up = Position.GetSafeNormal();
						const FQuat4d Lean = FQuat4d(FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitX()),
							FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)));
						const FQuat4d Tilt(FVector3d(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0), 0.0).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitX()),
							FMath::DegreesToRadians(Random.FRand() * Kit.BoulderTilt));
						const FQuat4d Spin(FVector3d::UnitZ(), Random.FRand() * UE_DOUBLE_TWO_PI);
						Boulder.Rotation = FQuat4f(Lean * Tilt * Spin);
						Boulder.Position = FVector3f(Position - Up * (Boulder.Scale * Kit.BoulderSink));
						Result.Boulders[Slot].Add(Boulder);
					}
				}
			}
		});
	}

	// every point of such a side is built by both cells, so in a build of every chunk each comes twice
	if (NumSlots == Grid.GetNumChunks())
	{
		TMap<FIntVector, int32> RidgeCounts;
		for (const TArray<FIntVector>& Keys : RidgeKeysBySlot)
		{
			for (const FIntVector& Key : Keys)
			{
				++RidgeCounts.FindOrAdd(Key);
			}
		}
		for (const TPair<FIntVector, int32>& Count : RidgeCounts)
		{
			if (Count.Value >= 2)
			{
				continue;
			}
			// the two cells may put a point a few millionths apart that lies on the edge of rounding, so the other may be in a key next to it
			bool bMatched = false;
			for (int32 Step = 0; Step < 27 && !bMatched; ++Step)
			{
				const FIntVector Offset(Step % 3 - 1, Step / 3 % 3 - 1, Step / 9 - 1);
				bMatched = Offset != FIntVector::ZeroValue && RidgeCounts.Contains(Count.Key + Offset);
			}
			Result.UnmatchedRidgePoints += bMatched ? 0 : 1;
		}
		Result.RidgePoints = RidgeCounts.Num();
	}

	// normals of vertices on sides sum those of every cell that meets there, also in the halo, in the detailed and the coarse mesh apart
	const auto SumSharedNormals = [&](const TArray<TArray<FSharedVertex>>& Lists)
	{
		int32 NumShared = 0;
		for (const TArray<FSharedVertex>& Shared : Lists)
		{
			NumShared += Shared.Num();
		}

		TMap<FIntVector, FVector3d> SharedNormals;
		SharedNormals.Reserve(NumShared / 2);
		for (const TArray<FSharedVertex>& Shared : Lists)
		{
			for (const FSharedVertex& Vertex : Shared)
			{
				SharedNormals.FindOrAdd(Vertex.Key) += RawSections[Vertex.Section].NormalSums[Vertex.Vertex];
			}
		}
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			for (const FSharedVertex& Vertex : Lists[Slot])
			{
				RawSections[Vertex.Section].NormalSums[Vertex.Vertex] = SharedNormals[Vertex.Key];
			}
		}
	};
	ParallelFor(2, [&](int32 Index) { SumSharedNormals(Index == 0 ? SharedBySlot : CoarseSharedBySlot); });
	RawSections.SetNum(HaloSection);

	// the mean height of the peaks of mountain cells, which V of the first UV measures heights by
	if (MeanPeak <= 0.0)
	{
		double PeakSum = 0.0;
		int32 NumPeaks = 0;

		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			const FCellMesh& Mesh = Result.CellMeshes[Cell];

			if (Levels[Cell] == MountainLevel && Mesh.Section != INDEX_NONE && Mesh.NumVertices > 0)
			{
				const FRawSection& Section = RawSections[ChunkSlots[Mesh.Section / 2] * 2 + Mesh.Section % 2];
				double Peak = 0.0;
				for (int32 Vertex = Mesh.FirstVertex; Vertex < Mesh.FirstVertex + Mesh.NumVertices; ++Vertex)
				{
					Peak = FMath::Max(Peak, FVector3d(Section.Positions[Vertex]).Length() - Params.Radius);
				}
				PeakSum += Peak;
				++NumPeaks;
			}
		}
		MeanPeak = NumPeaks > 0 ? FMath::Max(PeakSum / NumPeaks, 1.0) : 1.0;
	}
	Result.MeanPeak = MeanPeak;
	Result.CellRelief = MoveTemp(Context.Relief);

	Result.Sections.SetNum(RawSections.Num());

	ParallelFor(RawSections.Num(), [&](int32 Index)
	{
		FRawSection& Raw = RawSections[Index];
		FSection& Section = Result.Sections[Index];
		Section.Normals.SetNumUninitialized(Raw.Positions.Num());
		Section.Colors.SetNumUninitialized(Raw.Positions.Num());

		for (int32 Vertex = 0; Vertex < Raw.Positions.Num(); ++Vertex)
		{
			const FVector3d Up = FVector3d(Raw.Positions[Vertex]).GetSafeNormal();
			const FVector3d Normal = Raw.NormalSums[Vertex].GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Up);
			Section.Normals[Vertex] = FVector(Normal);

			const int32 Look = Raw.Looks[Vertex] & 7;
			const double Height = FVector3d(Raw.Positions[Vertex]).Length() - Params.Radius;
			const double FacetLow = Params.FacetLow[Look];
			const double FacetHigh = Params.FacetHigh[Look];
			Raw.MeshUVs[Vertex].X = FacetHigh > FacetLow ? FMath::SmoothStep(FacetLow, FacetHigh, Height + Params.Radius) : 0.0;

			// the painted tiles draw colors of their own, the surface none
			Section.Colors[Vertex] = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

			// V of the first UV is the height above the sea as a share of the mean peak of mountains, by which M_HexPlanet fades the grid line
			// between GridFadeStart and GridFadeEnd, as on mountains it only clutters the rock, and below the sea over GridSeaFade, as the water
			// draws its own grid lines (WaterGrid)
			Raw.GridUVs[Vertex].Y = float(Height / MeanPeak);
		}

		Section.Positions = MoveTemp(Raw.Positions);
		Section.Triangles = MoveTemp(Raw.Triangles);
		Section.GridUVs = MoveTemp(Raw.GridUVs);
		Section.MeshUVs = MoveTemp(Raw.MeshUVs);
		Section.CellUVs = MoveTemp(Raw.CellUVs);
		Section.LocalUVs = MoveTemp(Raw.LocalUVs);
		Raw = FRawSection();
	});

	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		Result.CoarseSections.Add(MoveTemp(Result.Sections[CoarseFirst + Slot]));
	}
	Result.Sections.SetNum(CoarseFirst);

	// the real coast: on each edge of a triangle with one end above the sea and one below, the point between them at the sea, each edge once
	Result.Coasts.SetNum(NumSlots);
	ParallelFor(NumSlots, [&](int32 Slot)
	{
		for (int32 Part = 0; Part < 2; ++Part)
		{
			const FSection& Section = Result.Sections[Slot * 2 + Part];
			TSet<uint64> Edges;
			for (int32 Index = 0; Index + 2 < Section.Triangles.Num(); Index += 3)
			{
				for (int32 Edge = 0; Edge < 3; ++Edge)
				{
					const int32 A = Section.Triangles[Index + Edge];
					const int32 B = Section.Triangles[Index + (Edge + 1) % 3];
					const double HeightA = Section.Positions[A].Length() - Params.Radius;
					const double HeightB = Section.Positions[B].Length() - Params.Radius;
					if ((HeightA > 0.0) == (HeightB > 0.0))
					{
						continue;
					}
					bool bSeen = false;
					Edges.Add((uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B)), &bSeen);
					if (!bSeen)
					{
						Result.Coasts[Slot].Add(FVector3f(FMath::Lerp(Section.Positions[A], Section.Positions[B], HeightA / (HeightA - HeightB)).GetSafeNormal()));
					}
				}
			}
		}
	});

	return Result;
}

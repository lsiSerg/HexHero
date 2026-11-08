// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetKit.h"

class FHexSphereGrid;
class UHexPlanetConfig;
class UHexPlanetKitConfig;

namespace HexPlanetRivers
{
	struct FCurve;
}

/**
 *  Builds the surface of the planet: two mesh sections per chunk of the grid (FHexSphereCell::Chunk), the second with the mountains, from the kit
 *  of each level, and a coarse section per chunk for far away, 12 triangles a cell. It can build only some chunks, so a change of levels rebuilds only the chunks it reaches. Everything but LoadKit reads only its
 *  arguments and no UObject, so it runs on any thread.
 *
 *  A cell's mesh depends on the levels of its neighbors (which of them share its level, the heights of its corners and sides) and on the massif
 *  steps of itself and its neighbors, and the normals of vertices on its sides sum those of the cells that meet there. So a changed level or
 *  massif step changes the mesh of the cell and its neighbors, and the normals one ring further (FindAffectedChunks). The normals on the sides
 *  towards chunks that are not built are summed with those of the cells there, which are built again only for their sums (the halo), so a built
 *  chunk meets the others without a seam.
 */
namespace HexPlanetSurface
{
	constexpr int32 MountainLevel = 2;


	/** Returns the section of a cell: two per chunk, the second with the mountains */
	inline int32 GetSection(int32 Chunk, int32 Level) { return Chunk * 2 + (Level == MountainLevel ? 1 : 0); }

	/** Columns of the texture of cell marks, which has a pixel per cell, row by row (AHexPlanet::CellMarks) */
	constexpr int32 MarkColumns = 2048;

	/** Returns the third UV of a vertex of a cell: its pixel in the texture of cell marks. UVs are half floats, which hold whole numbers
	 *  exactly only up to 2048, so the column and the row go in apart */
	inline FVector2D GetCellUV(int32 Cell) { return FVector2D(Cell % MarkColumns, Cell / MarkColumns); }

	/** Returns the fourth UV of a vertex of a cell: where it lies in a regular cell of radius 1 whose side K faces the angle K turns of a
	 *  side, from its weights between the corners of side Side, so M_HexPlanet finds the side nearest a pixel by its angle */
	inline FVector2D GetLocalUV(int32 NumSides, int32 Side, double WFrom, double WTo)
	{
		const double Step = UE_DOUBLE_TWO_PI / NumSides;
		const auto Corner = [Step](double Index) { return FVector2D(FMath::Cos((Index + 0.5) * Step), FMath::Sin((Index + 0.5) * Step)); };
		return Corner(Side - 1) * WFrom + Corner(Side) * WTo;
	}

	/**
	 *  The relief of a cell set by hand with the brush (relief.json of the planet): the version of its piece and which of its allowed turns it
	 *  takes, whether it is mirrored, each -1 to leave it to the hash, and a factor on its height. The version is one of the pieces with the
	 *  same sides and the turn one that gives the same set of neighbors, so the sides that join the neighbors stay as they were
	 */
	struct FReliefOverride
	{
		int8 Version = -1;
		int8 Turn = -1;
		int8 Mirror = -1;
		float Scale = 1.0f;

		bool IsEmpty() const { return Version < 0 && Turn < 0 && Mirror < 0 && FMath::IsNearlyEqual(Scale, 1.0f); }
		bool operator==(const FReliefOverride& Other) const
		{
			return Version == Other.Version && Turn == Other.Turn && Mirror == Other.Mirror && FMath::IsNearlyEqual(Scale, Other.Scale);
		}
	};

	/** Overrides by cell */
	using FReliefOverrides = TMap<int32, FReliefOverride>;

	/** Pieces of one level, read from its meshes */
	struct FKitLevel
	{
		HexPlanetKit::FPlacement Placement;
		TMap<int32, TArray<int32>> PiecesByMask;
		int32 MassifMaxSteps = 0;
		/** See FHexPlanetKit */
		bool bMirror = false;
		float HeightScaleMin = 1.0f;
		float HeightScaleMax = 1.0f;
		float HeightNoiseCells = 4.0f;
		float HeightJitter = 0.0f;
		float PeakChance = 0.0f;
		float PeakScale = 0.0f;
		float CenterShift = 0.0f;
		float HeightScaleFloor = 0.0f;
		int32 LoneNeighbors = -1;
		float LoneHeightMin = 1.0f;
		float LoneSharpness = 1.0f;
		float LoneShift = 1.0f;
		float LoneBase = 0.0f;
		float SpurHeightScale = 1.0f;
		int32 LoneSummitStyles = 0;
		float SplitNoiseCells = 3.0f;
		float SplitThreshold = 0.0f;
		float SummitBase = 0.55f;
		float SummitHeight = 1.0f;
		float SummitWidth = 1.0f;
		float SharpnessMin = 1.0f;
		float SharpnessMax = 1.0f;
		/** In radians */
		double Twist = 0.0;
		float RidgeMassifShare = 0.4f;
		float RidgeMassifLow = 0.2f;
		FLinearColor Color = FLinearColor::White;
		FLinearColor SlopeColor = FLinearColor::Gray;
	};

	/** Heights of a piece at the points of the coarse mesh of a cell: its center, and by sector of the piece the middle of that side and the
	 *  corner at its end, each the height of the vertex of the piece nearest to that point */
	struct FCoarsePiece
	{
		double Center = 0.0;
		double Middles[6] = {};
		double Ends[6] = {};
	};

	/** The kits of all levels, read once, which never change after */
	struct FKit
	{
		TMap<int32, FKitLevel> Levels;
		TArray<HexPlanetKit::FPiece> Pieces;
		/** By piece, for the coarse mesh */
		TArray<FCoarsePiece> CoarsePieces;
		TArray<FString> PieceNames;
		int32 UnreadMeshes = 0;

		/** Boulders at the foot of mountains (FHexPlanetBoulders): how many meshes there are, 0 for none, and where they go */
		int32 BoulderMeshes = 0;
		int32 BoulderMaxPerSide = 0;
		int32 BoulderMinLevel = -1;
		float BoulderSizeMin = 4.0f;
		float BoulderSizeMax = 12.0f;
		float BoulderSpread = 0.25f;
		float BoulderSink = 0.35f;
		float BoulderTilt = 25.0f;
	};

	/** A boulder at the foot of a mountain, in the space of the planet */
	struct FBoulder
	{
		FVector3f Position = FVector3f::ZeroVector;
		FQuat4f Rotation = FQuat4f::Identity;
		float Scale = 1.0f;
		/** Its mesh, and the cell of mountains it lies at, whose biome gives its rock */
		int32 Mesh = 0;
		int32 Cell = INDEX_NONE;
	};

	/** Reads the pieces of every kit from their meshes, on the game thread. A missing config gives an empty kit, so every cell is a flat tile */
	TSharedRef<const FKit> LoadKit(const UHexPlanetKitConfig* Config, double HexRadius);

	/** Values of the planet config that the surface needs, copied so a build off the game thread reads no UObject */
	struct FParams
	{
		double Radius = 0.0;
		double LevelHeight = 25.0;
		double ShallowDepth = 22.0;
		double DeepDepth = 70.0;
		FLinearColor FlatColors[5];
		FLinearColor SlopeColors[5];
		FLinearColor SandColor;
		FLinearColor SnowColor;
		float SlopeStart = 0.0f;
		float SlopeEnd = 1.0f;
		float SnowHeight = 0.0f;
		float SnowFade = 1.0f;
		float SnowFlatStart = 0.0f;
		float SnowFlatEnd = 1.0f;
		float BeachHeight = 0.0f;
		float BeachFade = 1.0f;
		double HexRadius = 100.0;
		double CoastWobble = 0.0;
		double CoastWobbleScale = 100.0;
		double CoastWobbleDetail = 0.0;
		/** By level from -2: the radii where the faces of high ground begin and are full */
		double FacetLow[5] = {};
		double FacetHigh[5] = {};
		/** The valley along a river (RiverValleyDepth, RiverValleyBottom, RiverValleySlope of DA_HexPlanet) */
		double RiverValleyDepth = 0.0;
		double RiverValleyBottom = 20.0;
		double RiverValleySlope = 20.0;

		static FParams Make(const UHexPlanetConfig& Config, const FKit& Kit, double Radius);

		/** Returns the radius of the top of a cell of a level */
		double GetTop(int32 Level) const;

		/** Sets the wobble of the coast of a placement */
		void SetWobble(HexPlanetKit::FPlacement& Placement) const;
	};

	/** Where the mesh of a cell lies in the sections of the surface */
	struct FCellMesh
	{
		int32 Section = INDEX_NONE;
		int32 FirstVertex = 0;
		int32 NumVertices = 0;
		int32 FirstIndex = 0;
		int32 NumIndices = 0;
	};

	/** Mesh of one section, ready for a procedural mesh */
	struct FSection
	{
		TArray<FVector> Positions;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> GridUVs;
		/** The second UV: in X the share of the faces of high ground (FHexPlanetKit::FacetHeightStart), which M_HexPlanet shades flat, and in
		 *  Y how much of a gully a vertex is (FHexPlanetKit::GullyHeight), which it darkens and lets snow lie in */
		TArray<FVector2D> MeshUVs;
		/** The cell of each vertex and where it lies in it, see GetCellUV and GetLocalUV */
		TArray<FVector2D> CellUVs;
		TArray<FVector2D> LocalUVs;
		/** Black, as only the tiles of painting draw colors of their own */
		TArray<FLinearColor> Colors;
	};

	/** Result of a build */
	struct FBuild
	{
		/** Chunks built, ascending */
		TArray<int32> Chunks;

		/** Two sections per chunk of Chunks, in their order */
		TArray<FSection> Sections;

		/** One coarse section per chunk of Chunks, in their order: each cell a fan of its center, corners and side middles, at the heights of
		 *  its piece there, which the planet draws in place of the two sections from far away */
		TArray<FSection> CoarseSections;

		/** By cell, set for the cells of the chunks built */
		TArray<FCellMesh> CellMeshes;
		TArray<int32> CellPieces;
		TArray<int8> CellTurns;
		/** By cell, set for the cells of the chunks built: the version of its piece and how many it could take (X and Y), and the same of its
		 *  turns among those its neighbors allow */
		TArray<FIntPoint> CellVersions;
		TArray<FIntPoint> CellTurnChoices;

		/** By cell, set for the cells of the chunks built: whether its piece is mirrored, and the factor of its height */
		TArray<bool> CellMirrored;
		TArray<float> CellRelief;

		/** Boulders by chunk of Chunks, in their order */
		TArray<TArray<FBoulder>> Boulders;

		/** Where the ground crosses the sea, by chunk of Chunks, in their order: on each edge of a triangle with one end above the sea and one
		 *  below, the point between them at the sea, as a direction from the center. The real coast, which winds across the cells, for the
		 *  distance to the coast of the sea field */
		TArray<TArray<FVector3f>> Coasts;

		/** Mean height of the peaks of mountain cells, by which V of the first UV measures heights */
		double MeanPeak = 1.0;

		/** Cells of the chunks built that have no piece */
		int32 FlatTiles = 0;

		/** In a build of every chunk: the points on sides between two cells of a kit that models those sides itself (mountains), and those of
		 *  them that only one cell built, which would be a crack */
		int32 RidgePoints = 0;
		int32 UnmatchedRidgePoints = 0;
	};

	/** Returns how many steps each cell of a kit with a massif lies inside its group of its level, counted from the cells at its edge */
	TArray<int32> FindMassifSteps(const FHexSphereGrid& Grid, const FKit& Kit, TConstArrayView<int8> Levels);

	/**
	 *  Returns the factor of the height of each cell (FHexPlanetKit::HeightScaleMin and the rest), from the place and the level of the cell
	 *  alone, so it changes only with the level. 1 for a cell whose kit has none, and for pentagons
	 */
	TArray<float> FindRelief(const FHexSphereGrid& Grid, const FKit& Kit, const FParams& Params, TConstArrayView<int8> Levels, const FReliefOverrides* Overrides = nullptr);

	/** Returns the chunks, ascending, whose sections change when the levels and massif steps change from the old ones to the new */
	TArray<int32> FindAffectedChunks(const FHexSphereGrid& Grid, TConstArrayView<int8> OldLevels, TConstArrayView<int8> NewLevels,
		TConstArrayView<int32> OldMassifSteps, TConstArrayView<int32> NewMassifSteps, TConstArrayView<int32> ChangedCells = {});

	/** Returns the cells whose overrides differ between two sets, which change as a level does */
	TArray<int32> FindChangedOverrides(const FReliefOverrides& Old, const FReliefOverrides& New);

	/** Returns every chunk of the grid */
	TArray<int32> GetAllChunks(const FHexSphereGrid& Grid);

	/**
	 *  Builds the sections of some chunks in parallel. A MeanPeak of 0 or less measures it from the mountains, which needs every chunk. Rivers is
	 *  the line of the rivers (HexPlanetRivers::BuildCurve), by which the ground lowers into a valley along each river and the vertices know
	 *  where its water lies, a function of the place and that line alone, so the cells that share a point lower it alike; null for none
	 */
	FBuild Build(const FHexSphereGrid& Grid, const FKit& Kit, const FParams& Params, TConstArrayView<int8> Levels, TConstArrayView<int32> MassifSteps,
		TConstArrayView<int32> Chunks, double MeanPeak, const FReliefOverrides* Overrides = nullptr, const HexPlanetRivers::FCurve* Rivers = nullptr);
}

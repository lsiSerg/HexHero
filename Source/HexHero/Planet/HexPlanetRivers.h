// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HexPlanetRivers.generated.h"

class AHexPlanet;
class FHexSphereGrid;
class UHexPlanetConfig;
class UHierarchicalInstancedStaticMeshComponent;
class UTexture2D;

/**
 *  Rivers run along the sides of the cells of a planet, from corner to corner, so for movement a river lies between two cells: crossing it costs
 *  more (RiverCrossCost of DA_HexPlanet) unless a bridge stands on that side. They look like rivers that wind across the cells, as the water
 *  winds off its side by up to RiverMeander, never at the corners nor under a bridge.
 *
 *  A side may hold a river only between two cells of land below the mountains (plains and hills), so a river starts at the foot of mountains
 *  and ends at a corner on the coast, where the sea takes it, or in another river. Which way each side flows is not stored but found from the
 *  rivers as a net (BuildNet): towards the nearest mouth along the rivers, or towards the lowest corner of a net with none.
 */
namespace HexPlanetRivers
{
	/** Returns the key of the side between two neighboring cells, the same either way */
	inline uint64 GetEdgeKey(int32 A, int32 B)
	{
		return (uint64(uint32(FMath::Min(A, B))) << 32) | uint64(uint32(FMath::Max(A, B)));
	}

	/** Returns the two cells of a key, the lower first */
	inline void GetEdgeCells(uint64 Key, int32& OutA, int32& OutB)
	{
		OutA = int32(Key >> 32);
		OutB = int32(Key & 0xFFFFFFFFu);
	}

	/** Returns true if a river may run between two cells of these levels */
	inline bool IsLevelAllowed(int32 Level) { return Level == 0 || Level == 1; }

	/** Bits of a cell in a net: the sides with a river in the low byte, and in the high byte which of them flow from corner K - 1 to corner K
	 *  (counterclockwise around the cell seen from outside, with the cell on the left of the water); the others flow the other way */
	constexpr uint16 SideMask = 0x3F;
	constexpr int32 FlowShift = 8;

	/** Sectors of a cell in CellRivers, one pixel each, and the width of RiverSegments, in texels (UHexPlanetRivers::UpdateTexture) */
	constexpr int32 MaxSectors = 6;
	constexpr int32 SegmentColumns = 4096;

	/**
	 *  Returns the bits of every cell (SideMask, FlowShift) for the rivers on the sides of Edges that the levels allow, so a side whose cell
	 *  became sea or a mountain drops out of the net until it is painted back. Each side flows towards the nearest corner on the coast along
	 *  the rivers, a net without one towards its lowest corner
	 */
	TArray<uint16> BuildNet(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, const TSet<uint64>& Edges);

	/** Returns the cells whose bits differ between two nets, both cells of each changed side */
	TArray<int32> FindChangedCells(TConstArrayView<uint16> Old, TConstArrayView<uint16> New);

	/** What BuildCurve reads of DA_HexPlanet, in world units */
	struct FCurveParams
	{
		double Radius = 1.0;
		double HexRadius = 100.0;
		double RiverWidth = 28.0;
		/** How far the water winds off its smooth line, and the size of the noise that winds it */
		double Meander = 12.0;
		double MeanderScale = 70.0;
		/** Over how much of a side from the end of a line the winding fades in, so lines meet where they join */
		double MeanderFade = 0.35;
		/** How far from its source a river widens from a spring to its full width, as a share of HexRadius */
		double SourceTaper = 0.9;
		/** How far a river goes on into the sea past the corner on the coast where it ends */
		double MouthLength = 40.0;
		/** How many times the line through the middles of the sides is rounded off (Chaikin) */
		int32 Smoothing = 3;
		/** How straight a line runs: how far each corner between two sides is pulled onto the chord between their middles (0 along the sides, 1
		 *  straight through their middles), how many passes straighten it further over several sides, and how far it may leave its sides at most,
		 *  in world units (0 for no limit) */
		double CornerCut = 1.0;
		int32 Relax = 0;
		double MaxOffset = 0.0;
		/** How far from the line a point still finds it (FindNearest) */
		double Reach = 80.0;
	};

	/**
	 *  The line the water of the rivers follows, the same for every cell, so the water, its banks and its valley join across the sides: the
	 *  rivers of the net as lines along their flow, a line going on through a corner where a smaller river joins it, each through the corner it
	 *  starts at, the middles of its sides and the corner it ends at, rounded off, so it cuts the corners of the zigzag of the sides and runs
	 *  straight where they do, then wound by a noise of the place. A river that ends on the coast goes on into the sea by MouthLength.
	 */
	struct FCurve
	{
		double Radius = 1.0;
		/** The points of the lines, as directions from the center */
		TArray<FVector3d> Points;
		/** How much nearer to its edge a point of a line is drawn, in world units, so a river narrows to a spring at its source */
		TArray<float> Narrowing;
		/** How far along its line each point lies from the start of the line, in world units */
		TArray<float> Lengths;
		/** 1 where the river flows from point I to point I + 1, -1 where it flows back, as a line goes on through a corner where its flow parts
		 *  or meets */
		TArray<int8> FlowSigns;
		/** True where point I joins point I + 1 */
		TArray<bool> Links;
		/** The two cells of the side each point lies along, which tell whether the river freezes there */
		TArray<FIntPoint> PointCells;
		/** The first points of the segments within Reach of any point of each cell */
		TArray<TArray<int32>> CellSegments;

		/** A corner on the coast where a river ends: its place, the way it goes on into the sea, and the two cells of its last side */
		struct FMouth
		{
			FVector3d Position = FVector3d::ZeroVector;
			FVector3d Direction = FVector3d::ZeroVector;
			int32 CellA = INDEX_NONE;
			int32 CellB = INDEX_NONE;
		};
		TArray<FMouth> Mouths;

		bool IsEmpty() const { return Points.IsEmpty(); }
	};

	/** How far from the line of a river anything of it reaches, in world units: its water, its banks, the gap it keeps from the run-up of the
	 *  sea and its valley, plus a fifth of a cell, so the distance M_HexPlanet takes from the vertices is right wherever it is used */
	double GetReach(const UHexPlanetConfig& Settings);

	/** Builds the line of the rivers of a net (BuildNet) for these levels */
	TSharedRef<const FCurve> BuildCurve(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<uint16> Net, const FCurveParams& Params);

	/** The nearest point of the line of the rivers */
	struct FCurveHit
	{
		/** In world units, plus the narrowing there */
		double Distance = TNumericLimits<double>::Max();
		/** The way it flows, along the line, and the way the line runs, by which the side is told */
		FVector3d Flow = FVector3d::ZeroVector;
		FVector3d LineDirection = FVector3d::ZeroVector;
		/** The point, as a direction from the center, and how far along its line it lies */
		FVector3d Point = FVector3d::ZeroVector;
		double Along = 0.0;
		/** The first point of its segment */
		int32 Segment = INDEX_NONE;
		/** True if the point asked for lies to the left of the flow, seen from outside */
		bool bLeft = false;
	};

	/** Finds the nearest point of the line to a point of a cell, a direction from the center; false with none within Reach of the cell */
	bool FindNearest(const FCurve& Curve, int32 Cell, const FVector3d& Direction, FCurveHit& Out);



	/** What Generate reads of DA_HexPlanet */
	struct FGenerateParams
	{
		int32 Seed = 0;
		float Density = 1.0f;
		int32 MinLength = 6;
		int32 MaxLength = 120;
		int32 FlatSteps = 3;
		double TurnPenalty = 0.5;
		double Wander = 0.2;
		int32 SourceSeaDistance = 4;
		int32 BridgeSpacing = 9;
	};

	/**
	 *  Generates rivers on the land of a planet: each starts at a corner by the foot of mountains or hills, at least SourceSeaDistance rings of
	 *  cells inland, and walks along sides of cells to the corner nearest the nearest cell of the sea, never higher, up to FlatSteps in a row
	 *  that bring it little nearer, until it reaches a corner on the coast or another river, and it keeps away from the rivers before it. As
	 *  at each corner it turns to the left or to the right, it pays TurnPenalty for turning the same way twice in a row, which takes it off to
	 *  the side, and Wander of randomness. A river that ends nowhere, or is shorter than MinLength, is left out. Bridges go about every
	 *  BridgeSpacing sides of a river, never at its ends
	 */
	void Generate(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, const FGenerateParams& Params, TSet<uint64>& OutEdges, TSet<uint64>& OutBridges);
}

/**
 *  The rivers and bridges of a planet (HexPlanetRivers), read from rivers.json of the planet or generated when it has none, and painted with the
 *  brush. The surface lowers a valley along each (HexPlanetSurface, by the net the planet builds it with), and M_HexPlanet draws the water from
 *  the segments of the line of the rivers near each sector of each cell (UpdateTexture, RIVER_CODE), from which it finds the exact distance to
 *  the nearest for each pixel. The bridges are instances of
 *  BridgeMesh of DA_HexPlanet on the middles of their sides.
 */
UCLASS()
class UHexPlanetRivers : public USceneComponent
{
	GENERATED_BODY()

public:

	/** Reads rivers.json of the planet, or generates the rivers. Call once the planet has its grid and levels, before its surface is built */
	void Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings);

	/** Returns true if the rivers were generated, as the planet had no rivers.json */
	bool WasGenerated() const { return bGenerated; }

	/** Writes rivers.json into a folder, leaving out the sides the levels do not allow */
	bool Save(const FString& Folder, FString& OutError) const;

	/** What stands on a side: 0 nothing, 1 a river, 2 a river with a bridge, by GetEdgeKey */
	uint8 GetEdge(uint64 Key) const;

	/** Sets the state of sides (GetEdge), shows the water at once and has the planet lower the valleys again */
	void SetEdges(const TMap<uint64, uint8>& Changes);

	/** Returns true if a river may run along the side between two cells, by their levels now */
	bool IsEdgeAllowed(int32 A, int32 B) const;

	/** Returns true if a river runs between two neighboring cells that their levels allow, and if a bridge crosses it there */
	bool IsRiver(int32 A, int32 B) const;
	bool HasBridge(int32 A, int32 B) const;

	/** Returns the factor on the cost of a move from a cell to its neighbor: RiverCrossCost across a river without a bridge, else 1 */
	float GetCrossingFactor(int32 From, int32 To) const;

	/** Returns the net of the rivers for these levels (HexPlanetRivers::BuildNet) */
	TArray<uint16> BuildNet(TConstArrayView<int8> Levels) const;

	/** Takes the net the surface shows now and its line, which places the bridges and tells the objects where the water is */
	void SetShownNet(TArray<uint16> Net, TSharedPtr<const HexPlanetRivers::FCurve> Curve);

	/** Returns the line of the rivers the surface shows, or null before the first build */
	TSharedPtr<const HexPlanetRivers::FCurve> GetShownCurve() const { return ShownCurve; }

	/** Returns what BuildCurve reads, from DA_HexPlanet and the radius of the planet */
	HexPlanetRivers::FCurveParams GetCurveParams() const;

	/** Sets how straight the rivers run (RiverCornerCut, RiverRelax and RiverMaxOffset of DA_HexPlanet as X, Y and Z) for trying them, or back to
	 *  the asset with none; the planet then builds its surface again (hex.PlanetRiverShape) */
	void SetShapeOverride(TOptional<FVector3d> Shape) { ShapeOverride = Shape; }

	/** Returns the net the surface shows */
	TConstArrayView<uint16> GetShownNet() const { return ShownNet; }

	/** Returns the distance in world units from a point of a cell to the line of the nearest river shown, or a large value with none near */
	double GetRiverDistance(int32 Cell, const FVector3d& Position) const;

	/** Returns the textures of the rivers that the materials of the surface read (RIVER_CODE): CellRivers, a pixel per sector of each cell with
	 *  the first and the number of the segments of the line near it, and RiverSegments, two texels per segment (UpdateTexture) */
	UTexture2D* GetTexture() const { return Texture; }
	UTexture2D* GetSegmentTexture() const { return SegmentTexture; }

	/** Writes the textures again, as when the biomes changed whether a river freezes */
	void RefreshTexture() { UpdateTexture(); }

	/** Returns a line for the info at the cursor: the rivers on the sides of a cell, or an empty string */
	FString GetCellText(int32 Cell) const;

	/** Returns how many sides hold a river */
	int32 GetNumEdges() const { return Edges.Num(); }

	/** Places the bridges again on the surface shown now, each where the line of its river passes nearest the middle of its side, across it */
	void UpdateBridges();

private:

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetConfig> Settings;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Texture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SegmentTexture;

	/** Texels of SegmentTexture, and how many rows it has room for */
	TArray<FVector4f> SegmentTexels;
	int32 SegmentRows = 0;

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Bridges;

	/** What stands on each side with something: 1 a river, 2 a river with a bridge */
	TMap<uint64, uint8> Edges;

	/** The net the surface shows and its line */
	TArray<uint16> ShownNet;
	TOptional<FVector3d> ShapeOverride;
	TSharedPtr<const HexPlanetRivers::FCurve> ShownCurve;

	/** Pixels of Texture */
	TArray<FColor> Pixels;

	bool bGenerated = false;

	/** Returns the sides with a river, with or without a bridge */
	TSet<uint64> GetRiverKeys() const;

	/** Writes the textures from the line of the rivers shown and the biomes: for each sector of each cell (the triangle from its center to its
	 *  corners K - 1 and K) the segments of the line, every other point of it, that come near enough for anything of a river to show there,
	 *  so M_HexPlanet finds the exact distance to the nearest of them for each pixel. Makes the textures the first time and whenever the
	 *  segments no longer fit, and then gives them to the planet's materials again */
	void UpdateTexture();
};

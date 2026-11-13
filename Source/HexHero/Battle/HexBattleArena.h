// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexBattle.h"
#include "HexHeroes.h"
#include "HexUiConfig.h"
#include "HexBattleArena.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UHexBattles;
class UPointLightComponent;
class UProceduralMeshComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class ADirectionalLight;
struct FPostProcessSettings;
struct FHexBattleGround;
class UHexBattleConfig;

/** A line that floats up over the field for a moment: damage, healing, morale, a spell */
struct FHexBattlePopup
{
	FVector World = FVector::ZeroVector;
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float Age = 0.0f;
	float Life = 1.2f;
};

/**
 *  The field of a battle (UHexBattles), a world of its own far below the planet (ArenaOrigin of DA_HexBattle), which waits hidden meanwhile,
 *  as the developer chose on 2026-11-12: the ground of the biome fought on, the lines of the hexes, obstacles and what stands around the field,
 *  a camera that looks at it from the side of the attacker, a sky and the light of its own, and the figures of the stacks (HexUnitFigure). It
 *  plays what happens (FHexBattleEvent) one after the other, the figures walking, striking, shooting, being struck and dying while numbers
 *  float up, gives the orders of the side the game fights for, and takes the person's: a click on a hex it reaches walks there, on an enemy
 *  it strikes from the hex beside it nearest the cursor or shoots, and with a spell chosen casts it. Shown while UHexBattles::IsShown
 */
UCLASS()
class AHexBattleArena : public AActor
{
	GENERATED_BODY()

public:

	AHexBattleArena();

	virtual void Tick(float DeltaSeconds) override;

	/** Builds the field of the battle the battles hold and takes the sun of the level; the post process of the camera of the planet */
	void Show(UHexBattles* InBattles, const FPostProcessSettings& PostProcess);

	/** Takes the field away and gives the sun back */
	void Hide();

	/** Puts the camera this far from the point it looks at, this many degrees down, at once (hex.Battle camera) */
	void SetCamera(float Distance, float PitchDegrees);

	/** The person moves the camera, as the developer asked on 2026-11-12: Pan moves the point it looks at, X to the right of the screen and Y
	 *  up it, a share of CameraScrollSpeed; Turn turns it around that point by degrees, clockwise seen from above; Zoom brings it nearer by
	 *  notches of the wheel */
	void MoveCamera(const FVector2D& Pan, float Turn, float DeltaSeconds);
	void ZoomCamera(float Notches);

	/** Where a ray of the world meets the ground of the field; and the point the camera looks at moved so that a point of the ground that was
	 *  grabbed lies under the cursor again, for dragging the field */
	bool GetGroundPoint(const FVector& Origin, const FVector& Direction, FVector& OutPoint) const;
	void DragCamera(const FVector& Grabbed, const FVector& Under);

	/** What a click where the cursor is would do, as the cursor of the game shows it, and for a strike the middles of the hex it strikes from
	 *  and of the one it strikes, by which the cursor turns */
	EHexCursor GetCursor(FVector& OutFrom, FVector& OutTo) const;

	bool IsShowing() const { return bShowing; }

	/** Where the cursor is, as a ray of the world, and the hex and the unit under it */
	void SetHover(const FVector& Origin, const FVector& Direction);
	FHexCoord GetHoverHex() const { return HoverHex; }
	bool HasHover() const { return bHover; }
	int32 GetHoverUnit() const;

	/** The person's orders: a left click where the cursor is, the right one, and the buttons */
	void Click();
	void Cancel();
	void OrderWait();
	void OrderDefend();
	void OrderAuto();
	void OrderQuick();
	void OrderRetreat();
	void OrderClose();

	/** Chooses a spell to cast with the next click, INDEX_NONE for none; and the one chosen */
	void AimSpell(int32 Spell);
	int32 GetAimedSpell() const { return Aimed; }

	/** Whether the person may give an order now: it is the turn of a unit of the side it fights for, nothing plays and the battle goes on */
	bool CanOrder() const;

	/** Whether what happened is still being played */
	bool IsPlaying() const { return Current.IsSet() || !Pending.IsEmpty(); }

	/** What the panel shows: a line about the unit or hex under the cursor and what a click there does, the lines of the log, the floating
	 *  lines, the count of each unit as it plays and where it stands */
	FString GetHoverText() const;
	/** Whose turn it is, and what a click where the cursor is would do with what it is likely to deal and kill, as the developer asked on
	 *  2026-11-12 for a hint at the bottom of the screen */
	FString GetTurnText() const;
	FString GetActionText() const;
	const TArray<FString>& GetLog() const { return Log; }
	const TArray<FHexBattlePopup>& GetPopups() const { return Popups; }
	int32 GetShownCount(int32 Unit) const { return ShownCounts.IsValidIndex(Unit) ? ShownCounts[Unit] : 0; }
	bool GetUnitPoint(int32 Unit, FVector& OutWorld) const;

	/** Where a part of the wall stands, over its top, for its health to show */
	bool GetWallPoint(int32 Wall, FVector& OutWorld) const;

	UHexBattles* GetBattles() const { return Battles.Get(); }

	/** The hex of the field at a point of the world, and its middle */
	FVector GetHexPoint(const FHexCoord& Hex) const;

	/** The point of the straight line of the wall at the row of a hex of its column: the line runs through the middle of what the hexes of the
	 *  column share, as they shift half a hex from row to row */
	FVector GetWallLinePoint(const FHexCoord& Hex) const;

	/** Where a part of the wall stands: on the line of the wall at its row, and a tower at an end of the wall in its corner, outside the field */
	FVector GetWallPartPoint(int32 Wall) const;

	/** The castle on the field, in the world: the corners of its front wall, those of its back wall past the last column, the way into it and
	 *  the moat before it, a capsule along the hexes of the moat (its middle, half its length without the round ends and half its width) */
	struct FSiegeShape
	{
		bool bValid = false;
		bool bMoat = false;
		FVector FrontStart = FVector::ZeroVector;
		FVector FrontEnd = FVector::ZeroVector;
		FVector BackStart = FVector::ZeroVector;
		FVector BackEnd = FVector::ZeroVector;
		FVector In = FVector::YAxisVector;
		/** Where the side walls end, past the palace */
		FVector SideStart = FVector::ZeroVector;
		FVector SideEnd = FVector::ZeroVector;
		/** Half the opening of the gate along the wall, and how far along In the face of the wall towards the attacker lies */
		double GateHalf = 0.0;
		double WallFace = 0.0;
		/** The moat by its hexes: the X of the middle of row 0 and the step of a row along X, and along In the far flat edge of an even row and of
		 *  an odd one, which the materials turn into a shore (the field lies with its rows along X and In along Y), and how far along X it reaches */
		FVector4 MoatRows = FVector4(0.0, 0.0, 0.0, 0.0);
		double MoatXMin = 0.0;
		double MoatXMax = 0.0;
	};
	FSiegeShape GetSiegeShape() const;

	/** The height of the ground of the field at a point of the world: flat over the field and the castle, hills beyond (HillHeight) */
	double GetGroundHeight(const FVector2D& World) const;

	/** The box of the field, and of the castle with its moat and palace, in the world, around which the forest, the rocks and the fog stand */
	FBox2D GetFieldBox() const;

	/** Whether a spell is chosen and waits for its hex */
	bool IsAiming() const { return Aimed != INDEX_NONE; }

	/** Gives an order from the console, by hexes as column and row (hex.Battle) */
	bool OrderAt(const FString& Order, const FHexCoord& Hex, int32 Unit, int32 Spell, FString& OutMessage);

private:

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Ground;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Grid;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Marks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Sky;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> SkyLight;

	/** A light without shadows from the other side of the sun, so the shadows are not black */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> FillLight;
	int32 SunPriority = 0;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Shot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> SpellLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Props;

	TWeakObjectPtr<UHexBattles> Battles;

	UPROPERTY(Transient)
	TObjectPtr<const UHexBattleConfig> Config;

	/** The sun of the level, which the field borrows, and how it was */
	TWeakObjectPtr<ADirectionalLight> Sun;
	FRotator SunRotation = FRotator::ZeroRotator;
	float SunIntensity = 0.0f;
	FLinearColor SunColor = FLinearColor::White;

	bool bShowing = false;

	/** The figure of each unit, by its index, as it is drawn now */
	struct FFigure
	{
		TWeakObjectPtr<USceneComponent> Component;
		FName Creature;
		float Scale = 1.0f;
		float Yaw = 0.0f;
		float Hover = 0.0f;
		FVector Position = FVector::ZeroVector;
		FVector Facing = FVector::ForwardVector;
		bool bDead = false;
	};
	TArray<FFigure> Figures;
	TArray<int32> ShownCounts;

	/** What is played now and what waits, and how far into it */
	TArray<FHexBattleEvent> Pending;
	TOptional<FHexBattleEvent> Current;
	float CurrentTime = 0.0f;
	float CurrentLength = 0.0f;
	float AiWait = 0.0f;

	TArray<FString> Log;
	TArray<FHexBattlePopup> Popups;

	bool bHover = false;
	FHexCoord HoverHex;
	FVector HoverPoint = FVector::ZeroVector;
	int32 Aimed = INDEX_NONE;

	/** What the marks were drawn for, so they are drawn again only when it changed */
	FString MarksKey;

	/** The wall of a castle on the field, as the developer asked on 2026-11-12: its pieces, made again when a part of it falls, the material
	 *  of the castle, and the gate, whose leaves open while a defender goes through or stands in it and stay open once it is jammed */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> SiegeProps;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CastleMaterial;

	FString SiegeKey;
	/** How high the shooters of a tower stand, over the ground, and the water of the moat, made once for a field */
	double TowerTop = 0.0;
	TWeakObjectPtr<UProceduralMeshComponent> MoatWater;
	/** What the cursor picks of the wall before the ground: an upright cylinder around each part as it stands, towers and the arch of the gate
	 *  too, the towers beside the gate as the parts of the wall they stand over, and the hex it gives */
	struct FWallPick
	{
		FVector Base;
		double Radius;
		double Height;
		FHexCoord Hex;
	};
	TArray<FWallPick> WallPicks;

	/** The meshes of the castle built on the field, the wall over the gate, kept while their instances draw */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> ArchMeshes;

	/** The materials of the props of a siege that dissolve with the wall in front of the hex under the cursor (the leaves of the gate too) */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FadeMaterials;

	/** The leaves of the gate of timber, on their hinges, and how open they show */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> GateParts;
	TObjectPtr<USceneComponent> GateHinges[2] = {nullptr, nullptr};
	float GateOpenShown = 0.0f;
	/** The corners of the field in the world, and a seed of the ground fought on for its hills */
	FVector2D FieldMin = FVector2D::ZeroVector;
	FVector2D FieldMax = FVector2D::ZeroVector;
	int32 GroundSeed = 0;

	UProceduralMeshComponent* MakeMoatWater(const FSiegeShape& Shape);
	void SetMoatParameters(UMaterialInstanceDynamic* Instance, const FSiegeShape& Shape) const;

	void BuildSiege();

	/** The debug views of the maps on the field, as the developer asked on 2026-11-13: hex.Wireframe (F) 1 draws the edges of the meshes over
	 *  the render and 2 the triangles alone, by the view modes of the engine, hex.Water (V) hides the water of the moat and G the lines of the
	 *  hexes; applied when they change (DebugKey) and undone when the field closes */
	int32 DebugKey = -1;

	/** Brings the camera to show two points of the world when either is off the screen or near its edge, as the developer asked on 2026-11-13
	 *  for the one that strikes or shoots and the one struck: it looks at their middle and goes back far enough for both */
	void FrameCamera(const FVector& A, const FVector& B);
	void UpdateDebugView(bool bReset = false);
	void UpdateGate(float DeltaSeconds);

	/** The hex beside a part of the wall from which the unit whose turn it is strikes it, nearest the cursor, or false */
	bool FindWallStrikeFrom(const FHexCoord& WallHex, FHexCoord& OutFrom) const;

	/** The unit under the cursor is outlined (OutlineMaterial of the config), by a copy of its figure that follows its pose */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> HoverOutline;
	int32 OutlinedUnit = INDEX_NONE;
	void UpdateOutline();

	/** The hexes the unit whose turn it is reaches, for its turn */
	TMap<FHexCoord, int32> Reach;
	int32 ReachUnit = INDEX_NONE;
	int32 ReachRound = 0;

	FVector2D FieldCenter = FVector2D::ZeroVector;

	/** The camera: the point of the ground it looks at from the middle of the field, its turn around it in degrees, 0 looking along X, and its
	 *  distance, each as it is now and as it goes to; PitchOverride for hex.Battle camera, 0 for the pitch by the distance */
	FVector2D LookAt = FVector2D::ZeroVector;
	FVector2D LookAtGoal = FVector2D::ZeroVector;
	float Yaw = 0.0f;
	float YawGoal = 0.0f;
	float Distance = 1900.0f;
	float DistanceGoal = 1900.0f;
	float PitchOverride = 0.0f;

	/** Places the camera by LookAt, Yaw and Distance */
	void PlaceCamera();

	FHexBattle* GetBattle() const;
	const FHexBattleGround* GetGround() const;

	void BuildGround();
	void BuildGrid();
	void BuildProps();
	void PlaceFigures();
	void ClearField();
	void UpdateMarks();
	void UpdateReach();

	/** Takes what happened from the battle into what waits to be played */
	void Gather();

	/** Starts playing an event, and moves what plays on by a frame */
	void BeginEvent(const FHexBattleEvent& Event);
	void PlayEvent(float DeltaSeconds);
	void EndEvent();

	void AddPopup(const FVector& World, const FString& Text, const FLinearColor& Color, float Life = 1.2f);
	void AddLog(const FString& Line);
	void PlaceFigure(int32 Unit);
	FString GetUnitName(int32 Unit) const;

	/** The hex beside a target from which the unit whose turn it is strikes it, nearest the cursor, or false */
	bool FindStrikeFrom(int32 Target, FHexCoord& OutFrom) const;

	/** The person's side, or INDEX_NONE */
	int32 GetPersonSide() const;
};

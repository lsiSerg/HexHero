// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HexCameraBookmarks.h"
#include "HexPlanetLayers.h"
#include "HexPlanetCameraPawn.generated.h"

class ADirectionalLight;
class AHexPlanet;
class UCameraComponent;
class UCurveFloat;
class UHexCameraConfig;
class UHexPlanetCameraConfig;
class UHexInputConfig;
struct FInputActionValue;

/** Place of the planet camera, as hex.PlanetSetCamera takes it and hex.CopyCamera gives it */
struct FHexPlanetCameraPlacement
{
	/** Point under the camera, in degrees */
	double Latitude = 0.0;
	double Longitude = 0.0;

	/** Direction the top of the screen looks, in degrees clockwise from north */
	double Heading = 0.0;

	/** Distance from the point under the camera, where 0 or less keeps the current one */
	double Distance = 0.0;

	/** Returns the place of an orientation whose Z is the point under the camera and X the top of the screen */
	static FHexPlanetCameraPlacement FromOrientation(const FQuat& Orientation, double Distance);

	/** Returns the orientation of this place */
	FQuat ToOrientation() const;
};

/**
 *  Camera that orbits the hex planet, with the keys of DA_HexInput.
 *  It looks at a point of the surface from above, and lowers its angle as it comes closer.
 *  Moving rolls the planet under it with the inertia of DA_HexCamera, the drag keeps the grabbed point under the mouse, the turn keys and the right mouse button turn it
 *  around the point it looks at, and the focus key glides to the cell under the mouse. The mouse at the screen edge moves it unless hex.EdgeScroll is 0.
 *  The camera modes of hex.Camera are those of the flat map: the free camera stays turned, and the top-down one turns at most MaxCameraTurn of DA_HexCamera
 *  away from north and turns back to it when the turn input is released.
 *  The sun follows the point the camera looks at, from a compass direction of that place, lazily so shadows stand still while the view stays
 *  about where it is, unless the console variable hex.PlanetSun is 0.
 *  hex.CopyCamera and its input copy the commands that open the planet and put the camera back where it is, with hex.PlanetSetCamera.
 *  The camera goes between the layers of the planet (TravelTo), the surface and the underground, through a fade to black, keeping its place;
 *  the light of each layer (FHexLayerLighting) moves the sun and sets the exposure.
 */
UCLASS()
class AHexPlanetCameraPawn : public APawn
{
	GENERATED_BODY()

	/** Camera */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UCameraComponent> Camera;

	/** A battle is shown (SetBattleShown) */
	bool bBattleShown = false;

	/** Planet the camera orbits */
	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	/** Sun of the level */
	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> Sun;

	/** Holds the input actions and their mapping context */
	UPROPERTY(Transient)
	TObjectPtr<const UHexInputConfig> InputConfig;

	/** Inertia of the movement, shared with the flat map */
	UPROPERTY(Transient)
	TObjectPtr<const UHexCameraConfig> CameraConfig;

	/** Scroll strength in the screen edge zone, from the camera config */
	UPROPERTY(Transient)
	TObjectPtr<const UCurveFloat> EdgeScrollCurve;

	/** Loaded PlanetCameraConfig */
	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetCameraConfig> LoadedPlanetCameraConfig;

protected:

	/** Distances, angles and speeds of the orbit camera */
	UPROPERTY(EditAnywhere, Category="Camera")
	TSoftObjectPtr<UHexPlanetCameraConfig> PlanetCameraConfig = TSoftObjectPtr<UHexPlanetCameraConfig>(FSoftObjectPath(TEXT("/Game/HexPlanet/DA_HexPlanetCamera.DA_HexPlanetCamera")));

	/** Returns the loaded camera config, or the class defaults before BeginPlay or while it is missing */
	const UHexPlanetCameraConfig* Settings() const;

public:

	/** Constructor */
	AHexPlanetCameraPawn();

	/** Moves, turns and zooms the camera */
	virtual void Tick(float DeltaTime) override;

	/** Returns the planet, once the camera has found it */
	AHexPlanet* GetPlanet() const { return Planet; }

	/** Glides the camera to a cell, and optionally to a distance from it. Returns false before the planet is built or if there is no such cell */
	bool FocusCell(int32 Cell, TOptional<float> Distance = {});

	/** Shows a cell of a layer: glides to it on the layer shown, else travels to its layer with the camera over it, as when a hero is chosen
	 *  from its panel or goes through an entrance. Returns false during a trip or without such a cell */
	bool ShowCell(EHexPlanetLayer Layer, int32 Cell);

	/** Glides after a direction from the center of the planet, as after a hero that walks, unless the player moves the camera */
	void Follow(const FVector& Direction);

	/** Glides to a direction from the center of the planet and to a distance from the ground there, as to a castle whose panel opens */
	void FocusOn(const FVector& Direction, float Distance);

	/** Returns where the camera is */
	FHexPlanetCameraPlacement GetPlacement() const;

	/** Puts the camera at a place at once, as a cut: the sun stands where it should there at once, and the renderer drops what it kept of
	 *  the frames before */
	void SetPlacement(const FHexPlanetCameraPlacement& Placement);

	/** Copies to the clipboard the console commands hex.LoadPlanet and hex.PlanetSetCamera, which open the planet and put the camera where it is now */
	void CopyCameraToClipboard() const;

	/** Saves the camera and its layer in a slot of the bookmarks of this planet (HexCameraBookmarks), 1 to NumSlots; a planet not saved keeps
	 *  them until it is saved, which writes them under its folder. Returns false before the planet is built or during a trip */
	bool SaveBookmark(int32 Slot);

	/** Puts the camera back where a slot of the bookmarks of this planet saved it, as hex.PlanetSetCamera does: once the pieces around that place
	 *  are built, through a trip when the slot is of the other layer. Returns false for an empty slot */
	bool JumpToBookmark(int32 Slot);

	/** Saves what the save key saves: a new planet under a name the player gives, painted levels and biomes, and the camera to the last view;
	 *  then runs OnSaved, which a cancelled name dialog or a failed write never runs */
	void SavePlanet(TFunction<void()> OnSaved);

	/** Returns true if LastView.json holds the camera as it is now, or the planet is not built yet */
	bool IsLastViewSaved() const;

	/** Gives the camera mode as pairs of name and value. Returns true if it is the mode the camera starts in */
	bool GetViewStatus(TArray<TPair<FString, FString>>& OutLines) const;

	/** Returns true while the sun follows the camera, the console variable hex.PlanetSun, which no key changes since L became the forced light */
	static bool IsSunFollowing();

	/** Gives whether the sun follows the camera as a pair of name and value. Returns true if it does, the usual view */
	static bool GetSunStatus(TArray<TPair<FString, FString>>& OutLines);

	/** Gives whether the forced light of L is on (AHexPlanet::IsLightForced) as a pair of name and value. Returns true while it is off, the
	 *  usual view */
	static bool GetLightStatus(TArray<TPair<FString, FString>>& OutLines);

	/**
	 *  Goes to a layer of the planet: ends the input held, asks the layer for the pieces around the place, fades to black, puts up the black
	 *  screen over the widgets, shows that layer with its light and puts the camera at the same place, or at Placement, as a cut, waits for
	 *  its pieces at most JumpWaitMax and at least TravelHoldMin, and fades back in (the Travel* times of DA_HexPlanetCamera). bStartBlack
	 *  skips the fade out, as at the start. Returns false while a trip runs or if the planet has no such layer
	 */
	bool TravelTo(EHexPlanetLayer Layer, TOptional<FHexPlanetCameraPlacement> Placement = {}, bool bStartBlack = false);

	/** Returns true while a trip between the layers runs */
	bool IsTravelling() const { return TravelPhase != ETravelPhase::None; }

	/** While a battle is shown the camera of the planet waits where it is, and takes no input but the clicks the controller gives the field */
	void SetBattleShown(bool bShown) { bBattleShown = bShown; bDragging = false; bTurning = false; bBattleDragging = false; }
	bool IsBattleShown() const { return bBattleShown; }

	/** The post process of the camera, which the field of a battle takes for its own */
	const FPostProcessSettings& GetCameraPostProcess() const;

	/** Returns the layer the camera shows, the surface until it has its planet */
	EHexPlanetLayer GetShownLayer() const;

	/** Returns the exposure bias of the camera now, which the light of the layer shown sets */
	float GetExposureBias() const;

protected:

	/** Finds the planet and the sun */
	virtual void BeginPlay() override;

	/** Creates the input bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:

	/** Point of the surface under the camera as up, and the forward direction of the screen as forward */
	FQuat Orientation = FQuat::Identity;

	/** Requested distance from the point the camera looks at */
	double TargetDistance = 0.0;

	/** Current distance from the point the camera looks at */
	double CurrentDistance = 0.0;

	/** Current keyboard movement, where X is right on screen and Y is up */
	FVector2D MoveInput = FVector2D::ZeroVector;

	/** Speed the planet rolls under the camera at, as an axis through its center times radians per second */
	FVector AngularVelocity = FVector::ZeroVector;

	/** Inertia of the input that last moved the camera, in seconds */
	float MoveInertia = 0.1f;

	/** Current turn input of the keys */
	float TurnKeyInput = 0.0f;

	/** True while the drag input holds the planet */
	bool bDragging = false;

	/** Direction from the center of the planet to the grabbed point */
	FVector DragDirection = FVector::ZeroVector;

	/** True while the right mouse button turns the camera */
	bool bTurning = false;

	/** Mouse X position at the last turn update */
	float LastTurnMouseX = 0.0f;

	/** Where the button that turns the camera went down, as a press and a release at one place is a click, which sends the hero chosen */
	FVector2D TurnStart = FVector2D::ZeroVector;
	bool bTurnDown = false;

	/** True while the camera glides to a cell */
	bool bFocusing = false;

	/** While a battle is shown its field takes the camera input (UpdateBattleCamera): the point of its ground grabbed by the left or middle
	 *  button held, and the turn of the right one */
	bool bBattleDragging = false;
	FVector BattleGrab = FVector::ZeroVector;
	void UpdateBattleCamera(float DeltaTime);
	bool StartBattleDrag();

	/** The camera follows the hero chosen while it walks (Follow), until the person moves it with the keys, a drag or the right button, which
	 *  takes it over until the hero stops, as the developer asked on 2026-11-12; the edge of the screen never takes it over and scrolls only
	 *  once the person has. FollowFrame is the frame Follow was last asked for */
	bool bFollowTaken = false;
	uint64 FollowFrame = 0;
	bool IsFollowing() const;
	void TakeFollow();

	/** The edge of the screen does not scroll until the mouse has moved EdgeScrollHoldDistance from EdgeHoldMouse, where it was when a glide
	 *  was asked for (HoldEdgeScroll), so the cursor left at the edge does not stop that glide */
	bool bEdgeScrollHeld = false;
	FVector2D EdgeHoldMouse = FVector2D::ZeroVector;
	void HoldEdgeScroll();

	/** Direction from the center of the planet to the cell the camera glides to */
	FVector FocusDirection = FVector::ZeroVector;

	/** Planet camera that LastView.json holds, as the view is compared with it */
	FHexPlanetCameraPlacement SavedPlacement;

	/** Cell that a focus asked for before the planet was built */
	int32 PendingFocusCell = INDEX_NONE;

	/** Distance that the waiting focus asked for */
	TOptional<float> PendingFocusDistance;

	void OnMove(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnTurnKeys(const FInputActionValue& Value);
	void OnDragStarted();
	void OnDragCompleted();
	void OnTurnStarted();
	void OnTurnCompleted();
	void OnFocusCellUnderMouse();

	/** The middle button drags the planet in any mode, and a release without moving centers the cell under the mouse */
	void OnMiddleStarted();
	void OnMiddleCompleted();

	/** Where the middle button went down, in pixels of the viewport */
	FVector2D MiddleStart = FVector2D::ZeroVector;
	bool bMiddleDown = false;

	/** Where the left button went down to drag the planet, and the cell and time of its last click, by which two clicks on a cell within
	 *  DoubleClickTime are a double click */
	FVector2D LeftStart = FVector2D::ZeroVector;
	bool bLeftDown = false;
	int32 LastClickCell = INDEX_NONE;
	double LastClickTime = 0.0;
	void OnToggleGrid();
	void OnToggleFreeCamera();
	void OnToggleForcedLight();
	void OnCycleWireframeMode();
	void OnCycleWireframeScope();
	void OnCycleWireframeTarget();
	void OnCycleWaterView();

	/** Goes back to the hex world map, which opens the last map or the menu */
	void OnBackToWorldMap();
	void OnCopyCamera();
	void OnToggleKeyLegend();
	void OnToggleCheatSheet();

	/** The bookmarks of the camera, the slot as the value of the action, 1 to 9 and 10 for the key 0 */
	void OnSaveBookmark(const FInputActionValue& Value);
	void OnJumpBookmark(const FInputActionValue& Value);

	/** Bookmarks saved on a planet that is not saved yet, by slot, which SaveView writes under its folder once it is */
	TMap<int32, FHexPlanetBookmark> UnsavedBookmarks;

	/** The game, which the player controller plays: a click of the left button, a press and a release within ClickTolerance of each other,
	 *  as held and moved the button drags the planet, and two of them within DoubleClickTime a double click; the key that walks the way
	 *  shown, and those of the next hero, the end of the day and the game itself */
	void OnHeroSelectStarted();
	void OnHeroSelectCompleted();
	void OnHeroSendStarted();
	void OnNextHero();
	void OnEndTurn();
	void OnSkipHero();
	void OnBattleHit();
	void OnPlayMode();

	/** Returns true if the mouse is within ClickTolerance of where a button went down */
	bool IsClickAt(const FVector2D& Start) const;

	/** Where those buttons went down, in pixels of the viewport, and whether each is held since */
	FVector2D HeroSelectStart = FVector2D::ZeroVector;
	bool bHeroSelectDown = false;

	/** Where and when the last click of the game fell, by which the next is a double click */
	FVector2D LastHeroClick = FVector2D::ZeroVector;
	double LastHeroClickTime = -1.0;

	/** Painting the levels, which the player controller does */
	void OnTogglePaintMode();
	void OnPaintLevel(const FInputActionValue& Value);
	void OnBrushSize();
	void OnUndo();
	void OnRedo();

	/** Saves the camera to the last view, which the planet opens with next time; a new planet is first saved under a name the player gives */
	void OnSaveView();

	/** Writes the camera and the folder of the planet to the last view */
	void SaveView();

	/** Returns the distance at which the camera sees all of the half of the planet that faces it */
	double GetMaxDistance() const;

	/** Returns the downward angle of the camera at a distance, in degrees */
	double GetPitchAtDistance(double Distance) const;

	/** Finds the direction from the center of the planet to the point of its sea under the mouse. Returns false if the mouse misses it */
	bool GetMouseDirection(FVector& OutDirection) const;

	/** Keeps the top-down camera within MaxCameraTurn of north, and turns it back to north while no turn input is held */
	void UpdateTopDownHeading(float DeltaTime);

	/** Returns a place rounded as LastView.json keeps it */
	static FHexPlanetCameraPlacement RoundPlacement(const FHexPlanetCameraPlacement& Placement);

	/** Rolls the planet under the camera, so the point under it moves towards a direction */
	void RotateTowards(const FVector& From, const FVector& To, double Alpha = 1.0);

	/** Places the camera */
	void UpdateCameraPosition();

	/** Returns where the camera stands for an orientation and a distance, as UpdateCameraPosition puts it */
	FVector GetCameraLocationAt(const FQuat& InOrientation, double Distance) const;

	/** How long the jump asked for by hex.PlanetSetCamera has waited for the planet to build the pieces around the place it goes to */
	float JumpWaitTime = 0.0f;

	/** Returns the direction the sun should shine from for the view now: SunAzimuth and an elevation by the distance over the point under the camera */
	FVector GetIdealSunFrom() const;

	/** Moves the sun lazily towards GetIdealSunFrom: it stands still while it lags less than SunLagShare of the view, then moves over SunMoveTime */
	void UpdateSun(float DeltaTime);

	/** Direction the sun shines from now, zero until it is first placed, which puts it where it should stand at once */
	FVector SunFrom = FVector::ZeroVector;

	/** Direction the sun moves from and how far it is along the move, from 0 to 1; 1 while it stands still */
	FVector SunMoveStart = FVector::ZeroVector;
	float SunMoveAlpha = 1.0f;

	/** How long the camera has stood still, in seconds */
	float StillTime = 0.0f;

	/** True once the sun was first placed */
	bool bSunPlaced = false;

	/** Goes to the other layer: through the entrance under the mouse to its exit, from an exit up to its entrance, else at the same place */
	void OnLayer();

	/** Goes through the entrance or exit of a cell to the other layer, the camera over it there, as the key of the layer does and a double click;
	 *  false if the cell holds none that can be used or a trip runs */
	bool TravelThroughEntrance(int32 Cell);

	/** Runs the steps of a trip between the layers */
	void UpdateTravel(float DeltaTime);

	/** Gives the sun and the camera the light of a layer (UHexPlanetCameraConfig::GetLighting) */
	void ApplyLighting(EHexPlanetLayer Layer);

	/** Gives the sun the color and intensity of the light of a layer, ForcedSunIntensity under the forced light of L, without moving it */
	void ApplySunLight(EHexPlanetLayer Layer);

	/** Whether the sun took the forced light, so a change of hex.PlanetForcedLight, by the key or the console, gives it the other */
	bool bSunLightForced = false;

	/** Steps of a trip: the fade to black, the black screen, while the layer is swapped and its pieces are built, and the fade back */
	enum class ETravelPhase : uint8
	{
		None,
		FadingOut,
		Black,
		FadingIn,
	};
	ETravelPhase TravelPhase = ETravelPhase::None;

	/** A cell ShowCell was asked for during a trip, shown once the trip ends, as when a hero goes through an entrance while the camera is
	 *  still on its way to the layer it set out on */
	TOptional<EHexPlanetLayer> PendingShowLayer;
	int32 PendingShowCell = INDEX_NONE;

	/** Layer and place the trip goes to */
	EHexPlanetLayer TravelLayer = EHexPlanetLayer::Surface;
	FHexPlanetCameraPlacement TravelPlacement;

	/** Time in the step of the trip and in the whole trip, frames of the black screen, and whether the layers were swapped, when and how long that took */
	float TravelPhaseTime = 0.0f;
	double TravelStartTime = 0.0;
	int32 TravelBlackFrames = 0;
	bool bTravelSwapped = false;
	double TravelSwapMs = 0.0;
	double TravelReadyMs = 0.0;

	/** Real time when the swap of the layers ended, and the frames drawn black since, by which the hold after the swap counts */
	double TravelSwapRealTime = 0.0;
	int32 TravelFramesSinceSwap = 0;

	/** The color and intensity of the sun placed in the level, which the light of the surface keeps */
	FLinearColor LevelSunColor = FLinearColor::White;
	float LevelSunIntensity = 0.0f;

	/** Layer of the camera that LastView.json holds */
	EHexPlanetLayer SavedLayer = EHexPlanetLayer::Surface;

	/** Returns where the camera stands for a place, its distance kept within the zoom, or the current one when it has none */
	FVector GetCameraLocationFor(const FHexPlanetCameraPlacement& Placement) const;
};

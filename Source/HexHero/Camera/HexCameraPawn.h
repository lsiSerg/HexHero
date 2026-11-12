// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HexLastView.h"
#include "HexCameraPawn.generated.h"

class AHexWorldMap;
class UCameraComponent;
class UCurveFloat;
class UHexCameraConfig;
class UHexInputConfig;
struct FHexCoord;
struct FInputActionValue;

/**
 *  Top-down camera that moves over the hex world map.
 *  Moves with its input and the screen edges, drags the map, turns, zooms, toggles the grid
 *  and opens the map menu. DA_HexInput holds the keys.
 *  The free camera turns all the way around and stays turned, so the map can be seen from any side.
 *  The console command hex.FocusHex and the focus input glide the camera to a hex, and hex.Camera switches the free camera.
 *  hex.CopyCamera and its input copy the commands that open the map and put the camera back where it is, with hex.SetCamera.
 *  The save input writes the map and the camera to LastView.json, where the next run starts.
 */
UCLASS()
class AHexCameraPawn : public APawn
{
	GENERATED_BODY()

	/** Camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> Camera;

	/** Map that limits the camera movement */
	UPROPERTY(Transient)
	TObjectPtr<AHexWorldMap> WorldMap;

	/** Holds the camera turn and scroll values */
	UPROPERTY(Transient)
	TObjectPtr<const UHexCameraConfig> CameraConfig;

	/** Scroll strength by the mouse position inside the screen edge zone */
	UPROPERTY(Transient)
	TObjectPtr<const UCurveFloat> EdgeScrollCurve;

	/** Holds the input actions and their mapping context */
	UPROPERTY(Transient)
	TObjectPtr<const UHexInputConfig> InputConfig;

protected:

	/** Returns the loaded camera config, or its class defaults before BeginPlay */
	const UHexCameraConfig* Settings() const;

	/** Requested camera distance */
	float TargetZoom = 3000.0f;

	/** Current camera distance */
	float CurrentZoom = 3000.0f;

	/** Current keyboard movement */
	FVector2D MoveInput = FVector2D::ZeroVector;

	/** Current camera movement over the ground per second, which changes gradually */
	FVector2D ScrollVelocity = FVector2D::ZeroVector;

	/** Inertia of the input that last moved the camera, in seconds */
	float ScrollInertia = 0.1f;

	/** True while the drag input holds the map */
	bool bDragging = false;

	/** Ground point that stays under the mouse during a drag */
	FVector2D DragGroundPoint = FVector2D::ZeroVector;

	/** True while the turn input turns the camera */
	bool bTurning = false;

	/** Mouse X position when the turn started */
	float TurnStartMouseX = 0.0f;

	/** Camera turn when the turn started */
	float TurnStartYaw = 0.0f;

	/** Current turn input of the keys, from -1 to the left to 1 to the right */
	float TurnKeyInput = 0.0f;

	/** Requested camera turn away from the default angle, in degrees */
	float TargetYaw = 0.0f;

	/** Current camera turn away from the default angle, in degrees */
	float CurrentYaw = 0.0f;

	/** True while the camera glides to keep a hex in the center of the screen */
	bool bFocusing = false;

	/** Point that the camera keeps in the center of the screen while it focuses on a hex */
	FVector FocusPoint = FVector::ZeroVector;

	/** Map and camera that LastView.json holds, read when the map loads and set by each save */
	FHexLastView SavedLastView;

public:

	/** Constructor */
	AHexCameraPawn();

	/** Moves and zooms the camera */
	virtual void Tick(float DeltaTime) override;

	/** Returns the downward angle of the camera at a distance. It changes evenly with each zoom step */
	float GetPitchAtZoom(float Zoom) const;

	/** Returns true while the camera turns freely, as the console variable hex.Camera says */
	static bool IsFreeCamera();

	/** Switches the free camera on or off through the console variable hex.Camera */
	static void SetFreeCamera(bool bFree);

	/** Sets hex.Camera to the mode the config starts in, unless it was set from the console or by the key, so another level keeps the chosen mode */
	static void InitCameraMode(const UHexCameraConfig& Config);

	/** Gives the camera mode as pairs of name and value. Returns true if it is the mode the config starts in */
	static bool GetCameraModeStatus(const UHexCameraConfig& Config, TArray<TPair<FString, FString>>& OutLines);

	/**
	 *  Returns the movement asked for by a mouse near the screen edge, where X is right on screen and Y is up, or nothing while hex.EdgeScroll is 0,
	 *  the mouse is outside the viewport or a menu has the input. The curve gives the strength over the zone, which is Margin pixels wide
	 */
	static FVector2D GetEdgeScrollInput(const APlayerController* PlayerController, const UCurveFloat* Curve, float Margin);

	/**
	 *  Returns true while the cursor is on a widget over the game that is clicked, dragged or typed into: a button, a check box, a spin box,
	 *  a slider, a text box or a scroll bar, of any panel. What is clicked comes before the game, as the developer asked on 2026-11-12
	 *  ("любе що клікається - вище по інпути, якщо нічого немає - можем скролитись"), so the edge of the screen does not scroll under it,
	 *  while labels and the backgrounds of panels let it scroll
	 */
	static bool IsCursorOnClickable(const APlayerController* PlayerController);

	/**
	 *  Glides the camera until a hex is in the center of the screen, and optionally turns and zooms it.
	 *  The turn is in degrees away from the default angle, and makes the camera free, as only the free camera stays turned.
	 *  Returns false if the hex is not on the map
	 */
	bool FocusHex(const FHexCoord& Hex, TOptional<float> Yaw = {}, TOptional<float> Zoom = {});

	/** Puts the camera at once over a point of the ground, and optionally turns and zooms it. A turn other than 0 makes the camera free */
	void SetCamera(const FVector2D& Location, TOptional<float> Yaw = {}, TOptional<float> Zoom = {});

	/** Copies to the clipboard the console commands hex.LoadMap and hex.SetCamera, which open this map and put the camera where it is now */
	void CopyCameraToClipboard() const;

	/** Saves the camera in a slot of the bookmarks of this map (HexCameraBookmarks), 1 to NumSlots. Returns false before the map has loaded */
	bool SaveBookmark(int32 Slot);

	/** Puts the camera back at once where a slot of the bookmarks of this map saved it. Returns false for an empty slot */
	bool JumpToBookmark(int32 Slot);

	/** Writes the map and the camera to LastView.json, where the next run starts. Returns false before the map has loaded or if the file could not be written */
	bool SaveLastView();

	/** Returns true if LastView.json holds the current map and camera, or there is nothing to save yet */
	bool IsLastViewSaved() const;

	/** Gives the camera mode as pairs of name and value. Returns true if it is the mode the camera starts in */
	bool GetViewStatus(TArray<TPair<FString, FString>>& OutLines) const;

protected:

	/** Loads the camera config */
	virtual void BeginPlay() override;

	/** Creates the input bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:

	/** Handles the move input */
	void OnMove(const FInputActionValue& Value);

	/** Handles the zoom input */
	void OnZoom(const FInputActionValue& Value);

	/** Handles the grid toggle input */
	void OnToggleGrid();

	/** Switches the debug wireframe to its next mode */
	void OnCycleWireframeMode();

	/** Switches the debug wireframe to its next set of hexes */
	void OnCycleWireframeScope();

	/** Switches the debug wireframe to its next set of geometry */
	void OnCycleWireframeTarget();

	/** Switches the water to its next debug view */
	void OnCycleWaterView();

	/** Shows or hides the legend of the keys */
	void OnToggleKeyLegend();

	/** Grabs the ground point under the mouse */
	void OnDragStarted();

	/** Releases the grabbed ground point */
	void OnDragCompleted();

	/** Starts turning the camera with the mouse */
	void OnTurnStarted();

	/** Sends the camera back to the default angle, unless it turns freely */
	void OnTurnCompleted();

	/** Handles the turn input of the keys */
	void OnTurnKeys(const FInputActionValue& Value);

	/** Switches the free camera on or off */
	void OnToggleFreeCamera();

	/** Opens the map menu */
	void OnOpenMapMenu();

	/** Focuses the camera on the hex under the mouse */
	void OnFocusHexUnderMouse();

	/** Copies the commands that bring the camera back here */
	void OnCopyCamera();

	/** The bookmarks of the camera, the slot as the value of the action, 1 to 9 and 10 for the key 0 */
	void OnSaveBookmark(const FInputActionValue& Value);
	void OnJumpBookmark(const FInputActionValue& Value);

	/** Returns the camera location that puts the focus point in the center of the screen at the current turn and distance */
	FVector GetFocusLocation() const;

	/** Moves the camera turn towards the requested angle */
	void UpdateCameraTurn(float DeltaTime);

	/** Finds the map and centers the camera on it once it has loaded */
	void FindWorldMap();

	/** Returns the scroll strength of a mouse at this distance from one screen edge */
	static double GetEdgeScrollStrength(double EdgeDistance, const UCurveFloat* Curve, float Margin);

	/** Finds the ground point under the mouse. Returns false if the mouse does not point at the ground */
	bool GetMouseGroundPoint(FVector2D& OutPoint) const;

	/** Moves the camera to a location inside the map bounds */
	void SetClampedLocation(FVector Location);

	/** Places the camera at the current distance */
	void UpdateCameraPosition();

	/** Writes the view to LastView.json and shows that it did */
	void OnSaveView();

	/** Returns the map and the camera as LastView.json holds them, or nothing before the map has loaded */
	TOptional<FHexLastView> GetLastView() const;
};

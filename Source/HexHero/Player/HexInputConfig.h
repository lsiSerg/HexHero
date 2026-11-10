// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 *  Input actions of the hex world map and the mapping context that gives them their keys.
 */
UCLASS(BlueprintType)
class UHexInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Keys of all the actions below */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	/** Camera movement, as a 2D axis where X is right on screen and Y is up on screen */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Camera zoom, as a 1D axis in mouse wheel steps */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ZoomAction;

	/** Grid toggle */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ToggleGridAction;

	/** Map drag, while held */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> DragAction;

	/** Camera turn, while held */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> TurnAction;

	/** Camera turn by keys, as a 1D axis from -1 to the left to 1 to the right, while held */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> TurnKeysAction;

	/** Switches the free camera, which turns all the way around and stays turned */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> FreeCameraAction;

	/** Moves the camera so that the hex under the mouse is in the center of the screen */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> FocusHexAction;

	/** Copies to the clipboard the console commands that open the map and put the camera back where it is */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> CopyCameraAction;

	/** Writes the map and the camera to LastView.json, where the next run starts */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SaveViewAction;

	/** Saves the camera in a slot of the bookmarks of the planet or map open (CameraBookmarks.json), a value of 1 to 9, and 10 for the key 0 */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SaveBookmarkAction;

	/** Puts the camera back where a slot of the bookmarks saved it, travelling to its layer, a value as SaveBookmarkAction gives */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> JumpBookmarkAction;

	/** Opens the map menu. Its keys also close the menu */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MapMenuAction;

	/** Switches the debug wireframe between off, over the normal render and alone */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> WireframeModeAction;

	/** Switches the hexes of the debug wireframe between all, the one under the cursor and that one with its neighbors */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> WireframeScopeAction;

	/** Switches the geometry of the debug wireframe between all, only the terrain and all but the terrain */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> WireframeTargetAction;

	/** Switches the water between shown, hidden, hidden under the cursor and hidden there and in the neighbors, for debugging */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> WaterViewAction;

	/** Shows or hides the legend of the keys */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> KeyLegendAction;

	/** Turns painting the levels of the planet on or off, with its palette */
	UPROPERTY(EditAnywhere, Category="Input|Painting")
	TObjectPtr<UInputAction> PaintModeAction;

	/** Chooses the level of the brush, a value of 1 for deep sea up to 5 for mountains */
	UPROPERTY(EditAnywhere, Category="Input|Painting")
	TObjectPtr<UInputAction> PaintLevelAction;

	/** Makes the brush one ring larger, from the largest back to one cell */
	UPROPERTY(EditAnywhere, Category="Input|Painting")
	TObjectPtr<UInputAction> BrushSizeAction;

	/** Undoes the last stroke of the brush */
	UPROPERTY(EditAnywhere, Category="Input|Painting")
	TObjectPtr<UInputAction> UndoAction;

	/** Redoes the last stroke undone */
	UPROPERTY(EditAnywhere, Category="Input|Painting")
	TObjectPtr<UInputAction> RedoAction;

	/** Makes the sun of the planet follow the camera or stay where it is, as the console variable hex.PlanetSun does */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SunFollowAction;

	/** Goes between the surface of the planet and its underground: through the entrance under the mouse to its exit, from an exit up to
	 *  its entrance, else at the same place */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> LayerAction;

	/**
	 *  Returns the keys of one action from the mapping context as the legend shows them, or nothing if it has none.
	 *  A key chorded with another action comes after the key of that action, as in Shift+F
	 */
	FString GetKeysText(const UInputAction* Action) const;

	/** Returns the keys mapped to the map menu action */
	TArray<FKey> GetMapMenuKeys() const;
};

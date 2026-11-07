// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HexMapPlayerController.generated.h"

class AHexWorldMap;
class SHexDebugOverlay;
class SHexHoverInfo;
class SHexMapMenu;

/**
 *  Player controller for the hex world map. Keeps the mouse cursor free so it can reach the screen edges,
 *  and owns the menu that chooses the map.
 */
UCLASS()
class AHexMapPlayerController : public APlayerController
{
	GENERATED_BODY()

	/** Map menu while it is open */
	TSharedPtr<SHexMapMenu> MapMenu;

	/** Debug panel with the hex under the cursor */
	TSharedPtr<SHexHoverInfo> HoverInfo;

	/** Legend of the keys and the debug view settings */
	TSharedPtr<SHexDebugOverlay> DebugOverlay;

	/** World map, found the first time it is needed */
	TWeakObjectPtr<AHexWorldMap> WorldMap;

public:

	/** Constructor */
	AHexMapPlayerController();

	/** Shows the map menu and takes the input away from the game */
	void OpenMapMenu();

	/** Shows or hides the legend of the keys */
	void ToggleKeyLegend();

	/** Reopens the level with a map, a folder in Content/Data/Maps */
	void OpenMap(const FString& MapFolder);

	/**
	 *  Gives a mode of the view its own panel of settings, shown while they differ from the usual view and for a while after any change.
	 *  GetStatus fills the pairs of name and value and returns true for the usual view. No lines mean the mode is not there yet,
	 *  and its first lines count as no change
	 */
	void AddViewStatus(TFunction<bool(TArray<TPair<FString, FString>>&)> GetStatus);

protected:

	/** Sets up the input mode, and opens the map menu unless the level was opened from it */
	virtual void BeginPlay() override;

	/** Removes the map menu */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Updates the debug panel */
	virtual void PlayerTick(float DeltaTime) override;

private:

	/** Returns the debug text of the hex under the cursor, or nothing if there is none */
	FString GetHoverInfoText();

	/** Hides the map menu and gives the input back to the game */
	void CloseMapMenu();

	/** Reopens the level with another map */
	void OnMapChosen(const FString& MapFolder);

	/** Opens a planet, a folder in Content/Data/Planets, or for an empty folder the one of the last view */
	void OnPlanetChosen(const FString& PlanetFolder);

	/** Lets the game get input while the cursor stays free */
	void SetGameInputMode();
};

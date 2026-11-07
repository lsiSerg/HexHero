// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HexPlanetFile.h"
#include "HexPlanetPlayerController.generated.h"

class AHexPlanet;
class SHexDebugOverlay;
class SHexDialog;
class SHexHoverInfo;
class SHexMapMenu;
class SHexPlanetPalette;
class UHexPlanetPainter;

/**
 *  Player controller of the planet prototype. Keeps the mouse cursor free, outlines the cell under it, its neighbors while the wireframe shows them or the edge of the brush while painting,
 *  shows that cell in the debug panel at the cursor, and shows the legend of the keys that the planet camera uses.
 */
UCLASS()
class AHexPlanetPlayerController : public APlayerController
{
	GENERATED_BODY()

	/** Debug panel with the cell under the cursor */
	TSharedPtr<SHexHoverInfo> HoverInfo;

	/** Legend of the keys */
	TSharedPtr<SHexDebugOverlay> DebugOverlay;

	/** Menu of the maps, shown over the planet, which stays open behind it */
	TSharedPtr<SHexMapMenu> MapMenu;

	/** Dialog over the planet, such as the name of a planet to save */
	TSharedPtr<SHexDialog> Dialog;

	/** Palette of the brush, shown while painting is on */
	TSharedPtr<SHexPlanetPalette> Palette;

	/** Cell under the cursor in the last tick */
	int32 MouseCell = INDEX_NONE;

	/** Brush that paints the levels of the planet, made when painting first turns on */
	UPROPERTY(Transient)
	TObjectPtr<UHexPlanetPainter> Painter;

	/** True once the legend has the keys that the pawn binds */
	bool bLegendSet = false;

public:

	/** Constructor */
	AHexPlanetPlayerController();

	/** Shows the menu of the maps over the planet; choosing a map opens it in the level of the flat map */
	void OpenMapMenu();

	/** Shows or hides the legend of the keys */
	void ToggleKeyLegend();

	/** Asks for the name of the planet and saves it, warning before it writes over another planet; then runs OnSaved */
	void OpenSavePlanetDialog(TFunction<void()> OnSaved);

	/** Asks whether to save a planet that is not saved before its camera is copied, as the copied command could not open it */
	void OpenCopyCameraDialog();

	/** Returns true while the left button paints the levels of the planet */
	bool IsPaintModeOn() const;

	/** Turns painting on or off, with its palette */
	void SetPaintMode(bool bOn);

	/** Sets the level of the brush, -2 deep sea to 2 mountains, and turns painting on */
	void SetPaintLevel(int32 Level);

	/** Makes the brush one ring larger, from the largest back to one cell */
	void CycleBrushSize();

	/** Starts and ends a stroke of the brush at the cell under the cursor */
	void BeginPaintStroke();
	void EndPaintStroke();

	/** Undoes or redoes a stroke */
	void UndoPaint();
	void RedoPaint();

protected:

	/** Sets up the input mode, the debug panel and the legend */
	virtual void BeginPlay() override;

	/** Removes the debug panel and the legend */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Finds the cell under the cursor */
	virtual void PlayerTick(float DeltaTime) override;

private:

	/** Removes the menu of the maps and gives the input back to the planet */
	void CloseMapMenu();

	/** Opens a map, a folder in Content/Data/Maps, in the level of the flat map */
	void OnMapChosen(const FString& MapFolder);

	/** Opens a planet, a folder in Content/Data/Planets, in its level again */
	void OnPlanetChosen(const FString& PlanetFolder);

	/** Opens a new planet of the menu, not saved yet */
	void OnNewPlanet(const FHexPlanetManifest& Manifest);

	/** Runs Leave, which opens another map or planet, at once when nothing would be lost, else after a dialog that offers to save the planet
	 *  first, to leave without saving, or to stay */
	void LeavePlanet(TFunction<void()> Leave);

	/** Saves the planet from the button of the menu as the save key does; a new planet closes the menu for the dialog of its name */
	void SaveFromMenu();

	/** Returns the pawn of the planet, or null */
	class AHexPlanetCameraPawn* GetCameraPawn() const;

	/** Shows a dialog over the planet and gives it the input */
	void ShowDialog(const TSharedRef<SHexDialog>& NewDialog);

	/** Removes the dialog and gives the input back to the planet */
	void CloseDialog();

	/** Returns the planet of the pawn, or null */
	AHexPlanet* GetPlanet() const;

	/** Returns the brush, made on the planet when first needed, or null without a planet */
	UHexPlanetPainter* GetPainter();

	/** Puts the input mode of play back */
	void SetGameInputMode();

	/** Fills the legend with the lines of DA_HexKeyLegend whose actions the pawn binds, once it has bound them */
	void UpdateLegend();
};

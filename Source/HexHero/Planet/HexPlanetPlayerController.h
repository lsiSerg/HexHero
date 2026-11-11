// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HexPlanetFile.h"
#include "HexPlanetPlayerController.generated.h"

class AHexPlanet;
class SHexDebugOverlay;
class SHexDialog;
class SHexCastlePanel;
class SHexHeroPanel;
class SHexModeBar;
class SHexHoverInfo;
class SHexMapMenu;
class SHexPlanetPalette;
class UHexHeroes;
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

	/** Black screen of a trip between the layers */
	TSharedPtr<SWidget> TravelScreen;

	/** Panel of the heroes of the planet, at the bottom right */
	TSharedPtr<SHexHeroPanel> HeroPanel;

	/** Panel of the castle a person looks into, at the right; a click outside it closes it */
	TSharedPtr<SHexCastlePanel> CastlePanel;

	/** The buttons Edit and Play at the top left */
	TSharedPtr<SHexModeBar> ModeBar;

	/** The heroes whose messages and trips this controller listens to */
	TWeakObjectPtr<UHexHeroes> BoundHeroes;

	/** Cell under the cursor in the last tick */
	int32 MouseCell = INDEX_NONE;

	/** Brush that paints both layers of the planet, the one shown, made when painting first turns on once both have their data */
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

	/** Returns true while painting is on, with its palette */
	bool IsPaintModeOn() const;

	/** Returns true while the left button paints with a tool of the brush, rather than dragging the planet */
	bool IsBrushArmed() const;

	/** Turns painting on or off, with its palette */
	void SetPaintMode(bool bOn);

	/** Sets the level of the brush by a number key, 1 deep sea to 5 mountains on the surface and 1 void, 2 passage underground, and turns
	 *  painting on */
	void SetPaintLevelKey(int32 Number);

	/** Travels to a layer at the same place, as its tab of the palette asks */
	void TravelToLayer(EHexPlanetLayer Layer);

	/** Runs hex.PlanetPaint, which drives the brush as the palette and the mouse would */
	void RunPaintCommand(const TArray<FString>& Args);

	/** Makes the brush one ring larger, from the largest back to one cell */
	void CycleBrushSize();

	/** Starts and ends a stroke of the brush at the cell under the cursor */
	void BeginPaintStroke();
	void EndPaintStroke();

	/** Undoes or redoes a stroke */
	void UndoPaint();
	void RedoPaint();

	/** Returns the cell under the cursor in the last tick, on the layer shown */
	int32 GetMouseCell() const { return MouseCell; }

	/** Returns true while the game is played (UHexHeroes::IsPlaying) */
	bool IsPlayMode() const;

	/** Starts the game, which puts the brush down and needs a hero on the planet, or stops it; the game and the editing never run together */
	void SetPlayMode(bool bOn);

	/** A left click of the game at the mouse (UHexHeroes::Click): it chooses the hero there, by its body or its cell, points the hero chosen
	 *  to the cell there or sends it; a double click on a hero chooses it and looks at it */
	void ClickGame(bool bDouble);

	/** A click of the button that turns the camera, while the game is played: the hero chosen is sent to the cell under the cursor at once */
	void OrderGame();

	/** Brings the camera to the hero chosen where it was just sent on the layer that is not shown, as a cell of the layer shown may be
	 *  clicked for a hero on the other, whose way goes through an entrance or exit */
	void OnHeroSent(int32 Id);

	/** Sends the hero chosen along the way shown, as the second click on its cell does */
	void WalkHero();

	/** Starts the game again: day 1, every hero where it starts */
	void RestartGame();

	/** Chooses the next hero and shows it */
	void SelectNextHero();

	/** Ends the day of the heroes */
	void EndHeroTurn();

	/** Leaves the hero chosen for today (UHexHeroes::Skip) */
	void SkipHero();

	/** Chooses a hero and shows it: the camera glides to it, or travels to its layer */
	void ShowHero(int32 Id);

	/** Shows or hides the black screen of a trip between the layers, over every widget, as the fade of the camera does not cover them */
	void SetTravelScreen(bool bShown);

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

	/** Returns the heroes of the planet once it is built, or null */
	UHexHeroes* GetHeroes() const;

	/** Shows a line of the heroes on the screen, as why one does not go */
	void OnHeroMessage(const FString& Message);

	/** The camera goes with the hero chosen when it goes through an entrance or exit */
	void OnHeroTravelled(int32 Id);

	/** The camera shows the hero the game chose by itself */
	void OnHeroChosen(int32 Id);

	/** A person looks into a castle of its own: its panel opens, and the camera comes close to the castle, which shows a little right of
	 *  the middle of the screen beside the panel */
	void OnCastleOpened(int32 Id);

	/** Chooses a castle from the list of the panel of the heroes and shows it; the one chosen already opens its panel */
	void ShowCastle(int32 Id);

	/** Returns the brush, made on the surface of the planet when first needed, or null without one */
	UHexPlanetPainter* GetPainter();

	/** Puts the input mode of play back */
	void SetGameInputMode();

	/** Fills the legend with the lines of DA_HexKeyLegend whose actions the pawn binds, once it has bound them */
	void UpdateLegend();
};

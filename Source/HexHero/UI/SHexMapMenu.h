// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexMapManifest.h"
#include "HexPlanetFile.h"
#include "InputCoreTypes.h"
#include "SHexNewPlanetPanel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SVerticalBox;

DECLARE_DELEGATE_OneParam(FOnHexMapChosen, const FString& /*MapFolder*/);

/**
 *  Full screen menu. Its main page leads to the list of the maps, the list of the planets and the page that makes a new planet, each with a
 *  way back, so it stays short however many maps there are. It closes with its button or its close keys on the main page; Escape on another
 *  page goes back to the main one, as a page may hold a text box that takes the other keys.
 */
class SHexMapMenu : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexMapMenu) {}
		/** Maps to list */
		SLATE_ARGUMENT(TArray<FHexMapEntry>, Maps)
		/** Keys that close the menu */
		SLATE_ARGUMENT(TArray<FKey>, CloseKeys)
		/** Called when a map is clicked */
		SLATE_EVENT(FOnHexMapChosen, OnMapChosen)
		/** Planets to list, whose page shows only when OnPlanetChosen is bound */
		SLATE_ARGUMENT(TArray<FHexPlanetEntry>, Planets)
		/** Called when a planet is clicked, with its folder in Content/Data/Planets */
		SLATE_EVENT(FOnHexMapChosen, OnPlanetChosen)
		/** Called with a new planet to open, not saved yet; the page of a new planet shows only when it is bound */
		SLATE_EVENT(FOnHexPlanetCreated, OnNewPlanet)
		/** Called by the button that saves the open planet, which shows only when it is bound */
		SLATE_EVENT(FSimpleDelegate, OnSave)
		/** Whether there is anything to save, else the save button is disabled */
		SLATE_ATTRIBUTE(bool, CanSave)
		/** Called when the menu is closed without choosing a map */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/** Creates the content of the menu */
	void Construct(const FArguments& InArgs);

	/** Keeps the keyboard focus, because the game gets no input while the menu is open */
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Closes the menu with its keys, or goes back to the main page */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Keeps clicks next to the buttons from taking the keyboard focus away */
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:

	/** Shows a page in the box of the menu, at a width */
	void ShowPage(const TSharedRef<SWidget>& Content, float Width, bool bMain);

	/** Shows the main page */
	void ShowMainPage();

	/** Returns a page with a title, a scrolled list and a button back to the main page */
	TSharedRef<SWidget> MakeListPage(const FText& Title, const TSharedRef<SWidget>& List);

	/** Returns a button of the menu */
	static TSharedRef<SWidget> MakeButton(const FText& Label, TFunction<void()> OnClicked);

	/** Box that shows the current page */
	TSharedPtr<SBox> Page;

	/** Pages of the menu */
	TSharedPtr<SWidget> MainPage;
	TSharedPtr<SWidget> MapsPage;
	TSharedPtr<SWidget> PlanetsPage;

	/** True while the main page is shown */
	bool bMainPage = true;

	/** Keys that close the menu */
	TArray<FKey> CloseKeys;

	/** Called when a map is clicked */
	FOnHexMapChosen OnMapChosen;

	/** Called when a planet is clicked */
	FOnHexMapChosen OnPlanetChosen;

	/** Called with a new planet */
	FOnHexPlanetCreated OnNewPlanet;

	/** Called when the menu is closed without choosing a map */
	FSimpleDelegate OnClosed;
};

// Copyright Epic Games, Inc. All Rights Reserved.


#include "SHexMapMenu.h"
#include "HexSphereGrid.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexMapMenu"

namespace
{
	const float MenuWidth = 400.0f;
	const float NewPlanetWidth = 560.0f;
	const float MaxListHeight = 480.0f;
	const float ButtonPadding = 8.0f;

	TSharedRef<SWidget> MakeTitle(const FText& Title)
	{
		return SNew(STextBlock)
			.Text(Title)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18));
	}
}

TSharedRef<SWidget> SHexMapMenu::MakeButton(const FText& Label, TFunction<void()> OnClicked)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.ContentPadding(ButtonPadding)
		.Text(Label)
		.OnClicked_Lambda([OnClicked]()
		{
			OnClicked();
			return FReply::Handled();
		});
}

void SHexMapMenu::Construct(const FArguments& InArgs)
{
	CloseKeys = InArgs._CloseKeys;
	OnMapChosen = InArgs._OnMapChosen;
	OnPlanetChosen = InArgs._OnPlanetChosen;
	OnNewPlanet = InArgs._OnNewPlanet;
	OnClosed = InArgs._OnClosed;

	// the maps
	TSharedRef<SVerticalBox> MapList = SNew(SVerticalBox);
	for (const FHexMapEntry& Map : InArgs._Maps)
	{
		const FString MapFolder = Map.Folder;
		MapList->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, ButtonPadding)
		[
			MakeButton(FText::FromString(FString::Printf(TEXT("%s (%dx%d)"), *Map.Manifest.Name, Map.Manifest.Width, Map.Manifest.Height)),
				[this, MapFolder]() { OnMapChosen.ExecuteIfBound(MapFolder); })
		];
	}
	MapsPage = MakeListPage(LOCTEXT("MapsTitle", "Load map"), MapList);

	// the planets
	TSharedRef<SVerticalBox> PlanetList = SNew(SVerticalBox);
	for (const FHexPlanetEntry& Planet : InArgs._Planets)
	{
		const FString PlanetFolder = Planet.Folder;
		PlanetList->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, ButtonPadding)
		[
			MakeButton(FText::Format(LOCTEXT("Planet", "{0} ({1} cells)"), FText::FromString(Planet.Manifest.Name),
				FText::AsNumber(FHexSphereGrid::CountCells(Planet.Manifest.Frequency))),
				[this, PlanetFolder]() { OnPlanetChosen.ExecuteIfBound(PlanetFolder); })
		];
	}
	PlanetsPage = MakeListPage(LOCTEXT("PlanetsTitle", "Load planet"), PlanetList);

	// the main page
	TSharedRef<SVerticalBox> Main = SNew(SVerticalBox);
	const auto AddMainButton = [&Main](const FText& Label, TFunction<void()> OnClicked)
	{
		Main->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, ButtonPadding)
		[
			MakeButton(Label, MoveTemp(OnClicked))
		];
	};

	Main->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Center)
	.Padding(0.0f, 0.0f, 0.0f, 16.0f)
	[
		MakeTitle(LOCTEXT("Title", "Hex Hero"))
	];

	AddMainButton(LOCTEXT("LoadMap", "Load map..."), [this]() { ShowPage(MapsPage.ToSharedRef(), MenuWidth, false); });

	if (OnPlanetChosen.IsBound())
	{
		AddMainButton(LOCTEXT("LoadPlanet", "Load planet..."), [this]() { ShowPage(PlanetsPage.ToSharedRef(), MenuWidth, false); });
	}

	if (OnNewPlanet.IsBound())
	{
		AddMainButton(LOCTEXT("NewPlanet", "New hex planet..."), [this]()
		{
			ShowPage(SNew(SHexNewPlanetPanel)
				.OnCreated_Lambda([this](const FHexPlanetManifest& Manifest) { OnNewPlanet.ExecuteIfBound(Manifest); })
				.OnBack(this, &SHexMapMenu::ShowMainPage),
				NewPlanetWidth, false);
		});
	}

	// saving the open planet, enabled only while it has something to save
	if (InArgs._OnSave.IsBound())
	{
		const FSimpleDelegate OnSave = InArgs._OnSave;
		TSharedRef<SWidget> SaveButton = MakeButton(LOCTEXT("Save", "Save planet"), [OnSave]() { OnSave.ExecuteIfBound(); });
		SaveButton->SetEnabled(InArgs._CanSave);

		Main->AddSlot()
		.AutoHeight()
		.Padding(0.0f, ButtonPadding, 0.0f, ButtonPadding)
		[
			SaveButton
		];
	}

	Main->AddSlot()
	.AutoHeight()
	.Padding(0.0f, ButtonPadding, 0.0f, 0.0f)
	[
		MakeButton(LOCTEXT("Close", "Close"), [this]() { OnClosed.ExecuteIfBound(); })
	];

	MainPage = Main;

	const FSlateBrush* Background = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(Background)
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(Background)
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.95f))
			.Padding(24.0f)
			[
				SAssignNew(Page, SBox)
				.WidthOverride(MenuWidth)
				[
					MainPage.ToSharedRef()
				]
			]
		]
	];
}

TSharedRef<SWidget> SHexMapMenu::MakeListPage(const FText& Title, const TSharedRef<SWidget>& List)
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 0.0f, 0.0f, 16.0f)
		[
			MakeTitle(Title)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(MaxListHeight)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				List
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, ButtonPadding, 0.0f, 0.0f)
		[
			MakeButton(LOCTEXT("Back", "Back"), [this]() { ShowMainPage(); })
		];
}

void SHexMapMenu::ShowPage(const TSharedRef<SWidget>& Content, float Width, bool bMain)
{
	bMainPage = bMain;
	Page->SetWidthOverride(Width);
	Page->SetContent(Content);
}

void SHexMapMenu::ShowMainPage()
{
	ShowPage(MainPage.ToSharedRef(), MenuWidth, true);
}

FReply SHexMapMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// another page may hold a text box that takes the other keys, so only Escape leaves it, back to the main page
	if (!bMainPage)
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			ShowMainPage();
			return FReply::Handled().SetUserFocus(SharedThis(this));
		}

		return FReply::Unhandled();
	}

	// a key held since it opened the menu must not close it again
	if (!InKeyEvent.IsRepeat() && CloseKeys.Contains(InKeyEvent.GetKey()))
	{
		OnClosed.ExecuteIfBound();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SHexMapMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().SetUserFocus(SharedThis(this));
}

#undef LOCTEXT_NAMESPACE

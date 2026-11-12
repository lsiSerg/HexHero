// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexUiConfig.h"
#include "HexHero.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateImageBrush.h"

namespace
{
	const TCHAR* UiConfigPath = TEXT("/Game/HexPlanet/UI/DA_HexUi.DA_HexUi");
}

const FHexCursorLook* UHexUiConfig::FindCursor(EHexCursor Cursor) const
{
	return Cursors.FindByPredicate([Cursor](const FHexCursorLook& Each) { return Each.Cursor == Cursor; });
}

const FHexStatIconLook* UHexUiConfig::FindIconLook(EHexStatIcon Icon) const
{
	return StatIcons.FindByPredicate([Icon](const FHexStatIconLook& Each) { return Each.Icon == Icon; });
}

int32 UHexUiConfig::GetFrame(int32 Frames, float FramesPerSecond)
{
	return Frames <= 1 ? 0 : int32(FMath::FloorToInt64(FPlatformTime::Seconds() * double(FMath::Max(FramesPerSecond, 0.1f))) % Frames);
}

void UHexUiConfig::MakeFrameBrushes(UTexture2D* Texture, int32 Frames, float Size, TArray<TSharedPtr<FSlateBrush>>& OutBrushes)
{
	OutBrushes.Reset();
	const int32 Count = FMath::Max(Frames, 1);
	for (int32 Frame = 0; Frame < Count; ++Frame)
	{
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateImageBrush>(Texture, FVector2D(Size, Size));
		Brush->SetUVRegion(FBox2f(FVector2f(float(Frame) / Count, 0.0f), FVector2f(float(Frame + 1) / Count, 1.0f)));
		OutBrushes.Add(Brush);
	}
}

TSoftObjectPtr<UTexture2D> UHexUiConfig::FindIcon(EHexStatIcon Icon) const
{
	const FHexStatIconLook* Found = StatIcons.FindByPredicate([Icon](const FHexStatIconLook& Each) { return Each.Icon == Icon; });
	return Found ? Found->Texture : TSoftObjectPtr<UTexture2D>();
}

const UHexUiConfig* UHexUiConfig::Load()
{
	static TWeakObjectPtr<const UHexUiConfig> Loaded;
	if (Loaded.IsValid())
	{
		return Loaded.Get();
	}
	const UHexUiConfig* Config = LoadObject<UHexUiConfig>(nullptr, UiConfigPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Config)
	{
		UE_LOG(LogHexHero, Warning, TEXT("UI: %s is missing, the defaults of UHexUiConfig are used (run Tools/HexUi/import_ui_icons.py)"), UiConfigPath);
		Config = GetDefault<UHexUiConfig>();
	}
	else
	{
		// nothing else holds it, and the widgets read it every frame
		const_cast<UHexUiConfig*>(Config)->AddToRoot();
	}
	Loaded = Config;
	return Config;
}

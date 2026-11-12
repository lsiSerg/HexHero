// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexGameCursor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ICursor.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateImageBrush.h"

void SHexGameCursor::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	const UHexUiConfig* Config = UHexUiConfig::Load();
	const int32 Num = int32(EHexCursor::Num);
	Brushes.SetNum(Num);
	Rates.SetNum(Num);
	HotSpots.SetNum(Num);
	Sizes.SetNum(Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FHexCursorLook* Look = Config ? Config->FindCursor(EHexCursor(Index)) : nullptr;
		UTexture2D* Texture = Look ? Look->Texture.LoadSynchronous() : nullptr;
		if (!Texture)
		{
			continue;
		}
		Textures.Emplace(Texture);
		UHexUiConfig::MakeFrameBrushes(Texture, Look->Frames, Look->Size, Brushes[Index]);
		Rates[Index] = Look->FramesPerSecond;
		HotSpots[Index] = Look->HotSpot;
		Sizes[Index] = Look->Size;
	}
}

const FSlateBrush* SHexGameCursor::GetBrush(EHexCursor Which) const
{
	const int32 Index = int32(Which);
	if (!Brushes.IsValidIndex(Index) || Brushes[Index].IsEmpty())
	{
		return nullptr;
	}
	// the frame of its strip that plays now
	return Brushes[Index][UHexUiConfig::GetFrame(Brushes[Index].Num(), Rates[Index])].Get();
}

bool SHexGameCursor::UsesSystemCursor() const
{
	const UHexUiConfig* Config = UHexUiConfig::Load();
	return !Config || !Config->bGameCursors || !GetBrush(Cursor);
}

int32 SHexGameCursor::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* Brush = UsesSystemCursor() ? nullptr : GetBrush(Cursor);
	if (!Brush || !FSlateApplication::IsInitialized())
	{
		return LayerId;
	}
	// where a widget shows a cursor of its own, as a text box does, the system draws it
	const TSharedPtr<ICursor> Platform = FSlateApplication::Get().GetPlatformCursor();
	if (Platform.IsValid() && Platform->GetType() != EMouseCursor::None)
	{
		return LayerId;
	}
	const FVector2D Local = AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	if (Local.X < 0.0 || Local.Y < 0.0 || Local.X > AllottedGeometry.GetLocalSize().X || Local.Y > AllottedGeometry.GetLocalSize().Y)
	{
		return LayerId;
	}
	const FVector2D Size(Sizes[int32(Cursor)], Sizes[int32(Cursor)]);
	const FVector2D HotSpot = HotSpots[int32(Cursor)];
	const FVector2D Corner = Local - HotSpot * Size;
	const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Corner));
	if (Cursor == EHexCursor::BattleStrike)
	{
		FSlateDrawElement::MakeRotatedBox(OutDrawElements, LayerId, Geometry, Brush, ESlateDrawEffect::None, FMath::DegreesToRadians(Angle),
			TOptional<FVector2f>(FVector2f(HotSpot * Size)), FSlateDrawElement::RelativeToElement);
	}
	else
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry, Brush);
	}
	return LayerId + 1;
}

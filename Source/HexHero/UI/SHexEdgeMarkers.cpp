// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexEdgeMarkers.h"
#include "HexCastles.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexPlanet.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"

namespace
{
	/** The marks, in the units of the widget: how far from the edges of the screen they lie, their size, how near two may lie before they are
	 *  a heap, and how fast they move apart */
	const float EdgeMargin = 30.0f;
	const float MarkSize = 30.0f;
	const float SelectedScale = 1.25f;
	const float HeapGap = 6.0f;
	const float HeapReach = 40.0f;
	const float HoverReach = 20.0f;
	const float MoveSpeed = 14.0f;
	const FLinearColor MarkDark(0.02f, 0.02f, 0.03f, 0.95f);
	const FLinearColor SelectedRing(1.0f, 0.86f, 0.3f, 1.0f);
	const FLinearColor LabelBack(0.0f, 0.0f, 0.0f, 0.75f);

	/** A disc: a box rounded by half its size, which a larger radius than that does not draw, so one brush for each size drawn */
	const FSlateBrush* GetRoundBrush(float Size)
	{
		static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Brushes;
		const int32 Key = FMath::RoundToInt32(Size * 2.0f);
		TSharedPtr<FSlateRoundedBoxBrush>& Brush = Brushes.FindOrAdd(Key);
		if (!Brush.IsValid())
		{
			Brush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, Size * 0.5f);
		}
		return Brush.Get();
	}

	const FSlateBrush* GetSquareBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 4.0f);
		return &Brush;
	}
}

void SHexEdgeMarkers::Construct(const FArguments& InArgs)
{
	OnLocate = InArgs._OnLocate;
	IsBlocked = InArgs._IsBlocked;
	// hit only while the cursor is on the marks, so what lies under them takes no click there; the click itself it leaves to the game
	SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SHexEdgeMarkers::GetMarksVisibility));
	SetCanTick(true);
}

EVisibility SHexEdgeMarkers::GetMarksVisibility() const
{
	return IsCursorOnMarks() ? EVisibility::Visible : EVisibility::HitTestInvisible;
}

FVector2D SHexEdgeMarkers::GetEdgePoint(float Along, FVector2D& OutInward) const
{
	const float Width = FMath::Max(float(LaidSize.X) - 2.0f * EdgeMargin, 1.0f);
	const float Height = FMath::Max(float(LaidSize.Y) - 2.0f * EdgeMargin, 1.0f);
	const float Around = 2.0f * (Width + Height);
	float At = FMath::Fmod(Along, Around);
	At += At < 0.0f ? Around : 0.0f;
	if (At < Width)
	{
		OutInward = FVector2D(0.0, 1.0);
		return FVector2D(EdgeMargin + At, EdgeMargin);
	}
	At -= Width;
	if (At < Height)
	{
		OutInward = FVector2D(-1.0, 0.0);
		return FVector2D(EdgeMargin + Width, EdgeMargin + At);
	}
	At -= Height;
	if (At < Width)
	{
		OutInward = FVector2D(0.0, -1.0);
		return FVector2D(EdgeMargin + Width - At, EdgeMargin + Height);
	}
	At -= Width;
	OutInward = FVector2D(1.0, 0.0);
	return FVector2D(EdgeMargin, EdgeMargin + Height - At);
}

bool SHexEdgeMarkers::GetHovered(bool& bOutCastle, int32& OutId) const
{
	if (!Marks.IsValidIndex(Hovered))
	{
		return false;
	}
	bOutCastle = Marks[Hovered].bCastle;
	OutId = Marks[Hovered].Id;
	return true;
}

void SHexEdgeMarkers::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	LaidSize = AllottedGeometry.GetLocalSize();
	const float Scale = FMath::Max(AllottedGeometry.Scale, 0.01f);
	const float Width = FMath::Max(float(LaidSize.X) - 2.0f * EdgeMargin, 1.0f);
	const float Height = FMath::Max(float(LaidSize.Y) - 2.0f * EdgeMargin, 1.0f);

	// what the person who plays has that does not show in the view
	TArray<FMark> Found;
	const UHexHeroes* Source = Heroes.Get();
	const AHexPlanet* Planet = Source && Source->IsPlaying() && Source->GetConfig() && OnLocate && !IsBlocked.Get(false) ? Source->GetLayerPlanet(EHexPlanetLayer::Surface) : nullptr;
	if (Planet)
	{
		const int32 Player = Source->GetShroudPlayer();
		const UHexCastles* Castles = Source->GetCastles();
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 10);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D Center = LaidSize * 0.5;
		const auto Add = [&](bool bCastle, int32 Id, const FString& Name, const FVector3d& Point, EHexPlanetLayer Layer, bool bSelected)
		{
			// only what is on the layer shown: a mark to a hero or castle of the other layer pointed the wrong way, and the developer asked
			// them away on 2026-11-12
			if (!Source->GetLayerPlanet(Layer) || !Source->GetLayerPlanet(Layer)->IsPresented())
			{
				return;
			}
			FVector2D Screen;
			FVector2D Direction;
			if (OnLocate(Point, Layer, Screen, Direction) || Direction.IsNearlyZero())
			{
				return;
			}
			// where the way to it from the middle of the screen meets the edge, as a length around the edge
			const FVector2D Way = Direction.GetSafeNormal();
			const double ReachX = FMath::Abs(Way.X) > 1e-6 ? (Width * 0.5) / FMath::Abs(Way.X) : TNumericLimits<double>::Max();
			const double ReachY = FMath::Abs(Way.Y) > 1e-6 ? (Height * 0.5) / FMath::Abs(Way.Y) : TNumericLimits<double>::Max();
			const FVector2D Hit = Center + Way * FMath::Min(ReachX, ReachY) - FVector2D(EdgeMargin, EdgeMargin);
			FMark Mark;
			Mark.bCastle = bCastle;
			Mark.Id = Id;
			Mark.Name = Name;
			Mark.Color = Source->GetConfig()->GetPlayer(Player).Color;
			Mark.bSelected = bSelected;
			Mark.Target = ReachY < ReachX ? (Way.Y < 0.0 ? float(Hit.X) : Width + Height + Width - float(Hit.X))
				: (Way.X > 0.0 ? Width + float(Hit.Y) : 2.0f * Width + Height + Height - float(Hit.Y));
			Mark.Shown = Mark.Target;
			Mark.LabelWidth = float(Measure->Measure(Name, Font).X);
			Found.Add(Mark);
		};
		const int32 ChosenCastle = Source->GetSelectedCastleId();
		for (const FHexHero& Hero : Source->GetHeroes())
		{
			// a hero in a castle shows with its castle
			if (Hero.Player == Player && Hero.CastleId == 0 && !Hero.Position.IsNearlyZero())
			{
				Add(false, Hero.Id, Hero.Name, Hero.Position, Hero.Layer, ChosenCastle == 0 && Source->GetSelectedId() == Hero.Id);
			}
		}
		for (int32 Index = 0; Castles && Index < Castles->GetCastles().Num(); ++Index)
		{
			const FHexCastle& Castle = Castles->GetCastles()[Index];
			const AHexPlanet* Of = Source->GetLayerPlanet(Castle.Layer);
			if (Castle.Owner != Player || !Of || !Of->GetGrid().GetCells().IsValidIndex(Castle.Cells[0]))
			{
				continue;
			}
			FVector3d Point;
			if (!Castles->GetGuardPoint(Castle, Point))
			{
				Point = Of->GetGrid().GetCells()[Castle.Cells[0]].Center * Of->GetRadius();
			}
			const FString Faction = Castles->GetConfig() && Castles->GetConfig()->Factions.IsValidIndex(Castle.Faction) ? Castles->GetConfig()->Factions[Castle.Faction].DisplayName : FString();
			Add(true, Castle.Id, Faction.IsEmpty() ? FString(TEXT("Castle")) : FString::Printf(TEXT("Castle, %s"), *Faction), Point, Castle.Layer, ChosenCastle == Castle.Id);
		}
	}
	// each keeps where it was drawn, so it glides as the view turns
	for (FMark& Mark : Found)
	{
		if (const FMark* Old = Marks.FindByPredicate([&Mark](const FMark& Each) { return Each.bCastle == Mark.bCastle && Each.Id == Mark.Id; }))
		{
			Mark.Shown = Old->Shown;
		}
	}
	Found.Sort([](const FMark& A, const FMark& B) { return A.Target != B.Target ? A.Target < B.Target : (A.bCastle != B.bCastle ? A.bCastle < B.bCastle : A.Id < B.Id); });
	Marks = MoveTemp(Found);

	// the mark under the cursor, by where the marks are drawn
	const FVector2D Cursor = AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	Hovered = INDEX_NONE;
	double Nearest = FMath::Square(double(HoverReach));
	for (int32 Index = 0; Index < Marks.Num(); ++Index)
	{
		FVector2D Inward;
		const double Distance = FVector2D::DistSquared(GetEdgePoint(Marks[Index].Shown, Inward), Cursor);
		if (Distance < Nearest)
		{
			Nearest = Distance;
			Hovered = Index;
		}
	}
	const auto FindMark = [this](bool bCastle, int32 Id) { return Marks.IndexOfByPredicate([&](const FMark& Each) { return Each.bCastle == bCastle && Each.Id == Id; }); };

	// A heap moves apart while the cursor is on it: the marks that lie within reach of each other where they point, from the one the cursor
	// came to first, around which the others move, so it stays under the cursor; held while the cursor is on any of them or between them
	int32 Anchor = bSpread ? FindMark(bAnchorCastle, AnchorId) : int32(INDEX_NONE);
	int32 First = Anchor;
	int32 Last = Anchor;
	const auto FindHeap = [&]()
	{
		First = Last = Anchor;
		while (Marks.IsValidIndex(First - 1) && Marks[First].Target - Marks[First - 1].Target < HeapReach)
		{
			--First;
		}
		while (Marks.IsValidIndex(Last + 1) && Marks[Last + 1].Target - Marks[Last].Target < HeapReach)
		{
			++Last;
		}
	};
	if (Anchor != INDEX_NONE)
	{
		FindHeap();
		// still on it: under the cursor, or within the stretch of the edge the heap is drawn along
		FVector2D Inward;
		const FVector2D From = GetEdgePoint(Marks[First].Shown, Inward);
		const FVector2D To = GetEdgePoint(Marks[Last].Shown, Inward);
		const FVector2D Along = To - From;
		const double Share = Along.SizeSquared() > 1.0 ? FMath::Clamp(FVector2D::DotProduct(Cursor - From, Along) / Along.SizeSquared(), 0.0, 1.0) : 0.0;
		const bool bBetween = FVector2D::Distance(From + Along * Share, Cursor) < HoverReach * 1.6;
		if (Hovered == INDEX_NONE && !bBetween)
		{
			Anchor = INDEX_NONE;
		}
	}
	if (Anchor == INDEX_NONE && Hovered != INDEX_NONE)
	{
		Anchor = Hovered;
		// it stays where it is drawn, under the cursor
		AnchorOffset = Marks[Anchor].Shown - Marks[Anchor].Target;
		FindHeap();
	}
	bSpread = Anchor != INDEX_NONE && Last > First;
	if (Anchor != INDEX_NONE)
	{
		bAnchorCastle = Marks[Anchor].bCastle;
		AnchorId = Marks[Anchor].Id;
	}

	// where each is drawn: where it points, but the one chosen keeps its neighbors off itself, and a heap moved apart gives each the room its
	// name takes, across the top and the bottom of the screen its width
	const float Around = 2.0f * (Width + Height);
	const auto IsAcross = [&](float Along)
	{
		const float At = FMath::Fmod(FMath::Fmod(Along, Around) + Around, Around);
		return At < Width || (At >= Width + Height && At < 2.0f * Width + Height);
	};
	const auto GetRoom = [&](int32 One, int32 Other)
	{
		const bool bApart = bSpread && One >= First && Other <= Last;
		// the one chosen keeps the others off itself in a heap moved apart too, so nothing moves as a heap with it opens and closes
		const float Chosen = Marks[One].bSelected || Marks[Other].bSelected ? MarkSize * (1.0f + SelectedScale) * 0.5f + HeapGap : 0.0f;
		if (bApart)
		{
			return FMath::Max(Chosen, IsAcross(Marks[One].Target) ? FMath::Max(MarkSize + HeapGap, (Marks[One].LabelWidth + Marks[Other].LabelWidth) * 0.5f + 16.0f) : MarkSize + HeapGap);
		}
		return Chosen;
	};
	TArray<float> Wanted;
	for (const FMark& Mark : Marks)
	{
		Wanted.Add(Mark.Target);
	}
	// from the one that stays, the others move away to both sides: the one chosen, where it points, or in a heap moved apart the one the
	// cursor came to, where it was drawn then
	const int32 Fixed = bSpread ? Anchor : Marks.IndexOfByPredicate([](const FMark& Each) { return Each.bSelected; });
	const int32 Start = Marks.IsValidIndex(Fixed) ? Fixed : 0;
	if (bSpread)
	{
		Wanted[Anchor] = Marks[Anchor].Target + AnchorOffset;
	}
	for (int32 Index = Start + 1; Index < Marks.Num(); ++Index)
	{
		Wanted[Index] = FMath::Max(Wanted[Index], Wanted[Index - 1] + GetRoom(Index - 1, Index));
	}
	for (int32 Index = Start - 1; Index >= 0; --Index)
	{
		Wanted[Index] = FMath::Min(Wanted[Index], Wanted[Index + 1] - GetRoom(Index, Index + 1));
	}
	const float Blend = FMath::Clamp(InDeltaTime * MoveSpeed, 0.0f, 1.0f);
	for (int32 Index = 0; Index < Marks.Num(); ++Index)
	{
		Marks[Index].Shown = FMath::Abs(Wanted[Index] - Marks[Index].Shown) > Around * 0.25f ? Wanted[Index] : FMath::Lerp(Marks[Index].Shown, Wanted[Index], Blend);
		// the name of the one under the cursor, and of all of a heap moved apart
		Marks[Index].bLabel = Index == Hovered || (bSpread && Index >= First && Index <= Last);
	}
}

int32 SHexEdgeMarkers::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const auto Box = [&](int32 Layer, const FVector2D& Middle, const FVector2D& Size, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Middle - Size * 0.5)), Brush, ESlateDrawEffect::None, Color);
	};

	// the one chosen and the one under the cursor over the others
	TArray<int32> Order;
	for (int32 Index = 0; Index < Marks.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Order.StableSort([this](int32 A, int32 B)
	{
		const int32 RankA = (A == Hovered ? 2 : 0) + (Marks[A].bSelected ? 1 : 0);
		const int32 RankB = (B == Hovered ? 2 : 0) + (Marks[B].bSelected ? 1 : 0);
		return RankA < RankB;
	});

	int32 Layer = LayerId;
	for (const int32 Index : Order)
	{
		const FMark& Mark = Marks[Index];
		FVector2D Inward;
		const FVector2D Middle = GetEdgePoint(Mark.Shown, Inward);
		const float Size = MarkSize * (Mark.bSelected ? SelectedScale : 1.0f);
		const FLinearColor Fill = Mark.Color;
		const FLinearColor Rim = Mark.bSelected ? SelectedRing : MarkDark;

		// an arrow towards the edge, a rim, and the mark in the color of the player
		const FVector2D Out = -Inward;
		const FVector2D Side(-Out.Y, Out.X);
		const FVector2D Tip = Middle + Out * (Size * 0.5f + 9.0f);
		const TArray<FVector2D> Arrow = {Tip - Out * 7.0f + Side * 6.0f, Tip, Tip - Out * 7.0f - Side * 6.0f};
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Arrow, ESlateDrawEffect::None, Rim, true, 3.0f);
		const float RimSize = Size + (Mark.bSelected ? 8.0f : 5.0f);
		Box(Layer, Middle, FVector2D(RimSize), Mark.bCastle ? GetSquareBrush() : GetRoundBrush(RimSize), Rim);
		Box(Layer + 1, Middle, FVector2D(Size), Mark.bCastle ? GetSquareBrush() : GetRoundBrush(Size), Fill);

		// what it is: a banner on a pole for a hero, a wall with battlements for a castle
		const FLinearColor Sign = FLinearColor::White;
		FLinearColor Shade = MarkDark;
		Shade.A = 0.55f;
		const float Unit = Size / 30.0f;
		if (Mark.bCastle)
		{
			Box(Layer + 2, Middle + FVector2D(0.0, 3.5) * Unit, FVector2D(17.0, 9.0) * Unit, White, Sign);
			for (const float Merlon : {-6.5f, 0.0f, 6.5f})
			{
				Box(Layer + 2, Middle + FVector2D(Merlon, -4.0) * Unit, FVector2D(4.0, 6.0) * Unit, White, Sign);
			}
			Box(Layer + 3, Middle + FVector2D(0.0, 5.0) * Unit, FVector2D(4.5, 6.0) * Unit, White, Shade);
		}
		else
		{
			Box(Layer + 2, Middle + FVector2D(-4.5, 0.0) * Unit, FVector2D(2.5, 19.0) * Unit, White, Sign);
			Box(Layer + 2, Middle + FVector2D(2.0, -4.5) * Unit, FVector2D(11.0, 8.0) * Unit, White, Sign);
			Box(Layer + 3, Middle + FVector2D(2.0, -4.5) * Unit, FVector2D(5.0, 3.0) * Unit, White, Shade);
		}

		// its name, into the screen from it
		if (Mark.bLabel)
		{
			const FString& Text = Mark.Name;
			const FVector2D TextSize = Measure->Measure(Text, Font);
			const FVector2D Margin(6.0, 3.0);
			const FVector2D BoxSize = TextSize + Margin * 2.0;
			const FVector2D LabelMiddle = Middle + Inward * (Size * 0.5f + 8.0f + float(FMath::Abs(Inward.X) > 0.5 ? BoxSize.X : BoxSize.Y) * 0.5f);
			Box(Layer + 4, LabelMiddle, BoxSize, White, LabelBack);
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 5, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(LabelMiddle - TextSize * 0.5)), Text, Font,
				ESlateDrawEffect::None, FLinearColor::White);
		}
		Layer += 6;
	}
	return Layer;
}

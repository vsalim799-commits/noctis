#include "UI/NoctisWidgets.h"

#include "Research/NoctisResearchLibrary.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Components/Image.h"
#include "Components/RichTextBlock.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace NoctisWidgetsImpl
{
void PaintBox(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FVector2D& TopLeft, const FVector2D& Size,
			  const FLinearColor& Color)
{
	// VERIFY on 5.5: FGeometry::ToPaintGeometry(LocalSize, FSlateLayoutTransform) (the (Offset, Size) form is deprecated since 5.2).
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)), FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
							   ESlateDrawEffect::None, Color);
}

FLinearColor FeatureColor(int32 KindIndex)
{
	// noctis::MapFeatureKind order: Water, Nest, Carcass, FossilSite, Trackway, Trail, Sighting, Danger, Event, Sensor, Vehicle, Note.
	static const FLinearColor Colors[] = {FLinearColor(0.2f, 0.5f, 1.0f), FLinearColor(1.0f, 0.85f, 0.3f), FLinearColor(0.6f, 0.1f, 0.1f),
										  FLinearColor(0.85f, 0.75f, 0.6f), FLinearColor(0.9f, 0.55f, 0.2f), FLinearColor(0.7f, 0.6f, 0.4f),
										  FLinearColor(0.3f, 1.0f, 0.4f), FLinearColor(1.0f, 0.1f, 0.1f), FLinearColor(1.0f, 0.4f, 1.0f),
										  FLinearColor(0.4f, 1.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f), FLinearColor(0.8f, 0.8f, 0.8f)};
	return Colors[FMath::Clamp(KindIndex, 0, static_cast<int32>(UE_ARRAY_COUNT(Colors)) - 1)];
}

FString EscapeRich(const FString& Text)
{
	FString Out = Text.Replace(TEXT("&"), TEXT("&amp;"));
	Out = Out.Replace(TEXT("<"), TEXT("&lt;"));
	Out = Out.Replace(TEXT(">"), TEXT("&gt;"));
	return Out;
}
} // namespace NoctisWidgetsImpl

// ------------------------------------------------------------------ Base

void UNoctisWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();
	Refresh();
}

void UNoctisWidgetBase::Refresh()
{
	NativeRefresh();
	OnRefreshed();
}

UNoctisWorldSubsystem* UNoctisWidgetBase::GetNoctis() const
{
	return UNoctisWorldSubsystem::Get(this);
}

// ------------------------------------------------------------------ Field journal

void UNoctisFieldJournalWidget::NativeRefresh()
{
	Observations = UNoctisResearchLibrary::GetObservations(this, MaxObservations);
	Catalog = UNoctisResearchLibrary::GetCatalog(this);
}

void UNoctisFieldJournalWidget::AddNote(const FString& Text)
{
	UNoctisResearchLibrary::AddFieldNote(this, Text);
}

bool UNoctisFieldJournalWidget::RenameIndividual(int32 EntryId, const FString& Nickname)
{
	const bool bOk = UNoctisResearchLibrary::RenameCatalogEntry(this, EntryId, Nickname);
	Refresh();
	return bOk;
}

// ------------------------------------------------------------------ Map

void UNoctisFieldMapWidget::NativeRefresh()
{
	MapTexture = UNoctisResearchLibrary::BuildFieldMapTexture(this, Resolution, bShowEffort);
	Features = UNoctisResearchLibrary::GetMapFeatures(this);
	if (MapImage && MapTexture)
	{
		MapImage->SetBrushFromTexture(MapTexture, false);
	}
}

int32 UNoctisFieldMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
										 FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	const FVector2D Marker(MarkerSize, MarkerSize);
	++Layer;
	for (const FNoctisMapFeatureView& Feature : Features)
	{
		const FVector2D At = Feature.MapUV * Size - Marker * 0.5;
		NoctisWidgetsImpl::PaintBox(OutDrawElements, Layer, AllottedGeometry, At, Marker, NoctisWidgetsImpl::FeatureColor(Feature.KindIndex));
	}
	// Own position, rover and drone from the latest published frame.
	if (const UNoctisWorldSubsystem* Sub = GetNoctis())
	{
		const FNoctisFrameOutput* Front = Sub->GetFrontFrame();
		const noctis::Rect2& Bounds = Sub->GetCoreBounds();
		const noctis::Vec2 Extent = Bounds.size();
		if (Front && Extent.x > 0.0f && Extent.y > 0.0f)
		{
			auto ToUV = [&Bounds, &Extent](const noctis::Vec3& P) {
				return FVector2D((P.x - Bounds.min.x) / Extent.x, 1.0f - (P.y - Bounds.min.y) / Extent.y);
			};
			const FVector2D Big = Marker * 1.6;
			NoctisWidgetsImpl::PaintBox(OutDrawElements, Layer + 1, AllottedGeometry, ToUV(Front->Snapshot.vehiclePosition) * Size - Big * 0.5, Big,
										FLinearColor(0.1f, 0.1f, 0.1f));
			if (Front->Snapshot.droneAirborne)
			{
				NoctisWidgetsImpl::PaintBox(OutDrawElements, Layer + 1, AllottedGeometry, ToUV(Front->Snapshot.dronePosition) * Size - Marker * 0.5, Marker,
											FLinearColor(0.2f, 0.9f, 1.0f));
			}
			NoctisWidgetsImpl::PaintBox(OutDrawElements, Layer + 2, AllottedGeometry, ToUV(Front->Snapshot.researcherPosition) * Size - Big * 0.5, Big,
										FLinearColor(1.0f, 0.95f, 0.1f));
		}
	}
	return Layer + 2;
}

// ------------------------------------------------------------------ Spectrogram

void UNoctisSpectrogramWidget::NativeRefresh()
{
	Recordings = UNoctisResearchLibrary::GetRecordings(this);
}

void UNoctisSpectrogramWidget::ShowRecording(int32 RecordingId)
{
	CurrentRecording = RecordingId;
	FNoctisRecordingAnalyzed Done;
	Done.BindDynamic(this, &UNoctisSpectrogramWidget::HandleAnalyzed);
	const FNoctisRecordingView* View = Recordings.FindByPredicate([RecordingId](const FNoctisRecordingView& R) { return R.Id == RecordingId; });
	if (View && View->bAnalyzed)
	{
		HandleAnalyzed(RecordingId);
	}
	else
	{
		UNoctisResearchLibrary::AnalyzeRecordingAsync(this, RecordingId, Done);
	}
}

void UNoctisSpectrogramWidget::HandleAnalyzed(int32 RecordingId)
{
	if (RecordingId != CurrentRecording)
	{
		return;
	}
	Recordings = UNoctisResearchLibrary::GetRecordings(this);
	const FNoctisRecordingView* View = Recordings.FindByPredicate([RecordingId](const FNoctisRecordingView& R) { return R.Id == RecordingId; });
	CurrentDurationS = View ? View->DurationSeconds : 0.0f;
	SpectrogramTexture = UNoctisResearchLibrary::BuildSpectrogramTexture(this, RecordingId, MinDb, MaxDb, MaxFrequencyHz);
	Events = UNoctisResearchLibrary::GetRecordingEvents(this, RecordingId);
	if (SpectrogramImage && SpectrogramTexture)
	{
		SpectrogramImage->SetBrushFromTexture(SpectrogramTexture, false);
	}
	OnRecordingShown(RecordingId);
}

int32 UNoctisSpectrogramWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
											FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (CurrentDurationS <= 0.0f || MaxFrequencyHz <= 0.0f)
	{
		return Layer;
	}
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	++Layer;
	for (const FNoctisAcousticEventView& Event : Events)
	{
		// Detected event: time span x [f0 - bandwidth/2, f0 + bandwidth/2]; colour by call cluster.
		const double X0 = Event.StartSeconds / CurrentDurationS * Size.X;
		const double X1 = Event.EndSeconds / CurrentDurationS * Size.X;
		const double FTop = FMath::Min(MaxFrequencyHz, Event.F0Hz + 0.5f * Event.BandwidthHz);
		const double FBottom = FMath::Max(0.0f, Event.F0Hz - 0.5f * Event.BandwidthHz);
		const double Y0 = (1.0 - FTop / MaxFrequencyHz) * Size.Y;
		const double Y1 = (1.0 - FBottom / MaxFrequencyHz) * Size.Y;
		const FLinearColor Color = Event.Cluster >= 0 ? FLinearColor::MakeFromHSV8(static_cast<uint8>((Event.Cluster * 67) % 255), 200, 255) : FLinearColor::White;
		NoctisWidgetsImpl::PaintBox(OutDrawElements, Layer, AllottedGeometry, FVector2D(X0, Y0), FVector2D(FMath::Max(2.0, X1 - X0), FMath::Max(2.0, Y1 - Y0)),
									Color.CopyWithNewOpacity(0.25f));
	}
	return Layer;
}

// ------------------------------------------------------------------ Hypotheses

void UNoctisHypothesisPanelWidget::NativeRefresh()
{
	Hypotheses = UNoctisResearchLibrary::GetHypotheses(this);
	Species = UNoctisResearchLibrary::GetSpeciesInWorld(this);
}

int32 UNoctisHypothesisPanelWidget::CreateFromDraft(const FNoctisHypothesisDraft& Draft)
{
	const int32 Id = UNoctisResearchLibrary::CreateHypothesis(this, Draft);
	Refresh();
	return Id;
}

FNoctisHypothesisView UNoctisHypothesisPanelWidget::Test(int32 HypothesisId, bool bCurrentExpeditionOnly)
{
	const FNoctisHypothesisView View = UNoctisResearchLibrary::TestHypothesis(this, HypothesisId, bCurrentExpeditionOnly);
	Refresh();
	return View;
}

// ------------------------------------------------------------------ Reports

void UNoctisReportViewerWidget::NativeRefresh()
{
	Reports = UNoctisResearchLibrary::GetReports(this);
}

void UNoctisReportViewerWidget::ShowReport(int32 Index)
{
	if (ReportText && Reports.IsValidIndex(Index))
	{
		ReportText->SetText(FText::FromString(MarkdownToRichText(Reports[Index].Markdown)));
	}
}

FString UNoctisReportViewerWidget::MarkdownToRichText(const FString& Markdown)
{
	TArray<FString> Lines;
	Markdown.ParseIntoArray(Lines, TEXT("\n"), false);
	FString Out;
	for (const FString& Raw : Lines)
	{
		FString Line = NoctisWidgetsImpl::EscapeRich(Raw);
		if (Line.StartsWith(TEXT("# ")))
		{
			Line = FString::Printf(TEXT("<Title>%s</>"), *Line.Mid(2));
		}
		else if (Line.StartsWith(TEXT("## ")) || Line.StartsWith(TEXT("### ")))
		{
			int32 Hashes = 0;
			while (Hashes < Line.Len() && Line[Hashes] == TEXT('#'))
			{
				++Hashes;
			}
			Line = FString::Printf(TEXT("<Heading>%s</>"), *Line.Mid(Hashes + 1));
		}
		else if (Line.StartsWith(TEXT("- ")))
		{
			Line = FString(TEXT("• ")) + Line.Mid(2);
		}
		// **bold** -> <Bold>bold</>
		int32 Start = Line.Find(TEXT("**"));
		while (Start != INDEX_NONE)
		{
			const int32 End = Line.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
			if (End == INDEX_NONE)
			{
				break;
			}
			Line = Line.Left(Start) + TEXT("<Bold>") + Line.Mid(Start + 2, End - Start - 2) + TEXT("</>") + Line.Mid(End + 2);
			Start = Line.Find(TEXT("**"));
		}
		Out += Line;
		Out += TEXT("\n");
	}
	return Out;
}

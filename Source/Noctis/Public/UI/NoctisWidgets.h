// UMG skeleton of the field-science interface. Each widget is a C++ base for a Widget Blueprint (WBP_*) that
// provides the layout; data come from UNoctisResearchLibrary (the player's own data only).
//   UNoctisFieldJournalWidget    — observations and the individual catalogue (photo-identification)
//   UNoctisFieldMapWidget        — the player's map: explored relief, effort, features, own position
//   UNoctisSpectrogramWidget     — recording analysis: spectrogram texture + detected events overlay
//   UNoctisHypothesisPanelWidget — hypotheses: create from a draft, test, read statistics and caveats
//   UNoctisReportViewerWidget    — expedition reports (Markdown from the core rendered as rich text)
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "Research/NoctisResearchTypes.h"

#include "NoctisWidgets.generated.h"

class UImage;
class URichTextBlock;
class UTexture2D;
class UNoctisWorldSubsystem;

UCLASS(Abstract)
class NOCTIS_API UNoctisWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Re-reads the data (short exclusive access to the simulation) and raises OnRefreshed. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|UI")
	void Refresh();

	UFUNCTION(BlueprintImplementableEvent, Category = "Noctis|UI")
	void OnRefreshed();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeRefresh() {}
	UNoctisWorldSubsystem* GetNoctis() const;
};

UCLASS(Abstract)
class NOCTIS_API UNoctisFieldJournalWidget : public UNoctisWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisObservationView> Observations;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisCatalogEntryView> Catalog;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|UI")
	int32 MaxObservations = 200;

	UFUNCTION(BlueprintCallable, Category = "Noctis|UI")
	void AddNote(const FString& Text);

	UFUNCTION(BlueprintCallable, Category = "Noctis|UI")
	bool RenameIndividual(int32 EntryId, const FString& Nickname);

protected:
	virtual void NativeRefresh() override;
};

UCLASS(Abstract)
class NOCTIS_API UNoctisFieldMapWidget : public UNoctisWidgetBase
{
	GENERATED_BODY()

public:
	/** Image that fills the widget; markers are painted over the whole widget area. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> MapImage;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisMapFeatureView> Features;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TObjectPtr<UTexture2D> MapTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|UI")
	int32 Resolution = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|UI")
	bool bShowEffort = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|UI")
	float MarkerSize = 7.0f;

protected:
	virtual void NativeRefresh() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
							  int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};

UCLASS(Abstract)
class NOCTIS_API UNoctisSpectrogramWidget : public UNoctisWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SpectrogramImage;

	/** Analyses the recording on the simulation thread (if needed), then shows it. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|UI")
	void ShowRecording(int32 RecordingId);

	UFUNCTION(BlueprintImplementableEvent, Category = "Noctis|UI")
	void OnRecordingShown(int32 RecordingId);

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	int32 CurrentRecording = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisRecordingView> Recordings;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisAcousticEventView> Events;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TObjectPtr<UTexture2D> SpectrogramTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|UI")
	float MaxFrequencyHz = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|UI")
	float MinDb = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|UI")
	float MaxDb = 90.0f;

protected:
	virtual void NativeRefresh() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
							  int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UFUNCTION()
	void HandleAnalyzed(int32 RecordingId);

private:
	float CurrentDurationS = 0.0f;
};

UCLASS(Abstract)
class NOCTIS_API UNoctisHypothesisPanelWidget : public UNoctisWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisHypothesisView> Hypotheses;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisSpeciesView> Species;

	UFUNCTION(BlueprintCallable, Category = "Noctis|UI")
	int32 CreateFromDraft(const FNoctisHypothesisDraft& Draft);

	UFUNCTION(BlueprintCallable, Category = "Noctis|UI")
	FNoctisHypothesisView Test(int32 HypothesisId, bool bCurrentExpeditionOnly);

protected:
	virtual void NativeRefresh() override;
};

UCLASS(Abstract)
class NOCTIS_API UNoctisReportViewerWidget : public UNoctisWidgetBase
{
	GENERATED_BODY()

public:
	/** Rich text block; its style set needs the rows "Title", "Heading", "Bold" (see Docs/UE5_INTEGRATION.md). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<URichTextBlock> ReportText;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|UI")
	TArray<FNoctisReportView> Reports;

	UFUNCTION(BlueprintCallable, Category = "Noctis|UI")
	void ShowReport(int32 Index);

	/** Minimal Markdown -> rich text (headings, bold, bullets, tables kept as text). */
	UFUNCTION(BlueprintPure, Category = "Noctis|UI")
	static FString MarkdownToRichText(const FString& Markdown);

protected:
	virtual void NativeRefresh() override;
};

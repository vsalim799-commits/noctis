// Blueprint views of the player's science (copied out of noctis::ResearchSystem for the UI).
// They only contain what the player's own data revealed — never the hidden truth fields of the core records.
#pragma once

#include "CoreMinimal.h"

#include "NoctisTypes.h"

#include "NoctisResearchTypes.generated.h"

UENUM(BlueprintType)
enum class ENoctisTool : uint8
{
	NakedEye,
	Binoculars,
	Camera,
	Microphone,
	MicArray,
	CameraTrap,
	AudioRecorder,
	WeatherStation,
	Notebook
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisPhotoSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis")
	float FocalMm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis")
	float SensorWidthMm = 36.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis")
	int32 WidthPx = 8192;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis")
	float ShutterSeconds = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis")
	float Aperture = 5.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis")
	int32 Iso = 800;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis")
	bool bLaserRangefinder = true;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisObservationView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString TimeText;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Method;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 SubjectCount = 0;
	/** "Edmontosaurus annectens (3), indéterminé (1)" — identified species with counts. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Summary;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float Quality = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Weather;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Note;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 PhotoId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FVector ObserverLocation = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisCatalogEntryView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Code;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Nickname;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Species;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Sightings = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float LastLengthM = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Notes;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisHypothesisView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Statement;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Description;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") ENoctisHypothesisStatus Status = ENoctisHypothesisStatus::Open;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString StatusText;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString TestMethod;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 SampleSize = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float PValue = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float EffectSize = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString EffectName;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float ConfidenceLow = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float ConfidenceHigh = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") TArray<FString> Warnings;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Interpretation;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Tests = 0;
};

/** Form data for a new hypothesis (the measure / condition keys are those of noctis::measureKey). */
USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisHypothesisDraft
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") FString Statement;
	/** 0 EventResponse, 1 Comparison, 2 Correlation, 3 Association (noctis::HypothesisKind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") int32 Kind = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") FName SpeciesId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") FString ResponseMeasure = TEXT("vigilant_fraction");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") FString SecondMeasure = TEXT("group_size");
	/** 0 CallDetected, 1 CallCluster, 2 PredatorAppears, 3 DroneOverhead (noctis::EventTrigger). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") int32 Trigger = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") int32 CallCluster = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") float WindowSeconds = 60.0f;
	/** noctis::ConditionKind: 0 DroneAirborne, 1 VehicleEngineOn, 2 PredatorVisible, 3 Night, 4 ObserverWithin, 5 TimeBetween, 6 MeasureAbove. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") int32 Condition = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") float ConditionValue = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") float ConditionValue2 = 0.0f;
	/** noctis::PredictedDirection: 0 Increase, 1 Decrease, 2 Different, 3 Positive, 4 Negative, 5 Associated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis") int32 Predicted = 2;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisRecordingView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float DurationSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 SampleRate = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 ArrayId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") bool bAnalyzed = false;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 EventCount = 0;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisAcousticEventView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float StartSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float EndSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float F0Hz = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float PeakFrequencyHz = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float BandwidthHz = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float PeakLevelDb = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float SnrDb = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") float Harmonicity = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Cluster = -1;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisMapFeatureView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Kind;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 KindIndex = 0;
	/** Normalised position on the map texture (0..1, row 0 = north). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FVector2D MapUV = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FVector WorldLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Species;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisReportView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") int32 ExpeditionId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Title;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Markdown;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString Confidence;
};

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisSpeciesView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString ScientificName;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") FString CommonNameFr;
	UPROPERTY(BlueprintReadOnly, Category = "Noctis") bool bIndividuallySimulated = true;
};

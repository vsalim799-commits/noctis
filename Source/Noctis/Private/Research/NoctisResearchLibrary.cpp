#include "Research/NoctisResearchLibrary.h"

#include "NoctisLog.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"

#include "Noctis/Research/ResearchSystem.h"
#include "Noctis/Science/Confidence.h"

#include <map>

namespace NoctisResearchImpl
{
FString Str(const std::string& Utf8)
{
	return NoctisUnreal::ToFString(Utf8);
}

FString Str(const char* Utf8)
{
	return Utf8 ? FString(UTF8_TO_TCHAR(Utf8)) : FString();
}

FString SpeciesName(const noctis::WorldSimulation& Sim, int Index)
{
	if (Index < 0 || static_cast<size_t>(Index) >= Sim.db().species().size())
	{
		return FString(TEXT("indéterminé"));
	}
	return Str(Sim.db().speciesAt(Index).scientificName);
}

UTexture2D* MakeTexture(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
	{
		return nullptr;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

// Perceptually ordered dark-to-bright map (magma-like), t in [0, 1].
FColor Magma(float T)
{
	static const FLinearColor Stops[] = {FLinearColor(0.0f, 0.0f, 0.02f), FLinearColor(0.23f, 0.06f, 0.44f), FLinearColor(0.55f, 0.16f, 0.51f),
										 FLinearColor(0.87f, 0.32f, 0.39f), FLinearColor(0.99f, 0.65f, 0.36f), FLinearColor(0.99f, 0.99f, 0.75f)};
	const float X = FMath::Clamp(T, 0.0f, 1.0f) * 5.0f;
	const int32 I = FMath::Min(4, static_cast<int32>(X));
	return FLinearColor::LerpUsingHSV(Stops[I], Stops[I + 1], X - float(I)).ToFColor(true);
}

FNoctisHypothesisView MakeHypothesisView(const noctis::Hypothesis& H, const noctis::EvidenceDatabase* Db)
{
	FNoctisHypothesisView View;
	View.Id = static_cast<int32>(H.id);
	View.Statement = Str(H.statementFr);
	View.Description = Str(noctis::HypothesisSystem::describe(H, Db));
	View.Status = static_cast<ENoctisHypothesisStatus>(H.status);
	View.StatusText = Str(noctis::hypothesisStatusLabelFr(H.status));
	View.TestMethod = Str(H.result.method);
	View.SampleSize = H.result.n;
	View.PValue = static_cast<float>(H.result.pValue);
	View.EffectSize = static_cast<float>(H.result.effectSize);
	View.EffectName = Str(H.result.effectName);
	View.ConfidenceLow = static_cast<float>(H.result.ciLow);
	View.ConfidenceHigh = static_cast<float>(H.result.ciHigh);
	for (const std::string& Warning : H.warningsFr)
	{
		View.Warnings.Add(Str(Warning));
	}
	View.Interpretation = Str(H.interpretationFr);
	View.Tests = H.tests;
	return View;
}
} // namespace NoctisResearchImpl

using namespace NoctisResearchImpl;

TArray<FNoctisSpeciesView> UNoctisResearchLibrary::GetSpeciesInWorld(const UObject* WorldContextObject)
{
	TArray<FNoctisSpeciesView> Result;
	if (const UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		for (const FNoctisSpeciesInfo& Info : Sub->GetSpecies())
		{
			if (Info.bInWorld)
			{
				FNoctisSpeciesView& View = Result.AddDefaulted_GetRef();
				View.Id = Info.Id;
				View.ScientificName = Info.ScientificName;
				View.CommonNameFr = Info.CommonNameFr;
				View.bIndividuallySimulated = Info.bAgent;
			}
		}
	}
	return Result;
}

TArray<FNoctisObservationView> UNoctisResearchLibrary::GetObservations(const UObject* WorldContextObject, int32 MaxCount)
{
	TArray<FNoctisObservationView> Result;
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return Result;
	}
	const FNoctisSpace Space = Sub->GetSpace();
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
		const std::vector<noctis::ObservationRecord>& Records = Sim.research().observations().records();
		for (auto It = Records.rbegin(); It != Records.rend() && Result.Num() < MaxCount; ++It)
		{
			const noctis::ObservationRecord& Rec = *It;
			FNoctisObservationView& View = Result.AddDefaulted_GetRef();
			View.Id = static_cast<int32>(Rec.id);
			View.TimeText = Str(Rec.timeText);
			View.Method = Str(noctis::observationMethodLabelFr(Rec.method));
			View.SubjectCount = static_cast<int32>(Rec.subjects.size());
			std::map<int, int> PerSpecies;
			for (const noctis::ObservedSubject& Subject : Rec.subjects)
			{
				++PerSpecies[Subject.speciesIndex];
			}
			TArray<FString> Parts;
			for (const auto& Pair : PerSpecies)
			{
				Parts.Add(FString::Printf(TEXT("%s (%d)"), *SpeciesName(Sim, Pair.first), Pair.second));
			}
			View.Summary = FString::Join(Parts, TEXT(", "));
			View.Quality = Rec.overallQuality;
			View.Weather = FString::Printf(TEXT("%.0f °C, vent %.0f m/s, visibilité %.0f m, %.0f lx"), Rec.env.temperatureC, Rec.env.windMs, Rec.env.visibilityM, Rec.env.lux);
			View.Note = Str(Rec.note);
			View.PhotoId = static_cast<int32>(Rec.mediaId);
			View.ObserverLocation = Space.ToUE(Rec.observerPos);
		}
	});
	return Result;
}

TArray<FNoctisCatalogEntryView> UNoctisResearchLibrary::GetCatalog(const UObject* WorldContextObject)
{
	TArray<FNoctisCatalogEntryView> Result;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
			for (const noctis::CatalogEntry& Entry : Sim.research().observations().catalog().entries())
			{
				FNoctisCatalogEntryView& View = Result.AddDefaulted_GetRef();
				View.Id = static_cast<int32>(Entry.id);
				View.Code = Str(Entry.code);
				View.Nickname = Str(Entry.nickname);
				View.Species = SpeciesName(Sim, Entry.speciesIndex);
				View.Sightings = static_cast<int32>(Entry.sightings.size());
				View.LastLengthM = Entry.lastLengthM;
				View.Notes = Str(Entry.notes);
			}
		});
	}
	return Result;
}

bool UNoctisResearchLibrary::RenameCatalogEntry(const UObject* WorldContextObject, int32 EntryId, const FString& Nickname)
{
	bool bOk = false;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
			bOk = Sim.research().observations().catalog().rename(static_cast<noctis::u32>(EntryId), NoctisUnreal::ToStdString(Nickname));
		});
	}
	return bOk;
}

TArray<FNoctisHypothesisView> UNoctisResearchLibrary::GetHypotheses(const UObject* WorldContextObject)
{
	TArray<FNoctisHypothesisView> Result;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
			for (const noctis::Hypothesis& H : Sim.research().hypotheses().all())
			{
				Result.Add(MakeHypothesisView(H, &Sim.db()));
			}
		});
	}
	return Result;
}

int32 UNoctisResearchLibrary::CreateHypothesis(const UObject* WorldContextObject, const FNoctisHypothesisDraft& Draft)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return 0;
	}
	const int Response = noctis::parseMeasure(NoctisUnreal::ToStdString(Draft.ResponseMeasure));
	if (Response < 0)
	{
		UE_LOG(LogNoctis, Warning, TEXT("Hypothèse : mesure inconnue '%s'."), *Draft.ResponseMeasure);
		return 0;
	}
	noctis::Hypothesis H;
	H.statementFr = NoctisUnreal::ToStdString(Draft.Statement);
	H.kind = static_cast<noctis::HypothesisKind>(FMath::Clamp(Draft.Kind, 0, 3));
	H.speciesIndex = static_cast<noctis::i16>(Draft.SpeciesId.IsNone() ? -1 : Sub->FindSpeciesIndex(Draft.SpeciesId));
	H.response = static_cast<noctis::Measure>(Response);
	const int Second = noctis::parseMeasure(NoctisUnreal::ToStdString(Draft.SecondMeasure));
	if (Second >= 0)
	{
		H.response2 = static_cast<noctis::Measure>(Second);
		H.condition.measure = static_cast<noctis::Measure>(Second);
	}
	H.trigger = static_cast<noctis::EventTrigger>(FMath::Clamp(Draft.Trigger, 0, 3));
	H.callCluster = Draft.CallCluster;
	H.windowS = FMath::Max(1.0f, Draft.WindowSeconds);
	H.condition.kind = static_cast<noctis::ConditionKind>(FMath::Clamp(Draft.Condition, 0, 6));
	H.condition.value = Draft.ConditionValue;
	H.condition.value2 = Draft.ConditionValue2;
	H.predicted = static_cast<noctis::PredictedDirection>(FMath::Clamp(Draft.Predicted, 0, 5));
	int32 Id = 0;
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) { Id = static_cast<int32>(Sim.research().createHypothesis(H, Sim.context())); });
	return Id;
}

FNoctisHypothesisView UNoctisResearchLibrary::TestHypothesis(const UObject* WorldContextObject, int32 HypothesisId, bool bCurrentExpeditionOnly)
{
	FNoctisHypothesisView View;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
			Sim.research().testHypothesis(static_cast<noctis::u32>(HypothesisId), Sim.context(), bCurrentExpeditionOnly);
			if (const noctis::Hypothesis* H = Sim.research().hypotheses().find(static_cast<noctis::u32>(HypothesisId)))
			{
				View = MakeHypothesisView(*H, &Sim.db());
			}
		});
	}
	return View;
}

TArray<FNoctisRecordingView> UNoctisResearchLibrary::GetRecordings(const UObject* WorldContextObject)
{
	TArray<FNoctisRecordingView> Result;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
			const noctis::ResearchSystem& Research = Sim.research();
			for (const noctis::Recording& Rec : Research.recordings())
			{
				FNoctisRecordingView& View = Result.AddDefaulted_GetRef();
				View.Id = static_cast<int32>(Rec.id);
				View.Label = Str(Rec.label);
				View.DurationSeconds = Rec.durationS;
				View.SampleRate = Rec.sampleRate;
				View.ArrayId = static_cast<int32>(Rec.arrayId);
				for (const noctis::RecordingAnalysis& Analysis : Research.analyses())
				{
					if (Analysis.recordingId == Rec.id && Analysis.analyzed)
					{
						View.bAnalyzed = true;
						View.EventCount = static_cast<int32>(Analysis.events.size());
					}
				}
			}
		});
	}
	return Result;
}

void UNoctisResearchLibrary::AnalyzeRecordingAsync(const UObject* WorldContextObject, int32 RecordingId, FNoctisRecordingAnalyzed OnDone)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return;
	}
	const noctis::u32 Id = static_cast<noctis::u32>(RecordingId);
	Sub->EnqueueCommand([Id](noctis::WorldSimulation& Sim) { Sim.research().analyzeRecording(Id, Sim.context()); },
						[OnDone, RecordingId]() { OnDone.ExecuteIfBound(RecordingId); });
}

TArray<FNoctisAcousticEventView> UNoctisResearchLibrary::GetRecordingEvents(const UObject* WorldContextObject, int32 RecordingId)
{
	TArray<FNoctisAcousticEventView> Result;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
			for (const noctis::RecordingAnalysis& Analysis : Sim.research().analyses())
			{
				if (Analysis.recordingId != static_cast<noctis::u32>(RecordingId))
				{
					continue;
				}
				for (const noctis::AcousticEvent& Event : Analysis.events)
				{
					FNoctisAcousticEventView& View = Result.AddDefaulted_GetRef();
					View.StartSeconds = Event.startS;
					View.EndSeconds = Event.endS;
					View.F0Hz = Event.f0Hz;
					View.PeakFrequencyHz = Event.peakFreqHz;
					View.BandwidthHz = Event.bandwidthHz;
					View.PeakLevelDb = Event.peakLevelDb;
					View.SnrDb = Event.snrDb;
					View.Harmonicity = Event.harmonicity;
					View.Cluster = Event.cluster;
				}
			}
		});
	}
	return Result;
}

UTexture2D* UNoctisResearchLibrary::BuildSpectrogramTexture(const UObject* WorldContextObject, int32 RecordingId, float MinDb, float MaxDb, float MaxFrequencyHz)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return nullptr;
	}
	TArray<FColor> Pixels;
	int32 Width = 0;
	int32 Height = 0;
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
		for (const noctis::RecordingAnalysis& Analysis : Sim.research().analyses())
		{
			if (Analysis.recordingId != static_cast<noctis::u32>(RecordingId) || !Analysis.analyzed)
			{
				continue;
			}
			const noctis::Spectrogram& S = Analysis.spectrogram;
			if (S.frames <= 0 || S.bins <= 0 || S.binHz <= 0.0f)
			{
				return;
			}
			const int32 Bins = FMath::Clamp(static_cast<int32>(MaxFrequencyHz / S.binHz), 1, S.bins);
			const int32 FrameStride = FMath::Max(1, S.frames / 2048);
			Width = S.frames / FrameStride;
			Height = FMath::Min(Bins, 512);
			const float BinStride = float(Bins) / float(Height);
			Pixels.SetNumUninitialized(Width * Height);
			for (int32 Row = 0; Row < Height; ++Row)
			{
				const int32 Bin = FMath::Clamp(static_cast<int32>(float(Height - 1 - Row) * BinStride), 0, S.bins - 1); // top row = highest frequency
				for (int32 Column = 0; Column < Width; ++Column)
				{
					const float Db = S.at(Column * FrameStride, Bin);
					Pixels[Row * Width + Column] = Magma((Db - MinDb) / FMath::Max(1.0f, MaxDb - MinDb));
				}
			}
			return;
		}
	});
	return MakeTexture(Width, Height, Pixels);
}

bool UNoctisResearchLibrary::ExportRecordingWav(const UObject* WorldContextObject, int32 RecordingId, const FString& FilePath)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return false;
	}
	std::vector<noctis::u8> Bytes;
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
		if (const noctis::Recording* Rec = Sim.research().findRecording(static_cast<noctis::u32>(RecordingId)))
		{
			Bytes = noctis::AcousticAnalysis::toWav(*Rec);
		}
	});
	if (Bytes.empty())
	{
		return false;
	}
	return FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes.data(), static_cast<int32>(Bytes.size())), *FilePath);
}

UTexture2D* UNoctisResearchLibrary::BuildFieldMapTexture(const UObject* WorldContextObject, int32 Resolution, bool bShowEffort)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return nullptr;
	}
	const int32 R = FMath::Clamp(Resolution, 64, 2048);
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(R * R);
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
		const noctis::Terrain& Terrain = Sim.terrain();
		const noctis::FieldMap& Map = Sim.research().map();
		noctis::WaterSystem& Water = Sim.water();
		const noctis::Rect2 Bounds = Terrain.bounds();
		const noctis::Vec2 Size = Bounds.size();
		const float Lo = Terrain.minHeight();
		const float Hi = FMath::Max(Lo + 1.0f, Terrain.maxHeight());
		const noctis::Vec3 LightDir = noctis::Vec3(-0.5f, 0.5f, 0.7f).normalized(); // north-west light, cartographic convention
		const std::vector<float>& Effort = Map.effort();
		for (int32 Row = 0; Row < R; ++Row)
		{
			for (int32 Column = 0; Column < R; ++Column)
			{
				const noctis::Vec2 P(Bounds.min.x + (float(Column) + 0.5f) / float(R) * Size.x, Bounds.max.y - (float(Row) + 0.5f) / float(R) * Size.y);
				const float H = (Terrain.heightAt(P) - Lo) / (Hi - Lo);
				const float Shade = FMath::Clamp(noctis::dot(Terrain.normalAt(P), LightDir) * 1.1f, 0.25f, 1.0f);
				FLinearColor Color = FLinearColor::LerpUsingHSV(FLinearColor(0.32f, 0.42f, 0.22f), FLinearColor(0.62f, 0.55f, 0.40f), H) * Shade;
				if (Water.depthAt(P, Terrain) > 0.05f)
				{
					Color = FLinearColor(0.16f, 0.32f, 0.46f) * (0.7f + 0.3f * Shade);
				}
				if (bShowEffort && Map.width() > 0 && Map.cellSize() > 0.0f)
				{
					const int32 CX = FMath::Clamp(static_cast<int32>((P.x - Map.bounds().min.x) / Map.cellSize()), 0, Map.width() - 1);
					const int32 CY = FMath::Clamp(static_cast<int32>((P.y - Map.bounds().min.y) / Map.cellSize()), 0, Map.height() - 1);
					const size_t Cell = static_cast<size_t>(CY) * static_cast<size_t>(Map.width()) + static_cast<size_t>(CX);
					if (Cell < Effort.size() && Effort[Cell] > 0.0f)
					{
						const float Heat = FMath::Clamp(FMath::LogX(10.0f, 1.0f + Effort[Cell]) / 4.0f, 0.0f, 1.0f); // up to ~3 h of effort
						Color = FMath::Lerp(Color, FLinearColor(0.95f, 0.55f, 0.15f), 0.45f * Heat);
					}
				}
				if (!Map.explored(P))
				{
					const float Grey = Color.GetLuminance();
					Color = FLinearColor(Grey, Grey, Grey) * 0.35f; // unexplored: relief only, dimmed
				}
				Pixels[Row * R + Column] = Color.ToFColor(true);
			}
		}
	});
	return MakeTexture(R, R, Pixels);
}

TArray<FNoctisMapFeatureView> UNoctisResearchLibrary::GetMapFeatures(const UObject* WorldContextObject)
{
	TArray<FNoctisMapFeatureView> Result;
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return Result;
	}
	const FNoctisSpace Space = Sub->GetSpace();
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
		const noctis::Rect2 Bounds = Sim.terrain().bounds();
		const noctis::Vec2 Size = Bounds.size();
		for (const noctis::MapFeature& Feature : Sim.research().map().features())
		{
			FNoctisMapFeatureView& View = Result.AddDefaulted_GetRef();
			View.Id = static_cast<int32>(Feature.id);
			View.Kind = Str(noctis::mapFeatureLabelFr(Feature.kind));
			View.KindIndex = static_cast<int32>(Feature.kind);
			View.MapUV = FVector2D((Feature.position.x - Bounds.min.x) / Size.x, 1.0f - (Feature.position.y - Bounds.min.y) / Size.y);
			View.WorldLocation = Space.ToUE(Feature.position, double(Sim.terrain().heightAt(Feature.position)));
			View.Label = Str(Feature.label);
			View.Species = Feature.speciesIndex >= 0 ? SpeciesName(Sim, Feature.speciesIndex) : FString();
		}
	});
	return Result;
}

FVector2D UNoctisResearchLibrary::WorldToMapUV(const UObject* WorldContextObject, FVector WorldLocation)
{
	const UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub)
	{
		return FVector2D::ZeroVector;
	}
	const noctis::Rect2& Bounds = Sub->GetCoreBounds();
	const noctis::Vec2 Size = Bounds.size();
	if (Size.x <= 0.0f || Size.y <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}
	const noctis::Vec2 P = Sub->GetSpace().FromUE2D(WorldLocation);
	return FVector2D((P.x - Bounds.min.x) / Size.x, 1.0f - (P.y - Bounds.min.y) / Size.y);
}

TArray<FNoctisReportView> UNoctisResearchLibrary::GetReports(const UObject* WorldContextObject)
{
	TArray<FNoctisReportView> Result;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
			for (const noctis::ScientificReport& Report : Sim.research().reports())
			{
				FNoctisReportView& View = Result.AddDefaulted_GetRef();
				View.ExpeditionId = static_cast<int32>(Report.expeditionId);
				View.Title = Str(Report.titleFr);
				View.Markdown = Str(Report.markdown);
				View.Confidence = Str(noctis::confidenceLabelFr(Report.overallConfidence));
			}
		});
	}
	return Result;
}

void UNoctisResearchLibrary::AddFieldNote(const UObject* WorldContextObject, const FString& Text)
{
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject))
	{
		const std::string Note = NoctisUnreal::ToStdString(Text);
		Sub->EnqueueCommand([Note](noctis::WorldSimulation& Sim) { Sim.research().addNote(Note, Sim.context()); });
	}
}

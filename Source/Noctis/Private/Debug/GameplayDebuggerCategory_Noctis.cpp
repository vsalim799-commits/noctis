#include "Debug/GameplayDebuggerCategory_Noctis.h"

#if NOCTIS_WITH_GAMEPLAY_DEBUGGER

#include "Creatures/NoctisCreatureActor.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "GameFramework/PlayerController.h"

#include "Noctis/Creatures/CreatureSystem.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace NoctisDebuggerImpl
{
FString Str(const char* Utf8)
{
	return Utf8 ? FString(UTF8_TO_TCHAR(Utf8)) : FString();
}

/** Creature id under the debug actor, else the individual closest to the view axis within 300 m. */
uint32 PickCreature(const UNoctisWorldSubsystem& Sub, APlayerController* OwnerPC, AActor* DebugActor)
{
	if (const ANoctisCreatureActor* Creature = Cast<ANoctisCreatureActor>(DebugActor))
	{
		return Creature->GetCreatureIdRaw();
	}
	const FNoctisFrameOutput* Front = Sub.GetFrontFrame();
	if (!Front || !OwnerPC)
	{
		return 0;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	OwnerPC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Axis = ViewRotation.Vector();
	uint32 Best = 0;
	double BestScore = 1e30;
	for (const noctis::CreatureRenderState& C : Front->Snapshot.creatures)
	{
		const FVector ToCreature = Sub.GetSpace().ToUE(C.position) - ViewLocation;
		const double Along = FVector::DotProduct(ToCreature, Axis);
		if (Along <= 0.0 || Along > 30000.0)
		{
			continue;
		}
		const double Off = (ToCreature - Axis * Along).Size();
		const double Score = Off / Along; // angular distance to the crosshair
		if (Score < BestScore && Score < 0.15)
		{
			BestScore = Score;
			Best = C.id;
		}
	}
	return Best;
}
} // namespace NoctisDebuggerImpl

FGameplayDebuggerCategory_Noctis::FGameplayDebuggerCategory_Noctis()
{
	bShowOnlyWithDebugActor = false;
	CollectDataInterval = 0.25f;
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_Noctis::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_Noctis());
}

void FGameplayDebuggerCategory_Noctis::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	using namespace NoctisDebuggerImpl;
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(OwnerPC);
	if (!Sub || !Sub->IsSimulationReady())
	{
		AddTextLine(TEXT("{red}Simulation Noctis non prête"));
		return;
	}
	const uint32 Id = PickCreature(*Sub, OwnerPC, DebugActor);
	if (Id == 0)
	{
		AddTextLine(TEXT("{grey}Aucun animal sous le réticule (viser un animal à moins de 300 m)."));
		return;
	}
	const FNoctisSpace Space = Sub->GetSpace();
	TArray<FString> Lines;
	TArray<FGameplayDebuggerShape> Shapes;
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
		const noctis::Creature* C = Sim.creatures().find(noctis::EntityId(Id));
		if (!C)
		{
			Lines.Add(TEXT("{red}Individu introuvable (mort ou émigré)."));
			return;
		}
		const noctis::SpeciesDefinition& Def = Sim.db().speciesAt(C->speciesIndex);
		const double Now = Sim.clock().seconds();
		Lines.Add(FString::Printf(TEXT("{yellow}#%u %s{white}  %s, %.1f ans, %.0f kg, %.1f m  LOD %s"), C->id.serial(), *NoctisUnreal::ToFString(Def.scientificName),
								  C->sex == noctis::Sex::Female ? TEXT("femelle") : TEXT("mâle"), C->ageYears(Now), C->massKg, C->lengthM, *Str(noctis::lodLabel(C->lod))));
		Lines.Add(FString::Printf(TEXT("{green}Comportement{white} %s (précédent %s)  posture %s  allure %s %.1f m/s  chasse %s"),
								  *Str(noctis::behaviorLabelFr(C->behavior.current)), *Str(noctis::behaviorLabelFr(C->behavior.previous)),
								  *Str(noctis::postureLabelFr(C->behavior.posture)), *Str(noctis::gaitLabelFr(C->loco.gait)), C->loco.speed,
								  *Str(noctis::huntPhaseLabelFr(C->behavior.hunt))));
		// Top behaviour scores (the utility the decision layer compared).
		std::vector<std::pair<float, int>> Scores;
		for (int Index = 0; Index < noctis::kBehaviorCount; ++Index)
		{
			Scores.emplace_back(C->behavior.scores[static_cast<size_t>(Index)], Index);
		}
		std::sort(Scores.begin(), Scores.end(), [](const auto& A, const auto& B) { return A.first > B.first; });
		FString Top;
		for (size_t Index = 0; Index < 5 && Index < Scores.size(); ++Index)
		{
			Top += FString::Printf(TEXT("%s %.2f  "), *Str(noctis::behaviorKey(static_cast<noctis::BehaviorId>(Scores[Index].second))), Scores[Index].first);
		}
		Lines.Add(TEXT("{green}Scores{white} ") + Top);
		const noctis::PhysiologyState& P = C->phys;
		Lines.Add(FString::Printf(TEXT("{green}Physiologie{white} réserves %.0f %%  faim %.2f  soif %.2f  hydratation %.2f  fatigue %.2f  sommeil %.2f  stress %.2f (chronique %.2f)  T %.1f °C"),
								  100.0f * P.energyReserveMJ / FMath::Max(0.001f, P.energyCapacityMJ), P.hunger, P.thirst, P.hydration, P.fatigue, P.sleepPressure, P.stress,
								  P.chronicStress, P.bodyTempC));
		Lines.Add(FString::Printf(TEXT("{green}Blessures{white} %d actives, %d cicatrices, boiterie %.2f, santé %.2f, douleur %.2f"),
								  static_cast<int32>(C->injuries.active.size()), static_cast<int32>(C->injuries.scars.size()), C->injuries.limp, C->injuries.healthIndex, P.pain));
		if (const noctis::Group* G = Sim.creatures().findGroup(C->social.groupId))
		{
			Lines.Add(FString::Printf(TEXT("{green}Groupe{white} %u : %d membres, formation %s, alerte %.2f, espacement moyen %.1f m"), G->id,
									  static_cast<int32>(G->members.size()), *Str(noctis::formationLabelFr(G->formation)), G->alert, G->meanNeighbourDistance));
		}
		Lines.Add(FString::Printf(TEXT("{green}Perception{white} vigilance %.2f, lumière %.2f, bruit ambiant %.0f dB, %d entités connues"), C->perception.alertness,
								  C->perception.lightLevel, C->perception.ambientNoiseDb, static_cast<int32>(C->perception.known.size())));
		const FVector Body = Space.ToUE(C->loco.position) + FVector(0.0, 0.0, C->hipHeightM * 130.0);
		int32 Shown = 0;
		for (const noctis::Awareness& A : C->perception.known)
		{
			if (Shown++ >= 8)
			{
				break;
			}
			Lines.Add(FString::Printf(TEXT("   %s  %s  stade %s  preuve %.2f  id %.2f  menace %.2f  opportunité %.2f  ±%.0f m"),
									  *Str(noctis::relationLabelFr(A.relation)), A.speciesIndex >= 0 ? *NoctisUnreal::ToFString(Sim.db().speciesAt(A.speciesIndex).id) : TEXT("-"),
									  *Str(noctis::awarenessStageLabelFr(A.stage)), A.evidence, A.identification, A.threat, A.opportunity, A.uncertaintyM));
			const FVector Believed = Space.ToUE(A.estimatedPos, double(C->loco.position.z) + 1.0);
			const FColor Color = A.threat > 0.5f ? FColor::Red : (A.opportunity > 0.5f ? FColor::Green : FColor::Yellow);
			Shapes.Add(FGameplayDebuggerShape::MakeSegment(Body, Believed, 2.0f, Color));
			Shapes.Add(FGameplayDebuggerShape::MakePoint(Believed, FMath::Max(20.0f, A.uncertaintyM * 100.0f * 0.1f), Color));
		}
		if (C->perception.hasLookTarget)
		{
			Shapes.Add(FGameplayDebuggerShape::MakeSegment(Body, Space.ToUE(C->perception.lookTarget, double(C->loco.position.z) + C->hipHeightM), 4.0f, FColor::Cyan));
		}
		if (C->behavior.hasTargetPos)
		{
			Shapes.Add(FGameplayDebuggerShape::MakeSegment(Space.ToUE(C->loco.position), Space.ToUE(C->behavior.targetPos, double(C->loco.position.z)), 3.0f, FColor::Magenta));
		}
	});
	for (const FString& Line : Lines)
	{
		AddTextLine(Line);
	}
	for (const FGameplayDebuggerShape& Shape : Shapes)
	{
		AddShape(Shape);
	}
}

#endif // NOCTIS_WITH_GAMEPLAY_DEBUGGER

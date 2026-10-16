// Headless validation for continuous integration:
//   UnrealEditor-Cmd <Project>/Noctis.uproject -run=NoctisValidateData [-WarningsAsErrors]
// Exit code 0 = Data/ loads and has no error, 1 otherwise. Warnings (e.g. a species of the world without visual data
// yet) only fail the run with -WarningsAsErrors.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "NoctisValidateDataCommandlet.generated.h"

UCLASS()
class UNoctisValidateDataCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UNoctisValidateDataCommandlet();

	virtual int32 Main(const FString& Params) override;
};

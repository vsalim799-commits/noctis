// Plugins and editor APIs used by the Noctis UE layer (stand-in, see ue_core.h).
#pragma once

#include "ue_engine.h"

// ------------------------------------------------------------------ Enhanced Input
class UInputAction : public UObject
{
};
class UInputMappingContext : public UObject
{
};
enum class ETriggerEvent : uint8 { None = 0, Triggered = 1, Started = 2, Ongoing = 4, Canceled = 8, Completed = 16 };
struct FInputActionValue
{
	template <typename T>
	T Get() const { return T(); }
};
template <>
inline bool FInputActionValue::Get<bool>() const { return false; }
using FInputActionInstance = FInputActionValue;
struct FEnhancedInputActionEventBinding
{
};
class UInputComponent : public UActorComponent
{
};
class UEnhancedInputComponent : public UInputComponent
{
public:
	template <typename U>
	FEnhancedInputActionEventBinding& BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, U* Object, void (U::*Func)(const FInputActionValue&)) { (void)Action; (void)TriggerEvent; (void)Object; (void)Func; static FEnhancedInputActionEventBinding B; return B; }
	template <typename U>
	FEnhancedInputActionEventBinding& BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, U* Object, void (U::*Func)()) { (void)Action; (void)TriggerEvent; (void)Object; (void)Func; static FEnhancedInputActionEventBinding B; return B; }
};
struct FModifyContextOptions
{
};
class UEnhancedInputLocalPlayerSubsystem : public ULocalPlayerSubsystem
{
public:
	void AddMappingContext(const UInputMappingContext* Context, int32 Priority, const FModifyContextOptions& Options = FModifyContextOptions()) { (void)Context; (void)Priority; (void)Options; }
	void RemoveMappingContext(const UInputMappingContext* Context, const FModifyContextOptions& Options = FModifyContextOptions()) { (void)Context; (void)Options; }
	void ClearAllMappings() {}
};

// ------------------------------------------------------------------ Niagara
class UFXSystemAsset : public UObject
{
};
class UNiagaraSystem : public UFXSystemAsset
{
};
class UFXSystemComponent : public UPrimitiveComponent
{
};
class UNiagaraComponent : public UFXSystemComponent
{
public:
	void SetVariableFloat(FName Name, float Value) { (void)Name; (void)Value; }
	void SetVariableVec3(FName Name, FVector Value) { (void)Name; (void)Value; }
	void SetVariableLinearColor(FName Name, const FLinearColor& Value) { (void)Name; (void)Value; }
	void SetVariableInt(FName Name, int32 Value) { (void)Name; (void)Value; }
	void SetAsset(UNiagaraSystem* Asset, bool bResetExistingOverrideParameters = true) { (void)Asset; (void)bResetExistingOverrideParameters; }
};
enum class ENCPoolMethod : uint8 { None, AutoRelease, ManualRelease };
class UNiagaraFunctionLibrary : public UBlueprintFunctionLibrary
{
public:
	static UNiagaraComponent* SpawnSystemAtLocation(const UObject* WorldContextObject, UNiagaraSystem* SystemTemplate, FVector Location, FRotator Rotation = FRotator::ZeroRotator, FVector Scale = FVector(1.f), bool bAutoDestroy = true, bool bAutoActivate = true, ENCPoolMethod PoolingMethod = ENCPoolMethod::None, bool bPreCullCheck = true) { (void)WorldContextObject; (void)SystemTemplate; (void)Location; (void)Rotation; (void)Scale; (void)bAutoDestroy; (void)bAutoActivate; (void)PoolingMethod; (void)bPreCullCheck; return nullptr; }
	static UNiagaraComponent* SpawnSystemAttached(UNiagaraSystem* SystemTemplate, USceneComponent* AttachToComponent, FName AttachPointName, FVector Location, FRotator Rotation, EAttachLocation::Type LocationType, bool bAutoDestroy, bool bAutoActivate = true, ENCPoolMethod PoolingMethod = ENCPoolMethod::None, bool bPreCullCheck = true) { (void)SystemTemplate; (void)AttachToComponent; (void)AttachPointName; (void)Location; (void)Rotation; (void)LocationType; (void)bAutoDestroy; (void)bAutoActivate; (void)PoolingMethod; (void)bPreCullCheck; return nullptr; }
};

// ------------------------------------------------------------------ PCG
class UPCGComponent : public UActorComponent
{
public:
	void GenerateLocal(bool bForce) { (void)bForce; }
	void Generate(bool bForce) { (void)bForce; }
	void Cleanup(bool bRemoveComponents) { (void)bRemoveComponents; }
};

// ------------------------------------------------------------------ Procedural mesh
struct FProcMeshTangent
{
	FVector TangentX;
	bool bFlipTangentY = false;
	FProcMeshTangent() = default;
	FProcMeshTangent(float X, float Y, float Z) : TangentX(X, Y, Z) {}
	FProcMeshTangent(FVector InTangentX, bool bInFlipTangentY) : TangentX(InTangentX), bFlipTangentY(bInFlipTangentY) {}
};
class UProceduralMeshComponent : public UMeshComponent
{
public:
	bool bUseAsyncCooking = false;
	void CreateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0,
									   const TArray<FLinearColor>& VertexColors, const TArray<FProcMeshTangent>& Tangents, bool bCreateCollision, bool bSRGBConversion = false)
	{
		(void)SectionIndex; (void)Vertices; (void)Triangles; (void)Normals; (void)UV0; (void)VertexColors; (void)Tangents; (void)bCreateCollision; (void)bSRGBConversion;
	}
	void UpdateMeshSection_LinearColor(int32 SectionIndex, const TArray<FVector>& Vertices, const TArray<FVector>& Normals, const TArray<FVector2D>& UV0,
									   const TArray<FLinearColor>& VertexColors, const TArray<FProcMeshTangent>& Tangents, bool bSRGBConversion = false)
	{
		(void)SectionIndex; (void)Vertices; (void)Normals; (void)UV0; (void)VertexColors; (void)Tangents; (void)bSRGBConversion;
	}
	void ClearMeshSection(int32 SectionIndex) { (void)SectionIndex; }
	void ClearAllMeshSections() {}
	void SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility) { (void)SectionIndex; (void)bNewVisibility; }
	int32 GetNumSections() const { return 0; }
};

// ------------------------------------------------------------------ Chaos vehicles
class UPawnMovementComponentBase : public UActorComponent
{
};
class UChaosVehicleMovementComponent : public UPawnMovementComponentBase
{
public:
	float Mass = 1500.0f;
	void SetThrottleInput(float Throttle) { (void)Throttle; }
	void SetBrakeInput(float Brake) { (void)Brake; }
	void SetSteeringInput(float Steering) { (void)Steering; }
	void SetHandbrakeInput(bool bNewHandbrake) { (void)bNewHandbrake; }
	float GetForwardSpeed() const { return 0.0f; }
	void SetParked(bool bParked) { (void)bParked; }
};
class UChaosWheeledVehicleMovementComponent : public UChaosVehicleMovementComponent
{
public:
	void SetWheelFrictionMultiplier(int WheelIndex, float Friction) { (void)WheelIndex; (void)Friction; }
	void SetMaxEngineTorque(float Torque) { (void)Torque; }
	int32 GetNumWheels() const { return 6; }
};
class AWheeledVehiclePawn : public APawn
{
public:
	AWheeledVehiclePawn(const FObjectInitializer& Init) : APawn(Init) {}
	USkeletalMeshComponent* GetMesh() const { return nullptr; }
	UChaosVehicleMovementComponent* GetVehicleMovementComponent() const { return nullptr; }
};

// ------------------------------------------------------------------ Slate / UMG
struct FSlateBrush
{
};
class ISlateStyle
{
public:
	virtual ~ISlateStyle() = default;
	const FSlateBrush* GetBrush(FName PropertyName, const ANSICHAR* Specifier = nullptr) const { (void)PropertyName; (void)Specifier; return nullptr; }
	FName GetStyleSetName() const { return NAME_None; }
};
class FCoreStyle
{
public:
	static const ISlateStyle& Get();
};
class FAppStyle
{
public:
	static const ISlateStyle& Get();
	static const FName& GetAppStyleSetName();
};
class FSlateLayoutTransform
{
public:
	FSlateLayoutTransform(float InScale = 1.0f) { (void)InScale; }
	explicit FSlateLayoutTransform(const FVector2f& InTranslation) { (void)InTranslation; }
	explicit FSlateLayoutTransform(const FVector2D& InTranslation) { (void)InTranslation; }
};
struct FPaintGeometry
{
};
class FGeometry
{
public:
	FVector2f GetLocalSize() const { return {}; }
	FVector2f GetAbsoluteSize() const { return {}; }
	FPaintGeometry ToPaintGeometry() const { return {}; }
	FPaintGeometry ToPaintGeometry(const FVector2f& LocalSize, const FSlateLayoutTransform& LayoutTransform) const { (void)LocalSize; (void)LayoutTransform; return {}; }
	FPaintGeometry ToPaintGeometry(const FVector2D& LocalSize, const FSlateLayoutTransform& LayoutTransform) const { (void)LocalSize; (void)LayoutTransform; return {}; }
};
class FSlateRect
{
};
class FSlateWindowElementList
{
};
class FWidgetStyle
{
};
class FPaintArgs
{
};
enum class ESlateDrawEffect : uint8 { None = 0, NoBlending = 1, DisabledEffect = 4 };
class FSlateDrawElement
{
public:
	static void MakeBox(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FSlateBrush* InBrush, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White) { (void)ElementList; (void)InLayer; (void)PaintGeometry; (void)InBrush; (void)InDrawEffects; (void)InTint; }
	static void MakeLines(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const TArray<FVector2D>& Points, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White, bool bAntialias = true, float Thickness = 1.0f) { (void)ElementList; (void)InLayer; (void)PaintGeometry; (void)Points; (void)InDrawEffects; (void)InTint; (void)bAntialias; (void)Thickness; }
	static void MakeText(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FString& InText, const struct FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White);
};
class UVisual : public UObject
{
};
class UWidget : public UVisual
{
public:
	void SetVisibility(uint8 V) { (void)V; }
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const { (void)Args; (void)AllottedGeometry; (void)MyCullingRect; (void)OutDrawElements; (void)InWidgetStyle; (void)bParentEnabled; return LayerId; }
};
class UUserWidget : public UWidget
{
public:
	virtual void NativeConstruct() {}
	virtual void NativeDestruct() {}
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) { (void)MyGeometry; (void)InDeltaTime; }
	void AddToViewport(int32 ZOrder = 0) { (void)ZOrder; }
	void RemoveFromParent() {}
	APlayerController* GetOwningPlayer() const { return nullptr; }
	virtual UWorld* GetWorld() const override { return nullptr; }
};
template <typename WidgetT = UUserWidget, typename OwnerType = UObject*>
WidgetT* CreateWidget(OwnerType OwningObject, TSubclassOf<UUserWidget> UserWidgetClass = WidgetT::StaticClass(), FName WidgetName = NAME_None)
{
	(void)*OwningObject;
	(void)UserWidgetClass;
	(void)WidgetName;
	return nullptr;
}
class UImage : public UWidget
{
public:
	void SetBrushFromTexture(UTexture2D* Texture, bool bMatchSize = false) { (void)Texture; (void)bMatchSize; }
	void SetColorAndOpacity(FLinearColor C) { (void)C; }
};
class UTextLayoutWidget : public UWidget
{
};
class URichTextBlock : public UTextLayoutWidget
{
public:
	void SetText(const FText& InText) { (void)InText; }
};
class UTextBlock : public UTextLayoutWidget
{
public:
	void SetText(FText InText) { (void)InText; }
};

// ------------------------------------------------------------------ Gameplay Debugger
struct FGameplayDebuggerShape
{
	static FGameplayDebuggerShape MakePoint(const FVector& Center, const float Radius, const FColor& Color, const FString& Description = FString()) { (void)Center; (void)Radius; (void)Color; (void)Description; return {}; }
	static FGameplayDebuggerShape MakeSegment(const FVector& StartPos, const FVector& EndPos, const float Thickness, const FColor& Color, const FString& Description = FString()) { (void)StartPos; (void)EndPos; (void)Thickness; (void)Color; (void)Description; return {}; }
	static FGameplayDebuggerShape MakeBox(const FVector& Center, const FVector& Extent, const FColor& Color, const FString& Description = FString()) { (void)Center; (void)Extent; (void)Color; (void)Description; return {}; }
};
class FGameplayDebuggerCanvasContext;
class FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory() = default;
	virtual ~FGameplayDebuggerCategory() = default;
	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) { (void)OwnerPC; (void)DebugActor; }
	virtual void DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext) { (void)OwnerPC; (void)CanvasContext; }

protected:
	void AddTextLine(const FString& TextLine) { (void)TextLine; }
	void AddShape(const FGameplayDebuggerShape& Shape) { (void)Shape; }
	bool bShowOnlyWithDebugActor = true;
	float CollectDataInterval = 0.0f;
};
enum class EGameplayDebuggerCategoryState : uint8 { EnabledInGameAndSimulate, EnabledInGame, EnabledInSimulate, Disabled, Hidden };
class IGameplayDebugger : public IModuleInterface
{
public:
	DECLARE_DELEGATE_RetVal(TSharedRef<FGameplayDebuggerCategory>, FOnGetCategory);
	static IGameplayDebugger& Get();
	static bool IsAvailable();
	virtual void RegisterCategory(FName CategoryName, FOnGetCategory MakeInstanceDelegate, EGameplayDebuggerCategoryState CategoryState = EGameplayDebuggerCategoryState::Disabled, int32 SlotIdx = INDEX_NONE) = 0;
	virtual void UnregisterCategory(FName CategoryName) = 0;
	virtual void NotifyCategoriesChanged() = 0;
};
// MakeShareable(new T) -> TSharedRef<Base> conversion used by gameplay debugger factories.
template <typename T, ESPMode Mode>
struct TSharedRefFromRaw;

// ------------------------------------------------------------------ editor: notifications, menus, message log, assets
class SNotificationItem
{
public:
	enum ECompletionState { CS_None, CS_Pending, CS_Success, CS_Fail };
	void SetCompletionState(ECompletionState State) { (void)State; }
	void ExpireAndFadeout() {}
};
struct FNotificationInfo
{
	explicit FNotificationInfo(const FText& InText) { (void)InText; }
	float ExpireDuration = 5.0f;
	bool bUseLargeFont = true;
	bool bFireAndForget = true;
};
class FSlateNotificationManager
{
public:
	static FSlateNotificationManager& Get();
	TSharedPtr<SNotificationItem> AddNotification(const FNotificationInfo& Info) { (void)Info; return {}; }
};
struct FSlateIcon
{
	FSlateIcon() = default;
	FSlateIcon(const FName StyleSetName, const FName StyleName) { (void)StyleSetName; (void)StyleName; }
};
using FExecuteAction = TStubDelegate<void()>;
using FCanExecuteAction = TStubDelegate<bool()>;
struct FUIAction
{
	FUIAction() = default;
	explicit FUIAction(FExecuteAction Execute, FCanExecuteAction CanExecute = FCanExecuteAction()) { (void)Execute; (void)CanExecute; }
};
struct FToolMenuEntry
{
};
struct FToolMenuSection
{
	FToolMenuEntry& AddMenuEntry(const FName InName, const FText& InLabel, const FText& InToolTip, const FSlateIcon& InIcon, const FUIAction& InAction) { (void)InName; (void)InLabel; (void)InToolTip; (void)InIcon; (void)InAction; static FToolMenuEntry E; return E; }
	void AddSeparator(FName InName) { (void)InName; }
};
class UToolMenu : public UObject
{
public:
	FToolMenuSection& FindOrAddSection(const FName SectionName, const FText& InLabel = FText()) { (void)SectionName; (void)InLabel; static FToolMenuSection S; return S; }
	FToolMenuSection& AddSection(const FName SectionName, const FText& InLabel = FText()) { (void)SectionName; (void)InLabel; static FToolMenuSection S; return S; }
};
class UToolMenus : public UObject
{
public:
	static UToolMenus* Get();
	UToolMenu* ExtendMenu(const FName Name) { (void)Name; return nullptr; }
	static FDelegateHandle RegisterStartupCallback(const FSimpleMulticastDelegate::FDelegate& InDelegate) { (void)InDelegate; return {}; }
	static void UnRegisterStartupCallback(const void* InOwner) { (void)InOwner; }
	static void UnregisterOwner(const void* InOwner) { (void)InOwner; }
};
struct FToolMenuOwnerScoped
{
	explicit FToolMenuOwnerScoped(const void* InOwner) { (void)InOwner; }
};

namespace EMessageSeverity { enum Type { CriticalError, Error, PerformanceWarning, Warning, Info }; }
class FTokenizedMessage
{
};
class FMessageLog
{
public:
	explicit FMessageLog(const FName& InLogName) { (void)InLogName; }
	void NewPage(const FText& Label) { (void)Label; }
	TSharedRef<FTokenizedMessage> Error(const FText& Message = FText()) { (void)Message; return MakeShared<FTokenizedMessage>(); }
	TSharedRef<FTokenizedMessage> Warning(const FText& Message = FText()) { (void)Message; return MakeShared<FTokenizedMessage>(); }
	TSharedRef<FTokenizedMessage> Info(const FText& Message = FText()) { (void)Message; return MakeShared<FTokenizedMessage>(); }
	void Open(EMessageSeverity::Type InSeverityFilter = EMessageSeverity::Info, bool bOpenEvenIfEmpty = false) { (void)InSeverityFilter; (void)bOpenEvenIfEmpty; }
	void Notify(const FText& InMessage = FText(), EMessageSeverity::Type InSeverityFilter = EMessageSeverity::Info, bool bForce = false) { (void)InMessage; (void)InSeverityFilter; (void)bForce; }
};
struct FMessageLogInitializationOptions
{
	bool bShowFilters = false;
	bool bShowPages = false;
	bool bAllowClear = true;
	bool bDiscardDuplicates = false;
	uint32 MaxPageCount = 20;
	bool bShowInLogWindow = true;
};
class FMessageLogModule : public IModuleInterface
{
public:
	void RegisterLogListing(const FName& LogName, const FText& LogLabel, const FMessageLogInitializationOptions& InitializationOptions = FMessageLogInitializationOptions()) { (void)LogName; (void)LogLabel; (void)InitializationOptions; }
	bool UnregisterLogListing(const FName& LogName) { (void)LogName; return true; }
};

struct FAssetData
{
	FName PackageName;
	FName AssetName;
	UObject* GetAsset() const { return nullptr; }
	bool IsValid() const { return true; }
	FString GetObjectPathString() const { return FString(); }
};
struct FARFilter
{
	TArray<FName> PackagePaths;
	TArray<FTopLevelAssetPath> ClassPaths;
	bool bRecursivePaths = false;
	bool bRecursiveClasses = false;
};
class IAssetRegistry
{
public:
	virtual ~IAssetRegistry() = default;
	virtual bool GetAssets(const FARFilter& Filter, TArray<FAssetData>& OutAssetData, bool bSkipARFilteredAssets = true) const = 0;
	virtual void SearchAllAssets(bool bSynchronousSearch) = 0;
	static IAssetRegistry& GetChecked();
};
class FAssetRegistryModule : public IModuleInterface
{
public:
	IAssetRegistry& Get() const;
};

class UEditorValidatorBase : public UObject
{
public:
	bool bIsEnabled = false;

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const { (void)InAssetData; (void)InObject; (void)InContext; return false; }
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) { (void)InAssetData; (void)InAsset; (void)Context; return EDataValidationResult::NotValidated; }
	void AssetFails(const UObject* InAsset, const FText& InMessage) { (void)InAsset; (void)InMessage; }
	void AssetWarning(const UObject* InAsset, const FText& InMessage) { (void)InAsset; (void)InMessage; }
	void AssetPasses(const UObject* InAsset) { (void)InAsset; }
};

class UCommandlet : public UObject
{
public:
	virtual int32 Main(const FString& Params) { (void)Params; return 0; }
	bool IsClient = true;
	bool IsServer = true;
	bool IsEditor = false;
	bool LogToConsole = false;
	bool ShowErrorCount = true;
};

class FScopedSlowTask
{
public:
	explicit FScopedSlowTask(float InAmountOfWork, const FText& InDefaultMessage = FText(), bool bInEnabled = true) { (void)InAmountOfWork; (void)InDefaultMessage; (void)bInEnabled; }
	void MakeDialog(bool bShowCancelButton = false, bool bAllowInPIE = false) { (void)bShowCancelButton; (void)bAllowInPIE; }
	void EnterProgressFrame(float ExpectedWorkThisFrame = 1.f, const FText& Text = FText()) { (void)ExpectedWorkThisFrame; (void)Text; }
};

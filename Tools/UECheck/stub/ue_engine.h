// Engine classes used by the Noctis UE layer (stand-in, see ue_core.h). Signatures as in UE 5.4/5.5.
#pragma once

#include "ue_object.h"

class AActor;
class APawn;
class AController;
class APlayerController;
class APlayerCameraManager;
class UActorComponent;
class USceneComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class ULevel;
class UGameInstance;
class ULocalPlayer;
class UInputComponent;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
class UAudioComponent;
class USkeletalMesh;
class UStaticMesh;
class UAnimInstance;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;
class UCanvas;
struct FHitResult;

struct FVector4f
{
	float X = 0, Y = 0, Z = 0, W = 0;
};
enum ENetMode { NM_Standalone, NM_DedicatedServer, NM_ListenServer, NM_Client };
enum ETickingGroup { TG_PrePhysics, TG_StartPhysics, TG_DuringPhysics, TG_EndPhysics, TG_PostPhysics, TG_PostUpdateWork, TG_LastDemotable, TG_NewlySpawned, TG_MAX };
enum ELevelTick { LEVELTICK_TimeOnly = 0, LEVELTICK_ViewportsOnly = 1, LEVELTICK_All = 2, LEVELTICK_PauseTick = 3 };
enum class ETeleportType : uint8 { None, TeleportPhysics, ResetPhysics };
namespace EComponentMobility { enum Type { Static, Stationary, Movable }; }
namespace ECollisionEnabled { enum Type { NoCollision, QueryOnly, PhysicsOnly, QueryAndPhysics, ProbeOnly, QueryAndProbe }; }
enum ECollisionChannel { ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_Visibility, ECC_Camera, ECC_PhysicsBody, ECC_Vehicle, ECC_Destructible, ECC_GameTraceChannel1, ECC_GameTraceChannel2, ECC_MAX };
enum class ESpawnActorCollisionHandlingMethod : uint8 { Undefined, AlwaysSpawn, AdjustIfPossibleButAlwaysSpawn, AdjustIfPossibleButDontSpawnIfColliding, DontSpawnIfColliding };
namespace EAttachLocation { enum Type { KeepRelativeOffset, KeepWorldPosition, SnapToTarget, SnapToTargetIncludingScale }; }
enum class EVisibilityBasedAnimTickOption : uint8 { AlwaysTickPoseAndRefreshBones, AlwaysTickPose, OnlyTickMontagesWhenNotRendered, OnlyTickPoseWhenRendered };
namespace ERootMotionMode { enum Type { NoRootMotionExtraction, IgnoreRootMotion, RootMotionFromEverything, RootMotionFromMontagesOnly }; }
enum class ELightUnits : uint8 { Unitless, Candelas, Lumens, EV };
enum EPixelFormat : uint8 { PF_Unknown, PF_B8G8R8A8, PF_R8G8B8A8, PF_G8, PF_FloatRGBA };
enum TextureFilter : uint8 { TF_Nearest, TF_Bilinear, TF_Trilinear, TF_Default };
enum TextureAddress : uint8 { TA_Wrap, TA_Clamp, TA_Mirror };
enum EBlendMode : uint8 { BLEND_Opaque, BLEND_Masked, BLEND_Translucent, BLEND_Additive };
enum ESceneDepthPriorityGroup { SDPG_World, SDPG_Foreground };
enum EBulkDataLockFlags { LOCK_READ_ONLY = 1, LOCK_READ_WRITE = 2 };

template <typename T>
struct TEnumAsByte
{
	TEnumAsByte() = default;
	TEnumAsByte(T V) : Value(V) {}
	operator T() const { return Value; }
	T Value{};
};

// ------------------------------------------------------------------ tick
struct FTickFunction
{
	bool bCanEverTick = false;
	bool bStartWithTickEnabled = true;
	bool bTickEvenWhenPaused = false;
	float TickInterval = 0.0f;
	TEnumAsByte<ETickingGroup> TickGroup = TG_PrePhysics;
	void AddPrerequisite(UObject* Target, FTickFunction& Function) { (void)Target; (void)Function; }
};
struct FActorTickFunction : FTickFunction
{
};
struct FActorComponentTickFunction : FTickFunction
{
};

// ------------------------------------------------------------------ collision
struct FCollisionQueryParams
{
	FCollisionQueryParams() = default;
	FCollisionQueryParams(FName TraceTag, bool bTraceComplex = false, const AActor* IgnoreActor = nullptr) { (void)TraceTag; (void)bTraceComplex; (void)IgnoreActor; }
	void AddIgnoredActor(const AActor* Actor) { (void)Actor; }
	bool bTraceComplex = false;
};
struct FHitResult
{
	FVector ImpactPoint;
	FVector ImpactNormal;
	FVector Location;
	FVector Normal;
	float Distance = 0.0f;
	bool bBlockingHit = false;
	AActor* GetActor() const { return nullptr; }
	UPrimitiveComponent* GetComponent() const { return nullptr; }
};

// ------------------------------------------------------------------ components
class UActorComponent : public UObject
{
public:
	UActorComponent() = default;
	UActorComponent(const FObjectInitializer& Init) : UObject(Init) {}
	FActorComponentTickFunction PrimaryComponentTick;
	bool bAutoActivate = true;
	AActor* GetOwner() const { return nullptr; }
	template <typename T>
	T* GetOwner() const { return nullptr; }
	virtual void BeginPlay() {}
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) { (void)EndPlayReason; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) { (void)DeltaTime; (void)TickType; (void)ThisTickFunction; }
	virtual void Activate(bool bReset = false) { (void)bReset; }
	virtual void Deactivate() {}
	bool IsActive() const { return true; }
	void RegisterComponent() {}
	void UnregisterComponent() {}
	void DestroyComponent(bool bPromoteChildren = false) { (void)bPromoteChildren; }
	void SetComponentTickEnabled(bool bEnabled) { (void)bEnabled; }
	void SetComponentTickInterval(float Interval) { (void)Interval; }
	bool IsRegistered() const { return true; }
	void AddTickPrerequisiteActor(AActor* Actor) { (void)Actor; }
	void AddTickPrerequisiteComponent(UActorComponent* Component) { (void)Component; }
	virtual UWorld* GetWorld() const override { return nullptr; }
};

class USceneComponent : public UActorComponent
{
public:
	USceneComponent() = default;
	USceneComponent(const FObjectInitializer& Init) : UActorComponent(Init) {}
	void SetupAttachment(USceneComponent* Parent, FName SocketName = NAME_None) { (void)Parent; (void)SocketName; }
	bool AttachToComponent(USceneComponent* Parent, const struct FAttachmentTransformRules& Rules, FName SocketName = NAME_None);
	void SetRelativeLocation(const FVector& Location, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)Location; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetRelativeRotation(const FRotator& Rotation, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)Rotation; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetRelativeRotation(const FQuat& Rotation, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)Rotation; (void)bSweep; (void)OutHit; (void)Teleport; }
	void AddRelativeRotation(const FRotator& Delta, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)Delta; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetRelativeScale3D(const FVector& Scale) { (void)Scale; }
	void SetRelativeLocationAndRotation(const FVector& L, const FRotator& R, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)L; (void)R; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetWorldLocation(const FVector& Location, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)Location; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetWorldRotation(const FRotator& Rotation, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)Rotation; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetWorldRotation(const FQuat& Rotation, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)Rotation; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetWorldLocationAndRotation(const FVector& L, const FRotator& R, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)L; (void)R; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetWorldTransform(const FTransform& T, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)T; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetWorldScale3D(const FVector& Scale) { (void)Scale; }
	FVector GetComponentLocation() const { return {}; }
	FRotator GetComponentRotation() const { return {}; }
	FQuat GetComponentQuat() const { return {}; }
	const FTransform& GetComponentTransform() const { static FTransform T; return T; }
	FVector GetComponentScale() const { return {}; }
	FVector GetRelativeLocation() const { return {}; }
	FRotator GetRelativeRotation() const { return {}; }
	FVector GetRelativeScale3D() const { return {}; }
	FVector GetForwardVector() const { return {}; }
	FVector GetRightVector() const { return {}; }
	FVector GetUpVector() const { return {}; }
	FTransform GetSocketTransform(FName Socket) const { (void)Socket; return {}; }
	FVector GetSocketLocation(FName Socket) const { (void)Socket; return {}; }
	void SetVisibility(bool bNewVisibility, bool bPropagateToChildren = false) { (void)bNewVisibility; (void)bPropagateToChildren; }
	void SetHiddenInGame(bool bHidden, bool bPropagateToChildren = false) { (void)bHidden; (void)bPropagateToChildren; }
	bool IsVisible() const { return true; }
	void SetMobility(EComponentMobility::Type Mobility) { (void)Mobility; }
	USceneComponent* GetAttachParent() const { return nullptr; }
};

class UPrimitiveComponent : public USceneComponent
{
public:
	UPrimitiveComponent() = default;
	UPrimitiveComponent(const FObjectInitializer& Init) : USceneComponent(Init) {}
	void SetCollisionEnabled(ECollisionEnabled::Type Type) { (void)Type; }
	void SetCollisionProfileName(FName Profile, bool bUpdateOverlaps = true) { (void)Profile; (void)bUpdateOverlaps; }
	void SetCollisionResponseToAllChannels(int32 Response) { (void)Response; }
	void SetGenerateOverlapEvents(bool b) { (void)b; }
	void SetCastShadow(bool b) { (void)b; }
	void SetSimulatePhysics(bool b) { (void)b; }
	bool IsSimulatingPhysics(FName Bone = NAME_None) const { (void)Bone; return false; }
	void AddForce(FVector Force, FName BoneName = NAME_None, bool bAccelChange = false) { (void)Force; (void)BoneName; (void)bAccelChange; }
	void AddImpulse(FVector Impulse, FName BoneName = NAME_None, bool bVelChange = false) { (void)Impulse; (void)BoneName; (void)bVelChange; }
	FVector GetPhysicsLinearVelocity(FName BoneName = NAME_None) { (void)BoneName; return {}; }
	float GetMass() const { return 1.0f; }
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) { (void)ElementIndex; (void)Material; }
	UMaterialInterface* GetMaterial(int32 ElementIndex) const { (void)ElementIndex; return nullptr; }
	int32 GetNumMaterials() const { return 0; }
	void SetCustomPrimitiveDataFloat(int32 DataIndex, float Value) { (void)DataIndex; (void)Value; }
	void SetCustomPrimitiveDataVector4(int32 DataIndex, const FVector4f& Value);
	void SetRenderCustomDepth(bool b) { (void)b; }
	bool bCastDynamicShadow = true;
	bool bReceivesDecals = true;
};
inline void UPrimitiveComponent::SetCustomPrimitiveDataVector4(int32 DataIndex, const FVector4f& Value)
{
	(void)DataIndex;
	(void)Value;
}

class UMeshComponent : public UPrimitiveComponent
{
public:
	UMeshComponent() = default;
	UMeshComponent(const FObjectInitializer& Init) : UPrimitiveComponent(Init) {}
};

class USkinnedAsset : public UObject
{
};
class USkeleton : public UObject
{
};
struct FReferenceSkeleton
{
	int32 FindBoneIndex(FName BoneName) const { (void)BoneName; return INDEX_NONE; }
	int32 GetNum() const { return 0; }
	FName GetBoneName(int32 Index) const { (void)Index; return NAME_None; }
};
class USkeletalMesh : public USkinnedAsset
{
public:
	const FReferenceSkeleton& GetRefSkeleton() const { static FReferenceSkeleton R; return R; }
	USkeleton* GetSkeleton() const { return nullptr; }
	FBoxSphereBounds GetImportedBounds() const { return {}; }
	FBoxSphereBounds GetBounds() const { return {}; }
};

class UAnimSequenceBase : public UObject
{
public:
	float GetPlayLength() const { return 1.0f; }
};
class UAnimSequence : public UAnimSequenceBase
{
public:
	bool bEnableRootMotion = false;
};

class USkinnedMeshComponent : public UMeshComponent
{
public:
	USkinnedMeshComponent() = default;
	USkinnedMeshComponent(const FObjectInitializer& Init) : UMeshComponent(Init) {}
	bool bEnableUpdateRateOptimizations = false;
	EVisibilityBasedAnimTickOption VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	void SetSkinnedAssetAndUpdate(USkinnedAsset* Asset, bool bReinitPose = true) { (void)Asset; (void)bReinitPose; }
	USkinnedAsset* GetSkinnedAsset() const { return nullptr; }
	int32 GetNumBones() const { return 0; }
	FName GetBoneName(int32 Index) const { (void)Index; return NAME_None; }
	int32 GetBoneIndex(FName Bone) const { (void)Bone; return INDEX_NONE; }
	void SetForcedLOD(int32 LOD) { (void)LOD; }
};
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	USkeletalMeshComponent() = default;
	USkeletalMeshComponent(const FObjectInitializer& Init) : USkinnedMeshComponent(Init) {}
	USkeletalMesh* GetSkeletalMeshAsset() const { return nullptr; }
	void SetSkeletalMeshAsset(USkeletalMesh* Mesh) { (void)Mesh; }
	void SetAnimInstanceClass(UClass* Class) { (void)Class; }
	UAnimInstance* GetAnimInstance() const { return nullptr; }
	void SetAnimationMode(uint8 Mode) { (void)Mode; }
	void PlayAnimation(UAnimSequenceBase* Anim, bool bLooping) { (void)Anim; (void)bLooping; }
};

class UStaticMesh : public UObject
{
public:
	FBoxSphereBounds GetBounds() const { return {}; }
};
class UStaticMeshComponent : public UMeshComponent
{
public:
	UStaticMeshComponent() = default;
	UStaticMeshComponent(const FObjectInitializer& Init) : UMeshComponent(Init) {}
	virtual bool SetStaticMesh(UStaticMesh* Mesh) { (void)Mesh; return true; }
	UStaticMesh* GetStaticMesh() const { return nullptr; }
};
class UInstancedStaticMeshComponent : public UStaticMeshComponent
{
public:
	virtual int32 AddInstance(const FTransform& Transform, bool bWorldSpace = false) { (void)Transform; (void)bWorldSpace; return 0; }
	virtual TArray<int32> AddInstances(const TArray<FTransform>& Transforms, bool bShouldReturnIndices, bool bWorldSpace = false, bool bUpdateNavigation = true) { (void)Transforms; (void)bShouldReturnIndices; (void)bWorldSpace; (void)bUpdateNavigation; return {}; }
	virtual void ClearInstances() {}
	int32 GetInstanceCount() const { return 0; }
	virtual bool UpdateInstanceTransform(int32 Index, const FTransform& T, bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false) { (void)Index; (void)T; (void)bWorldSpace; (void)bMarkRenderStateDirty; (void)bTeleport; return true; }
};
class UHierarchicalInstancedStaticMeshComponent : public UInstancedStaticMeshComponent
{
};

class UShapeComponent : public UPrimitiveComponent
{
};
class UCapsuleComponent : public UShapeComponent
{
public:
	void InitCapsuleSize(float Radius, float HalfHeight) { (void)Radius; (void)HalfHeight; }
	void SetCapsuleSize(float Radius, float HalfHeight, bool bUpdateOverlaps = true) { (void)Radius; (void)HalfHeight; (void)bUpdateOverlaps; }
	float GetScaledCapsuleHalfHeight() const { return 0.0f; }
	float GetScaledCapsuleRadius() const { return 0.0f; }
};

// ------------------------------------------------------------------ cameras
class UCameraComponent : public USceneComponent
{
public:
	UCameraComponent() = default;
	UCameraComponent(const FObjectInitializer& Init) : USceneComponent(Init) {}
	float FieldOfView = 90.0f;
	bool bUsePawnControlRotation = false;
	void SetFieldOfView(float Fov) { FieldOfView = Fov; }
	void SetPostProcessBlendWeight(float W) { (void)W; }
};
class USpringArmComponent : public USceneComponent
{
public:
	float TargetArmLength = 300.0f;
	bool bUsePawnControlRotation = false;
};

// ------------------------------------------------------------------ lights / sky / fog
class ULightComponentBase : public USceneComponent
{
public:
	void SetCastShadows(bool b) { (void)b; }
	bool bAffectsWorld = true;
};
class ULightComponent : public ULightComponentBase
{
public:
	void SetIntensity(float Intensity) { (void)Intensity; }
	void SetLightColor(FLinearColor Color, bool bSRGB = true) { (void)Color; (void)bSRGB; }
	void SetTemperature(float Kelvin) { (void)Kelvin; }
	void SetUseTemperature(bool b) { (void)b; }
	float Intensity = 1.0f;
};
class ULocalLightComponent : public ULightComponent
{
public:
	void SetAttenuationRadius(float Radius) { (void)Radius; }
	void SetIntensityUnits(ELightUnits Units) { (void)Units; }
};
class UPointLightComponent : public ULocalLightComponent
{
};
class USpotLightComponent : public UPointLightComponent
{
public:
	void SetInnerConeAngle(float Angle) { (void)Angle; }
	void SetOuterConeAngle(float Angle) { (void)Angle; }
};
class UDirectionalLightComponent : public ULightComponent
{
public:
	void SetAtmosphereSunLight(bool b) { (void)b; }
	void SetAtmosphereSunLightIndex(int32 Index) { (void)Index; }
	void SetDynamicShadowCascades(int32 N) { (void)N; }
	void SetLightSourceAngle(float A) { (void)A; }
};
class USkyLightComponent : public ULightComponentBase
{
public:
	bool bRealTimeCapture = false;
	void RecaptureSky() {}
	void SetIntensity(float I) { (void)I; }
};
class USkyAtmosphereComponent : public USceneComponent
{
public:
	void SetMieScatteringScale(float V) { (void)V; }
	void SetRayleighScatteringScale(float V) { (void)V; }
	void SetMieAnisotropy(float V) { (void)V; }
	void SetMultiScatteringFactor(float V) { (void)V; }
};
class UExponentialHeightFogComponent : public USceneComponent
{
public:
	void SetFogDensity(float V) { (void)V; }
	void SetFogHeightFalloff(float V) { (void)V; }
	void SetSecondFogDensity(float V) { (void)V; }
	void SetSecondFogHeightFalloff(float V) { (void)V; }
	void SetSecondFogHeightOffset(float V) { (void)V; }
	void SetFogInscatteringColor(FLinearColor C) { (void)C; }
	void SetVolumetricFog(bool b) { (void)b; }
	void SetVolumetricFogExtinctionScale(float V) { (void)V; }
	void SetStartDistance(float V) { (void)V; }
};
class UVolumetricCloudComponent : public USceneComponent
{
public:
	void SetLayerBottomAltitude(float V) { (void)V; }
	void SetLayerHeight(float V) { (void)V; }
	void SetMaterial(UMaterialInterface* M) { (void)M; }
};
class UWindDirectionalSourceComponent : public USceneComponent
{
public:
	void SetStrength(float V) { (void)V; }
	void SetSpeed(float V) { (void)V; }
	void SetMinimumGustAmount(float V) { (void)V; }
	void SetMaximumGustAmount(float V) { (void)V; }
	void SetRadius(float V) { (void)V; }
	void SetWindType(uint8 T) { (void)T; }
};

// ------------------------------------------------------------------ audio
class USoundBase : public UObject
{
public:
	virtual float GetDuration() const { return 1.0f; }
};
class USoundWave : public USoundBase
{
public:
	int32 NumChannels = 1;
	float Duration = 0.0f;
	bool bLooping = false;
	void SetSampleRate(uint32 Rate) { (void)Rate; }
};
namespace Audio
{
namespace EAudioMixerStreamDataFormat { enum Type { Unknown, Float, Int16, Unsupported }; }
} // namespace Audio
class USoundWaveProcedural : public USoundWave
{
public:
	USoundWaveProcedural() = default;
	USoundWaveProcedural(const FObjectInitializer& Init) { (void)Init; }
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) { (void)OutAudio; return NumSamples; }
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const { return Audio::EAudioMixerStreamDataFormat::Int16; }
	void QueueAudio(const uint8* Data, const int32 Bytes) { (void)Data; (void)Bytes; }
	void ResetAudio() {}
	TStubDelegate<void(USoundWaveProcedural*, int32)> OnSoundWaveProceduralUnderflow;
};
class USoundCue : public USoundBase
{
};
class USoundAttenuation : public UObject
{
};
class USoundConcurrency : public UObject
{
};
class USoundClass : public UObject
{
};
class UAudioComponent : public USceneComponent
{
public:
	UAudioComponent() = default;
	UAudioComponent(const FObjectInitializer& Init) : USceneComponent(Init) {}
	TObjectPtr<USoundBase> Sound;
	void SetSound(USoundBase* NewSound) { (void)NewSound; }
	virtual void Play(float StartTime = 0.0f) { (void)StartTime; }
	virtual void Stop() {}
	bool IsPlaying() const { return false; }
	void SetVolumeMultiplier(float V) { (void)V; }
	void SetPitchMultiplier(float V) { (void)V; }
	void FadeIn(float Duration, float Volume = 1.0f, float StartTime = 0.0f) { (void)Duration; (void)Volume; (void)StartTime; }
	void FadeOut(float Duration, float Volume) { (void)Duration; (void)Volume; }
	void SetFloatParameter(FName Name, float Value) { (void)Name; (void)Value; }
	void SetIntParameter(FName Name, int32 Value) { (void)Name; (void)Value; }
	void SetBoolParameter(FName Name, bool Value) { (void)Name; (void)Value; }
	void SetTriggerParameter(FName Name) { (void)Name; }
	TObjectPtr<USoundAttenuation> AttenuationSettings;
	bool bAllowSpatialization = true;
};

// ------------------------------------------------------------------ materials / textures
class UMaterialInterface : public UObject
{
};
class UMaterial : public UMaterialInterface
{
};
class UMaterialInstance : public UMaterialInterface
{
};
class UMaterialInstanceDynamic : public UMaterialInstance
{
public:
	void SetScalarParameterValue(FName Name, float Value) { (void)Name; (void)Value; }
	void SetVectorParameterValue(FName Name, const FLinearColor& Value) { (void)Name; (void)Value; }
	void SetTextureParameterValue(FName Name, UTexture* Value) { (void)Name; (void)Value; }
	static UMaterialInstanceDynamic* Create(UMaterialInterface* Parent, UObject* Outer) { (void)Parent; (void)Outer; return nullptr; }
};
class UMaterialParameterCollection : public UObject
{
};
class UMaterialParameterCollectionInstance : public UObject
{
public:
	bool SetScalarParameterValue(FName Name, float Value) { (void)Name; (void)Value; return true; }
	bool SetVectorParameterValue(FName Name, const FLinearColor& Value) { (void)Name; (void)Value; return true; }
};

struct FByteBulkData
{
	void* Lock(uint32 Flags) { (void)Flags; return nullptr; }
	void Unlock() {}
	void* Realloc(int64 Size) { (void)Size; return nullptr; }
};
struct FTexture2DMipMap
{
	int32 SizeX = 0, SizeY = 0;
	FByteBulkData BulkData;
};
struct FTexturePlatformData
{
	TArray<FTexture2DMipMap> Mips;
};
struct FUpdateTextureRegion2D
{
	uint32 DestX = 0, DestY = 0;
	int32 SrcX = 0, SrcY = 0;
	uint32 Width = 0, Height = 0;
	FUpdateTextureRegion2D() = default;
	FUpdateTextureRegion2D(uint32 InDestX, uint32 InDestY, int32 InSrcX, int32 InSrcY, uint32 InWidth, uint32 InHeight) : DestX(InDestX), DestY(InDestY), SrcX(InSrcX), SrcY(InSrcY), Width(InWidth), Height(InHeight) {}
};
class UTexture : public UObject
{
public:
	bool SRGB = true;
	TEnumAsByte<TextureFilter> Filter = TF_Default;
	void UpdateResource() {}
};
class UTexture2D : public UTexture
{
public:
	TEnumAsByte<TextureAddress> AddressX = TA_Wrap;
	TEnumAsByte<TextureAddress> AddressY = TA_Wrap;
	static UTexture2D* CreateTransient(int32 SizeX, int32 SizeY, EPixelFormat Format = PF_B8G8R8A8, const FName Name = NAME_None) { (void)SizeX; (void)SizeY; (void)Format; (void)Name; return nullptr; }
	FTexturePlatformData* GetPlatformData() { return nullptr; }
	int32 GetSizeX() const { return 0; }
	int32 GetSizeY() const { return 0; }
	void UpdateTextureRegions(int32 MipIndex, uint32 NumRegions, const FUpdateTextureRegion2D* Regions, uint32 SrcPitch, uint32 SrcBpp, uint8* SrcData,
							  TFunction<void(uint8* SrcData, const FUpdateTextureRegion2D* Regions)> DataCleanupFunc = [](uint8*, const FUpdateTextureRegion2D*) {})
	{
		(void)MipIndex; (void)NumRegions; (void)Regions; (void)SrcPitch; (void)SrcBpp; (void)SrcData; (void)DataCleanupFunc;
	}
};
class UTextureRenderTarget : public UTexture
{
};
class UTextureRenderTarget2D : public UTextureRenderTarget
{
public:
	int32 SizeX = 0, SizeY = 0;
};
class UFont : public UObject
{
};
class UCanvas : public UObject
{
public:
	void K2_DrawTexture(UTexture* RenderTexture, FVector2D ScreenPosition, FVector2D ScreenSize, FVector2D CoordinatePosition, FVector2D CoordinateSize = FVector2D::UnitVector,
						FLinearColor RenderColor = FLinearColor::White, EBlendMode BlendMode = BLEND_Translucent, float Rotation = 0.f, FVector2D PivotPoint = FVector2D(0.5, 0.5))
	{
		(void)RenderTexture; (void)ScreenPosition; (void)ScreenSize; (void)CoordinatePosition; (void)CoordinateSize; (void)RenderColor; (void)BlendMode; (void)Rotation; (void)PivotPoint;
	}
};
struct FDrawToRenderTargetContext
{
};

// ------------------------------------------------------------------ actors
struct FAttachmentTransformRules
{
	static const FAttachmentTransformRules KeepRelativeTransform, KeepWorldTransform, SnapToTargetNotIncludingScale, SnapToTargetIncludingScale;
};
inline bool USceneComponent::AttachToComponent(USceneComponent* Parent, const FAttachmentTransformRules& Rules, FName SocketName)
{
	(void)Parent; (void)Rules; (void)SocketName;
	return true;
}
struct FActorSpawnParameters
{
	FName Name;
	AActor* Owner = nullptr;
	APawn* Instigator = nullptr;
	ESpawnActorCollisionHandlingMethod SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::Undefined;
	EObjectFlags ObjectFlags = RF_Transactional;
	bool bDeferConstruction = false;
};

class AActor : public UObject
{
public:
	AActor() = default;
	AActor(const FObjectInitializer& Init) : UObject(Init) {}
	FActorTickFunction PrimaryActorTick;
	TObjectPtr<USceneComponent> RootComponent;
	TArray<FName> Tags;
	virtual void BeginPlay() {}
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) { (void)EndPlayReason; }
	virtual void Tick(float DeltaSeconds) { (void)DeltaSeconds; }
	virtual void PostInitializeComponents() {}
	virtual void OnConstruction(const FTransform& Transform) { (void)Transform; }
	virtual UWorld* GetWorld() const override { return nullptr; }
	FVector GetActorLocation() const { return {}; }
	FRotator GetActorRotation() const { return {}; }
	FQuat GetActorQuat() const { return {}; }
	FVector GetActorForwardVector() const { return {}; }
	FVector GetActorRightVector() const { return {}; }
	FVector GetActorUpVector() const { return {}; }
	const FTransform& GetActorTransform() const { static FTransform T; return T; }
	FVector GetActorScale3D() const { return {}; }
	virtual FVector GetVelocity() const { return {}; }
	bool SetActorLocation(const FVector& L, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)L; (void)bSweep; (void)OutHit; (void)Teleport; return true; }
	bool SetActorRotation(FRotator R, ETeleportType Teleport = ETeleportType::None) { (void)R; (void)Teleport; return true; }
	bool SetActorRotation(const FQuat& Q, ETeleportType Teleport = ETeleportType::None) { (void)Q; (void)Teleport; return true; }
	bool SetActorLocationAndRotation(FVector L, FRotator R, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)L; (void)R; (void)bSweep; (void)OutHit; (void)Teleport; return true; }
	bool SetActorLocationAndRotation(FVector L, const FQuat& R, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)L; (void)R; (void)bSweep; (void)OutHit; (void)Teleport; return true; }
	void SetActorTransform(const FTransform& T, bool bSweep = false, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) { (void)T; (void)bSweep; (void)OutHit; (void)Teleport; }
	void SetActorScale3D(FVector S) { (void)S; }
	virtual bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest = false, bool bNoCheck = false) { (void)DestLocation; (void)DestRotation; (void)bIsATest; (void)bNoCheck; return true; }
	virtual void SetActorHiddenInGame(bool bNewHidden) { (void)bNewHidden; }
	bool IsHidden() const { return false; }
	void SetActorEnableCollision(bool b) { (void)b; }
	void SetActorTickEnabled(bool b) { (void)b; }
	void SetActorTickInterval(float I) { (void)I; }
	bool ActorHasTag(FName Tag) const { (void)Tag; return false; }
	USceneComponent* GetRootComponent() const { return RootComponent; }
	void SetRootComponent(USceneComponent* C) { RootComponent = C; }
	template <typename T>
	T* FindComponentByClass() const { return nullptr; }
	template <typename T>
	void GetComponents(TArray<T*>& Out) const { (void)Out; }
	AActor* GetOwner() const { return nullptr; }
	void SetOwner(AActor* O) { (void)O; }
	virtual bool Destroy(bool bNetForce = false, bool bShouldModifyLevel = true) { (void)bNetForce; (void)bShouldModifyLevel; return true; }
	bool HasActorBegunPlay() const { return true; }
	void AddTickPrerequisiteActor(AActor* A) { (void)A; }
	ULevel* GetLevel() const { return nullptr; }
	float GetGameTimeSinceCreation() const { return 0.0f; }
};

class AInfo : public AActor
{
};

class AController;
class APawn : public AActor
{
public:
	APawn() = default;
	APawn(const FObjectInitializer& Init) : AActor(Init) {}
	TSubclassOf<AController> AIControllerClass;
	bool bUseControllerRotationYaw = false;
	bool bUseControllerRotationPitch = false;
	float BaseEyeHeight = 64.0f;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) { (void)PlayerInputComponent; }
	virtual void PawnClientRestart() {}
	virtual void PossessedBy(AController* NewController) { (void)NewController; }
	virtual void UnPossessed() {}
	AController* GetController() const { return nullptr; }
	template <typename T>
	T* GetController() const { return nullptr; }
	bool IsPlayerControlled() const { return true; }
	bool IsLocallyControlled() const { return true; }
	FRotator GetControlRotation() const { return {}; }
	FRotator GetBaseAimRotation() const { return {}; }
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) { (void)WorldDirection; (void)ScaleValue; (void)bForce; }
	virtual void AddControllerYawInput(float Val) { (void)Val; }
	virtual void AddControllerPitchInput(float Val) { (void)Val; }
	virtual void AddControllerRollInput(float Val) { (void)Val; }
	virtual FVector GetPawnViewLocation() const { return {}; }
};

class UPawnMovementComponent : public UActorComponent
{
};
struct FNavAgentProperties
{
	bool bCanCrouch = false;
	bool bCanJump = true;
	bool bCanWalk = true;
	bool bCanSwim = false;
	bool bCanFly = false;
};
class UCharacterMovementComponent : public UPawnMovementComponent
{
public:
	float MaxWalkSpeed = 600.0f;
	float MaxWalkSpeedCrouched = 300.0f;
	float MaxAcceleration = 2048.0f;
	float BrakingDecelerationWalking = 2048.0f;
	float JumpZVelocity = 420.0f;
	float GravityScale = 1.0f;
	bool bOrientRotationToMovement = false;
	bool bUseControllerDesiredRotation = false;
	FRotator RotationRate;
	FNavAgentProperties& GetNavAgentPropertiesRef() { static FNavAgentProperties P; return P; }
	bool IsFalling() const { return false; }
	bool IsMovingOnGround() const { return true; }
};
class ACharacter : public APawn
{
public:
	ACharacter() = default;
	ACharacter(const FObjectInitializer& Init) : APawn(Init) {}
	float CrouchedEyeHeight = 32.0f;
	uint32 bIsCrouched : 1 = 0;
	UCapsuleComponent* GetCapsuleComponent() const { return nullptr; }
	USkeletalMeshComponent* GetMesh() const { return nullptr; }
	UCharacterMovementComponent* GetCharacterMovement() const { return nullptr; }
	virtual void Crouch(bool bClientSimulation = false) { (void)bClientSimulation; }
	virtual void UnCrouch(bool bClientSimulation = false) { (void)bClientSimulation; }
	virtual void Jump() {}
	virtual void StopJumping() {}
};

class APlayerCameraManager : public AActor
{
public:
	FVector GetCameraLocation() const { return {}; }
	FRotator GetCameraRotation() const { return {}; }
	float GetFOVAngle() const { return 90.0f; }
};
class AController : public AActor
{
public:
	virtual void Possess(APawn* InPawn) { (void)InPawn; }
	virtual void UnPossess() {}
	APawn* GetPawn() const { return nullptr; }
	template <typename T>
	T* GetPawn() const { return nullptr; }
	virtual FRotator GetControlRotation() const { return {}; }
	virtual void SetControlRotation(const FRotator& R) { (void)R; }
	virtual void GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const { (void)Location; (void)Rotation; }
};
class APlayerController : public AController
{
public:
	TObjectPtr<APlayerCameraManager> PlayerCameraManager;
	bool bShowMouseCursor = false;
	ULocalPlayer* GetLocalPlayer() const { return nullptr; }
	bool IsLocalController() const { return true; }
	bool DeprojectScreenPositionToWorld(float X, float Y, FVector& WorldLocation, FVector& WorldDirection) const { (void)X; (void)Y; (void)WorldLocation; (void)WorldDirection; return true; }
	void SetInputMode(const struct FInputModeDataBase& Mode);
	bool GetMousePosition(float& X, float& Y) const { (void)X; (void)Y; return true; }
};

class AGameModeBase : public AInfo
{
public:
	AGameModeBase() = default;
	AGameModeBase(const FObjectInitializer& Init) { (void)Init; }
	TSubclassOf<APawn> DefaultPawnClass;
	TSubclassOf<APlayerController> PlayerControllerClass;
	TSubclassOf<AActor> HUDClass;
};

class ALight : public AActor
{
public:
	ULightComponent* GetLightComponent() const { return nullptr; }
};
class ADirectionalLight : public ALight
{
};
class ASkyLight : public AInfo
{
public:
	USkyLightComponent* GetLightComponent() const { return nullptr; }
};
class AExponentialHeightFog : public AInfo
{
public:
	UExponentialHeightFogComponent* GetComponent() const { return nullptr; }
};
class ASkyAtmosphere : public AInfo
{
public:
	USkyAtmosphereComponent* GetComponent() const { return nullptr; }
};
class AVolumetricCloud : public AInfo
{
public:
	UVolumetricCloudComponent* GetComponent() const { return nullptr; }
};
class AWindDirectionalSource : public AInfo
{
public:
	UWindDirectionalSourceComponent* GetComponent() const { return nullptr; }
};

// ------------------------------------------------------------------ world
struct FURL
{
	FURL() = default;
	explicit FURL(const TCHAR* Filename) { (void)Filename; }
};
class UWorldSubsystem;
class AWorldSettings : public AInfo
{
};
class UWorld : public UObject
{
public:
	EWorldType::Type WorldType = EWorldType::Game;
	template <typename T>
	T* SpawnActor(UClass* Class, const FTransform& Transform, const FActorSpawnParameters& Params = FActorSpawnParameters()) { (void)Class; (void)Transform; (void)Params; return nullptr; }
	template <typename T>
	T* SpawnActor(UClass* Class, const FVector& Location, const FRotator& Rotation, const FActorSpawnParameters& Params = FActorSpawnParameters()) { (void)Class; (void)Location; (void)Rotation; (void)Params; return nullptr; }
	template <typename T>
	T* SpawnActor(const FActorSpawnParameters& Params = FActorSpawnParameters()) { (void)Params; return nullptr; }
	template <typename T>
	T* GetSubsystem() const { return nullptr; }
	APlayerController* GetFirstPlayerController() const { return nullptr; }
	double GetTimeSeconds() const { return 0.0; }
	double GetRealTimeSeconds() const { return 0.0; }
	double GetUnpausedTimeSeconds() const { return 0.0; }
	float GetDeltaSeconds() const { return 0.0f; }
	bool IsPaused() const { return false; }
	bool IsGameWorld() const { return true; }
	UGameInstance* GetGameInstance() const { return nullptr; }
	AWorldSettings* GetWorldSettings(bool bCheckStreamingPersistent = false, bool bChecked = true) const { (void)bCheckStreamingPersistent; (void)bChecked; return nullptr; }
	UMaterialParameterCollectionInstance* GetParameterCollectionInstance(const UMaterialParameterCollection* Collection) { (void)Collection; return nullptr; }
	bool LineTraceSingleByChannel(FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params = FCollisionQueryParams()) const { (void)OutHit; (void)Start; (void)End; (void)TraceChannel; (void)Params; return false; }
	static UWorld* CreateWorld(const EWorldType::Type InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None, UPackage* InWorldPackage = nullptr, bool bAddToRoot = true) { (void)InWorldType; (void)bInformEngineOfWorld; (void)WorldName; (void)InWorldPackage; (void)bAddToRoot; return nullptr; }
	void InitializeActorsForPlay(const FURL& InURL, bool bResetTime = true) { (void)InURL; (void)bResetTime; }
	void BeginPlay() {}
	void Tick(ELevelTick TickType, float DeltaSeconds) { (void)TickType; (void)DeltaSeconds; }
	void DestroyWorld(bool bInformEngineOfWorld, UWorld* NewWorld = nullptr) { (void)bInformEngineOfWorld; (void)NewWorld; }
	ENetMode GetNetMode() const;
};
inline ENetMode UWorld::GetNetMode() const
{
	return NM_Standalone;
}

struct FWorldDelegates
{
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FWorldPreActorTick, UWorld*, ELevelTick, float);
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FWorldPostActorTick, UWorld*, ELevelTick, float);
	static FWorldPreActorTick OnWorldPreActorTick;
	static FWorldPostActorTick OnWorldPostActorTick;
};

template <typename T = AActor>
class TActorIterator
{
public:
	explicit TActorIterator(const UWorld* World, UClass* Class = T::StaticClass()) { (void)World; (void)Class; }
	explicit operator bool() const { return false; }
	T* operator*() const { return nullptr; }
	T* operator->() const { return nullptr; }
	TActorIterator& operator++() { return *this; }
};
class FActorIterator : public TActorIterator<AActor>
{
public:
	using TActorIterator<AActor>::TActorIterator;
};

class FSubsystemCollectionBase
{
public:
	template <typename T>
	T* InitializeDependency() { return nullptr; }
};
class USubsystem : public UObject
{
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const { (void)Outer; return true; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) { (void)Collection; }
	virtual void Deinitialize() {}
};
class UWorldSubsystem : public USubsystem
{
public:
	virtual UWorld* GetWorld() const override { return nullptr; }
	UWorld& GetWorldRef() const;
	virtual void OnWorldBeginPlay(UWorld& InWorld) { (void)InWorld; }
	virtual void PostInitialize() {}

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const { (void)WorldType; return true; }
};
class UTickableWorldSubsystem : public UWorldSubsystem
{
};
class UGameInstanceSubsystem : public USubsystem
{
};
class ULocalPlayerSubsystem : public USubsystem
{
};
class UEngineSubsystem : public USubsystem
{
};

struct FWorldContext
{
	EWorldType::Type WorldType = EWorldType::None;
	UWorld* World() const { return nullptr; }
	void SetCurrentWorld(UWorld* World) { (void)World; }
};
class UEngine : public UObject
{
public:
	UWorld* GetWorldFromContextObject(const UObject* Object, EGetWorldErrorMode::Type ErrorMode) const { (void)Object; (void)ErrorMode; return nullptr; }
	void AddOnScreenDebugMessage(int32 Key, float TimeToDisplay, FColor DisplayColor, const FString& DebugMessage, bool bNewerOnTop = true, const FVector2D& TextScale = FVector2D::UnitVector) { (void)Key; (void)TimeToDisplay; (void)DisplayColor; (void)DebugMessage; (void)bNewerOnTop; (void)TextScale; }
	const TArray<FWorldContext>& GetWorldContexts() const { static TArray<FWorldContext> C; return C; }
	FWorldContext& CreateNewWorldContext(EWorldType::Type WorldType) { static FWorldContext C; (void)WorldType; return C; }
	void DestroyWorldContext(UWorld* InWorld) { (void)InWorld; }
};
extern UEngine* GEngine;

class ULocalPlayer : public UObject
{
public:
	template <typename T>
	static T* GetSubsystem(const ULocalPlayer* LocalPlayer) { (void)LocalPlayer; return nullptr; }
	template <typename T>
	T* GetSubsystem() const { return nullptr; }
};
class UGameInstance : public UObject
{
};

// ------------------------------------------------------------------ data assets / settings / save
class UDataAsset : public UObject
{
};
class UPrimaryDataAsset : public UDataAsset
{
public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return {}; }
};
class UDeveloperSettings : public UObject
{
public:
	virtual FName GetContainerName() const { return NAME_None; }
	virtual FName GetCategoryName() const { return NAME_None; }
	virtual FName GetSectionName() const { return NAME_None; }
};
class USaveGame : public UObject
{
};
class UBlueprintFunctionLibrary : public UObject
{
};

// ------------------------------------------------------------------ asset manager / streaming
struct FStreamableHandle
{
	bool HasLoadCompleted() const { return true; }
	bool IsActive() const { return false; }
	void CancelHandle() {}
	UObject* GetLoadedAsset() const { return nullptr; }
};
using FStreamableDelegate = TStubDelegate<void()>;
class FStreamableManager
{
public:
	TSharedPtr<FStreamableHandle> RequestAsyncLoad(TArray<FSoftObjectPath> TargetsToStream, FStreamableDelegate DelegateToCall = FStreamableDelegate(), int32 Priority = 0, bool bManageActiveHandle = false, bool bStartStalled = false, FString DebugName = FString()) { (void)TargetsToStream; (void)DelegateToCall; (void)Priority; (void)bManageActiveHandle; (void)bStartStalled; (void)DebugName; return {}; }
	TSharedPtr<FStreamableHandle> RequestSyncLoad(TArray<FSoftObjectPath> TargetsToStream, bool bManageActiveHandle = false, FString DebugName = FString()) { (void)TargetsToStream; (void)bManageActiveHandle; (void)DebugName; return {}; }
};
class UAssetManager : public UObject
{
public:
	static UAssetManager& Get();
	bool GetPrimaryAssetIdList(FPrimaryAssetType PrimaryAssetType, TArray<FPrimaryAssetId>& PrimaryAssetIdList, uint32 Filter = 0) const { (void)PrimaryAssetType; (void)PrimaryAssetIdList; (void)Filter; return true; }
	TSharedPtr<FStreamableHandle> LoadPrimaryAssets(const TArray<FPrimaryAssetId>& AssetsToLoad, const TArray<FName>& LoadBundles = TArray<FName>(), FStreamableDelegate DelegateToCall = FStreamableDelegate(), int32 Priority = 0) { (void)AssetsToLoad; (void)LoadBundles; (void)DelegateToCall; (void)Priority; return {}; }
	template <typename T>
	T* GetPrimaryAssetObject(const FPrimaryAssetId& Id) const { (void)Id; return nullptr; }
	FStreamableManager& GetStreamableManager() { static FStreamableManager M; return M; }
};

// ------------------------------------------------------------------ gameplay statics / rendering helpers
class USoundAttenuation;
class UGameplayStatics : public UBlueprintFunctionLibrary
{
public:
	static UAudioComponent* SpawnSoundAtLocation(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, FRotator Rotation = FRotator::ZeroRotator, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, float StartTime = 0.f, USoundAttenuation* AttenuationSettings = nullptr, USoundConcurrency* ConcurrencySettings = nullptr, bool bAutoDestroy = true) { (void)WorldContextObject; (void)Sound; (void)Location; (void)Rotation; (void)VolumeMultiplier; (void)PitchMultiplier; (void)StartTime; (void)AttenuationSettings; (void)ConcurrencySettings; (void)bAutoDestroy; return nullptr; }
	static UAudioComponent* SpawnSoundAttached(USoundBase* Sound, USceneComponent* AttachToComponent, FName AttachPointName = NAME_None, FVector Location = FVector(ForceInit), EAttachLocation::Type LocationType = EAttachLocation::KeepRelativeOffset, bool bStopWhenAttachedToDestroyed = false, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, float StartTime = 0.f, USoundAttenuation* AttenuationSettings = nullptr, USoundConcurrency* ConcurrencySettings = nullptr, bool bAutoDestroy = true) { (void)Sound; (void)AttachToComponent; (void)AttachPointName; (void)Location; (void)LocationType; (void)bStopWhenAttachedToDestroyed; (void)VolumeMultiplier; (void)PitchMultiplier; (void)StartTime; (void)AttenuationSettings; (void)ConcurrencySettings; (void)bAutoDestroy; return nullptr; }
	static UAudioComponent* SpawnSound2D(const UObject* WorldContextObject, USoundBase* Sound, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, float StartTime = 0.f, USoundConcurrency* ConcurrencySettings = nullptr, bool bPersistAcrossLevelTransition = false, bool bAutoDestroy = true) { (void)WorldContextObject; (void)Sound; (void)VolumeMultiplier; (void)PitchMultiplier; (void)StartTime; (void)ConcurrencySettings; (void)bPersistAcrossLevelTransition; (void)bAutoDestroy; return nullptr; }
	static void PlaySound2D(const UObject* WorldContextObject, USoundBase* Sound, float VolumeMultiplier = 1.f) { (void)WorldContextObject; (void)Sound; (void)VolumeMultiplier; }
	static APlayerController* GetPlayerController(const UObject* WorldContextObject, int32 PlayerIndex) { (void)WorldContextObject; (void)PlayerIndex; return nullptr; }
	static APawn* GetPlayerPawn(const UObject* WorldContextObject, int32 PlayerIndex) { (void)WorldContextObject; (void)PlayerIndex; return nullptr; }
	static USaveGame* CreateSaveGameObject(TSubclassOf<USaveGame> SaveGameClass) { (void)SaveGameClass; return nullptr; }
	static bool SaveGameToSlot(USaveGame* SaveGameObject, const FString& SlotName, const int32 UserIndex) { (void)SaveGameObject; (void)SlotName; (void)UserIndex; return true; }
	static USaveGame* LoadGameFromSlot(const FString& SlotName, const int32 UserIndex) { (void)SlotName; (void)UserIndex; return nullptr; }
	static bool DoesSaveGameExist(const FString& SlotName, const int32 UserIndex) { (void)SlotName; (void)UserIndex; return false; }
	static void SetGamePaused(const UObject* WorldContextObject, bool bPaused) { (void)WorldContextObject; (void)bPaused; }
	static void SetGlobalTimeDilation(const UObject* WorldContextObject, float TimeDilation) { (void)WorldContextObject; (void)TimeDilation; }
};
class UKismetRenderingLibrary : public UBlueprintFunctionLibrary
{
public:
	static void BeginDrawCanvasToRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, UCanvas*& Canvas, FVector2D& Size, FDrawToRenderTargetContext& Context) { (void)WorldContextObject; (void)TextureRenderTarget; (void)Canvas; (void)Size; (void)Context; }
	static void EndDrawCanvasToRenderTarget(UObject* WorldContextObject, const FDrawToRenderTargetContext& Context) { (void)WorldContextObject; (void)Context; }
	static void ClearRenderTarget2D(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, FLinearColor ClearColor = FLinearColor(0, 0, 0, 1)) { (void)WorldContextObject; (void)TextureRenderTarget; (void)ClearColor; }
};
class UKismetSystemLibrary : public UBlueprintFunctionLibrary
{
public:
	static void ExecuteConsoleCommand(const UObject* WorldContextObject, const FString& Command, APlayerController* SpecificPlayer = nullptr) { (void)WorldContextObject; (void)Command; (void)SpecificPlayer; }
};

inline void DrawDebugPoint(const UWorld* InWorld, FVector const& Position, float Size, FColor const& PointColor, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0) { (void)InWorld; (void)Position; (void)Size; (void)PointColor; (void)bPersistentLines; (void)LifeTime; (void)DepthPriority; }
inline void DrawDebugString(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, AActor* TestBaseActor = nullptr, FColor const& TextColor = FColor::White, float Duration = -1.f, bool bDrawShadow = false, float FontScale = 1.f) { (void)InWorld; (void)TextLocation; (void)Text; (void)TestBaseActor; (void)TextColor; (void)Duration; (void)bDrawShadow; (void)FontScale; }
inline void DrawDebugLine(const UWorld* InWorld, FVector const& LineStart, FVector const& LineEnd, FColor const& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f) { (void)InWorld; (void)LineStart; (void)LineEnd; (void)Color; (void)bPersistentLines; (void)LifeTime; (void)DepthPriority; (void)Thickness; }
inline void DrawDebugSphere(const UWorld* InWorld, FVector const& Center, float Radius, int32 Segments, FColor const& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f) { (void)InWorld; (void)Center; (void)Radius; (void)Segments; (void)Color; (void)bPersistentLines; (void)LifeTime; (void)DepthPriority; (void)Thickness; }

class FScreenshotRequest
{
public:
	static void RequestScreenshot(const FString& InFilename, bool bInShowUI, bool bAddFilenameSuffix, bool bHdrScreenshot = false) { (void)InFilename; (void)bInShowUI; (void)bAddFilenameSuffix; (void)bHdrScreenshot; }
};

// ------------------------------------------------------------------ animation
struct FAnimInstanceProxy
{
};
class UAnimInstance : public UObject
{
public:
	UAnimInstance() = default;
	UAnimInstance(const FObjectInitializer& Init) : UObject(Init) {}
	TEnumAsByte<ERootMotionMode::Type> RootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) { (void)DeltaSeconds; }
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) { (void)DeltaSeconds; }
	virtual void NativeBeginPlay() {}
	USkeletalMeshComponent* GetSkelMeshComponent() const { return nullptr; }
	AActor* GetOwningActor() const { return nullptr; }
	float Montage_Play(class UAnimMontage* Montage, float PlayRate = 1.f) { (void)Montage; (void)PlayRate; return 0.f; }
};

// UObject system, delegates, soft references, modules, console, files, tasks (stand-in, see ue_core.h).
#pragma once

#include "ue_core.h"

// ------------------------------------------------------------------ reflection macros (UHT)
#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UINTERFACE(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UMETA(...)
#define UPARAM(...)
#define GENERATED_UCLASS_BODY()
#define NOCTIS_STUB_GENERATED_CLASS(ThisType, SuperType) \
public: \
	using Super = SuperType; \
	using ThisClass = ThisType; \
	static UClass* StaticClass(); \
private:
#define NOCTIS_STUB_GENERATED_STRUCT(ThisType) \
public: \
	static class UScriptStruct* StaticStruct();
#define GENERATED_BODY() static_assert(false, "GENERATED_BODY not rewritten by the stub preprocessor");

class UClass;
class UObject;
class UWorld;
class UPackage;
class UScriptStruct;

enum EObjectFlags : uint32 { RF_NoFlags = 0, RF_Public = 1, RF_Standalone = 2, RF_Transactional = 8, RF_Transient = 64 };
inline EObjectFlags operator|(EObjectFlags A, EObjectFlags B) { return EObjectFlags(uint32(A) | uint32(B)); }
inline EObjectFlags& operator|=(EObjectFlags& A, EObjectFlags B) { A = A | B; return A; }
enum ELoadFlags : uint32 { LOAD_None = 0, LOAD_NoWarn = 0x2, LOAD_Quiet = 0x2000 };
inline ELoadFlags operator|(ELoadFlags A, ELoadFlags B) { return ELoadFlags(uint32(A) | uint32(B)); }

namespace EGetWorldErrorMode { enum Type { ReturnNull, LogAndReturnNull, Assert }; }
namespace EWorldType { enum Type { None, Game, Editor, PIE, EditorPreview, GamePreview, GameRPC, Inactive }; }
namespace EEndPlayReason { enum Type { Destroyed, LevelTransition, EndPlayInEditor, RemovedFromWorld, Quit }; }
enum class EDataValidationResult : uint8 { Invalid, Valid, NotValidated };

struct FTopLevelAssetPath
{
	FTopLevelAssetPath() = default;
	FTopLevelAssetPath(const TCHAR* Path) { (void)Path; }
};

class FObjectInitializer
{
public:
	template <typename T>
	FObjectInitializer& SetDefaultSubobjectClass(FName Name) { (void)Name; return *this; }
	FObjectInitializer& DoNotCreateDefaultSubobject(FName Name) { (void)Name; return *this; }
	static FObjectInitializer& Get();
};

class FDataValidationContext
{
public:
	void AddError(const FText& Text) { (void)Text; }
	void AddWarning(const FText& Text) { (void)Text; }
	void AddMessage(const FText& Text) { (void)Text; }
};

class UObject
{
public:
	UObject() = default;
	UObject(const FObjectInitializer& ObjectInitializer) { (void)ObjectInitializer; }
	virtual ~UObject() = default;
	static UClass* StaticClass();
	UClass* GetClass() const;
	virtual UWorld* GetWorld() const;
	FString GetName() const { return FString(); }
	FName GetFName() const { return FName(); }
	FString GetPathName() const { return FString(); }
	UObject* GetOuter() const { return nullptr; }
	UPackage* GetPackage() const { return nullptr; }
	bool MarkPackageDirty() const { return true; }
	template <typename T>
	bool IsA() const { return true; }
	bool IsA(const UClass* Class) const { (void)Class; return true; }
	bool IsRooted() const { return false; }
	void AddToRoot() {}
	void RemoveFromRoot() {}
	bool IsValidLowLevel() const { return true; }
	bool HasAnyFlags(EObjectFlags Flags) const { (void)Flags; return false; }
	virtual void PostInitProperties() {}
	virtual void PostLoad() {}
	virtual void BeginDestroy() {}
	virtual void Serialize(class FArchive& Ar) { (void)Ar; }
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const { (void)Context; return EDataValidationResult::NotValidated; }
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& Event) { (void)Event; }
#endif
	void SaveConfig() {}
	void ReloadConfig() {}
	void TryUpdateDefaultConfigFile() {}
	bool UpdateDefaultConfigFile() { return true; }
	template <typename T>
	T* CreateDefaultSubobject(FName Name, bool bTransient = false) { (void)Name; (void)bTransient; return nullptr; }
	virtual struct FPrimaryAssetId GetPrimaryAssetId() const;
};

class UField : public UObject
{
};
class UStruct : public UField
{
public:
	bool IsChildOf(const UStruct* Other) const { (void)Other; return true; }
};
class UScriptStruct : public UStruct
{
};
class UClass : public UStruct
{
public:
	UObject* GetDefaultObject(bool bCreateIfNeeded = true) const { (void)bCreateIfNeeded; return nullptr; }
	template <typename T>
	T* GetDefaultObject() const { return nullptr; }
	FTopLevelAssetPath GetClassPathName() const { return {}; }
	bool HasAnyClassFlags(uint32 Flags) const { (void)Flags; return false; }
};
class UPackage : public UObject
{
};

template <typename T>
T* Cast(UObject* Obj) { (void)Obj; return nullptr; }
template <typename T>
const T* Cast(const UObject* Obj) { (void)Obj; return nullptr; }
template <typename T>
T* CastChecked(UObject* Obj) { (void)Obj; return nullptr; }
template <typename T>
const T* CastChecked(const UObject* Obj) { (void)Obj; return nullptr; }
template <typename T>
T* ExactCast(UObject* Obj) { (void)Obj; return nullptr; }
inline bool IsValid(const UObject* Obj) { return Obj != nullptr; }
inline FString GetNameSafe(const UObject* Obj) { (void)Obj; return FString(); }
template <typename T>
T* NewObject(UObject* Outer = nullptr, UClass* Class = nullptr, FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags) { (void)Outer; (void)Class; (void)Name; (void)Flags; return nullptr; }
template <typename T>
T* NewObject(UObject* Outer, FName Name, EObjectFlags Flags = RF_NoFlags) { (void)Outer; (void)Name; (void)Flags; return nullptr; }
template <typename T>
const T* GetDefault() { return nullptr; }
template <typename T>
T* GetMutableDefault() { return nullptr; }
template <typename T>
T* LoadObject(UObject* Outer, const TCHAR* Name, const TCHAR* Filename = nullptr, uint32 LoadFlags = LOAD_None, UPackage* Sandbox = nullptr) { (void)Outer; (void)Name; (void)Filename; (void)LoadFlags; (void)Sandbox; return nullptr; }
template <typename T>
T* FindObject(UObject* Outer, const TCHAR* Name, bool bExact = false) { (void)Outer; (void)Name; (void)bExact; return nullptr; }

// TObjectPtr: implicit conversion to T*, like UE.
template <typename T>
class TObjectPtr
{
public:
	TObjectPtr() = default;
	TObjectPtr(std::nullptr_t) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TObjectPtr(U* P) : Ptr(P) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TObjectPtr(const TObjectPtr<U>& O) : Ptr(O.Get()) {}
	T* Get() const { return Ptr; }
	operator T*() const { return Ptr; }
	T* operator->() const { return Ptr; }
	T& operator*() const { return *Ptr; }
	explicit operator bool() const { return Ptr != nullptr; }
	bool operator!() const { return Ptr == nullptr; }
	bool operator==(std::nullptr_t) const { return Ptr == nullptr; }
	bool operator!=(std::nullptr_t) const { return Ptr != nullptr; }
	template <typename U>
	bool operator==(const TObjectPtr<U>& O) const { return Ptr == O.Get(); }

private:
	T* Ptr = nullptr;
};
template <typename T>
uint32 GetTypeHash(const TObjectPtr<T>&) { return 0; }

template <typename T>
class TWeakObjectPtr
{
public:
	TWeakObjectPtr() = default;
	TWeakObjectPtr(std::nullptr_t) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TWeakObjectPtr(U* P) : Ptr(P) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TWeakObjectPtr(const TObjectPtr<U>& P) : Ptr(P.Get()) {}
	T* Get() const { return Ptr; }
	T* operator->() const { return Ptr; }
	T& operator*() const { return *Ptr; }
	bool IsValid() const { return Ptr != nullptr; }
	bool IsStale() const { return false; }
	explicit operator bool() const { return IsValid(); }
	void Reset() { Ptr = nullptr; }
	bool operator==(const TWeakObjectPtr& O) const { return Ptr == O.Ptr; }
	bool operator==(const T* O) const { return Ptr == O; }

private:
	T* Ptr = nullptr;
};

template <typename T = UObject>
class TSubclassOf
{
public:
	TSubclassOf() = default;
	TSubclassOf(std::nullptr_t) {}
	TSubclassOf(UClass* C) : Class(C) {}
	UClass* Get() const { return Class; }
	operator UClass*() const { return Class; }
	UClass* operator*() const { return Class; }
	UClass* operator->() const { return Class; }
	T* GetDefaultObject() const { return nullptr; }

private:
	UClass* Class = nullptr;
};

struct FSoftObjectPath
{
	FSoftObjectPath() = default;
	FSoftObjectPath(const TCHAR* Path) { (void)Path; }
	explicit FSoftObjectPath(const FString& Path) { (void)Path; }
	FSoftObjectPath(const UObject* Object) { (void)Object; }
	bool IsValid() const { return true; }
	bool IsNull() const { return false; }
	FString ToString() const { return FString(); }
	FString GetAssetName() const { return FString(); }
	UObject* TryLoad() const { return nullptr; }
	UObject* ResolveObject() const { return nullptr; }
	bool operator==(const FSoftObjectPath& O) const { (void)O; return true; }
};
using FSoftClassPath = FSoftObjectPath;

template <typename T = UObject>
class TSoftObjectPtr
{
public:
	TSoftObjectPtr() = default;
	TSoftObjectPtr(std::nullptr_t) {}
	explicit TSoftObjectPtr(const FSoftObjectPath& Path) { (void)Path; }
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSoftObjectPtr(U* Object) { (void)Object; }
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSoftObjectPtr(const TObjectPtr<U>& Object) { (void)Object; }
	T* Get() const { return nullptr; }
	T* LoadSynchronous() const { return nullptr; }
	bool IsNull() const { return true; }
	bool IsValid() const { return false; }
	bool IsPending() const { return false; }
	explicit operator bool() const { return IsValid(); }
	FSoftObjectPath ToSoftObjectPath() const { return {}; }
	FString ToString() const { return FString(); }
	FString GetAssetName() const { return FString(); }
	void Reset() {}
	bool operator==(const TSoftObjectPtr& O) const { (void)O; return true; }
};

template <typename T = UObject>
class TSoftClassPtr
{
public:
	TSoftClassPtr() = default;
	explicit TSoftClassPtr(const FSoftObjectPath& Path) { (void)Path; }
	TSoftClassPtr(UClass* Class) { (void)Class; }
	UClass* Get() const { return nullptr; }
	UClass* LoadSynchronous() const { return nullptr; }
	bool IsNull() const { return true; }
	bool IsValid() const { return false; }
	explicit operator bool() const { return IsValid(); }
	FSoftObjectPath ToSoftObjectPath() const { return {}; }
};

struct FPrimaryAssetType
{
	FPrimaryAssetType() = default;
	FPrimaryAssetType(FName Name) { (void)Name; }
	FPrimaryAssetType(const TCHAR* Name) { (void)Name; }
	bool operator==(const FPrimaryAssetType& O) const { (void)O; return true; }
};
struct FPrimaryAssetId
{
	FPrimaryAssetId() = default;
	FPrimaryAssetId(FPrimaryAssetType Type, FName Name) { (void)Type; (void)Name; }
	FPrimaryAssetType PrimaryAssetType;
	FName PrimaryAssetName;
	bool IsValid() const { return true; }
	bool operator==(const FPrimaryAssetId& O) const { (void)O; return true; }
};
inline FPrimaryAssetId UObject::GetPrimaryAssetId() const
{
	return {};
}

// ------------------------------------------------------------------ delegates
struct FDelegateHandle
{
	bool IsValid() const { return true; }
	void Reset() {}
	bool operator==(const FDelegateHandle& O) const { (void)O; return true; }
};

template <typename Sig>
class TStubDelegate;
template <typename R, typename... A>
class TStubDelegate<R(A...)>
{
public:
	TStubDelegate() = default;
	static TStubDelegate CreateStatic(R (*Fn)(A...)) { (void)Fn; return {}; }
	template <typename U>
	static TStubDelegate CreateUObject(U* Obj, R (U::*Fn)(A...)) { (void)Obj; (void)Fn; return {}; }
	template <typename U>
	static TStubDelegate CreateUObject(const U* Obj, R (U::*Fn)(A...) const) { (void)Obj; (void)Fn; return {}; }
	template <typename U>
	static TStubDelegate CreateRaw(U* Obj, R (U::*Fn)(A...)) { (void)Obj; (void)Fn; return {}; }
	template <typename U>
	static TStubDelegate CreateSP(U* Obj, R (U::*Fn)(A...)) { (void)Obj; (void)Fn; return {}; }
	template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<R, F&, A...>>>
	static TStubDelegate CreateLambda(F&& Fn) { (void)Fn; return {}; }
	template <typename U>
	void BindUObject(U* Obj, R (U::*Fn)(A...)) { (void)Obj; (void)Fn; }
	template <typename F>
	void BindLambda(F&& Fn) { (void)Fn; }
	bool IsBound() const { return false; }
	bool ExecuteIfBound(A... Args) const { ((void)Args, ...); return false; }
	R Execute(A... Args) const { ((void)Args, ...); return R(); }
	void Unbind() {}
};

template <typename Sig>
class TStubMulticastDelegate;
template <typename... A>
class TStubMulticastDelegate<void(A...)>
{
public:
	using FDelegate = TStubDelegate<void(A...)>;
	template <typename U>
	FDelegateHandle AddUObject(U* Obj, void (U::*Fn)(A...)) { (void)Obj; (void)Fn; return {}; }
	template <typename U>
	FDelegateHandle AddUObject(const U* Obj, void (U::*Fn)(A...) const) { (void)Obj; (void)Fn; return {}; }
	template <typename U>
	FDelegateHandle AddRaw(U* Obj, void (U::*Fn)(A...)) { (void)Obj; (void)Fn; return {}; }
	template <typename F, typename = std::enable_if_t<std::is_invocable_v<F&, A...>>>
	FDelegateHandle AddLambda(F&& Fn) { (void)Fn; return {}; }
	FDelegateHandle AddStatic(void (*Fn)(A...)) { (void)Fn; return {}; }
	FDelegateHandle Add(const FDelegate& D) { (void)D; return {}; }
	bool Remove(FDelegateHandle Handle) { (void)Handle; return true; }
	void RemoveAll(const void* Obj) { (void)Obj; }
	void Clear() {}
	bool IsBound() const { return false; }
	void Broadcast(A... Args) const { ((void)Args, ...); }
};

template <typename Sig>
class TStubDynamicDelegate;
template <typename R, typename... A>
class TStubDynamicDelegate<R(A...)>
{
public:
	template <typename U>
	void BindDynamicStub(U* Obj, R (U::*Fn)(A...)) { (void)Obj; (void)Fn; }
	bool IsBound() const { return false; }
	void ExecuteIfBound(A... Args) const { ((void)Args, ...); }
	R Execute(A... Args) const { ((void)Args, ...); return R(); }
	void Unbind() {}
};
template <typename Sig>
class TStubDynamicMulticastDelegate;
template <typename... A>
class TStubDynamicMulticastDelegate<void(A...)>
{
public:
	template <typename U>
	void AddDynamicStub(U* Obj, void (U::*Fn)(A...)) { (void)Obj; (void)Fn; }
	template <typename U>
	void RemoveDynamicStub(U* Obj, void (U::*Fn)(A...)) { (void)Obj; (void)Fn; }
	template <typename U>
	void AddUniqueDynamicStub(U* Obj, void (U::*Fn)(A...)) { (void)Obj; (void)Fn; }
	void Broadcast(A... Args) const { ((void)Args, ...); }
	bool IsBound() const { return false; }
	void Clear() {}
};
#define AddDynamic(Obj, Fn) AddDynamicStub(Obj, Fn)
#define AddUniqueDynamic(Obj, Fn) AddUniqueDynamicStub(Obj, Fn)
#define RemoveDynamic(Obj, Fn) RemoveDynamicStub(Obj, Fn)
#define BindDynamic(Obj, Fn) BindDynamicStub(Obj, Fn)

#define DECLARE_DELEGATE(Name) using Name = TStubDelegate<void()>;
#define DECLARE_DELEGATE_OneParam(Name, T1) using Name = TStubDelegate<void(T1)>;
#define DECLARE_DELEGATE_TwoParams(Name, T1, T2) using Name = TStubDelegate<void(T1, T2)>;
#define DECLARE_DELEGATE_RetVal(R, Name) using Name = TStubDelegate<R()>;
#define DECLARE_MULTICAST_DELEGATE(Name) using Name = TStubMulticastDelegate<void()>;
#define DECLARE_MULTICAST_DELEGATE_OneParam(Name, T1) using Name = TStubMulticastDelegate<void(T1)>;
#define DECLARE_MULTICAST_DELEGATE_TwoParams(Name, T1, T2) using Name = TStubMulticastDelegate<void(T1, T2)>;
#define DECLARE_MULTICAST_DELEGATE_ThreeParams(Name, T1, T2, T3) using Name = TStubMulticastDelegate<void(T1, T2, T3)>;
#define DECLARE_DYNAMIC_DELEGATE(Name) class Name : public TStubDynamicDelegate<void()> {};
#define DECLARE_DYNAMIC_DELEGATE_OneParam(Name, T1, N1) class Name : public TStubDynamicDelegate<void(T1)> {};
#define DECLARE_DYNAMIC_DELEGATE_TwoParams(Name, T1, N1, T2, N2) class Name : public TStubDynamicDelegate<void(T1, T2)> {};
#define DECLARE_DYNAMIC_MULTICAST_DELEGATE(Name) class Name : public TStubDynamicMulticastDelegate<void()> {};
#define DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(Name, T1, N1) class Name : public TStubDynamicMulticastDelegate<void(T1)> {};
#define DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(Name, T1, N1, T2, N2) class Name : public TStubDynamicMulticastDelegate<void(T1, T2)> {};
#define DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(Name, T1, N1, T2, N2, T3, N3) class Name : public TStubDynamicMulticastDelegate<void(T1, T2, T3)> {};

DECLARE_DELEGATE(FSimpleDelegate)
DECLARE_MULTICAST_DELEGATE(FSimpleMulticastDelegate)

// ------------------------------------------------------------------ modules
class IModuleInterface
{
public:
	virtual ~IModuleInterface() = default;
	virtual void StartupModule() {}
	virtual void ShutdownModule() {}
	virtual bool IsGameModule() const { return false; }
};
class FDefaultModuleImpl : public IModuleInterface
{
};
class FDefaultGameModuleImpl : public FDefaultModuleImpl
{
public:
	virtual bool IsGameModule() const override { return true; }
};
class FModuleManager
{
public:
	static FModuleManager& Get();
	bool IsModuleLoaded(FName Name) const { (void)Name; return true; }
	template <typename T>
	static T& LoadModuleChecked(FName Name);
	template <typename T>
	static T& GetModuleChecked(FName Name);
	template <typename T>
	static T* LoadModulePtr(FName Name) { (void)Name; return nullptr; }
};
#define IMPLEMENT_MODULE(ModuleClass, ModuleName) \
	static_assert(std::is_base_of_v<IModuleInterface, ModuleClass>, "module class"); \
	IModuleInterface* InitializeModule_##ModuleName() { return new ModuleClass(); }
#define IMPLEMENT_PRIMARY_GAME_MODULE(ModuleClass, ModuleName, GameName) IMPLEMENT_MODULE(ModuleClass, ModuleName)
#define IMPLEMENT_GAME_MODULE(ModuleClass, ModuleName) IMPLEMENT_MODULE(ModuleClass, ModuleName)

// ------------------------------------------------------------------ console
enum EConsoleVariableFlags : uint32 { ECVF_Default = 0, ECVF_Cheat = 1, ECVF_ReadOnly = 4, ECVF_Scalability = 0x40, ECVF_RenderThreadSafe = 0x20 };
inline EConsoleVariableFlags operator|(EConsoleVariableFlags A, EConsoleVariableFlags B) { return EConsoleVariableFlags(uint32(A) | uint32(B)); }
template <typename T>
class TAutoConsoleVariable
{
public:
	TAutoConsoleVariable(const TCHAR* Name, const T& Default, const TCHAR* Help, uint32 Flags = ECVF_Default) { (void)Name; (void)Default; (void)Help; (void)Flags; }
	T GetValueOnGameThread() const { return T(); }
	T GetValueOnAnyThread() const { return T(); }
	T GetValueOnRenderThread() const { return T(); }
};
using FConsoleCommandDelegate = TStubDelegate<void()>;
using FConsoleCommandWithArgsDelegate = TStubDelegate<void(const TArray<FString>&)>;
using FConsoleCommandWithWorldDelegate = TStubDelegate<void(UWorld*)>;
using FConsoleCommandWithWorldAndArgsDelegate = TStubDelegate<void(const TArray<FString>&, UWorld*)>;
struct FAutoConsoleCommand
{
	FAutoConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandDelegate& Command, uint32 Flags = ECVF_Default) { (void)Name; (void)Help; (void)Command; (void)Flags; }
	FAutoConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsDelegate& Command, uint32 Flags = ECVF_Default) { (void)Name; (void)Help; (void)Command; (void)Flags; }
};
struct FAutoConsoleCommandWithWorldAndArgs
{
	FAutoConsoleCommandWithWorldAndArgs(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldAndArgsDelegate& Command, uint32 Flags = ECVF_Default) { (void)Name; (void)Help; (void)Command; (void)Flags; }
};
class IConsoleVariable
{
public:
	virtual ~IConsoleVariable() = default;
	virtual int32 GetInt() const = 0;
	virtual float GetFloat() const = 0;
	virtual void Set(const TCHAR* Value, uint32 SetBy = 0) = 0;
};
class IConsoleManager
{
public:
	static IConsoleManager& Get();
	virtual ~IConsoleManager() = default;
	virtual IConsoleVariable* FindConsoleVariable(const TCHAR* Name, bool bTrackFrequentCalls = true) const = 0;
};

// ------------------------------------------------------------------ platform / files / paths
struct FPlatformTime
{
	static double Seconds() { return 0.0; }
	static uint64 Cycles64() { return 0; }
};
struct FPlatformProcess
{
	static void Sleep(float Seconds) { (void)Seconds; }
};
struct FPlatformMisc
{
	static int32 NumberOfCores() { return 4; }
	static int32 NumberOfCoresIncludingHyperthreads() { return 8; }
};
bool IsInGameThread();

enum EFileRead : uint32 { FILEREAD_None = 0, FILEREAD_NoFail = 1, FILEREAD_Silent = 2 };
enum EFileWrite : uint32 { FILEWRITE_None = 0 };
class IFileManager
{
public:
	static IFileManager& Get();
	virtual ~IFileManager() = default;
	bool MakeDirectory(const TCHAR* Path, bool Tree = false) { (void)Path; (void)Tree; return true; }
	bool DirectoryExists(const TCHAR* Path) { (void)Path; return true; }
	bool FileExists(const TCHAR* Path) { (void)Path; return true; }
	bool DeleteDirectory(const TCHAR* Path, bool RequireExists = false, bool Tree = false) { (void)Path; (void)RequireExists; (void)Tree; return true; }
	bool Delete(const TCHAR* Path, bool RequireExists = false, bool EvenReadOnly = false, bool Quiet = false) { (void)Path; (void)RequireExists; (void)EvenReadOnly; (void)Quiet; return true; }
	void FindFiles(TArray<FString>& FileNames, const TCHAR* Directory, const TCHAR* FileExtension) { (void)FileNames; (void)Directory; (void)FileExtension; }
	void FindFilesRecursive(TArray<FString>& FileNames, const TCHAR* StartDirectory, const TCHAR* Filename, bool Files, bool Directories, bool bClearFileNames = true) { (void)FileNames; (void)StartDirectory; (void)Filename; (void)Files; (void)Directories; (void)bClearFileNames; }
	int64 FileSize(const TCHAR* Path) { (void)Path; return 0; }
	FDateTime GetTimeStamp(const TCHAR* Path) { (void)Path; return {}; }
};
struct FFileHelper
{
	static bool LoadFileToArray(TArray<uint8>& Result, const TCHAR* Filename, uint32 Flags = 0) { (void)Result; (void)Filename; (void)Flags; return true; }
	static bool LoadFileToString(FString& Result, const TCHAR* Filename, uint32 VerifyFlags = 0, uint32 ReadFlags = 0) { (void)Result; (void)Filename; (void)VerifyFlags; (void)ReadFlags; return true; }
	static bool SaveArrayToFile(TArrayView<const uint8> Array, const TCHAR* Filename, class IFileManager* FileManager = nullptr, uint32 WriteFlags = 0) { (void)Array; (void)Filename; (void)FileManager; (void)WriteFlags; return true; }
	static bool SaveStringToFile(const FString& String, const TCHAR* Filename) { (void)String; (void)Filename; return true; }
};
struct FPaths
{
	static FString ProjectDir() { return FString(); }
	static FString ProjectContentDir() { return FString(); }
	static FString ProjectSavedDir() { return FString(); }
	static FString ProjectConfigDir() { return FString(); }
	static FString EngineDir() { return FString(); }
	static FString GetPath(const FString& Path) { return Path; }
	static FString GetCleanFilename(const FString& Path) { return Path; }
	static FString GetBaseFilename(const FString& Path, bool bRemovePath = true) { (void)bRemovePath; return Path; }
	static FString GetExtension(const FString& Path, bool bIncludeDot = false) { (void)bIncludeDot; return Path; }
	static bool IsRelative(const FString& Path) { (void)Path; return false; }
	static FString ConvertRelativePathToFull(const FString& Path) { return Path; }
	static void NormalizeFilename(FString& Path) { (void)Path; }
	static void NormalizeDirectoryName(FString& Path) { (void)Path; }
	static bool FileExists(const FString& Path) { (void)Path; return true; }
	static bool DirectoryExists(const FString& Path) { (void)Path; return true; }
	template <typename... T>
	static FString Combine(T&&... Parts) { static_assert(sizeof...(Parts) >= 2, "Combine needs 2+ parts"); ((void)FString(Parts), ...); return FString(); }
};
struct FParse
{
	static bool Param(const TCHAR* Stream, const TCHAR* Param) { (void)Stream; (void)Param; return false; }
	static bool Value(const TCHAR* Stream, const TCHAR* Match, FString& Value) { (void)Stream; (void)Match; (void)Value; return false; }
	static bool Value(const TCHAR* Stream, const TCHAR* Match, int32& Value) { (void)Stream; (void)Match; (void)Value; return false; }
};

enum ECompressionFlags : uint32 { COMPRESS_NoFlags = 0 };
struct FCompression
{
	static int32 CompressMemoryBound(FName FormatName, int32 UncompressedSize, ECompressionFlags Flags = COMPRESS_NoFlags, int32 CompressionData = 0) { (void)FormatName; (void)Flags; (void)CompressionData; return UncompressedSize; }
	static bool CompressMemory(FName FormatName, void* CompressedBuffer, int32& CompressedSize, const void* UncompressedBuffer, int32 UncompressedSize, ECompressionFlags Flags = COMPRESS_NoFlags, int32 CompressionData = 0) { (void)FormatName; (void)CompressedBuffer; (void)CompressedSize; (void)UncompressedBuffer; (void)UncompressedSize; (void)Flags; (void)CompressionData; return true; }
	static bool UncompressMemory(FName FormatName, void* UncompressedBuffer, int32 UncompressedSize, const void* CompressedBuffer, int32 CompressedSize, ECompressionFlags Flags = COMPRESS_NoFlags, int32 CompressionData = 0) { (void)FormatName; (void)UncompressedBuffer; (void)UncompressedSize; (void)CompressedBuffer; (void)CompressedSize; (void)Flags; (void)CompressionData; return true; }
};

// ------------------------------------------------------------------ tasks
namespace UE::Tasks
{
class FTask
{
public:
	bool IsCompleted() const { return true; }
	bool IsValid() const { return true; }
	bool Wait() const { return true; }
	bool Wait(double TimeoutSeconds) const { (void)TimeoutSeconds; return true; }
};
template <typename R>
class TTask : public FTask
{
public:
	std::add_lvalue_reference_t<R> GetResult() requires(!std::is_void_v<R>) { static std::remove_reference_t<R> V; return V; }
};
enum class ETaskPriority : int8 { High, Normal, BackgroundHigh, BackgroundNormal, BackgroundLow, Default = Normal };
template <typename F>
TTask<std::invoke_result_t<std::decay_t<F>>> Launch(const TCHAR* DebugName, F&& Body, ETaskPriority Priority = ETaskPriority::Default)
{
	static_assert(std::is_invocable_v<std::decay_t<F>&>, "UE::Tasks::Launch body must be callable without arguments");
	(void)DebugName;
	(void)Body;
	(void)Priority;
	return {};
}
} // namespace UE::Tasks

enum class EParallelForFlags : uint8 { None = 0, ForceSingleThread = 1, Unbalanced = 2, PumpRenderingThread = 4, BackgroundPriority = 8 };
inline void ParallelFor(int32 Num, TFunctionRef<void(int32)> Body, bool bForceSingleThread, bool bPumpRenderingThread = false) { (void)Num; (void)Body; (void)bForceSingleThread; (void)bPumpRenderingThread; }
inline void ParallelFor(int32 Num, TFunctionRef<void(int32)> Body, EParallelForFlags Flags = EParallelForFlags::None) { (void)Num; (void)Body; (void)Flags; }
inline void ParallelFor(const TCHAR* DebugName, int32 Num, int32 MinBatchSize, TFunctionRef<void(int32)> Body, EParallelForFlags Flags = EParallelForFlags::None) { (void)DebugName; (void)Num; (void)MinBatchSize; (void)Body; (void)Flags; }

class FTaskGraphInterface
{
public:
	static FTaskGraphInterface& Get();
	int32 GetNumWorkerThreads() { return 3; }
};

// ------------------------------------------------------------------ automation
namespace EAutomationTestFlags
{
enum Type : uint32 { EditorContext = 1, ClientContext = 2, ServerContext = 4, CommandletContext = 8, ApplicationContextMask = 15, SmokeFilter = 0x1000000, EngineFilter = 0x2000000, ProductFilter = 0x4000000, PerfFilter = 0x8000000 };
}
inline uint32 operator|(EAutomationTestFlags::Type A, EAutomationTestFlags::Type B) { return uint32(A) | uint32(B); }
inline uint32 operator|(uint32 A, EAutomationTestFlags::Type B) { return A | uint32(B); }
class FAutomationTestBase
{
public:
	FAutomationTestBase(const FString& InName, bool bInComplexTask) { (void)InName; (void)bInComplexTask; }
	virtual ~FAutomationTestBase() = default;
	virtual bool RunTest(const FString& Parameters) = 0;
	void AddError(const FString& Error, int32 StackOffset = 0) { (void)Error; (void)StackOffset; }
	void AddWarning(const FString& Warning, int32 StackOffset = 0) { (void)Warning; (void)StackOffset; }
	void AddInfo(const FString& Info, int32 StackOffset = 0) { (void)Info; (void)StackOffset; }
	bool TestTrue(const TCHAR* What, bool bValue) { (void)What; return bValue; }
	bool TestTrue(const FString& What, bool bValue) { (void)What; return bValue; }
	bool TestFalse(const TCHAR* What, bool bValue) { (void)What; return !bValue; }
	bool TestEqual(const TCHAR* What, int32 Actual, int32 Expected) { (void)What; return Actual == Expected; }
	bool TestEqual(const TCHAR* What, int64 Actual, int64 Expected) { (void)What; return Actual == Expected; }
	bool TestEqual(const TCHAR* What, float Actual, float Expected, float Tolerance = UE_KINDA_SMALL_NUMBER) { (void)What; (void)Tolerance; return Actual == Expected; }
	bool TestEqual(const TCHAR* What, double Actual, double Expected, double Tolerance = UE_KINDA_SMALL_NUMBER) { (void)What; (void)Tolerance; return Actual == Expected; }
	bool TestEqual(const TCHAR* What, const FString& Actual, const FString& Expected) { (void)What; return Actual == Expected; }
	bool TestEqual(const TCHAR* What, const FVector& Actual, const FVector& Expected, float Tolerance = UE_KINDA_SMALL_NUMBER) { (void)What; (void)Tolerance; return Actual == Expected; }
	template <typename T>
	bool TestNotNull(const TCHAR* What, const T* Pointer) { (void)What; return Pointer != nullptr; }
	template <typename T>
	bool TestNull(const TCHAR* What, const T* Pointer) { (void)What; return Pointer == nullptr; }
};
class IAutomationLatentCommand
{
public:
	virtual ~IAutomationLatentCommand() = default;
	virtual bool Update() = 0;
};
#define IMPLEMENT_SIMPLE_AUTOMATION_TEST(TClass, PrettyName, TFlags) \
	static_assert(std::is_convertible_v<decltype(TFlags), uint32>, "test flags"); \
	class TClass : public FAutomationTestBase \
	{ \
	public: \
		TClass(const FString& InName) : FAutomationTestBase(InName, false) {} \
		virtual bool RunTest(const FString& Parameters) override; \
	}; \
	[[maybe_unused]] static const TCHAR* TClass##_Name = TEXT(PrettyName);
#define ADD_LATENT_AUTOMATION_COMMAND(ClassDeclaration) do { IAutomationLatentCommand* Cmd = new ClassDeclaration; (void)Cmd; } while (0)

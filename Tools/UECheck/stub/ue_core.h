// Minimal Unreal Engine 5 API stand-in for SYNTAX / TYPE checking of the Noctis UE layer with clang -fsyntax-only.
// Signatures follow UE 5.4/5.5 as documented; bodies are dummies. Nothing here is meant to run.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ------------------------------------------------------------------ build macros
#ifndef NOCTIS_STUB_SHIPPING
#define NOCTIS_STUB_SHIPPING 0
#endif
#if NOCTIS_STUB_SHIPPING
#define WITH_EDITOR 0
#define WITH_EDITORONLY_DATA 0
#define WITH_DEV_AUTOMATION_TESTS 0
#define ENABLE_DRAW_DEBUG 0
#define WITH_GAMEPLAY_DEBUGGER 0
#define WITH_GAMEPLAY_DEBUGGER_CORE 0
#define WITH_GAMEPLAY_DEBUGGER_MENU 0
#define UE_BUILD_SHIPPING 1
#else
#define WITH_EDITOR 1
#define WITH_EDITORONLY_DATA 1
#define WITH_DEV_AUTOMATION_TESTS 1
#define ENABLE_DRAW_DEBUG 1
#define WITH_GAMEPLAY_DEBUGGER 1
#define WITH_GAMEPLAY_DEBUGGER_CORE 1
#define WITH_GAMEPLAY_DEBUGGER_MENU 1
#define UE_BUILD_SHIPPING 0
#endif
#define DO_CHECK 1
#define DO_GUARD_SLOW 0
#define FORCEINLINE inline
#define DLLEXPORT
#define DLLIMPORT
#define UE_SOURCE_LOCATION TEXT("stub")
#define UE_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

// ------------------------------------------------------------------ basic types
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using TCHAR = wchar_t;
using ANSICHAR = char;
using UTF8CHAR = char8_t;
using SIZE_T = std::size_t;
#define TEXT(x) L##x
#define MAX_int32 (std::numeric_limits<int32>::max())
#define MAX_uint32 (std::numeric_limits<uint32>::max())
#define MAX_uint64 (std::numeric_limits<uint64>::max())
#define MAX_flt (3.402823466e+38F)
enum { INDEX_NONE = -1 };
enum EForceInit { ForceInit, ForceInitToZero };
enum ENoInit { NoInit };

#define UE_PI (3.1415926535897932f)
#define UE_DOUBLE_PI (3.141592653589793238462643383279502884197169399)
#define UE_HALF_PI (1.57079632679f)
#define UE_TWO_PI (6.28318530717f)
#define UE_SMALL_NUMBER (1.e-8f)
#define UE_KINDA_SMALL_NUMBER (1.e-4f)
#define UE_BIG_NUMBER (3.4e+38f)

// ------------------------------------------------------------------ asserts and logs
#define check(expr) ((void)sizeof(!!(expr)))
#define checkf(expr, ...) ((void)sizeof(!!(expr)))
#define checkSlow(expr) ((void)sizeof(!!(expr)))
#define checkNoEntry() ((void)0)
#define verify(expr) ((void)(expr))
#define verifyf(expr, ...) ((void)(expr))
#define ensure(expr) (!!(expr))
#define ensureMsgf(expr, ...) (!!(expr))
#define ensureAlways(expr) (!!(expr))

namespace ELogVerbosity
{
enum Type : uint8 { NoLogging, Fatal, Error, Warning, Display, Log, Verbose, VeryVerbose, All, SetColor };
}

namespace StubFormat
{
// UE's printf-style formatting takes arithmetic values, enums and C strings / pointers. Passing an FString (instead of
// *FString) or a std::string compiles in some UE configurations and prints garbage: reject it here.
template <typename T>
constexpr bool IsFormatArg = std::is_arithmetic_v<std::decay_t<T>> || std::is_enum_v<std::decay_t<T>> || std::is_pointer_v<std::decay_t<T>> ||
							 std::is_null_pointer_v<std::decay_t<T>>;
template <typename... Args>
inline void Check(const TCHAR* Fmt, Args&&...)
{
	(void)Fmt;
	static_assert((IsFormatArg<Args> && ...), "printf-style argument must be arithmetic, enum or pointer (use *FString)");
}
} // namespace StubFormat

struct FLogCategoryBase
{
	FLogCategoryBase(const TCHAR*, ELogVerbosity::Type, ELogVerbosity::Type) {}
};
template <ELogVerbosity::Type Default, ELogVerbosity::Type Compile>
struct FLogCategory : FLogCategoryBase
{
	FLogCategory(const TCHAR* Name) : FLogCategoryBase(Name, Default, Compile) {}
};
#define DECLARE_LOG_CATEGORY_EXTERN(Name, Default, Compile) \
	extern struct FLogCategory##Name : public FLogCategory<ELogVerbosity::Default, ELogVerbosity::Compile> { FLogCategory##Name() : FLogCategory(TEXT(#Name)) {} } Name;
#define DEFINE_LOG_CATEGORY(Name) FLogCategory##Name Name;
#define DEFINE_LOG_CATEGORY_STATIC(Name, Default, Compile) \
	static struct FLogCategory##Name : public FLogCategory<ELogVerbosity::Default, ELogVerbosity::Compile> { FLogCategory##Name() : FLogCategory(TEXT(#Name)) {} } Name;
#define UE_LOG(Category, Verbosity, Format, ...) \
	do { (void)&Category; (void)ELogVerbosity::Verbosity; StubFormat::Check(Format, ##__VA_ARGS__); } while (0)

#define TRACE_CPUPROFILER_EVENT_SCOPE(Name) do {} while (0)
#define SCOPE_CYCLE_COUNTER(x) do {} while (0)
#define SCENE_QUERY_STAT(Name) FName(TEXT(#Name))

// ------------------------------------------------------------------ memory
struct FMemory
{
	static void* Memcpy(void* Dest, const void* Src, SIZE_T Count) { return std::memcpy(Dest, Src, Count); }
	static void* Memset(void* Dest, uint8 Char, SIZE_T Count) { return std::memset(Dest, Char, Count); }
	static void* Memzero(void* Dest, SIZE_T Count) { return std::memset(Dest, 0, Count); }
	static void* Malloc(SIZE_T Count, uint32 Alignment = 0) { (void)Alignment; return std::malloc(Count); }
	static void Free(void* Ptr) { std::free(Ptr); }
};

template <typename T>
struct TNumericLimits
{
	static constexpr T Min() { return std::numeric_limits<T>::lowest(); }
	static constexpr T Max() { return std::numeric_limits<T>::max(); }
	static constexpr T Lowest() { return std::numeric_limits<T>::lowest(); }
};

template <typename T>
constexpr std::remove_reference_t<T>&& MoveTemp(T&& Obj) { return static_cast<std::remove_reference_t<T>&&>(Obj); }
template <typename T>
constexpr T&& Forward(std::remove_reference_t<T>& Obj) { return static_cast<T&&>(Obj); }
template <typename T>
constexpr T&& Forward(std::remove_reference_t<T>&& Obj) { return static_cast<T&&>(Obj); }
template <typename T>
inline void Swap(T& A, T& B) { std::swap(A, B); }

// ------------------------------------------------------------------ search enums
namespace ESearchCase { enum Type { CaseSensitive, IgnoreCase }; }
namespace ESearchDir { enum Type { FromStart, FromEnd }; }
enum class EAllowShrinking : uint8 { No, Yes, Default = Yes };

// ------------------------------------------------------------------ containers
template <int N>
struct TInlineAllocator {};
struct FDefaultAllocator {};

template <typename T, typename Allocator = FDefaultAllocator>
class TArray
{
public:
	using ElementType = T;
	TArray() = default;
	TArray(std::initializer_list<T> Init) : Data(Init) {}
	template <typename OtherAllocator>
	TArray(const TArray<T, OtherAllocator>& Other) : Data(Other.begin(), Other.end()) {}
	TArray(const T* Ptr, int32 Count) : Data(Ptr, Ptr + Count) {}
	TArray& operator=(std::initializer_list<T> Init) { Data.assign(Init); return *this; }

	int32 Num() const { return static_cast<int32>(Data.size()); }
	bool IsEmpty() const { return Data.empty(); }
	bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < Num(); }
	T* GetData() { return Data.data(); }
	const T* GetData() const { return Data.data(); }
	T& operator[](int32 Index) { return Data[static_cast<size_t>(Index)]; }
	const T& operator[](int32 Index) const { return Data[static_cast<size_t>(Index)]; }
	T& Last(int32 IndexFromEnd = 0) { return Data[Data.size() - 1 - IndexFromEnd]; }
	const T& Last(int32 IndexFromEnd = 0) const { return Data[Data.size() - 1 - IndexFromEnd]; }
	T& Top() { return Data.back(); }

	int32 Add(const T& Item) { Data.push_back(Item); return Num() - 1; }
	int32 Add(T&& Item) { Data.push_back(std::move(Item)); return Num() - 1; }
	int32 AddUnique(const T& Item) { return Add(Item); }
	template <typename... ArgsType>
	int32 Emplace(ArgsType&&... Args) { Data.emplace_back(std::forward<ArgsType>(Args)...); return Num() - 1; }
	template <typename... ArgsType>
	T& Emplace_GetRef(ArgsType&&... Args) { return Data.emplace_back(std::forward<ArgsType>(Args)...); }
	int32 AddDefaulted(int32 Count = 1) { const int32 Index = Num(); Data.resize(Data.size() + Count); return Index; }
	T& AddDefaulted_GetRef() { Data.emplace_back(); return Data.back(); }
	int32 AddZeroed(int32 Count = 1) { return AddDefaulted(Count); }
	int32 AddUninitialized(int32 Count = 1) { return AddDefaulted(Count); }
	void Append(const T* Ptr, int32 Count) { Data.insert(Data.end(), Ptr, Ptr + Count); }
	template <typename OtherAllocator>
	void Append(const TArray<T, OtherAllocator>& Other) { Data.insert(Data.end(), Other.begin(), Other.end()); }
	void Insert(const T& Item, int32 Index) { Data.insert(Data.begin() + Index, Item); }
	void Init(const T& Value, int32 Count) { Data.assign(static_cast<size_t>(Count), Value); }
	void Reserve(int32 Count) { Data.reserve(static_cast<size_t>(Count)); }
	void Reset(int32 NewSize = 0) { Data.clear(); Data.reserve(static_cast<size_t>(NewSize)); }
	void Empty(int32 Slack = 0) { Data.clear(); (void)Slack; }
	void SetNum(int32 NewNum, EAllowShrinking = EAllowShrinking::Default) { Data.resize(static_cast<size_t>(NewNum)); }
	void SetNumZeroed(int32 NewNum, EAllowShrinking = EAllowShrinking::Default) { Data.resize(static_cast<size_t>(NewNum)); }
	void SetNumUninitialized(int32 NewNum, EAllowShrinking = EAllowShrinking::Default) { Data.resize(static_cast<size_t>(NewNum)); }
	T Pop(EAllowShrinking = EAllowShrinking::Default) { T V = std::move(Data.back()); Data.pop_back(); return V; }
	void RemoveAt(int32 Index, int32 Count = 1, EAllowShrinking = EAllowShrinking::Default) { Data.erase(Data.begin() + Index, Data.begin() + Index + Count); }
	void RemoveAtSwap(int32 Index, int32 Count = 1, EAllowShrinking = EAllowShrinking::Default) { RemoveAt(Index, Count); }
	int32 Remove(const T& Item) { (void)Item; return 0; }
	int32 RemoveSingle(const T& Item) { (void)Item; return 0; }
	template <typename Predicate>
	int32 RemoveAll(Predicate Pred) { int32 C = 0; for (auto& E : Data) { if (Pred(E)) { ++C; } } return C; }
	template <typename Predicate>
	int32 RemoveAllSwap(Predicate Pred) { return RemoveAll(Pred); }
	bool Contains(const T& Item) const { return std::find(Data.begin(), Data.end(), Item) != Data.end(); }
	template <typename Predicate>
	bool ContainsByPredicate(Predicate Pred) const { for (const auto& E : Data) { if (Pred(E)) { return true; } } return false; }
	template <typename Predicate>
	T* FindByPredicate(Predicate Pred) { for (auto& E : Data) { if (Pred(E)) { return &E; } } return nullptr; }
	template <typename Predicate>
	const T* FindByPredicate(Predicate Pred) const { for (const auto& E : Data) { if (Pred(E)) { return &E; } } return nullptr; }
	int32 Find(const T& Item) const { auto It = std::find(Data.begin(), Data.end(), Item); return It == Data.end() ? INDEX_NONE : int32(It - Data.begin()); }
	template <typename Predicate>
	int32 IndexOfByPredicate(Predicate Pred) const { for (int32 I = 0; I < Num(); ++I) { if (Pred(Data[I])) { return I; } } return INDEX_NONE; }
	void Sort() { std::sort(Data.begin(), Data.end()); }
	template <typename Predicate>
	void Sort(Predicate Pred) { std::sort(Data.begin(), Data.end(), Pred); }
	template <typename Predicate>
	void StableSort(Predicate Pred) { std::stable_sort(Data.begin(), Data.end(), Pred); }
	bool operator==(const TArray& O) const { return Data == O.Data; }
	bool operator!=(const TArray& O) const { return Data != O.Data; }

	auto begin() { return Data.begin(); }
	auto end() { return Data.end(); }
	auto begin() const { return Data.begin(); }
	auto end() const { return Data.end(); }

private:
	std::vector<T> Data;
};

template <typename T>
class TArrayView
{
public:
	TArrayView() = default;
	TArrayView(T* InData, int32 InNum) : Ptr(InData), Count(InNum) {}
	template <typename A>
	TArrayView(TArray<std::remove_const_t<T>, A>& Arr) : Ptr(Arr.GetData()), Count(Arr.Num()) {}
	template <typename A>
	TArrayView(const TArray<std::remove_const_t<T>, A>& Arr) : Ptr(Arr.GetData()), Count(Arr.Num()) {}
	int32 Num() const { return Count; }
	T* GetData() const { return Ptr; }
	T& operator[](int32 I) const { return Ptr[I]; }
	T* begin() const { return Ptr; }
	T* end() const { return Ptr + Count; }

private:
	T* Ptr = nullptr;
	int32 Count = 0;
};
template <typename T>
using TConstArrayView = TArrayView<const T>;

template <typename K, typename V>
struct TPair
{
	K Key;
	V Value;
	TPair() = default;
	TPair(const K& InKey, const V& InValue) : Key(InKey), Value(InValue) {}
	bool operator==(const TPair& O) const { return Key == O.Key && Value == O.Value; }
};
template <typename A, typename B>
using TTuple = TPair<A, B>;

template <typename K, typename V>
class TMap
{
public:
	using ElementType = TPair<K, V>;
	int32 Num() const { return static_cast<int32>(Items.size()); }
	bool IsEmpty() const { return Items.empty(); }
	V& Add(const K& Key, const V& Value) { if (V* E = Find(Key)) { *E = Value; return *E; } Items.push_back({Key, Value}); return Items.back().Value; }
	V& Add(const K& Key) { return Add(Key, V()); }
	V& Emplace(const K& Key, const V& Value) { return Add(Key, Value); }
	V& FindOrAdd(const K& Key) { if (V* E = Find(Key)) { return *E; } return Add(Key, V()); }
	V* Find(const K& Key) { for (auto& I : Items) { if (I.Key == Key) { return &I.Value; } } return nullptr; }
	const V* Find(const K& Key) const { for (const auto& I : Items) { if (I.Key == Key) { return &I.Value; } } return nullptr; }
	const V& FindChecked(const K& Key) const { return *Find(Key); }
	V& FindChecked(const K& Key) { return *Find(Key); }
	V FindRef(const K& Key) const { const V* E = Find(Key); return E ? *E : V(); }
	bool Contains(const K& Key) const { return Find(Key) != nullptr; }
	int32 Remove(const K& Key) { (void)Key; return 0; }
	bool RemoveAndCopyValue(const K& Key, V& OutValue) { if (V* E = Find(Key)) { OutValue = *E; return true; } return false; }
	void Reset() { Items.clear(); }
	void Empty(int32 Slack = 0) { Items.clear(); (void)Slack; }
	void Reserve(int32 Count) { Items.reserve(static_cast<size_t>(Count)); }
	int32 GetKeys(TArray<K>& Out) const { for (const auto& I : Items) { Out.Add(I.Key); } return Num(); }
	V& operator[](const K& Key) { return FindOrAdd(Key); }
	auto begin() { return Items.begin(); }
	auto end() { return Items.end(); }
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }

private:
	std::vector<TPair<K, V>> Items;
};

template <typename T>
class TSet
{
public:
	int32 Num() const { return static_cast<int32>(Items.size()); }
	void Add(const T& Item) { Items.push_back(Item); }
	bool Contains(const T& Item) const { return std::find(Items.begin(), Items.end(), Item) != Items.end(); }
	void Reserve(int32 Count) { Items.reserve(static_cast<size_t>(Count)); }
	void Reset() { Items.clear(); }
	void Empty() { Items.clear(); }
	int32 Remove(const T& Item) { (void)Item; return 0; }
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }

private:
	std::vector<T> Items;
};

template <typename T>
class TOptional
{
public:
	TOptional() = default;
	TOptional(const T& V) : Value(V), bSet(true) {}
	bool IsSet() const { return bSet; }
	const T& GetValue() const { return Value; }
	T Get(const T& Default) const { return bSet ? Value : Default; }
	explicit operator bool() const { return bSet; }

private:
	T Value{};
	bool bSet = false;
};

// ------------------------------------------------------------------ functions
template <typename Sig>
class TUniqueFunction;
template <typename R, typename... A>
class TUniqueFunction<R(A...)>
{
public:
	TUniqueFunction() = default;
	TUniqueFunction(std::nullptr_t) {}
	template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<R, F&, A...> && !std::is_same_v<std::decay_t<F>, TUniqueFunction>>>
	TUniqueFunction(F&& Fn) : Impl(std::make_shared<std::decay_t<F>>(std::forward<F>(Fn))), Call([](void* P, A... Args) -> R { return (*static_cast<std::decay_t<F>*>(P))(std::forward<A>(Args)...); }) {}
	TUniqueFunction(TUniqueFunction&&) = default;
	TUniqueFunction& operator=(TUniqueFunction&&) = default;
	TUniqueFunction(const TUniqueFunction&) = delete;
	TUniqueFunction& operator=(const TUniqueFunction&) = delete;
	explicit operator bool() const { return Call != nullptr; }
	R operator()(A... Args) const { return Call(Impl.get(), std::forward<A>(Args)...); }

private:
	std::shared_ptr<void> Impl;
	R (*Call)(void*, A...) = nullptr;
};

template <typename Sig>
class TFunction;
template <typename R, typename... A>
class TFunction<R(A...)>
{
public:
	TFunction() = default;
	TFunction(std::nullptr_t) {}
	template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<R, F&, A...> && !std::is_same_v<std::decay_t<F>, TFunction>>>
	TFunction(F&& Fn) : Impl(std::forward<F>(Fn)) {}
	explicit operator bool() const { return static_cast<bool>(Impl); }
	R operator()(A... Args) const { return Impl(std::forward<A>(Args)...); }

private:
	std::function<R(A...)> Impl;
};

template <typename Sig>
class TFunctionRef;
template <typename R, typename... A>
class TFunctionRef<R(A...)>
{
public:
	template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<R, F&, A...> && !std::is_same_v<std::decay_t<F>, TFunctionRef>>>
	TFunctionRef(F&& Fn) : Ptr(const_cast<void*>(static_cast<const void*>(&Fn))), Call([](void* P, A... Args) -> R { return (*static_cast<std::remove_reference_t<F>*>(P))(std::forward<A>(Args)...); }) {}
	R operator()(A... Args) const { return Call(Ptr, std::forward<A>(Args)...); }

private:
	void* Ptr;
	R (*Call)(void*, A...);
};

// ------------------------------------------------------------------ smart pointers
enum class ESPMode : uint8 { NotThreadSafe = 0, ThreadSafe = 1 };

template <typename T>
struct TRawPtrProxy
{
	T* Ptr;
};
template <typename T>
TRawPtrProxy<T> MakeShareable(T* Ptr)
{
	return {Ptr};
}

template <typename T, ESPMode Mode = ESPMode::ThreadSafe>
class TSharedRef;
template <typename T, ESPMode Mode = ESPMode::ThreadSafe>
class TSharedPtr
{
public:
	TSharedPtr() = default;
	TSharedPtr(std::nullptr_t) {}
	explicit TSharedPtr(std::shared_ptr<T> P) : Impl(std::move(P)) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSharedPtr(TRawPtrProxy<U> Proxy) : Impl(Proxy.Ptr) {}
	template <typename U, ESPMode M, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSharedPtr(const TSharedRef<U, M>& Ref);
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSharedPtr(const TSharedPtr<U, Mode>& Other) : Impl(Other.Impl) {}
	bool IsValid() const { return Impl != nullptr; }
	explicit operator bool() const { return IsValid(); }
	T* Get() const { return Impl.get(); }
	T* operator->() const { return Impl.get(); }
	T& operator*() const { return *Impl; }
	void Reset() { Impl.reset(); }
	TSharedRef<T, Mode> ToSharedRef() const;
	std::shared_ptr<T> Impl;
};

template <typename T, ESPMode Mode>
class TSharedRef
{
public:
	explicit TSharedRef(std::shared_ptr<T> P) : Impl(std::move(P)) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSharedRef(TRawPtrProxy<U> Proxy) : Impl(Proxy.Ptr) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSharedRef(const TSharedRef<U, Mode>& Other) : Impl(Other.Impl) {}
	T* operator->() const { return Impl.get(); }
	T& operator*() const { return *Impl; }
	T& Get() const { return *Impl; }
	std::shared_ptr<T> Impl;
};

template <typename T, ESPMode Mode>
template <typename U, ESPMode M, typename>
TSharedPtr<T, Mode>::TSharedPtr(const TSharedRef<U, M>& Ref) : Impl(Ref.Impl)
{
}
template <typename T, ESPMode Mode>
TSharedRef<T, Mode> TSharedPtr<T, Mode>::ToSharedRef() const
{
	return TSharedRef<T, Mode>(Impl);
}

template <typename T, ESPMode Mode = ESPMode::ThreadSafe, typename... Args>
TSharedRef<T, Mode> MakeShared(Args&&... A)
{
	return TSharedRef<T, Mode>(std::make_shared<T>(std::forward<Args>(A)...));
}


template <typename T>
class TWeakPtr
{
public:
	TWeakPtr() = default;
	template <ESPMode M>
	TWeakPtr(const TSharedPtr<T, M>& P) : Impl(P.Impl) {}
	TSharedPtr<T> Pin() const { return TSharedPtr<T>(Impl.lock()); }
	bool IsValid() const { return !Impl.expired(); }

private:
	std::weak_ptr<T> Impl;
};

template <typename T>
class TUniquePtr
{
public:
	TUniquePtr() = default;
	TUniquePtr(std::nullptr_t) {}
	explicit TUniquePtr(T* P) : Impl(P) {}
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TUniquePtr(TUniquePtr<U>&& Other) : Impl(Other.Release()) {}
	TUniquePtr(TUniquePtr&&) = default;
	TUniquePtr& operator=(TUniquePtr&&) = default;
	T* Get() const { return Impl.get(); }
	T* operator->() const { return Impl.get(); }
	T& operator*() const { return *Impl; }
	explicit operator bool() const { return Impl != nullptr; }
	bool IsValid() const { return Impl != nullptr; }
	void Reset(T* P = nullptr) { Impl.reset(P); }
	T* Release() { return Impl.release(); }

private:
	std::unique_ptr<T> Impl;
};
template <typename T, typename... Args>
TUniquePtr<T> MakeUnique(Args&&... A)
{
	return TUniquePtr<T>(new T(std::forward<Args>(A)...));
}

// ------------------------------------------------------------------ strings
class FString;
struct FCString
{
	static float Atof(const TCHAR* S) { return static_cast<float>(std::wcstod(S, nullptr)); }
	static int32 Atoi(const TCHAR* S) { return static_cast<int32>(std::wcstol(S, nullptr, 10)); }
	static int64 Atoi64(const TCHAR* S) { return static_cast<int64>(std::wcstoll(S, nullptr, 10)); }
	static uint64 Strtoui64(const TCHAR* S, TCHAR** End, int32 Base) { return static_cast<uint64>(std::wcstoull(S, End, Base)); }
	static int32 Strlen(const TCHAR* S) { return static_cast<int32>(std::wcslen(S)); }
	static int32 Strcmp(const TCHAR* A, const TCHAR* B) { return std::wcscmp(A, B); }
};

class FString
{
public:
	FString() = default;
	FString(const TCHAR* S) : Data(S ? S : L"") {}
	FString(int32 Len, const TCHAR* S) : Data(S, static_cast<size_t>(Len)) {}
	explicit FString(const ANSICHAR* S) : Data(S ? std::wstring(S, S + std::strlen(S)) : std::wstring()) {}
	const TCHAR* operator*() const { return Data.c_str(); }
	int32 Len() const { return static_cast<int32>(Data.size()); }
	bool IsEmpty() const { return Data.empty(); }
	void Reset(int32 = 0) { Data.clear(); }
	void Empty() { Data.clear(); }
	TCHAR& operator[](int32 I) { return Data[static_cast<size_t>(I)]; }
	const TCHAR& operator[](int32 I) const { return Data[static_cast<size_t>(I)]; }
	FString& operator+=(const FString& O) { Data += O.Data; return *this; }
	FString& operator+=(const TCHAR* O) { Data += O; return *this; }
	FString& operator+=(TCHAR C) { Data += C; return *this; }
	FString& Append(const FString& O) { Data += O.Data; return *this; }
	FString& AppendChar(TCHAR C) { Data += C; return *this; }
	friend FString operator+(const FString& A, const FString& B) { FString R(A); R += B; return R; }
	friend FString operator+(const FString& A, const TCHAR* B) { FString R(A); R += B; return R; }
	friend FString operator+(const TCHAR* A, const FString& B) { FString R(A); R += B; return R; }
	bool operator==(const FString& O) const { return Data == O.Data; }
	bool operator!=(const FString& O) const { return Data != O.Data; }
	bool operator==(const TCHAR* O) const { return Data == O; }
	bool operator!=(const TCHAR* O) const { return Data != O; }
	bool operator<(const FString& O) const { return Data < O.Data; }
	bool Equals(const FString& O, ESearchCase::Type = ESearchCase::CaseSensitive) const { return Data == O.Data; }
	bool StartsWith(const FString& Prefix, ESearchCase::Type = ESearchCase::IgnoreCase) const { return Data.rfind(Prefix.Data, 0) == 0; }
	bool EndsWith(const FString& Suffix, ESearchCase::Type = ESearchCase::IgnoreCase) const { return Data.size() >= Suffix.Data.size(); }
	bool Contains(const FString& Sub, ESearchCase::Type = ESearchCase::IgnoreCase, ESearchDir::Type = ESearchDir::FromStart) const { return Data.find(Sub.Data) != std::wstring::npos; }
	int32 Find(const TCHAR* Sub, ESearchCase::Type = ESearchCase::IgnoreCase, ESearchDir::Type = ESearchDir::FromStart, int32 StartPosition = INDEX_NONE) const { (void)StartPosition; auto P = Data.find(Sub); return P == std::wstring::npos ? INDEX_NONE : int32(P); }
	int32 Find(const FString& Sub, ESearchCase::Type C = ESearchCase::IgnoreCase, ESearchDir::Type D = ESearchDir::FromStart, int32 StartPosition = INDEX_NONE) const { return Find(*Sub, C, D, StartPosition); }
	FString Left(int32 Count) const { return FString(Data.substr(0, static_cast<size_t>(Count)).c_str()); }
	FString Right(int32 Count) const { return FString(Data.substr(Data.size() - static_cast<size_t>(Count)).c_str()); }
	FString Mid(int32 Start, int32 Count = MAX_int32) const { return FString(Data.substr(static_cast<size_t>(Start), static_cast<size_t>(Count)).c_str()); }
	FString LeftChop(int32 Count) const { return Left(Len() - Count); }
	FString RightChop(int32 Count) const { return Mid(Count); }
	FString ToLower() const { return *this; }
	FString ToUpper() const { return *this; }
	FString TrimStartAndEnd() const { return *this; }
	FString Replace(const TCHAR* From, const TCHAR* To, ESearchCase::Type = ESearchCase::IgnoreCase) const { (void)From; (void)To; return *this; }
	int32 ReplaceInline(const TCHAR* From, const TCHAR* To, ESearchCase::Type = ESearchCase::IgnoreCase) { (void)From; (void)To; return 0; }
	int32 ParseIntoArray(TArray<FString>& Out, const TCHAR* Delim, bool bCullEmpty = true) const { (void)Out; (void)Delim; (void)bCullEmpty; return 0; }
	bool Split(const FString& InS, FString* LeftS, FString* RightS) const { (void)InS; (void)LeftS; (void)RightS; return false; }
	template <typename... Args>
	static FString Printf(const TCHAR* Fmt, Args&&... A) { StubFormat::Check(Fmt, std::forward<Args>(A)...); return FString(); }
	template <typename... Args>
	static FString Format(const TCHAR* Fmt, Args&&...) { (void)Fmt; return FString(); }
	template <typename A>
	static FString Join(const TArray<FString, A>& Parts, const TCHAR* Sep) { (void)Parts; (void)Sep; return FString(); }
	static FString FromInt(int32 V) { (void)V; return FString(); }
	static FString SanitizeFloat(double V, int32 = 1) { (void)V; return FString(); }
	bool IsNumeric() const { return false; }

private:
	std::wstring Data;
};

inline uint32 GetTypeHash(const FString&) { return 0; }

// UTF-8 <-> TCHAR conversions (UE: StringConv.h).
struct FUTF8ToTCHAR
{
	explicit FUTF8ToTCHAR(const ANSICHAR* S) : Buffer(S ? std::wstring(S, S + std::strlen(S)) : std::wstring()) {}
	const TCHAR* Get() const { return Buffer.c_str(); }
	int32 Length() const { return static_cast<int32>(Buffer.size()); }
	std::wstring Buffer;
};
struct FTCHARToUTF8
{
	explicit FTCHARToUTF8(const TCHAR* S) : Buffer(S ? std::string(S, S + std::wcslen(S)) : std::string()) {}
	const ANSICHAR* Get() const { return Buffer.c_str(); }
	int32 Length() const { return static_cast<int32>(Buffer.size()); }
	std::string Buffer;
};
#define UTF8_TO_TCHAR(str) FUTF8ToTCHAR(static_cast<const ANSICHAR*>(str)).Get()
#define TCHAR_TO_UTF8(str) FTCHARToUTF8(static_cast<const TCHAR*>(str)).Get()
#define ANSI_TO_TCHAR(str) FUTF8ToTCHAR(static_cast<const ANSICHAR*>(str)).Get()

class FName
{
public:
	FName() = default;
	FName(const TCHAR* S) : Text(S) {}
	FName(ENoInit) {}
	explicit FName(const FString& S) : Text(S) {}
	FString ToString() const { return Text; }
	bool IsNone() const { return Text.IsEmpty(); }
	bool IsValid() const { return true; }
	bool operator==(const FName& O) const { return Text == O.Text; }
	bool operator!=(const FName& O) const { return !(*this == O); }
	bool operator<(const FName& O) const { return Text < O.Text; }
	bool LexicalLess(const FName& O) const { return Text < O.Text; }

private:
	FString Text;
};
inline const FName NAME_None;
inline const FName NAME_Zlib(TEXT("Zlib"));
inline uint32 GetTypeHash(const FName&) { return 0; }

class FDateTime
{
public:
	static FDateTime Now() { return FDateTime(); }
	static FDateTime UtcNow() { return FDateTime(); }
	FString ToString() const { return FString(); }
	int64 GetTicks() const { return 0; }
};

class FText;
struct FFormatArgumentValue
{
	FFormatArgumentValue(const FText&);
	FFormatArgumentValue(int32) {}
	FFormatArgumentValue(uint32) {}
	FFormatArgumentValue(int64) {}
	FFormatArgumentValue(uint64) {}
	FFormatArgumentValue(float) {}
	FFormatArgumentValue(double) {}
};
using FFormatOrderedArguments = TArray<FFormatArgumentValue>;
class FText
{
public:
	FText() = default;
	static FText FromString(const FString& S) { (void)S; return FText(); }
	static FText FromName(const FName& N) { (void)N; return FText(); }
	static FText AsNumber(double V) { (void)V; return FText(); }
	static FText AsNumber(int32 V) { (void)V; return FText(); }
	static FText AsNumber(int64 V) { (void)V; return FText(); }
	static FText AsNumber(float V) { (void)V; return FText(); }
	static FText AsDateTime(const FDateTime& D) { (void)D; return FText(); }
	static FText AsCultureInvariant(const FString& S) { (void)S; return FText(); }
	static const FText& GetEmpty() { static FText E; return E; }
	template <typename... Args>
	static FText Format(const FText& Fmt, Args&&... A) { (void)Fmt; FFormatOrderedArguments Ordered{FFormatArgumentValue(A)...}; return FText(); }
	FString ToString() const { return FString(); }
	bool IsEmpty() const { return true; }
};
inline FFormatArgumentValue::FFormatArgumentValue(const FText&) {}
#define LOCTEXT(Key, Text) FText()
#define NSLOCTEXT(Ns, Key, Text) FText()
#define INVTEXT(Text) FText()

// ------------------------------------------------------------------ math
namespace UE::Math
{
template <typename T>
struct TVector2;
template <typename T>
struct TRotator;
template <typename T>
struct TQuat;
template <typename T>
struct TVector
{
	T X = 0, Y = 0, Z = 0;
	static const TVector ZeroVector;
	static const TVector OneVector;
	static const TVector UpVector;
	static const TVector ForwardVector;
	static const TVector RightVector;
	TVector() = default;
	explicit TVector(T V) : X(V), Y(V), Z(V) {}
	TVector(T InX, T InY, T InZ) : X(InX), Y(InY), Z(InZ) {}
	explicit TVector(EForceInit) {}
	TVector(const TVector2<T>& V, T InZ);
	template <typename U, typename = std::enable_if_t<!std::is_same_v<T, U>>>
	explicit TVector(const TVector<U>& O) : X(T(O.X)), Y(T(O.Y)), Z(T(O.Z)) {}
	TVector operator+(const TVector& O) const { return {X + O.X, Y + O.Y, Z + O.Z}; }
	TVector operator-(const TVector& O) const { return {X - O.X, Y - O.Y, Z - O.Z}; }
	TVector operator-() const { return {-X, -Y, -Z}; }
	template <typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
	TVector operator*(S V) const { return {X * T(V), Y * T(V), Z * T(V)}; }
	template <typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
	TVector operator/(S V) const { return {X / T(V), Y / T(V), Z / T(V)}; }
	TVector operator*(const TVector& O) const { return {X * O.X, Y * O.Y, Z * O.Z}; }
	TVector& operator+=(const TVector& O) { X += O.X; Y += O.Y; Z += O.Z; return *this; }
	TVector& operator-=(const TVector& O) { X -= O.X; Y -= O.Y; Z -= O.Z; return *this; }
	template <typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
	TVector& operator*=(S V) { X *= T(V); Y *= T(V); Z *= T(V); return *this; }
	bool operator==(const TVector& O) const { return X == O.X && Y == O.Y && Z == O.Z; }
	bool operator!=(const TVector& O) const { return !(*this == O); }
	T& operator[](int32 I) { return (&X)[I]; }
	T Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	T SizeSquared() const { return X * X + Y * Y + Z * Z; }
	T Size2D() const { return std::sqrt(X * X + Y * Y); }
	T SizeSquared2D() const { return X * X + Y * Y; }
	T Length() const { return Size(); }
	TVector GetSafeNormal(T Tolerance = UE_SMALL_NUMBER, const TVector& ResultIfZero = ZeroVector) const { (void)Tolerance; (void)ResultIfZero; return *this; }
	TVector GetSafeNormal2D(T Tolerance = UE_SMALL_NUMBER, const TVector& ResultIfZero = ZeroVector) const { (void)Tolerance; (void)ResultIfZero; return *this; }
	TVector GetUnsafeNormal() const { return *this; }
	bool Normalize(T Tolerance = UE_SMALL_NUMBER) { (void)Tolerance; return true; }
	bool IsNearlyZero(T Tolerance = UE_KINDA_SMALL_NUMBER) const { (void)Tolerance; return false; }
	bool IsZero() const { return X == 0 && Y == 0 && Z == 0; }
	bool ContainsNaN() const { return false; }
	TRotator<T> Rotation() const;
	TRotator<T> ToOrientationRotator() const;
	TVector2<T> ToVector2D() const;
	static T Dist(const TVector& A, const TVector& B) { return (A - B).Size(); }
	static T Distance(const TVector& A, const TVector& B) { return (A - B).Size(); }
	static T DistSquared(const TVector& A, const TVector& B) { return (A - B).SizeSquared(); }
	static T Dist2D(const TVector& A, const TVector& B) { return (A - B).Size2D(); }
	static T DistSquared2D(const TVector& A, const TVector& B) { return (A - B).SizeSquared2D(); }
	static T DotProduct(const TVector& A, const TVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	static TVector CrossProduct(const TVector& A, const TVector& B) { return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X}; }
	T operator|(const TVector& O) const { return DotProduct(*this, O); }
	TVector operator^(const TVector& O) const { return CrossProduct(*this, O); }
};
template <typename T, typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
TVector<T> operator*(S V, const TVector<T>& A) { return A * V; }
template <typename T> const TVector<T> TVector<T>::ZeroVector{0, 0, 0};
template <typename T> const TVector<T> TVector<T>::OneVector{1, 1, 1};
template <typename T> const TVector<T> TVector<T>::UpVector{0, 0, 1};
template <typename T> const TVector<T> TVector<T>::ForwardVector{1, 0, 0};
template <typename T> const TVector<T> TVector<T>::RightVector{0, 1, 0};

template <typename T>
struct TVector2
{
	T X = 0, Y = 0;
	static const TVector2 ZeroVector;
	static const TVector2 UnitVector;
	TVector2() = default;
	TVector2(T InX, T InY) : X(InX), Y(InY) {}
	explicit TVector2(T V) : X(V), Y(V) {}
	template <typename U, typename = std::enable_if_t<!std::is_same_v<T, U>>>
	TVector2(const TVector2<U>& O) : X(T(O.X)), Y(T(O.Y)) {}  // UE: implicit float <-> double for 2D vectors in Slate code paths
	explicit TVector2(const TVector<T>& V) : X(V.X), Y(V.Y) {}
	TVector2 operator+(const TVector2& O) const { return {X + O.X, Y + O.Y}; }
	TVector2 operator-(const TVector2& O) const { return {X - O.X, Y - O.Y}; }
	TVector2 operator*(const TVector2& O) const { return {X * O.X, Y * O.Y}; }
	template <typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
	TVector2 operator*(S V) const { return {X * T(V), Y * T(V)}; }
	template <typename S, typename = std::enable_if_t<std::is_arithmetic_v<S>>>
	TVector2 operator/(S V) const { return {X / T(V), Y / T(V)}; }
	TVector2& operator+=(const TVector2& O) { X += O.X; Y += O.Y; return *this; }
	bool operator==(const TVector2& O) const { return X == O.X && Y == O.Y; }
	T Size() const { return std::sqrt(X * X + Y * Y); }
	T SizeSquared() const { return X * X + Y * Y; }
	bool IsNearlyZero(T Tolerance = UE_KINDA_SMALL_NUMBER) const { (void)Tolerance; return false; }
	bool IsZero() const { return X == 0 && Y == 0; }
	TVector2 GetSafeNormal(T Tolerance = UE_SMALL_NUMBER) const { (void)Tolerance; return *this; }
	static T Distance(const TVector2& A, const TVector2& B) { return (A - B).Size(); }
	static T DistSquared(const TVector2& A, const TVector2& B) { return (A - B).SizeSquared(); }
	static T DotProduct(const TVector2& A, const TVector2& B) { return A.X * B.X + A.Y * B.Y; }
};
template <typename T> const TVector2<T> TVector2<T>::ZeroVector{0, 0};
template <typename T> const TVector2<T> TVector2<T>::UnitVector{1, 1};
template <typename T>
TVector<T>::TVector(const TVector2<T>& V, T InZ) : X(V.X), Y(V.Y), Z(InZ)
{
}
template <typename T>
TVector2<T> TVector<T>::ToVector2D() const
{
	return {X, Y};
}

template <typename T>
struct TRotator
{
	T Pitch = 0, Yaw = 0, Roll = 0;
	static const TRotator ZeroRotator;
	TRotator() = default;
	TRotator(T P, T Y, T R) : Pitch(P), Yaw(Y), Roll(R) {}
	explicit TRotator(T V) : Pitch(V), Yaw(V), Roll(V) {}
	explicit TRotator(const TQuat<T>& Q);
	TRotator operator+(const TRotator& O) const { return {Pitch + O.Pitch, Yaw + O.Yaw, Roll + O.Roll}; }
	TRotator operator-(const TRotator& O) const { return {Pitch - O.Pitch, Yaw - O.Yaw, Roll - O.Roll}; }
	TRotator operator*(T S) const { return {Pitch * S, Yaw * S, Roll * S}; }
	TRotator& operator+=(const TRotator& O) { Pitch += O.Pitch; Yaw += O.Yaw; Roll += O.Roll; return *this; }
	bool operator==(const TRotator& O) const { return Pitch == O.Pitch && Yaw == O.Yaw && Roll == O.Roll; }
	TVector<T> Vector() const { return {}; }
	TQuat<T> Quaternion() const;
	TVector<T> RotateVector(const TVector<T>& V) const { return V; }
	TVector<T> UnrotateVector(const TVector<T>& V) const { return V; }
	TRotator GetNormalized() const { return *this; }
	void Normalize() {}
	bool IsNearlyZero(T Tolerance = UE_KINDA_SMALL_NUMBER) const { (void)Tolerance; return false; }
	static T NormalizeAxis(T Angle) { return Angle; }
	static T ClampAxis(T Angle) { return Angle; }
};
template <typename T> const TRotator<T> TRotator<T>::ZeroRotator{0, 0, 0};

template <typename T>
struct TQuat
{
	T X = 0, Y = 0, Z = 0, W = 1;
	static const TQuat Identity;
	TQuat() = default;
	TQuat(T InX, T InY, T InZ, T InW) : X(InX), Y(InY), Z(InZ), W(InW) {}
	TQuat(const TVector<T>& Axis, T AngleRad) { (void)Axis; (void)AngleRad; }
	explicit TQuat(const TRotator<T>& R) { (void)R; }
	TQuat operator*(const TQuat& O) const { (void)O; return *this; }
	TVector<T> operator*(const TVector<T>& V) const { return V; }
	TVector<T> RotateVector(const TVector<T>& V) const { return V; }
	TVector<T> UnrotateVector(const TVector<T>& V) const { return V; }
	TRotator<T> Rotator() const { return {}; }
	TQuat Inverse() const { return *this; }
	TVector<T> GetForwardVector() const { return {}; }
	TVector<T> GetRightVector() const { return {}; }
	TVector<T> GetUpVector() const { return {}; }
	static TQuat Slerp(const TQuat& A, const TQuat& B, T Alpha) { (void)B; (void)Alpha; return A; }
	static TQuat FindBetweenNormals(const TVector<T>& A, const TVector<T>& B) { (void)A; (void)B; return {}; }
};
template <typename T> const TQuat<T> TQuat<T>::Identity{0, 0, 0, 1};
template <typename T>
TRotator<T>::TRotator(const TQuat<T>& Q)
{
	(void)Q;
}
template <typename T>
TQuat<T> TRotator<T>::Quaternion() const
{
	return {};
}
template <typename T>
TRotator<T> TVector<T>::Rotation() const
{
	return {};
}
template <typename T>
TRotator<T> TVector<T>::ToOrientationRotator() const
{
	return {};
}

template <typename T>
struct TTransform
{
	static const TTransform Identity;
	TTransform() = default;
	explicit TTransform(const TVector<T>& Translation) { (void)Translation; }
	explicit TTransform(const TRotator<T>& Rotation) { (void)Rotation; }
	explicit TTransform(const TQuat<T>& Rotation) { (void)Rotation; }
	TTransform(const TRotator<T>& Rotation, const TVector<T>& Translation, const TVector<T>& Scale = TVector<T>::OneVector) { (void)Rotation; (void)Translation; (void)Scale; }
	TTransform(const TQuat<T>& Rotation, const TVector<T>& Translation, const TVector<T>& Scale = TVector<T>::OneVector) { (void)Rotation; (void)Translation; (void)Scale; }
	TVector<T> GetLocation() const { return {}; }
	TVector<T> GetTranslation() const { return {}; }
	TQuat<T> GetRotation() const { return {}; }
	TRotator<T> Rotator() const { return {}; }
	TVector<T> GetScale3D() const { return TVector<T>::OneVector; }
	void SetLocation(const TVector<T>& V) { (void)V; }
	void SetRotation(const TQuat<T>& Q) { (void)Q; }
	void SetScale3D(const TVector<T>& V) { (void)V; }
	TVector<T> TransformPosition(const TVector<T>& V) const { return V; }
	TVector<T> TransformVector(const TVector<T>& V) const { return V; }
	TVector<T> InverseTransformPosition(const TVector<T>& V) const { return V; }
	TVector<T> InverseTransformVector(const TVector<T>& V) const { return V; }
	TVector<T> GetUnitAxis(int32 Axis) const { (void)Axis; return {}; }
	TTransform operator*(const TTransform& O) const { (void)O; return *this; }
	TTransform Inverse() const { return *this; }
};
template <typename T> const TTransform<T> TTransform<T>::Identity{};

template <typename T>
struct TBox
{
	TVector<T> Min, Max;
	bool IsValid = false;
	TBox() = default;
	TBox(const TVector<T>& InMin, const TVector<T>& InMax) : Min(InMin), Max(InMax), IsValid(true) {}
	explicit TBox(EForceInit) {}
	TVector<T> GetCenter() const { return (Min + Max) * 0.5; }
	TVector<T> GetExtent() const { return (Max - Min) * 0.5; }
	TVector<T> GetSize() const { return Max - Min; }
	bool IsInside(const TVector<T>& P) const { (void)P; return true; }
	TBox& operator+=(const TVector<T>& P) { (void)P; return *this; }
	TBox ExpandBy(T W) const { (void)W; return *this; }
};

template <typename T>
struct TBoxSphereBounds
{
	TVector<T> Origin;
	TVector<T> BoxExtent;
	T SphereRadius = 0;
	TBox<T> GetBox() const { return {}; }
};

template <typename T>
struct TMatrix
{
};
template <typename T>
struct TRotationMatrix : TMatrix<T>
{
	explicit TRotationMatrix(const TRotator<T>& R) { (void)R; }
	TVector<T> GetUnitAxis(int32 Axis) const { (void)Axis; return {}; }
	TVector<T> GetScaledAxis(int32 Axis) const { (void)Axis; return {}; }
	static TMatrix<T> MakeFromX(const TVector<T>& X) { (void)X; return {}; }
};
} // namespace UE::Math

using FVector = UE::Math::TVector<double>;
using FVector3d = UE::Math::TVector<double>;
using FVector3f = UE::Math::TVector<float>;
using FVector2D = UE::Math::TVector2<double>;
using FVector2f = UE::Math::TVector2<float>;
using FRotator = UE::Math::TRotator<double>;
using FRotator3f = UE::Math::TRotator<float>;
using FQuat = UE::Math::TQuat<double>;
using FTransform = UE::Math::TTransform<double>;
using FBox = UE::Math::TBox<double>;
using FBoxSphereBounds = UE::Math::TBoxSphereBounds<double>;
using FMatrix = UE::Math::TMatrix<double>;
using FRotationMatrix = UE::Math::TRotationMatrix<double>;

namespace EAxis { enum Type { None, X, Y, Z }; }

struct FIntPoint
{
	int32 X = 0, Y = 0;
	FIntPoint() = default;
	FIntPoint(int32 InX, int32 InY) : X(InX), Y(InY) {}
};
struct FIntVector
{
	int32 X = 0, Y = 0, Z = 0;
};

struct FColor;
struct FLinearColor
{
	float R = 0, G = 0, B = 0, A = 1;
	static const FLinearColor White, Black, Red, Green, Blue, Yellow, Transparent, Gray;
	FLinearColor() = default;
	FLinearColor(float InR, float InG, float InB, float InA = 1.0f) : R(InR), G(InG), B(InB), A(InA) {}
	explicit FLinearColor(const FVector& V) : R(float(V.X)), G(float(V.Y)), B(float(V.Z)) {}
	FLinearColor(const FColor& C);
	FLinearColor operator*(float S) const { return {R * S, G * S, B * S, A * S}; }
	FLinearColor operator*(const FLinearColor& O) const { return {R * O.R, G * O.G, B * O.B, A * O.A}; }
	FLinearColor operator+(const FLinearColor& O) const { return {R + O.R, G + O.G, B + O.B, A + O.A}; }
	FLinearColor operator-(const FLinearColor& O) const { return {R - O.R, G - O.G, B - O.B, A - O.A}; }
	FLinearColor& operator*=(float S) { R *= S; G *= S; B *= S; A *= S; return *this; }
	FLinearColor& operator+=(const FLinearColor& O) { R += O.R; G += O.G; B += O.B; A += O.A; return *this; }
	FColor ToFColor(bool bSRGB) const;
	FColor ToFColorSRGB() const;
	float GetLuminance() const { return 0.3f * R + 0.59f * G + 0.11f * B; }
	FLinearColor CopyWithNewOpacity(float Alpha) const { FLinearColor C = *this; C.A = Alpha; return C; }
	static FLinearColor LerpUsingHSV(const FLinearColor& From, const FLinearColor& To, float Progress) { (void)To; (void)Progress; return From; }
	static FLinearColor MakeFromHSV8(uint8 H, uint8 S, uint8 V) { (void)H; (void)S; (void)V; return {}; }
	static FLinearColor MakeRandomColor() { return {}; }
};
inline const FLinearColor FLinearColor::White{1, 1, 1, 1};
inline const FLinearColor FLinearColor::Black{0, 0, 0, 1};
inline const FLinearColor FLinearColor::Red{1, 0, 0, 1};
inline const FLinearColor FLinearColor::Green{0, 1, 0, 1};
inline const FLinearColor FLinearColor::Blue{0, 0, 1, 1};
inline const FLinearColor FLinearColor::Yellow{1, 1, 0, 1};
inline const FLinearColor FLinearColor::Transparent{0, 0, 0, 0};
inline const FLinearColor FLinearColor::Gray{0.5f, 0.5f, 0.5f, 1};

struct FColor
{
	uint8 B = 0, G = 0, R = 0, A = 255;
	static const FColor White, Black, Red, Green, Blue, Yellow, Cyan, Magenta, Orange, Purple, Silver, Emerald;
	FColor() = default;
	constexpr FColor(uint8 InR, uint8 InG, uint8 InB, uint8 InA = 255) : B(InB), G(InG), R(InR), A(InA) {}
	explicit FColor(uint32 InColor) { (void)InColor; }
	FLinearColor ReinterpretAsLinear() const { return {}; }
	bool operator==(const FColor& O) const { return R == O.R && G == O.G && B == O.B && A == O.A; }
};
inline const FColor FColor::White(255, 255, 255);
inline const FColor FColor::Black(0, 0, 0);
inline const FColor FColor::Red(255, 0, 0);
inline const FColor FColor::Green(0, 255, 0);
inline const FColor FColor::Blue(0, 0, 255);
inline const FColor FColor::Yellow(255, 255, 0);
inline const FColor FColor::Cyan(0, 255, 255);
inline const FColor FColor::Magenta(255, 0, 255);
inline const FColor FColor::Orange(243, 156, 18);
inline const FColor FColor::Purple(169, 7, 228);
inline const FColor FColor::Silver(189, 195, 199);
inline const FColor FColor::Emerald(46, 204, 113);
inline FLinearColor::FLinearColor(const FColor& C) : R(C.R / 255.0f), G(C.G / 255.0f), B(C.B / 255.0f), A(C.A / 255.0f)
{
}
inline FColor FLinearColor::ToFColor(bool bSRGB) const
{
	(void)bSRGB;
	return {};
}
inline FColor FLinearColor::ToFColorSRGB() const
{
	return {};
}

struct FMath
{
	template <typename T> static constexpr T Max(T A, T B) { return A < B ? B : A; }
	template <typename T> static constexpr T Min(T A, T B) { return A < B ? A : B; }
	template <typename T> static constexpr T Clamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	template <typename T> static constexpr T Abs(T V) { return V < T(0) ? -V : V; }
	template <typename T> static constexpr T Sign(T V) { return V > T(0) ? T(1) : (V < T(0) ? T(-1) : T(0)); }
	template <typename T> static constexpr T Square(T V) { return V * V; }
	template <typename T> static constexpr T Max3(T A, T B, T C) { return Max(Max(A, B), C); }
	// UE 5 also accepts mixed float / double arguments (UnrealMathUtility.h "MIX_FLOATS" overloads, result double).
	static constexpr double Max(float A, double B) { return A < B ? B : A; }
	static constexpr double Max(double A, float B) { return A < B ? B : A; }
	static constexpr double Min(float A, double B) { return A < B ? A : B; }
	static constexpr double Min(double A, float B) { return A < B ? A : B; }
	template <typename A, typename B, typename C, typename = std::enable_if_t<std::is_floating_point_v<A> && std::is_floating_point_v<B> && std::is_floating_point_v<C> && !(std::is_same_v<A, B> && std::is_same_v<B, C>)>>
	static constexpr double Clamp(A V, B Lo, C Hi) { return V < Lo ? double(Lo) : (V > Hi ? double(Hi) : double(V)); }
	template <typename T, typename U>
	static T Lerp(const T& A, const T& B, const U& Alpha) { return T(A + (B - A) * Alpha); }
	static float Sqrt(float V) { return std::sqrt(V); }
	static double Sqrt(double V) { return std::sqrt(V); }
	static float InvSqrt(float V) { return 1.0f / std::sqrt(V); }
	static float Sin(float V) { return std::sin(V); }
	static double Sin(double V) { return std::sin(V); }
	static float Cos(float V) { return std::cos(V); }
	static double Cos(double V) { return std::cos(V); }
	static float Tan(float V) { return std::tan(V); }
	static float Atan(float V) { return std::atan(V); }
	static double Atan(double V) { return std::atan(V); }
	static float Atan2(float Y, float X) { return std::atan2(Y, X); }
	static double Atan2(double Y, double X) { return std::atan2(Y, X); }
	static float Acos(float V) { return std::acos(V); }
	static float Asin(float V) { return std::asin(V); }
	static float Exp(float V) { return std::exp(V); }
	static double Exp(double V) { return std::exp(V); }
	static float Loge(float V) { return std::log(V); }
	static double Loge(double V) { return std::log(V); }
	static float LogX(float Base, float V) { return std::log(V) / std::log(Base); }
	static double LogX(double Base, double V) { return std::log(V) / std::log(Base); }
	static float Log2(float V) { return std::log2(V); }
	static float Pow(float A, float B) { return std::pow(A, B); }
	static double Pow(double A, double B) { return std::pow(A, B); }
	static float Fmod(float A, float B) { return std::fmod(A, B); }
	static double Fmod(double A, double B) { return std::fmod(A, B); }
	static float FloorToFloat(float V) { return std::floor(V); }
	static double FloorToDouble(double V) { return std::floor(V); }
	static float CeilToFloat(float V) { return std::ceil(V); }
	static double CeilToDouble(double V) { return std::ceil(V); }
	static float RoundToFloat(float V) { return std::round(V); }
	static double RoundToDouble(double V) { return std::round(V); }
	static int32 FloorToInt(float V) { return int32(std::floor(V)); }
	static int32 FloorToInt(double V) { return int32(std::floor(V)); }
	static int32 FloorToInt32(float V) { return int32(std::floor(V)); }
	static int32 FloorToInt32(double V) { return int32(std::floor(V)); }
	static int64 FloorToInt64(double V) { return int64(std::floor(V)); }
	static int32 CeilToInt(float V) { return int32(std::ceil(V)); }
	static int32 CeilToInt(double V) { return int32(std::ceil(V)); }
	static int32 CeilToInt32(float V) { return int32(std::ceil(V)); }
	static int32 RoundToInt(float V) { return int32(std::lround(V)); }
	static int32 RoundToInt(double V) { return int32(std::lround(V)); }
	static int32 RoundToInt32(float V) { return int32(std::lround(V)); }
	static int32 TruncToInt(float V) { return int32(V); }
	static float Frac(float V) { return V - std::floor(V); }
	static constexpr float RadiansToDegrees(float V) { return V * (180.0f / UE_PI); }
	static constexpr double RadiansToDegrees(double V) { return V * (180.0 / UE_DOUBLE_PI); }
	static constexpr float DegreesToRadians(float V) { return V * (UE_PI / 180.0f); }
	static constexpr double DegreesToRadians(double V) { return V * (UE_DOUBLE_PI / 180.0); }
	static bool IsNearlyEqual(float A, float B, float Tolerance = UE_SMALL_NUMBER) { return std::fabs(A - B) <= Tolerance; }
	static bool IsNearlyEqual(double A, double B, double Tolerance = UE_SMALL_NUMBER) { return std::fabs(A - B) <= Tolerance; }
	static bool IsNearlyZero(float V, float Tolerance = UE_SMALL_NUMBER) { return std::fabs(V) <= Tolerance; }
	static bool IsNearlyZero(double V, double Tolerance = UE_SMALL_NUMBER) { return std::fabs(V) <= Tolerance; }
	static bool IsFinite(float V) { return std::isfinite(V); }
	static bool IsFinite(double V) { return std::isfinite(V); }
	static bool IsNaN(float V) { return std::isnan(V); }
	static float FInterpTo(float Current, float Target, float DeltaTime, float Speed) { (void)Target; (void)DeltaTime; (void)Speed; return Current; }
	static float FInterpConstantTo(float Current, float Target, float DeltaTime, float Speed) { (void)Target; (void)DeltaTime; (void)Speed; return Current; }
	static FVector VInterpTo(const FVector& Current, const FVector& Target, float DeltaTime, float Speed) { (void)Target; (void)DeltaTime; (void)Speed; return Current; }
	static FRotator RInterpTo(const FRotator& Current, const FRotator& Target, float DeltaTime, float Speed) { (void)Target; (void)DeltaTime; (void)Speed; return Current; }
	static float FindDeltaAngleDegrees(float A, float B) { return B - A; }
	static float UnwindDegrees(float A) { return A; }
	static float UnwindRadians(float A) { return A; }
	static float FRand() { return 0.5f; }
	static int32 RandRange(int32 Lo, int32 Hi) { (void)Hi; return Lo; }
	static float FRandRange(float Lo, float Hi) { (void)Hi; return Lo; }
	static float GetMappedRangeValueClamped(const FVector2D& In, const FVector2D& Out, float V) { (void)In; (void)Out; return V; }
	static float SmoothStep(float A, float B, float X) { (void)A; (void)B; return X; }
	static bool IsPowerOfTwo(uint32 V) { return (V & (V - 1)) == 0; }
	template <typename T>
	static T DivideAndRoundUp(T A, T B) { return (A + B - 1) / B; }
};

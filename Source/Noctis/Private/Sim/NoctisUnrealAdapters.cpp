#include "Sim/NoctisUnrealAdapters.h"

#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <algorithm>

// ------------------------------------------------------------------ Scheduler

void FNoctisParallelForScheduler::parallelFor(size_t count, const std::function<void(size_t)>& fn, size_t minBatch)
{
	if (count == 0)
	{
		return;
	}
	if (count <= minBatch || count > static_cast<size_t>(MAX_int32))
	{
		for (size_t i = 0; i < count; ++i)
		{
			fn(i);
		}
		return;
	}
	const int32 Num = static_cast<int32>(count);
	const int32 MinBatch = FMath::Max(1, static_cast<int32>(FMath::Min<size_t>(minBatch, 4096)));
	// The core guarantees fn(i) writes only data owned by index i, so any execution order gives the same result.
	// VERIFY: ParallelFor(DebugName, Num, MinBatchSize, Body, Flags) overload (present in UE 5.1+).
	ParallelFor(TEXT("NoctisCore.parallelFor"), Num, MinBatch, [&fn](int32 Index) { fn(static_cast<size_t>(Index)); }, EParallelForFlags::None);
}

int FNoctisParallelForScheduler::workerCount() const
{
	return FMath::Max(1, FTaskGraphInterface::Get().GetNumWorkerThreads() + 1);
}

// ------------------------------------------------------------------ File system

namespace NoctisUnreal
{
FString ToFString(const std::string& Utf8)
{
	return FString(UTF8_TO_TCHAR(Utf8.c_str()));
}

std::string ToStdString(const FString& Text)
{
	const FTCHARToUTF8 Converted(*Text);
	return std::string(Converted.Get(), static_cast<size_t>(Converted.Length()));
}

std::unique_ptr<noctis::JobScheduler> MakeScheduler(ENoctisSchedulerKind Kind, int32 Threads)
{
	switch (Kind)
	{
	case ENoctisSchedulerKind::ParallelFor:
		return std::make_unique<FNoctisParallelForScheduler>();
	case ENoctisSchedulerKind::ThreadPool:
		return noctis::makeThreadPoolScheduler(Threads);
	case ENoctisSchedulerKind::Serial:
		return std::make_unique<noctis::SerialScheduler>();
	}
	return std::make_unique<noctis::SerialScheduler>();
}

std::unique_ptr<noctis::FileSystem> MakeFileSystem()
{
	return std::make_unique<FNoctisUnrealFileSystem>();
}
} // namespace NoctisUnreal

bool FNoctisUnrealFileSystem::readText(const std::string& path, std::string& out) const
{
	// Raw bytes: the JSON files are UTF-8 and must reach the core unchanged (LoadFileToString would re-encode).
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *NoctisUnreal::ToFString(path), FILEREAD_Silent))
	{
		return false;
	}
	out.assign(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
	return true;
}

bool FNoctisUnrealFileSystem::writeText(const std::string& path, const std::string& content) const
{
	const FString Path = NoctisUnreal::ToFString(path);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const TArrayView<const uint8> View(reinterpret_cast<const uint8*>(content.data()), static_cast<int32>(content.size()));
	return FFileHelper::SaveArrayToFile(View, *Path);
}

bool FNoctisUnrealFileSystem::writeBinary(const std::string& path, const std::vector<noctis::u8>& bytes) const
{
	const FString Path = NoctisUnreal::ToFString(path);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const TArrayView<const uint8> View(bytes.data(), static_cast<int32>(bytes.size()));
	return FFileHelper::SaveArrayToFile(View, *Path);
}

std::vector<std::string> FNoctisUnrealFileSystem::listFiles(const std::string& directory, const std::string& extension, bool recursive) const
{
	std::vector<std::string> Result;
	const FString Dir = NoctisUnreal::ToFString(directory);
	if (!IFileManager::Get().DirectoryExists(*Dir))
	{
		return Result;
	}
	const FString Ext = NoctisUnreal::ToFString(extension); // ".json"
	TArray<FString> Found;
	if (recursive)
	{
		// Full paths.
		IFileManager::Get().FindFilesRecursive(Found, *Dir, *(FString(TEXT("*")) + Ext), /*Files*/ true, /*Directories*/ false, /*bClearFileNames*/ true);
	}
	else
	{
		// File names only.
		IFileManager::Get().FindFiles(Found, *Dir, *Ext);
		for (FString& Name : Found)
		{
			Name = FPaths::Combine(Dir, Name);
		}
	}
	Result.reserve(static_cast<size_t>(Found.Num()));
	for (const FString& F : Found)
	{
		// Same separators as std::filesystem::generic_string() in the standalone build.
		FString Normalised = F;
		FPaths::NormalizeFilename(Normalised);
		if (Normalised.EndsWith(Ext, ESearchCase::CaseSensitive))
		{
			Result.push_back(NoctisUnreal::ToStdString(Normalised));
		}
	}
	// Byte order sort, identical to the standalone file system: species indices depend on load order.
	std::sort(Result.begin(), Result.end());
	return Result;
}

bool FNoctisUnrealFileSystem::exists(const std::string& path) const
{
	const FString Path = NoctisUnreal::ToFString(path);
	return IFileManager::Get().FileExists(*Path) || IFileManager::Get().DirectoryExists(*Path);
}

bool FNoctisUnrealFileSystem::makeDirectories(const std::string& path) const
{
	return IFileManager::Get().MakeDirectory(*NoctisUnreal::ToFString(path), true);
}

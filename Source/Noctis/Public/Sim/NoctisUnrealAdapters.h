// Unreal implementations of the core's platform abstractions:
//   FNoctisParallelForScheduler : noctis::JobScheduler on the engine task graph (ParallelFor);
//   FNoctisUnrealFileSystem     : noctis::FileSystem on IFileManager / FFileHelper (works with staged and pak'd files).
#pragma once

#include "CoreMinimal.h"

#include "Noctis/Core/FileSystem.h"
#include "Noctis/Core/Jobs.h"

#include <memory>
#include <string>

class NOCTIS_API FNoctisParallelForScheduler final : public noctis::JobScheduler
{
public:
	virtual void parallelFor(size_t count, const std::function<void(size_t)>& fn, size_t minBatch) override;
	virtual int workerCount() const override;
};

class NOCTIS_API FNoctisUnrealFileSystem final : public noctis::FileSystem
{
public:
	virtual bool readText(const std::string& path, std::string& out) const override;
	virtual bool writeText(const std::string& path, const std::string& content) const override;
	virtual bool writeBinary(const std::string& path, const std::vector<noctis::u8>& bytes) const override;
	virtual std::vector<std::string> listFiles(const std::string& directory, const std::string& extension, bool recursive) const override;
	virtual bool exists(const std::string& path) const override;
	virtual bool makeDirectories(const std::string& path) const override;
};

/** Which scheduler the runner hands to the core. */
enum class ENoctisSchedulerKind : uint8
{
	ParallelFor, // engine task graph (default in game)
	ThreadPool,  // the core's own std::thread pool (same as the headless runner)
	Serial       // single-threaded reference (determinism tests)
};

namespace NoctisUnreal
{
NOCTIS_API std::unique_ptr<noctis::JobScheduler> MakeScheduler(ENoctisSchedulerKind Kind, int32 Threads);
NOCTIS_API std::unique_ptr<noctis::FileSystem> MakeFileSystem();

/** UTF-8 conversions used at the core boundary (the core stores UTF-8 std::string, mostly French text). */
NOCTIS_API FString ToFString(const std::string& Utf8);
NOCTIS_API std::string ToStdString(const FString& Text);
} // namespace NoctisUnreal

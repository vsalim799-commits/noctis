// Parallel-for abstraction. The headless build uses a std::thread pool; the Unreal module
// installs a scheduler backed by ParallelFor so simulation work runs on the engine's workers.
#pragma once

#include "Noctis/Core/Platform.h"

#include <functional>
#include <memory>

namespace noctis
{
class NOCTIS_API JobScheduler
{
public:
    virtual ~JobScheduler() = default;
    // Calls fn(i) for i in [0, count). Blocks until all calls returned.
    // fn must only write to data owned by index i (or use thread-safe sinks).
    virtual void parallelFor(size_t count, const std::function<void(size_t)>& fn, size_t minBatch = 16) = 0;
    virtual int workerCount() const = 0;
};

class NOCTIS_API SerialScheduler final : public JobScheduler
{
public:
    void parallelFor(size_t count, const std::function<void(size_t)>& fn, size_t minBatch) override;
    int workerCount() const override { return 1; }
};

// Creates a std::thread pool scheduler (threads <= 0 -> hardware concurrency).
NOCTIS_API std::unique_ptr<JobScheduler> makeThreadPoolScheduler(int threads);
} // namespace noctis

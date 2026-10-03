#include "Noctis/Core/Jobs.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace noctis
{
void SerialScheduler::parallelFor(size_t count, const std::function<void(size_t)>& fn, size_t /*minBatch*/)
{
    for (size_t i = 0; i < count; ++i)
    {
        fn(i);
    }
}

namespace jobsimpl
{
class ThreadPoolScheduler final : public JobScheduler
{
public:
    explicit ThreadPoolScheduler(int threads)
    {
        const int n = threads > 0 ? threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        for (int i = 0; i < n - 1; ++i) // calling thread participates too
        {
            workers_.emplace_back([this] { workerLoop(); });
        }
        workerCount_ = n;
    }

    ~ThreadPoolScheduler() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            ++generation_;
        }
        cv_.notify_all();
        for (std::thread& t : workers_)
        {
            t.join();
        }
    }

    void parallelFor(size_t count, const std::function<void(size_t)>& fn, size_t minBatch) override
    {
        if (count == 0)
        {
            return;
        }
        if (workers_.empty() || count <= minBatch)
        {
            for (size_t i = 0; i < count; ++i)
            {
                fn(i);
            }
            return;
        }
        std::lock_guard<std::mutex> callLock(callMutex_); // one parallelFor at a time
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_ = &fn;
            jobCount_ = count;
            batch_ = std::max<size_t>(1, std::min(minBatch, count / (static_cast<size_t>(workerCount_) * 4u) + 1u));
            next_.store(0);
            active_ = static_cast<int>(workers_.size());
            ++generation_;
        }
        cv_.notify_all();
        runBatches();
        std::unique_lock<std::mutex> lock(mutex_);
        doneCv_.wait(lock, [this] { return active_ == 0; });
        job_ = nullptr;
    }

    int workerCount() const override { return workerCount_; }

private:
    void runBatches()
    {
        while (true)
        {
            const size_t start = next_.fetch_add(batch_);
            if (start >= jobCount_)
            {
                break;
            }
            const size_t end = std::min(jobCount_, start + batch_);
            for (size_t i = start; i < end; ++i)
            {
                (*job_)(i);
            }
        }
    }

    void workerLoop()
    {
        u64 seen = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return generation_ != seen; });
                seen = generation_;
                if (stop_)
                {
                    return;
                }
            }
            runBatches();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --active_;
            }
            doneCv_.notify_one();
        }
    }

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::mutex callMutex_;
    std::condition_variable cv_;
    std::condition_variable doneCv_;
    const std::function<void(size_t)>* job_ = nullptr;
    size_t jobCount_ = 0;
    size_t batch_ = 1;
    std::atomic<size_t> next_{0};
    int active_ = 0;
    u64 generation_ = 0;
    bool stop_ = false;
    int workerCount_ = 1;
};
} // namespace jobsimpl

std::unique_ptr<JobScheduler> makeThreadPoolScheduler(int threads)
{
    return std::make_unique<jobsimpl::ThreadPoolScheduler>(threads);
}
} // namespace noctis

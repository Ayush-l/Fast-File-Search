#include "SearchEngine.h"
#include <algorithm>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <system_error>

namespace fs = std::filesystem;
namespace {
std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}
}

SearchEngine::~SearchEngine() { cancel(); wait(); }
void SearchEngine::cancel() { cancel_.store(true); }
void SearchEngine::wait() { if (coordinator_.joinable()) coordinator_.join(); }

bool SearchEngine::start(fs::path root, std::wstring query, MatchCallback onMatch, FinishCallback onFinish) {
    if (running_.load()) return false;
    wait();
    cancel_ = false;
    running_ = true;
    try {
        coordinator_ = std::thread([this, root = std::move(root), query = std::move(query),
                                    onMatch = std::move(onMatch), onFinish = std::move(onFinish)]() mutable {
            run(std::move(root), std::move(query), std::move(onMatch), std::move(onFinish));
        });
    } catch (...) { running_ = false; return false; }
    return true;
}

void SearchEngine::run(fs::path root, std::wstring query, MatchCallback onMatch, FinishCallback onFinish) {
    const auto startTime = std::chrono::steady_clock::now();
    const auto needle = lower(std::move(query));
    std::mutex queueMutex;
    std::condition_variable cv;
    std::deque<fs::path> directories;
    directories.push_back(std::move(root));
    size_t outstanding = 1; // queued + currently processed directories
    std::atomic<uint64_t> scanned{0}, matched{0};
    const unsigned workerCount = std::min(32u, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    auto worker = [&] {
        while (true) {
            fs::path dir;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                cv.wait(lock, [&] { return cancel_.load() || !directories.empty() || outstanding == 0; });
                if (cancel_ || outstanding == 0) return;
                dir = std::move(directories.front());
                directories.pop_front();
            }
            std::error_code ec;
            fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
            for (; !cancel_ && it != end; it.increment(ec)) {
                if (ec) { ec.clear(); continue; }
                const auto p = it->path();
                std::error_code typeError;
                // Don't follow directory symlinks/junctions: avoids recursive cycles.
                const bool isDir = it->is_directory(typeError);
                const bool isSymlink = it->is_symlink(typeError);
                if (isDir && !isSymlink) {
                    {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        directories.push_back(p);
                        ++outstanding;
                    }
                    cv.notify_one();
                } else if (!isDir) {
                    ++scanned;
                    if (lower(p.filename().wstring()).find(needle) != std::wstring::npos) {
                        ++matched;
                        onMatch(p.wstring());
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                --outstanding;
            }
            cv.notify_all();
        }
    };
    for (unsigned i = 0; i < workerCount; ++i) workers.emplace_back(worker);
    for (auto& t : workers) t.join();
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    const bool wasCancelled = cancel_.load();
    running_ = false;
    onFinish(wasCancelled, seconds, scanned.load(), matched.load());
}

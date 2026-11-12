#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class SearchEngine {
public:
    using MatchCallback = std::function<void(std::wstring)>;
    using FinishCallback = std::function<void(bool, double, uint64_t, uint64_t)>;
    SearchEngine() = default;
    ~SearchEngine();
    SearchEngine(const SearchEngine&) = delete;
    SearchEngine& operator=(const SearchEngine&) = delete;
    bool start(std::filesystem::path root, std::wstring query, MatchCallback onMatch, FinishCallback onFinish);
    void cancel();
    void wait();
    bool running() const { return running_.load(); }
private:
    void run(std::filesystem::path root, std::wstring query, MatchCallback onMatch, FinishCallback onFinish);
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::thread coordinator_;
};

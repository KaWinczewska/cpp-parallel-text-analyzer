#include "analyzer.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef ANALYZER_TESTING
// Defined only by the test executable; absent from the application build.
void simulateThreadCreationFailure(std::size_t workerIndex);
#endif

FileStats processFile(const std::string& filename, const std::string& searchWord) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    FileStats stats;
    std::string line;
    while (std::getline(file, line)) {
        ++stats.lines;
        std::istringstream words(line);
        std::string word;
        while (words >> word) {
            ++stats.words;
            if (word == searchWord) {
                ++stats.matches;
            }
        }
    }
    if (file.bad() || (file.fail() && !file.eof())) {
        throw std::runtime_error("Error reading file: " + filename);
    }
    return stats;
}

void addStats(FileStats& total, const FileStats& stats) {
    total.lines += stats.lines;
    total.words += stats.words;
    total.matches += stats.matches;
}

FileStats processSequential(const std::vector<std::string>& files, const std::string& searchWord) {
    FileStats total;
    for (const std::string& filename : files) {
        addStats(total, processFile(filename, searchWord));
    }
    return total;
}

namespace {
void processRange(const std::vector<std::string>& files, const std::string& searchWord, std::size_t begin,
                  std::size_t end, FileStats& result, std::exception_ptr& error) {
    try {
        for (std::size_t i = begin; i < end; ++i) {
            addStats(result, processFile(files[i], searchWord));
        }
    } catch (...) {
        error = std::current_exception();
    }
}
}  // namespace

FileStats processParallel(const std::vector<std::string>& files, const std::string& searchWord, int requestedThreads) {
    if (requestedThreads <= 0) {
        throw std::invalid_argument("Thread count must be positive");
    }
    if (files.empty()) {
        return FileStats{};
    }
    const std::size_t workerCount = std::min(files.size(), static_cast<std::size_t>(requestedThreads));
    std::vector<FileStats> results(workerCount);
    std::vector<std::exception_ptr> errors(workerCount);
    std::vector<std::thread> threads;
    threads.reserve(workerCount);

    const std::size_t filesPerWorker = files.size() / workerCount;
    const std::size_t extraFiles = files.size() % workerCount;
    std::size_t begin = 0;
    try {
        for (std::size_t i = 0; i < workerCount; ++i) {
            const std::size_t end = begin + filesPerWorker + (i < extraFiles ? 1 : 0);
#ifdef ANALYZER_TESTING
            simulateThreadCreationFailure(i);
#endif
            threads.emplace_back(processRange, std::cref(files), std::cref(searchWord), begin, end,
                                 std::ref(results[i]), std::ref(errors[i]));
            begin = end;
        }
    } catch (...) {
        // A thread-creation failure must not leave earlier workers running.
        for (std::thread& thread : threads) {
            thread.join();
        }
        throw;
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    FileStats total;
    for (std::size_t i = 0; i < workerCount; ++i) {
        if (errors[i]) {
            std::rethrow_exception(errors[i]);
        }
        addStats(total, results[i]);
    }
    return total;
}

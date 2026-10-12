#ifndef ANALYZER_H
#define ANALYZER_H

#include <cstdint>
#include <string>
#include <vector>

struct FileStats {
    std::uint64_t lines = 0;
    std::uint64_t words = 0;
    std::uint64_t matches = 0;
};

FileStats processFile(const std::string& filename, const std::string& searchWord);
void addStats(FileStats& total, const FileStats& stats);
FileStats processSequential(const std::vector<std::string>& files, const std::string& searchWord);
FileStats processParallel(const std::vector<std::string>& files, const std::string& searchWord, int requestedThreads);

#endif

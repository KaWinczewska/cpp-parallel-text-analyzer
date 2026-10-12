#include <exception>
#include <iostream>
#include <sstream>

#include "analyzer.h"

void printStats(const FileStats& stats) {
    std::cout << "Lines: " << stats.lines << '\n'
              << "Words: " << stats.words << '\n'
              << "Matches: " << stats.matches << '\n';
}

int main(int argc, char* argv[]) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if ((mode != "sequential" && mode != "parallel") || (mode == "sequential" && argc < 4) ||
        (mode == "parallel" && argc < 5)) {
        std::cerr << "Usage: " << argv[0] << " sequential SEARCH_WORD FILE [FILE ...]\n"
                  << "       " << argv[0] << " parallel THREADS SEARCH_WORD FILE [FILE ...]\n";
        return 1;
    }
    int requestedThreads = 1;
    if (mode == "parallel") {
        std::istringstream threadInput(argv[2]);
        char extra;
        if (!(threadInput >> requestedThreads) || (threadInput >> extra) || requestedThreads <= 0) {
            std::cerr << "Thread count must be a positive integer within the int range.\n";
            return 1;
        }
    }
    const int searchIndex = mode == "parallel" ? 3 : 2;
    const std::string searchWord = argv[searchIndex];
    std::istringstream searchInput(searchWord);
    std::string firstWord;
    std::string extraWord;
    if (!(searchInput >> firstWord) || (searchInput >> extraWord) || firstWord != searchWord) {
        std::cerr << "Search word must be one nonempty token without whitespace.\n";
        return 1;
    }

    try {
        std::vector<std::string> files;
        for (int i = searchIndex + 1; i < argc; ++i) {
            files.push_back(argv[i]);
        }
        if (mode == "parallel") {
            printStats(processParallel(files, searchWord, requestedThreads));
        } else {
            printStats(processSequential(files, searchWord));
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

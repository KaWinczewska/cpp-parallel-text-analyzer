#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>

#include "analyzer.h"

// Only the calling test thread reads or writes this setting.
int failBeforeWorker = -1;

void simulateThreadCreationFailure(std::size_t workerIndex) {
    if (failBeforeWorker >= 0 && workerIndex == static_cast<std::size_t>(failBeforeWorker)) {
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again),
                                "Simulated thread creation failure");
    }
}

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectStats(const FileStats& stats, std::uint64_t lines, std::uint64_t words, std::uint64_t matches) {
    check(stats.lines == lines && stats.words == words && stats.matches == matches, "Unexpected statistics");
}

void writeFile(const std::string& name, const std::string& contents) {
    std::ofstream file(name);
    file << contents;
    file.close();
    check(!file.fail(), "Cannot write test input");
}

int main() {
    // CTest runs in the build directory. Keep fixtures separate from source files.
    const std::string directory = "analyzer-test-input";
    try {
        check(std::filesystem::create_directory(directory), "Test directory already exists");
        const std::string empty = directory + "/empty.txt";
        const std::string single = directory + "/single.txt";
        const std::string multiple = directory + "/multiple.txt";
        const std::string whitespace = directory + "/whitespace.txt";
        writeFile(empty, "");
        writeFile(single, "error warning error");
        writeFile(multiple, "error warning error\nINFO error.\n");
        writeFile(whitespace, "\n \t\r\nerror\tError  error.\vERROR\ferror\n");

        expectStats(processFile(empty, "error"), 0, 0, 0);
        expectStats(processFile(single, "error"), 1, 3, 2);
        expectStats(processFile(multiple, "error"), 2, 5, 2);
        expectStats(processFile(whitespace, "error"), 3, 5, 2);
        expectStats(processFile(multiple, "error."), 2, 5, 1);
        expectStats(processSequential({}, "error"), 0, 0, 0);
        expectStats(processSequential({empty, single, multiple}, "error"), 3, 8, 4);
        expectStats(processSequential({single, single}, "error"), 2, 6, 4);
        for (int i = 0; i < 20; ++i) {
            expectStats(processSequential({single, multiple}, "error"), 3, 8, 4);
        }

        bool missingFailed = false;
        try {
            processSequential({single, directory + "/missing.txt"}, "error");
        } catch (const std::runtime_error& error) {
            missingFailed = std::string(error.what()).find("missing.txt") != std::string::npos;
        }
        check(missingFailed, "Missing file must report its path");

        // On Linux, opening a directory may succeed, but reading it must fail.
        bool readFailed = false;
        try {
            processFile(directory, "error");
        } catch (const std::runtime_error&) {
            readFailed = true;
        }
        check(readFailed, "Directory input must fail");

        const std::vector<std::string> files = {empty,    single, multiple,   whitespace, single,
                                                multiple, empty,  whitespace, single};
        const FileStats reference = processSequential(files, "error");
        for (int count : {1, 2, 4, 8, 20}) {
            for (int trial = 0; trial < 20; ++trial) {
                expectStats(processParallel(files, "error", count), reference.lines, reference.words,
                            reference.matches);
            }
            expectStats(processParallel({}, "error", count), 0, 0, 0);
            expectStats(processParallel({single}, "error", count), 1, 3, 2);
            expectStats(processParallel({empty, empty}, "error", count), 0, 0, 0);
            bool workerFailed = false;
            try {
                processParallel({single, directory + "/missing.txt", multiple}, "error", count);
            } catch (const std::runtime_error& error) {
                workerFailed = std::string(error.what()).find("missing.txt") != std::string::npos;
            }
            check(workerFailed, "Worker error must reach the caller");
        }
        for (int count : {0, -1}) {
            bool invalidFailed = false;
            try {
                processParallel({}, "error", count);
            } catch (const std::invalid_argument&) {
                invalidFailed = true;
            }
            check(invalidFailed, "Invalid thread count must fail even with no files");
        }

        for (int count : {1, 2, 4, 8}) {
            bool directoryFailed = false;
            try {
                processParallel({single, directory, multiple}, "error", count);
            } catch (const std::runtime_error& error) {
                directoryFailed = std::string(error.what()).find(directory) != std::string::npos;
            }
            check(directoryFailed, "Worker read error must reach the caller");
        }

        const std::string unreadable = directory + "/unreadable.txt";
        writeFile(unreadable, "error\n");
        std::filesystem::permissions(unreadable, std::filesystem::perms::none);
        std::ifstream permissionProbe(unreadable);
        const bool permissionsEnforced = !permissionProbe.is_open();
        permissionProbe.close();
        if (permissionsEnforced) {
            for (bool parallel : {false, true}) {
                bool denied = false;
                try {
                    if (parallel) {
                        processParallel({single, unreadable}, "error", 2);
                    } else {
                        processSequential({single, unreadable}, "error");
                    }
                } catch (const std::runtime_error& error) {
                    denied = std::string(error.what()).find(unreadable) != std::string::npos;
                }
                check(denied, "Unreadable file must report its path");
            }
        } else {
            std::cout << "SKIP: current user can read files without permission bits.\n";
        }
        std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all);

        // Failure before worker 0, 1, or 3 tests zero, one, or several started threads.
        for (int failureIndex : {0, 1, 3}) {
            for (int trial = 0; trial < 10; ++trial) {
                failBeforeWorker = failureIndex;
                bool creationFailed = false;
                try {
                    processParallel(files, "error", 4);
                } catch (const std::system_error&) {
                    creationFailed = true;
                }
                failBeforeWorker = -1;
                check(creationFailed, "Thread creation error must reach the caller");
                expectStats(processParallel(files, "error", 4), reference.lines, reference.words, reference.matches);
            }
        }
        // Exercise creation failure while an earlier worker also encounters a file error.
        failBeforeWorker = 1;
        bool creationFailed = false;
        try {
            processParallel({directory + "/missing.txt", single}, "error", 2);
        } catch (const std::system_error&) {
            creationFailed = true;
        }
        failBeforeWorker = -1;
        check(creationFailed, "Creation error takes precedence after joining earlier workers");
        std::filesystem::remove_all(directory);
        std::cout << "All sequential and parallel tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

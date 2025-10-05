#include <istream>
#include <qumir/runner/runner_ir.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <chrono>

using namespace NQumir;

namespace {

void PrintResultIR(const std::optional<std::string>& v) {
    if (v.has_value()) {
        std::cout << *v << std::endl;
    } else {
        std::cout << "<null>" << std::endl;
    }
}

} // namespace

int main(int argc, char ** argv) {
    enum class RunnerType { IR, LLVM };
    RunnerType runnerType = RunnerType::IR; // default
    bool printEvalTimeUs = false;
    bool printAst = false;
    bool printIr = false;
    bool printLlvm = false;
    int optLevel = 0;
    std::string inputFile; // stdin by default if empty

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--runner-type")) {
            if (i + 1 < argc) {
                const char* v = argv[++i];
                if (!std::strcmp(v, "ir")) {
                    runnerType = RunnerType::IR;
                } else if (!std::strcmp(v, "llvm")) {
                    runnerType = RunnerType::LLVM;
                } else {
                    std::cerr << "Unknown runner type '" << v << "', using 'ir' by default\n";
                    runnerType = RunnerType::IR;
                }
            } else {
                std::cerr << "--runner-type requires a value: ir|llvm; using 'ir' by default\n";
                runnerType = RunnerType::IR;
            }
        } else if (!std::strcmp(argv[i], "--time-us")) {
            printEvalTimeUs = true;
        } else if (!std::strcmp(argv[i], "--print-ast")) {
            printAst = true;
        } else if (!std::strcmp(argv[i], "--print-ir")) {
            printIr = true;
        } else if (!std::strcmp(argv[i], "--print-llvm")) {
            printLlvm = true;
        } else if (!std::strcmp(argv[i], "--input-file") || !std::strcmp(argv[i], "-i")) {
            if (i + 1 < argc) {
                inputFile = argv[++i];
            } else {
                std::cerr << "--input-file requires a filename argument\n";
                return 1;
            }
        } else if (!std::strcmp(argv[i], "-O")) {
            if (i + 1 < argc) {
                optLevel = std::atoi(argv[++i]);
                if (optLevel < 0 || optLevel > 3) {
                    std::cerr << "Optimization level must be between 0 and 3\n";
                    return 1;
                }
            } else {
                std::cerr << "-O requires an argument\n";
                return 1;
            }
        } else if (!std::strcmp(argv[i], "-O0")) {
            optLevel = 0;
        } else if (!std::strcmp(argv[i], "-O1")) {
            optLevel = 1;
        } else if (!std::strcmp(argv[i], "-O2")) {
            optLevel = 2;
        } else if (!std::strcmp(argv[i], "-O3")) {
            optLevel = 3;
        } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::cout << "ozrepl [options]\n"
                         "Options:\n"
                         "  --runner-type <type>  Select runner type: ir (default), llvm\n"
                         "  --time-us             Print evaluation time in microseconds\n"
                         "  --print-ast          Print AST after parsing\n"
                         "  --print-ir           Print IR after lowering\n"
                         "  --print-llvm         Print LLVM IR after codegen\n"
                         "  --input-file|-i <file>  Input file (default: stdin)\n"
                         "  -O <level>           Optimization level (0-3), default 0\n"
                         "  -O0                  Optimization level 0 (no optimizations)\n"
                         "  -O1                  Optimization level 1 (some optimizations)\n"
                         "  -O2                  Optimization level 2 (more optimizations)\n"
                         "  -O3                  Optimization level 3 (aggressive optimizations)\n"
                         "  --help, -h           Show this help message\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            return 1;
        }
    }

    std::istream* in = &std::cin;
    std::ifstream infile;
    if (!inputFile.empty()) {
        infile.open(inputFile);
        if (!infile.is_open()) {
            std::cerr << "Failed to open input file: " << inputFile << "\n";
            return 1;
        }
        in = &infile;
    }

    TIRRunner irRunner(TIRRunnerOptions {
        .PrintAst = printAst,
        .PrintIr = printIr
    });

    auto t0 = std::chrono::steady_clock::now();
    auto r = irRunner.Run(*in);
    auto t1 = std::chrono::steady_clock::now();

    std::optional<std::string> irValue;
    long long lastEvalUs = 0;
    std::string errStr;

    if (!r) {
        errStr = r.error().ToString();
    } else {
        irValue = std::move(r.value());
        lastEvalUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    }

    PrintResultIR(irValue);
    if (printEvalTimeUs) {
        std::cout << lastEvalUs << " us" << std::endl;
    }

    return 0;
}

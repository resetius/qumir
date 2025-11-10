#include <cstring>
#include <iostream>
#include <fstream>
#include <cstdlib>

#include <qumir/parser/parser.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/builder.h>
#include <qumir/ir/passes/transforms/pipeline.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/codegen/llvm/llvm_initializer.h>
#include <qumir/modules/system/system.h>
#include <sstream>
#include <string>

#include <sys/wait.h>
#include <thread>

#ifdef _WIN32
static constexpr std::string A_OUT = "a.exe";
#else
static constexpr std::string A_OUT = "a.out";
#endif

using namespace NQumir;

namespace {

int GenerateAst(const std::string& inputFile, const std::string& outputFile, bool verbose) {
    if (verbose) {
        std::cerr << "Generating AST from " << inputFile << " to " << outputFile << "\n";
    }

    std::ifstream in(inputFile);
    if (!in) {
        std::cerr << "Failed to open input file: " << inputFile << "\n";
        return 1;
    }

    NAst::TTokenStream ts(in);
    NAst::TParser p;
    auto&& expected = p.parse(ts);
    if (!expected.has_value()) {
        std::cerr << "Parse error: " << expected.error().ToString() << std::endl;
        return 1;
    }
    auto ast = std::move(expected.value());
    std::ofstream out(outputFile);
    if (!out) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        return 1;
    }

    out << *ast;
    return 0;
}

int GenerateIr(const std::string& inputFile, const std::string& outputFile, int optLevel, bool verbose) {
    if (verbose) {
        std::cerr << "Generating IR from " << inputFile << " to " << outputFile << "\n";
    }

    std::ifstream in(inputFile);
    if (!in) {
        std::cerr << "Failed to open input file: " << inputFile << "\n";
        return 1;
    }

    NAst::TTokenStream ts(in);
    NAst::TParser p;
    auto&& expected = p.parse(ts);
    if (!expected.has_value()) {
        std::cerr << "Parse error: " << expected.error().ToString() << std::endl;
        return 1;
    }
    auto ast = std::move(expected.value());

    NSemantics::TNameResolver r;
    NRegistry::SystemModule().Register(r);

    auto error = NTransform::Pipeline(ast, r);
    if (!error) {
        std::cerr << "Transform error: " << error.error().ToString() << "\n";
        return 1;
    }

    NIR::TModule module;
    NIR::TBuilder builder(module);

    NIR::TAstLowerer lowerer(module, builder, r);
    auto lowerResult = lowerer.LowerTop(ast);
    if (!lowerResult.has_value()) {
        std::cerr << "Lowering error: " << lowerResult.error().ToString() << "\n";
        return 1;
    }
    if (optLevel > 0) {
        NIR::NPasses::Pipeline(module);
    }

    std::ofstream out(outputFile);
    if (!out) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        return 1;
    }

    module.Print(out);
    return 0;
}

int GenerateLlvm(const std::string& inputFile, const std::string& outputFile, int optLevel, bool verbose) {
    if (verbose) {
        std::cerr << "Generating LLVM IR from " << inputFile << " to " << outputFile << "\n";
    }

    std::ifstream in(inputFile);
    if (!in) {
        std::cerr << "Failed to open input file: " << inputFile << "\n";
        return 1;
    }

    NAst::TTokenStream ts(in);
    NAst::TParser p;
    auto&& expected = p.parse(ts);
    if (!expected.has_value()) {
        std::cerr << "Parse error: " << expected.error().ToString() << std::endl;
        return 1;
    }
    auto ast = std::move(expected.value());

    NSemantics::TNameResolver r;
    NRegistry::SystemModule().Register(r);

    auto error = NTransform::Pipeline(ast, r);
    if (!error) {
        std::cerr << "Transform error: " << error.error().ToString() << "\n";
        return 1;
    }

    NIR::TModule module;
    NIR::TBuilder builder(module);

    NIR::TAstLowerer lowerer(module, builder, r);
    auto lowerResult = lowerer.LowerTop(ast);
    if (!lowerResult.has_value()) {
        std::cerr << "Lowering error: " << lowerResult.error().ToString() << "\n";
        return 1;
    }

    NCodeGen::TLLVMCodeGen cg;
    auto artifacts = cg.Emit(module, optLevel);
    if (!artifacts) {
        std::cerr << "Codegen error " << "\n";
        return 1;
    }

    std::ofstream out(outputFile);
    if (!out) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        return 1;
    }

    artifacts->PrintModule(out);
    return 0;
}

#if 0
struct TSpawner {
    TSpawner(std::string command, std::vector<const char*> args)
        : Command(std::move(command))
        , Args(std::move(args))
    {
    }

    void Run() {
        std::cerr << "Spawning process: " << Command;
        for (const auto& arg : Args) {
            std::cerr << " " << arg;
        }
        std::cerr << "\n";

        if (pipe(Pipefd) == -1) {
            throw std::runtime_error("pipe() failed");
        }

        Pid = fork();
        if (Pid == -1) {
            throw std::runtime_error("fork() failed");
        }

        if (Pid == 0) {
            // Child process
            close(Pipefd[0]); // Close read end
            dup2(Pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
            close(Pipefd[1]); // Close original write end

            std::vector<char*> execArgs;
            execArgs.push_back(const_cast<char*>(Command.c_str()));
            for (const auto& arg : Args) {
                execArgs.push_back(const_cast<char*>(arg));
            }
            execArgs.push_back(nullptr);

            execvp(Command.c_str(), execArgs.data());
            // If execvp returns, an error occurred
            std::cerr << "execvp() failed\n";
            exit(1);
        } else {
            // Parent process
            close(Pipefd[1]); // Close write end
        }
    }

    void Wait() {
        int status;
        waitpid(Pid, &status, 0);
        if (WIFEXITED(status)) {
            int exitStatus = WEXITSTATUS(status);
            if (exitStatus != 0) {
                throw std::runtime_error("Child process exited with status " + std::to_string(exitStatus));
            }
        } else {
            throw std::runtime_error("Child process did not terminate normally");
        }
    }

    int GetReadFd() const {
        return Pipefd[0];
    }

    int GetWriteFd() const {
        return Pipefd[1];
    }

    std::string Command;
    std::vector<const char*> Args;
    int Pipefd[2];
    int Pid;
};

void GenerateObjFromAsm(const std::string& asmCode, std::ostream& objOut) {
    TSpawner spawner("/usr/bin/as", {"as", "-o", "/dev/stdout", "-", nullptr});

    spawner.Run();

    auto reader = std::jthread([&]() {
        char buffer[4096];
        ssize_t bytesRead;
        while ((bytesRead = read(spawner.GetReadFd(), buffer, sizeof(buffer))) > 0) {
            objOut.write(buffer, bytesRead);
        }
        close(spawner.GetReadFd());
    });

    auto writer = std::jthread([&]() {
        ssize_t totalWritten = 0;
        ssize_t toWrite = asmCode.size();
        const char* data = asmCode.data();
        while (totalWritten < toWrite) {
            ssize_t written = write(spawner.GetWriteFd(), data + totalWritten, toWrite - totalWritten);
            if (written == -1) {
                if (errno == EINTR) continue; // Retry on interrupt
                std::cerr << "write() failed\n";
                break;
            }
            totalWritten += written;
        }
        close(spawner.GetWriteFd());
    });

    reader.join();
    writer.join();

    spawner.Wait();
}
#endif

int Generate(const std::string& inputFile, const std::string& outputFile, bool compileOnly, bool generateAsm, int optLevel, bool targetWasm, bool verbose) {
    if (verbose) {
        std::cerr << "Compiling " << inputFile << " to " << outputFile << "\n";
    }

    std::ifstream in(inputFile);
    if (!in) {
        std::cerr << "Failed to open input file: " << inputFile << "\n";
        return 1;
    }

    NAst::TTokenStream ts(in);
    NAst::TParser p;
    auto&& expected = p.parse(ts);
    if (!expected.has_value()) {
        std::cerr << "Parse error: " << expected.error().ToString() << std::endl;
        return 1;
    }
    auto ast = std::move(expected.value());

    NSemantics::TNameResolver r;
    NRegistry::SystemModule().Register(r);

    auto error = NTransform::Pipeline(ast, r);
    if (!error) {
        std::cerr << "Transform error: " << error.error().ToString() << "\n";
        return 1;
    }

    NIR::TModule module;
    NIR::TBuilder builder(module);

    NIR::TAstLowerer lowerer(module, builder, r);
    auto lowerResult = lowerer.LowerTop(ast);
    if (!lowerResult.has_value()) {
        std::cerr << "Lowering error: " << lowerResult.error().ToString() << "\n";
        return 1;
    }
    if (optLevel > 0) {
        NIR::NPasses::Pipeline(module);
    }

    NCodeGen::TLLVMCodeGenOptions cgOpts;
    if (targetWasm) {
        cgOpts.TargetTriple = "wasm32-unknown-unknown";
    }
    NCodeGen::TLLVMCodeGen cg(cgOpts);
    auto artifacts = cg.Emit(module, optLevel);
    if (!artifacts) {
        std::cerr << "Codegen error " << "\n";
        return 1;
    }

    // Special path: when targeting wasm and linking, produce a final .wasm via wasm-ld
    if (targetWasm && !generateAsm && !compileOnly) {
        // TODO: move to ->Generate
        const std::string objTmp = outputFile + ".tmp.o";
        {
            std::ofstream objFile(objTmp, std::ios::binary);
            if (!objFile) {
                std::cerr << "Failed to open temp object file: " << objTmp << "\n";
                return 1;
            }
            artifacts->Generate(objFile, /*asm*/false, /*obj*/true);
        }
        std::string cmd = std::string("wasm-ld --no-entry --export-all --allow-undefined -o ") + outputFile + " " + objTmp;
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            std::cerr << "wasm-ld failed with code " << rc << "\n";
            return 1;
        }
        std::remove(objTmp.c_str());
        return 0;
    }

    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        return 1;
    }
    artifacts->Generate(outFile, generateAsm, compileOnly && !generateAsm);
    return 0;
}

std::string OutputFilename(const std::string& inputFile, const std::string& newExt) {
    auto dotPos = inputFile.rfind('.');
    if (dotPos != std::string::npos) {
        return inputFile.substr(0, dotPos) + newExt;
    } else {
        return inputFile + newExt;
    }
}

} // namespace {

// Compiler driver (stub)
int main(int argc, char** argv) {
    NCodeGen::TLLVMInitializer llvmInit;

    bool compileOnly = false;
    std::string outputFile;
    std::string inputFile;
    bool generateAst = false;
    bool generateIr = false;
    bool generateLlvm = false;
    bool generateAsm = false;
    int optLevel = 0;
    bool targetWasm = false;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-c")) {
            compileOnly = true;
        } else if (!std::strcmp(argv[i], "-o")) {
            if (i + 1 < argc) {
                outputFile = argv[++i];
            } else {
                std::cerr << "-o requires an argument\n";
                return 1;
            }
        } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::cout << "qumirc [options] <input file>\n"
                         "Options:\n"
                         "  -c            Compile only, do not link\n"
                         "  -o <file>     Write output to <file> (default: " << (compileOnly ? "N/A" : A_OUT) << ")\n"
                         "  --ast         Generate AST only (no IR, no codegen)\n"
                         "  --ir          Generate IR only (no codegen)\n"
                         "  --wasm        Target WebAssembly (wasm32-unknown-unknown)\n"
                         "  -S            Generate assembly only (no linking), implies -c\n"
                         "  -O <level>    Optimization level (0-3), default 0\n"
                         "  -O0           Optimization level 0 (no optimizations)\n"
                         "  -O1           Optimization level 1\n"
                         "  -O2           Optimization level 2\n"
                         "  -O3           Optimization level 3\n"
                         "  --verbose     Enable verbose output\n"
                         "  --version, -v Show version information\n"
                         "  --help, -h    Show this help message\n";
            return 0;
        } else if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::cout << "qumirc version 0.0.1\n";
            std::cout << "Build Date: " << __DATE__ << "\n";
            std::cout << "Build Time: " << __TIME__ << "\n";
            return 0;
        } else if (!std::strcmp(argv[i], "--ast")) {
            generateAst = true;
        } else if (!std::strcmp(argv[i], "--ir")) {
            generateIr = true;
        } else if (!std::strcmp(argv[i], "--llvm")) {
            generateLlvm = true;
        } else if (!std::strcmp(argv[i], "--wasm")) {
            targetWasm = true;
        } else if (!std::strcmp(argv[i], "-S")) {
            generateAsm = true;
            compileOnly = true;
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
        } else if (!std::strcmp(argv[i], "--verbose")) {
            verbose = true;
        } else if (argv[i][0] == '-') {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            return 1;
        } else {
            inputFile = argv[i];
        }
    }
    if (inputFile.empty()) {
        std::cerr << "No input file\n";
        return 1;
    }

    if (generateAst) {
        if (outputFile.empty()) {
            outputFile = OutputFilename(inputFile, ".ast");
        }
        return GenerateAst(inputFile, outputFile, verbose);
    }

    if (generateIr) {
        if (outputFile.empty()) {
            outputFile = OutputFilename(inputFile, ".ir");
        }
        return GenerateIr(inputFile, outputFile, optLevel, verbose);
    }

    if (generateLlvm) {
        if (outputFile.empty()) {
            outputFile = OutputFilename(inputFile, ".ll");
        }
        return GenerateLlvm(inputFile, outputFile, optLevel, verbose);
    }

    if (!compileOnly && outputFile.empty()) {
        outputFile = targetWasm ? OutputFilename(inputFile, ".wasm") : A_OUT;
    }

    std::string finalOutput = outputFile;
    if (finalOutput.empty()) {
        finalOutput = compileOnly
            ? (generateAsm
                ? OutputFilename(inputFile, ".s")
                : OutputFilename(inputFile, ".o"))
            : outputFile;
    }

    return Generate(inputFile, finalOutput, compileOnly, generateAsm, optLevel, targetWasm, verbose);
}
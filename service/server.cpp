#include <chrono>
#include <iostream>
#include <fstream>
#include <vector>
#include <set>

#include <filesystem>

#include <llvm/Support/JSON.h>
#include <llvm/Support/Error.h>

#include <coroio/all.hpp>
#include <coroio/http/httpd.hpp>
#include <coroio/pipe/pipe.hpp>
#include <coroio/ws/utils.hpp>

using namespace NNet;

namespace {

std::string UrlEncode(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            char buf[16];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            result += buf;
        }
    }
    return result;
}

} // namespace

struct TOptions {
    std::function<TPipe(const std::string&, const std::vector<std::string>&, bool)> PipeFactory;
    std::string StaticDir = "static";
    std::string BinaryDir = "bin";
    std::string ExamplesDir = "examples";
    std::string SharedLinksDir = "shared";
};

class TRouter : public IRouter {
public:
    TRouter(TOptions options)
        : PipeFactory(std::move(options.PipeFactory))
        , StaticDir(std::move(options.StaticDir))
        , BinaryDir(std::move(options.BinaryDir))
        , ExamplesDir(std::move(options.ExamplesDir))
        , SharedLinksDir(std::move(options.SharedLinksDir))
    {
        std::error_code ec;
        StaticBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(StaticDir), ec);
        if (ec || StaticBaseCanonical.empty()) {
            // Fallback to lexical normalization if canonicalization fails
            StaticBaseCanonical = std::filesystem::path(StaticDir).lexically_normal();
        }

        BinaryBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(BinaryDir), ec);
        if (ec || BinaryBaseCanonical.empty()) {
            // Fallback to lexical normalization if canonicalization fails
            BinaryBaseCanonical = std::filesystem::path(BinaryDir).lexically_normal();
        }

        ExamplesBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(ExamplesDir), ec);
        if (ec || ExamplesBaseCanonical.empty()) {
            // Fallback to lexical normalization if canonicalization fails
            ExamplesBaseCanonical = std::filesystem::path(ExamplesDir).lexically_normal();
        }

        SharedLinksBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(SharedLinksDir), ec);
        if (ec || SharedLinksBaseCanonical.empty()) {
            // Fallback to lexical normalization if canonicalization fails
            SharedLinksBaseCanonical = std::filesystem::path(SharedLinksDir).lexically_normal();
        }
    }

    TFuture<void> HandleRequest(TRequest& request, TResponse& response) override {
        if (request.Method() == "OPTIONS") {
            response.SetStatus(200);
            response.SetHeader("Access-Control-Allow-Origin", "*");
            response.SetHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            response.SetHeader("Access-Control-Allow-Headers", "Content-Type, X-Qumir-O");
            response.SetHeader("Content-Length", "0");
            co_await response.SendHeaders();
        } else if (request.Method() == "GET") {
            co_await Get(request, response);
        } else if (request.Method() == "POST") {
            co_await Post(request, response);
        } else {
            response.SetStatus(405);
            response.SetHeader("Content-Type", "text/plain");
            response.SetHeader("Connection", "close");
            co_await response.SendHeaders();
            co_await response.WriteBodyFull("Method Not Allowed");
        }
    }

private:
    void Trim(std::string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        size_t last = s.find_last_not_of(" \t\r\n");
        if (first == std::string::npos || last == std::string::npos) {
            s = "";
        } else {
            s = s.substr(first, last - first + 1);
        }
    }

    TFuture<void> SendJson(TResponse& response, const std::string& json, int statusCode = 200) {
        response.SetStatus(statusCode);
        response.SetHeader("Content-Type", "application/json; charset=utf-8");
        response.SetHeader("Content-Length", std::to_string(json.size()));
        co_await response.SendHeaders();
        co_await response.WriteBodyFull(json);
    }

    TFuture<void> Send404(TResponse& response, const std::string& message = "Not Found") {
        response.SetStatus(404);
        response.SetHeader("Content-Type", "text/plain");
        response.SetHeader("Content-Length", std::to_string(message.size()));
        co_await response.SendHeaders();
        co_await response.WriteBodyFull(message);
    }

    TFuture<void> Get(const TRequest& request, TResponse& response) {
        auto&& path = request.Uri().Path();
        if (path == "/api/version") {
            auto [shortOut, shortCode] = co_await ReadPipe("git", {"rev-parse", "--short", "HEAD"}, false, true);
            auto [dateOut, dateCode] = co_await ReadPipe("git", {"log", "-1", "--date=iso-strict", "--format=%cd"}, false, true);
            auto [branchOut, branchCode] = co_await ReadPipe("git", {"rev-parse", "--abbrev-ref", "HEAD"}, false, true);
            if (shortCode != 0 || dateCode != 0 || branchCode != 0) {
                co_await SendJson(response, "{\"error\":\"git command failed\"}", 500);
                co_return;
            } else {
                Trim(shortOut); Trim(dateOut); Trim(branchOut);
                std::string json = std::string("{\"hash\":\"") + shortOut + "\",\"date\":\"" + dateOut + "\",\"branch\":\"" + branchOut + "\"}";
                co_await SendJson(response, json);
                co_return;
            }
        } else if (path == "/api/examples") {
            std::vector<llvm::json::Value> items;
            for (auto& p : std::filesystem::recursive_directory_iterator(ExamplesBaseCanonical)) {
                if (p.is_regular_file()) {
                    auto ext = p.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".kum") {
                        std::filesystem::path relPath = std::filesystem::relative(p.path(), ExamplesBaseCanonical);
                        items.push_back(llvm::json::Object{
                            {"path", relPath.generic_string()},
                            {"name", p.path().filename().string()}
                        });
                    }
                }
            }
            llvm::json::Object root{
                {"examples", llvm::json::Array(std::move(items))}
            };
            std::string jsonStr;
            llvm::raw_string_ostream os(jsonStr);
            os << llvm::json::Value(std::move(root));
            os.flush();
            co_await SendJson(response, jsonStr);
        } else if (path == "/api/example") {
            auto queryParams = request.Uri().QueryParameters();
            auto it = queryParams.find("path");
            if (it == queryParams.end()) {
                co_await SendJson(response, "{\"error\":\"missing 'path' query parameter\"}", 400);
                co_return;
            }
            co_await ServeStaticFile(response, it->second, ExamplesBaseCanonical);
        } else if (path == "/api/share") {
            co_await ServeShare(request, response);
        } else if (path.find("/s/") == 0) {
            // redirect => /index.html?share={uriencode(sid)}
            auto shareId = path.substr(3); // after /s/
            // file = path / shareId . kum
            std::filesystem::path shareFilePath = SharedLinksBaseCanonical / (shareId + ".kum");
            if (!std::filesystem::exists(shareFilePath) || !std::filesystem::is_regular_file(shareFilePath)) {
                co_await Send404(response);
                co_return;
            }
            std::string redirectUrl = "/index.html?share=" + UrlEncode(shareId);
            response.SetStatus(302);
            response.SetHeader("Location", redirectUrl);
            response.SetHeader("Content-Length", "0");
            co_await response.SendHeaders();
        } else {
            co_await ServeStaticFile(response, request.Uri().Path(), StaticBaseCanonical);
        }
    }

    TFuture<void> Post(TRequest& request, TResponse& response) {
        auto&& path = request.Uri().Path();
        if (path == "/api/compile-ir") {
            co_await Compile(request, response, "ir");
        } else if (path == "/api/compile-llvm") {
            co_await Compile(request, response, "llvm");
        } else if (path == "/api/compile-asm") {
            co_await Compile(request, response, "asm");
        } else if (path == "/api/compile-llvm") {
            co_await Compile(request, response, "llvm");
        } else if (path == "/api/compile-wasm") {
            co_await Compile(request, response, "wasm");
        } else if (path == "/api/compile-wasm-text") {
            co_await Compile(request, response, "wasm-text");
        } else if (path == "/api/share") {
            co_await ServeShareCreate(request, response);
        } else {
            co_await Send404(response);
        }
    }

    TFuture<void> Compile(TRequest& request, TResponse& response, const std::string& target) {
        int olevel = 0;
        auto it = request.Headers().find("X-Qumir-O");
        if (it != request.Headers().end()) {
            std::string val(it->second);
            bool ok = true;
            try {
                olevel = std::stoi(val);
            } catch (...) {
                ok = false;
            }
            if (!ok) {
                co_await SendJson(response, "{\"error\":\"X-Qumir-O must be numeric\"}", 400);
                co_return;
            }
        }
        if (olevel < 0) olevel = 0; if (olevel > 3) olevel = 3;

        // qumirc: BinaryBaseCanonical / qumirc
        auto qumirc = (BinaryBaseCanonical / "qumirc").generic_string();
        std::vector<std::string> args;

        auto printCmd = [&]() {
            std::string cmdStr = qumirc;
            for (const auto& arg : args) cmdStr += " " + arg;
            std::cerr << "Running command: " << cmdStr << std::endl;
        };

        // Stream for all but wasm binary (wasm-ld does not support streaming)

        if (target != "wasm") {
            if (target == "ir") {
                args = {"--ir", "-O" + std::to_string(olevel), "-o", "-", "-"};
            } else if (target == "llvm") {
                args = {"--llvm", "-O" + std::to_string(olevel), "-o", "-", "-"};
            } else if (target == "asm") {
                args = {"-S", "-O" + std::to_string(olevel), "-o", "-", "-"};
            } else if (target == "wasm-text") {
                args = {"--wasm", "-S", "-O" + std::to_string(olevel), "-o", "-", "-"};
            }

            printCmd();
            auto pipe = PipeFactory(qumirc, args, /* stderr to stdout */ true);

            {
                char ibuf[1024];
                while (true) {
                    ssize_t n = co_await request.ReadBodySome(ibuf, sizeof(ibuf));
                    if (n <= 0) break;
                    co_await TByteWriter(pipe).Write(ibuf, n);
                }
                pipe.CloseWrite();
            }

            response.SetStatus(200);
            response.SetHeader("Content-Type", "text/plain; charset=utf-8");
            response.SetHeader("Transfer-Encoding", "chunked");
            response.SetHeader("X-Qumir-O", std::to_string(olevel));
            co_await response.SendHeaders();

            char obuf[4096];
            auto reader = TByteReader(pipe); // buffered reader for pipe
            while (true) {
                ssize_t r = co_await reader.ReadSome(obuf, sizeof(obuf));
                if (r <= 0) break;
                co_await response.WriteBodyChunk(obuf, r);
            }
            co_await response.WriteBodyChunk("", 0);
            pipe.Wait();
            co_return;
        }

        // wasm binary: keep file-based path (read entire body)
        std::string code = co_await request.ReadBodyFull();
        if (code.empty()) {
            co_await SendJson(response, "{\"error\":\"empty body\"}", 400);
            co_return;
        }
        auto src = WriteTemp(code);
        std::string dst;
        std::string contentType = "application/wasm";
        dst = src + ".wasm";
        args = {"--wasm", "-O" + std::to_string(olevel), "-o", dst, src};

        printCmd();
        auto [output, exitCode] = co_await ReadPipe(qumirc, args, /*stderr*/ false, /*searchExe*/ false, /*stderrToStdout*/ true);
        if (exitCode != 0) {
            std::string errBody;
            llvm::json::Object obj;
            obj["error"] = std::string("compilation failed with code ") + std::to_string(exitCode);
            obj["output"] = output;
            llvm::raw_string_ostream os(errBody);
            os << llvm::json::Value(std::move(obj));
            co_await SendJson(response, errBody);
        } else {
            std::ifstream ifs(dst, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            response.SetStatus(200);
            response.SetHeader("Content-Type", contentType);
            response.SetHeader("Content-Length", std::to_string(content.size()));
            response.SetHeader("X-Qumir-O", std::to_string(olevel));
            co_await response.SendHeaders();
            co_await response.WriteBodyFull(content);
        }
        std::remove(src.c_str());
        std::remove(dst.c_str());
    }

    TFuture<void> ServeShare(const TRequest& request, TResponse& response) {
        auto queryParams = request.Uri().QueryParameters();
        auto it = queryParams.find("id");
        if (it == queryParams.end()) {
            co_await SendJson(response, "{\"error\":\"missing 'id' query parameter\"}", 400);
            co_return;
        }
        auto shareId = it->second;
        // file = path / shareId . kum
        std::filesystem::path shareFilePath = SharedLinksBaseCanonical / (shareId + ".kum");
        if (!std::filesystem::exists(shareFilePath) || !std::filesystem::is_regular_file(shareFilePath)) {
            co_await Send404(response);
            co_return;
        }
        std::ifstream ifs(shareFilePath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        std::filesystem::path shareMetaPath = SharedLinksBaseCanonical / (shareId + ".json");
        if (std::filesystem::exists(shareMetaPath) && std::filesystem::is_regular_file(shareMetaPath)) {
            // read metadata
            std::ifstream mfs(shareMetaPath, std::ios::binary);
            std::string metaContent((std::istreambuf_iterator<char>(mfs)), std::istreambuf_iterator<char>());

            llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(metaContent);
            llvm::json::Object metaObj;
            if (parsed) {
                if (auto *obj = parsed->getAsObject()) {
                    metaObj = *obj;
                }
            }

            llvm::json::Object rootObj;
            rootObj["code"] = content;
            for (auto &entry : metaObj) {
                rootObj[entry.first] = entry.second;
            }

            std::string json;
            {
                llvm::raw_string_ostream os(json);
                os << llvm::json::Value(std::move(rootObj));
            }
            co_await SendJson(response, json);
            co_return;
        }

        response.SetStatus(200);
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetHeader("Content-Length", std::to_string(content.size()));
        co_await response.SendHeaders();
        co_await response.WriteBodyFull(content);
    }

    TFuture<void> ServeShareCreate(TRequest& request, TResponse& response) {
        std::string payloadJson = co_await request.ReadBodyFull();
        // Parse JSON payload and extract 'code', optional 'args' and 'stdin'
        llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(payloadJson);
        if (!parsed) {
            std::string errMsg;
            llvm::handleAllErrors(parsed.takeError(), [&](const llvm::ErrorInfoBase &E){ errMsg = E.message(); });
            co_await SendJson(response, std::string("{\"error\":\"json parse failed: ") + errMsg + "\"}", 400);
            co_return;
        }

        llvm::json::Value &root = *parsed;
        auto *obj = root.getAsObject();
        if (!obj) {
            co_await SendJson(response, "{\"error\":\"object root required\"}", 400);
            co_return;
        }

        // code
        std::string code;
        if (auto *codeVal = obj->get("code")) {
            if (auto codeStr = codeVal->getAsString()) {
                code = *codeStr;
            } else {
                co_await SendJson(response, "{\"error\":\"code must be string\"}", 400);
                co_return;
            }
        } else {
            co_await SendJson(response, "{\"error\":\"code missing\"}", 400);
            co_return;
        }

        const llvm::json::Value* argsVal = obj->get("args");
        const llvm::json::Value* stdinVal = obj->get("stdin");
        const llvm::json::Value* filesVal = obj->get("files");
        std::string shareId = ComputeSharedId(code, argsVal, stdinVal, filesVal);

        // write code to file
        std::filesystem::path shareFilePath = SharedLinksBaseCanonical / (shareId + ".kum");
        {
            std::ofstream ofs(shareFilePath, std::ios::binary);
            ofs << code;
        }
        // write metadata to .json
        std::filesystem::path shareMetaPath = SharedLinksBaseCanonical / (shareId + ".json");
        {
            llvm::json::Object metaObj;
            if (auto argsVal = obj->get("args")) {
                metaObj["args"] = *argsVal;
            }
            if (auto stdinVal = obj->get("stdin")) {
                metaObj["stdin"] = *stdinVal;
            }
            if (auto filesVal = obj->get("files")) {
                metaObj["files"] = *filesVal;
            }
            std::string metaJson;
            {
                llvm::raw_string_ostream os(metaJson);
                os << llvm::json::Value(std::move(metaObj));
            }
            std::ofstream mofs(shareMetaPath, std::ios::binary);
            mofs << metaJson;
        }

        std::string host;
        auto maybeHost = request.Headers().find("Host");
        if (maybeHost == request.Headers().end()) {
            host = "localhost";
        } else {
            host = maybeHost->second;
        }
        std::string base = "http://" + host;
        co_await SendJson(response, std::string("{\"id\":\"") + shareId +
            "\",\"url\":\"" + base + "/s/" + shareId +
            "\",\"raw_url\":\"" + base + "/api/share?id=" + UrlEncode(shareId) + "\"}");
    }

    std::string ComputeSharedId(const std::string& code, const llvm::json::Value* argsVal, const llvm::json::Value* stdinVal, const llvm::json::Value* filesVal) {
        // simple hash: sha1(code + args + stdin + files)
        std::string toHash = code;
        if (argsVal) {
            std::string argsStr;
            {
                llvm::raw_string_ostream os(argsStr);
                os << *argsVal;
            }
            toHash += argsStr;
        }
        if (stdinVal) {
            std::string stdinStr;
            {
                llvm::raw_string_ostream os(stdinStr);
                os << *stdinVal;
            }
            toHash += stdinStr;
        }
        if (filesVal) {
            std::string filesStr;
            {
                llvm::raw_string_ostream os(filesStr);
                os << *filesVal;
            }
            toHash += filesStr;
        }

        // compute sha1
        unsigned char hash[41];
        NUtils::SHA1Digest(reinterpret_cast<const unsigned char*>(toHash.data()), toHash.size(), hash);
        // convert to hex
        std::string sharedId = NUtils::Base64Encode(hash, 9);
        // make URL-safe: replace '+' with '-', '/' with '_', remove '='
        std::string hashHex;
        for (char c : sharedId) {
            if (c == '+') {
                hashHex += '-';
            } else if (c == '/') {
                hashHex += '_';
            } else if (c == '=') {
                // skip
            } else {
                hashHex += c;
            }
        }
        return hashHex;
    }

    TFuture<std::pair<std::string,int>> ReadPipe(std::string exe, const std::vector<std::string>& args, bool stderr = true, bool searchExe = false, bool stderrToStdout = false) {
        if (searchExe) {
            exe = SearchInPath(exe);
        }
        auto pipe = PipeFactory(exe, args, stderrToStdout);
        pipe.CloseWrite();
        if (stderr) {
            pipe.CloseRead();
        } else {
            pipe.CloseErr();
        }

        std::string output;
        char buf[4096];
        while (true) {
            ssize_t n = stderr ? co_await pipe.ReadSomeErr(buf, sizeof(buf)) : co_await pipe.ReadSome(buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            output.append(buf, n);
        }

        auto exitCode = pipe.Wait();
        co_return std::make_pair(output, exitCode);
    }

    std::string SearchInPath(const std::string& exe) {
        if (Path.empty()) {
            Path = std::getenv("PATH");
        }
        // split Path by ':' and search for exe
        std::istringstream iss(Path);
        std::string dir;
        while (std::getline(iss, dir, ':')) {
            std::string candidate = dir + "/" + exe;
            if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
                return candidate;
            }
        }
        return exe;
    }

    std::string WriteTemp(const std::string& text) {
        namespace fs = std::filesystem;
        fs::path tempDir = fs::temp_directory_path();
        fs::path tempFile = tempDir / fs::path("qumir_temp_XXXXXX");
        std::string tempFileStr = tempFile.generic_string();
        std::vector<char> tmpl(tempFileStr.begin(), tempFileStr.end());
        tmpl.push_back('\0');
        int fd = mkstemp(tmpl.data());
        if (fd == -1) {
            throw std::runtime_error("failed to create temporary file");
        }
        std::string finalPath = std::string(tmpl.data());
        {
            std::ofstream ofs(finalPath, std::ios::binary);
            ofs << text;
        }
        close(fd);
        return finalPath;
    }

    TFuture<void> ServeStaticFile(TResponse& response, std::string uriPath, std::filesystem::path baseCanonical) {
        namespace fs = std::filesystem;

        // Build a path relative to the static base
        // Ensure no leading slash to keep path relative when appending
        if (!uriPath.empty() && uriPath.front() == '/') {
            uriPath.erase(0, 1);
        }

        fs::path rel = fs::path(uriPath).lexically_normal();

        // If empty or directory-like (trailing slash), serve index.html
        if (uriPath.empty() || (!uriPath.empty() && uriPath.back() == '/')) {
            rel /= "index.html";
        }

        std::error_code ec;
        fs::path candidate = (baseCanonical / rel).lexically_normal();
        fs::path target = fs::weakly_canonical(candidate, ec);
        if (ec || target.empty()) {
            // If weakly_canonical failed (e.g., components don't exist yet), fall back to lexical
            target = candidate;
        }

        // If requested a directory (without trailing slash), also try index.html
        if (fs::exists(target) && fs::is_directory(target)) {
            target /= "index.html";
        }

        // Ensure the resolved target stays within the static base
        fs::path base = baseCanonical;
        fs::path relToBase;
        {
            std::error_code ec2;
            relToBase = fs::relative(target, base, ec2);
            if (ec2) {
                // If relative couldn't be computed, treat as out of base
                relToBase = fs::path("../");
            }
        }
        auto relStr = relToBase.generic_string();
        bool underBase = !relToBase.empty() && !relToBase.is_absolute() && (relStr == "." || (relStr.rfind("..", 0) != 0));

        if (!underBase || !fs::exists(target) || !fs::is_regular_file(target)) {
            co_await Send404(response);
            co_return;
        }

        // Read and serve the file
        std::ifstream file(target, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        response.SetStatus(200);
        std::string extension = target.extension().string();
        auto it = MimeTypes.find(extension);
        if (it != MimeTypes.end()) {
            response.SetHeader("Content-Type", it->second);
        } else {
            response.SetHeader("Content-Type", "application/octet-stream");
        }
        response.SetHeader("Content-Length", std::to_string(data.size()));
        co_await response.SendHeaders();
        co_await response.WriteBodyFull(data);
    }

    std::unordered_map<std::string, std::string> MimeTypes = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".txt", "text/plain"},
        {".json", "application/json"},
    };

    std::function<TPipe(const std::string&, const std::vector<std::string>&, bool)> PipeFactory;
    std::string StaticDir;
    std::filesystem::path StaticBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(StaticDir));
    std::string BinaryDir;
    std::filesystem::path BinaryBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(BinaryDir));
    std::string ExamplesDir;
    std::filesystem::path ExamplesBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(ExamplesDir));
    std::string SharedLinksDir;
    std::filesystem::path SharedLinksBaseCanonical = std::filesystem::weakly_canonical(std::filesystem::path(SharedLinksDir));
    std::string Path;
};

int main(int argc, char** argv) {
    NNet::TInitializer init;
    int port = 8080;
    TOptions options;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i < argc-1) {
            port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--static-dir") && i < argc-1) {
            options.StaticDir = argv[++i];
        } else if (!strcmp(argv[i], "--binary-dir") && i < argc-1) {
            options.BinaryDir = argv[++i];
        } else if (!strcmp(argv[i], "--examples-dir") && i < argc-1) {
            options.ExamplesDir = argv[++i];
        } else if (!strcmp(argv[i], "--shared-links-dir") && i < argc-1) {
            options.SharedLinksDir = argv[++i];
        } else if (!strcmp(argv[i], "--help")) {
            std::cout << "Usage: " << argv[0] << " [--port port] [--static-dir dir] [--binary-dir dir] [--examples-dir dir] [--shared-links-dir dir]\n";
            return 0;
        }
    }

    TAddress address{"::", port};
    std::cerr << "Starting HTTP server on port " << port << "\n";

    auto logger = [](const std::string& msg) {
        std::cout << "[HTTPD] " << msg << std::endl;
    };

    using TPoller = TDefaultPoller;
    using TSocket = typename TPoller::TSocket;
    TLoop<TPoller> loop;
    TSocket listenSocket(loop.Poller(), address.Domain());
    listenSocket.Bind(address);
    listenSocket.Listen();
    std::cerr << "Listening on: " << listenSocket.LocalAddr()->ToString() << std::endl;

    auto pipeFactory = [&](const std::string& cmd, const std::vector<std::string>& args, bool stderrToStdout) {
        return TPipe(loop.Poller(), cmd, args, stderrToStdout);
    };

    options.PipeFactory = pipeFactory;

    TRouter router(options);
    TWebServer<TSocket> server(std::move(listenSocket), router, logger);
    server.Start();

    loop.Loop();
    return 0;
}
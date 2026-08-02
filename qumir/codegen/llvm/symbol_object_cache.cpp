#include "symbol_object_cache.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace NQumir::NCodeGen {

namespace {

using TSymMap = std::unordered_map<std::string, std::string>;

// Exclusive advisory lock over the cache dir for the read-modify-write of META.
// flock is per open-file-description, so it also serializes threads of one process.
class TDirLock {
public:
    explicit TDirLock(const std::string& dir) {
        std::string path = dir + "/.lock";
        Fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
        if (Fd_ >= 0 && ::flock(Fd_, LOCK_EX) == 0) {
            Locked_ = true;
        }
    }
    ~TDirLock() {
        if (Fd_ >= 0) {
            if (Locked_) {
                (void)::flock(Fd_, LOCK_UN);
            }
            (void)::close(Fd_);
        }
    }
    bool Locked() const { return Locked_; }
    TDirLock(const TDirLock&) = delete;
    TDirLock& operator=(const TDirLock&) = delete;
private:
    int Fd_ = -1;
    bool Locked_ = false;
};

std::string MetaPath(const std::string& dir) {
    return dir + "/meta.v1";
}

TSymMap ReadMeta(const std::string& dir) {
    TSymMap map;
    std::ifstream in(MetaPath(dir));
    std::string line;
    while (std::getline(in, line)) {
        auto tab = line.find('\t');
        if (tab != std::string::npos) {
            map.emplace(line.substr(0, tab), line.substr(tab + 1));
        }
    }
    return map;
}

// Atomic under the dir lock: fixed tmp + rename. Returns false on any failure.
bool WriteMeta(const std::string& dir, const TSymMap& map) {
    std::string tmp = MetaPath(dir) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        for (const auto& [sym, obj] : map) {
            out << sym << "\t" << obj << "\n";
        }
        if (!out) {
            return false;
        }
    }
    if (llvm::sys::fs::rename(tmp, MetaPath(dir))) {
        (void)llvm::sys::fs::remove(tmp);
        return false;
    }
    return true;
}

int NextIncVer(const TSymMap& map) {
    int next = 0;
    for (const auto& [sym, obj] : map) {
        int n = 0;
        if (std::sscanf(obj.c_str(), "qumir_inc_%d.o", &n) == 1 && n + 1 > next) {
            next = n + 1;
        }
    }
    return next;
}

} // namespace

std::string TBuildFingerprint::ToDigest() const {
    llvm::SHA256 hash;
    auto add = [&](const std::string& s) {
        hash.update(s);
        hash.update(llvm::StringRef("\0", 1));
    };
    add(CacheSchema);
    add(KernelLibVersion);
    add(LlvmVersion);
    add(Triple);
    add(DataLayout);
    add(CpuFeatures);
    add(OptSettings);
    auto digest = hash.final();
    return llvm::toHex(llvm::ArrayRef<uint8_t>(digest.data(), digest.size()), true);
}

TSymbolObjectCache::TSymbolObjectCache(std::string dir, TSymMap symToObj)
    : Dir_(std::move(dir))
    , Mutex_(std::make_unique<std::shared_mutex>())
    , SymToObj_(std::move(symToObj))
{}

std::expected<TSymbolObjectCache, TError> TSymbolObjectCache::Open(
    const std::string& root, const TBuildFingerprint& fingerprint)
{
    std::string dir = root + "/" + fingerprint.ToDigest();
    if (llvm::sys::fs::create_directories(dir)) {
        return std::unexpected(TError("symbol object cache: cannot create " + dir));
    }
    TDirLock lock(dir);
    if (!lock.Locked()) {
        return std::unexpected(TError("symbol object cache: cannot lock " + dir));
    }
    return TSymbolObjectCache(dir, ReadMeta(dir));
}

std::string TSymbolObjectCache::ObjectPath(const std::string& file) const {
    return Dir_ + "/" + file;
}

TSymbolObjectCache::TResolvePlan TSymbolObjectCache::Resolve(
    const std::vector<std::string>& required) const
{
    TResolvePlan plan;
    std::unordered_set<std::string> seenObj;
    std::unordered_set<std::string> seenMiss;
    std::shared_lock lock(*Mutex_);
    for (const auto& sym : required) {
        auto it = SymToObj_.find(sym);
        if (it == SymToObj_.end()) {
            if (seenMiss.insert(sym).second) {
                plan.Misses.push_back(sym);
            }
        } else if (seenObj.insert(it->second).second) {
            plan.ObjectFiles.push_back(ObjectPath(it->second));
        }
    }
    return plan;
}

std::expected<ERegisterResult, TError> TSymbolObjectCache::Register(
    std::string_view objectBytes, const std::vector<std::string>& providedSymbols)
{
    TDirLock lock(Dir_);
    if (!lock.Locked()) {
        return std::unexpected(TError("symbol object cache: cannot lock " + Dir_));
    }
    TSymMap map = ReadMeta(Dir_); // authoritative, may include concurrent writers

    for (const auto& sym : providedSymbols) {
        if (map.count(sym)) {
            std::unique_lock update(*Mutex_);
            SymToObj_ = std::move(map);
            return ERegisterResult::AlreadyPresent;
        }
    }

    std::string file = "qumir_inc_" + std::to_string(NextIncVer(map)) + ".o";
    std::string objPath = ObjectPath(file);

    llvm::SmallString<256> model(objPath);
    model += ".%%%%%%.tmp";
    int fd = -1;
    llvm::SmallString<256> tmp;
    if (llvm::sys::fs::createUniqueFile(model, fd, tmp)) {
        return std::unexpected(TError("symbol object cache: cannot create temp in " + Dir_));
    }
    llvm::raw_fd_ostream out(fd, true);
    out.write(objectBytes.data(), objectBytes.size());
    out.close();
    if (out.has_error() || llvm::sys::fs::rename(tmp, objPath)) {
        (void)llvm::sys::fs::remove(tmp);
        return std::unexpected(TError("symbol object cache: cannot write object " + objPath));
    }

    for (const auto& sym : providedSymbols) {
        map[sym] = file;
    }
    if (!WriteMeta(Dir_, map)) {
        (void)llvm::sys::fs::remove(objPath);
        return std::unexpected(TError("symbol object cache: cannot write meta in " + Dir_));
    }

    std::unique_lock update(*Mutex_);
    SymToObj_ = std::move(map);
    return ERegisterResult::Installed;
}

} // namespace NQumir::NCodeGen

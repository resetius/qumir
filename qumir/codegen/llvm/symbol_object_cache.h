#pragma once

#include <qumir/error.h>

#include <expected>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NQumir::NCodeGen {

// Any change here selects a different cache generation. CacheSchema/KernelLibVersion
// are hand-bumped source constants (codegen and dep-lib versions respectively).
struct TBuildFingerprint {
    std::string CacheSchema;
    std::string KernelLibVersion;
    std::string LlvmVersion;
    std::string Triple;
    std::string DataLayout;
    std::string CpuFeatures; // "" for generic codegen
    std::string OptSettings;

    std::string ToDigest() const;
};

// Both are successful outcomes; real failures come back as TError.
enum class ERegisterResult {
    Installed,       // object written, all its symbols newly mapped
    AlreadyPresent,  // discarded: some/all provided symbols already cached
};

// Symbol-granular on-disk cache of compiled objects, keyed by symbol name.
// Only content-addressed symbols belong here: the name must uniquely identify
// canonical semantics, ABI and layout (e.g. __generic_<name>$<TypeKey>...).
// Feeding a human name like "filter_12" would silently serve wrong code.
// Each build fingerprint gets its own subdirectory, so generations never wipe
// each other. Objects stay mutually disjoint and self-attributed (all-or-nothing
// Register), so loading any subset never double-defines a symbol.
class TSymbolObjectCache {
public:
    struct TResolvePlan {
        std::vector<std::string> ObjectFiles; // deduped absolute paths to load
        std::vector<std::string> Misses;      // deduped required symbols not cached
    };

    // Opens (creating if needed) <root>/<fingerprint-digest>/.
    static std::expected<TSymbolObjectCache, TError> Open(
        const std::string& root, const TBuildFingerprint& fingerprint);

    TResolvePlan Resolve(const std::vector<std::string>& required) const;

    // First-writer-wins: writes objectBytes and maps every provided symbol only
    // if none is already cached; otherwise discards and returns AlreadyPresent.
    // AlreadyPresent does not return the winner's path — the caller must still
    // make the symbols available in the current JIT (load its own object).
    std::expected<ERegisterResult, TError> Register(
        std::string_view objectBytes,
        const std::vector<std::string>& providedSymbols);

private:
    TSymbolObjectCache(std::string dir, std::unordered_map<std::string, std::string> symToObj);

    std::string ObjectPath(const std::string& file) const;

    std::string Dir_;
    std::unique_ptr<std::shared_mutex> Mutex_; // in unique_ptr to keep the cache movable
    std::unordered_map<std::string, std::string> SymToObj_; // symbol -> object filename
};

} // namespace NQumir::NCodeGen

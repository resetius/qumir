#include "builtins.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace NQumir {
namespace NRegistry {

BuiltinsModule::BuiltinsModule() {
    auto i64Type = std::make_shared<NAst::TIntegerType>();
    auto i32Type = std::make_shared<NAst::TIntegerType>(NAst::TIntegerType::I32);
    auto u8Type = std::make_shared<NAst::TIntegerType>(NAst::TIntegerType::U8);
    auto ptrU8Type = std::make_shared<NAst::TPointerType>(u8Type);

    ExternalFunctions_ = {
        {
            .Name = "builtin::memcpy",
            .MangledName = "memcpy",
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                std::memcpy(
                    std::bit_cast<void*>(args[0]),
                    std::bit_cast<const void*>(args[1]),
                    static_cast<size_t>(args[2]));
                return args[0];
            },
            .ArgTypes = { ptrU8Type, ptrU8Type, i64Type },
            .ReturnType = ptrU8Type,
        },
        {
            .Name = "builtin::memmove",
            .MangledName = "memmove",
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                std::memmove(
                    std::bit_cast<void*>(args[0]),
                    std::bit_cast<const void*>(args[1]),
                    static_cast<size_t>(args[2]));
                return args[0];
            },
            .ArgTypes = { ptrU8Type, ptrU8Type, i64Type },
            .ReturnType = ptrU8Type,
        },
        {
            .Name = "builtin::memcmp",
            .MangledName = "memcmp",
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                const int result = std::memcmp(
                    std::bit_cast<const void*>(args[0]),
                    std::bit_cast<const void*>(args[1]),
                    static_cast<size_t>(args[2]));
                return static_cast<uint64_t>(static_cast<int64_t>(result));
            },
            .ArgTypes = { ptrU8Type, ptrU8Type, i64Type },
            .ReturnType = i32Type,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir

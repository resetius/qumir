#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace NQumir {
namespace NIR {

class TRegisterFile {
public:
    void Reset(size_t size, size_t alignment) {
        alignment = std::max(alignment, alignof(std::max_align_t));
        assert((alignment & (alignment - 1)) == 0);
        if (size > Capacity_ || alignment > Alignment_) {
            auto* data = static_cast<uint8_t*>(::operator new(size, std::align_val_t(alignment)));
            Data_ = {data, TDeleter{alignment}};
            Capacity_ = size;
            Alignment_ = alignment;
        }
        Size_ = size;
        if (size != 0) {
            std::memset(Data_.get(), 0, size);
        }
    }

    template<typename T>
    T& Get(int32_t byteOffset) {
        CheckAccess<T>(byteOffset);
        return *reinterpret_cast<T*>(Data_.get() + byteOffset);
    }

    template<typename T>
    const T& Get(int32_t byteOffset) const {
        CheckAccess<T>(byteOffset);
        return *reinterpret_cast<const T*>(Data_.get() + byteOffset);
    }

    uint8_t* Data() { return Data_.get(); }
    const uint8_t* Data() const { return Data_.get(); }
    size_t Size() const { return Size_; }

private:
    struct TDeleter {
        size_t Alignment = alignof(std::max_align_t);

        void operator()(uint8_t* data) const {
            ::operator delete(data, std::align_val_t(Alignment));
        }
    };

    template<typename T>
    void CheckAccess(int32_t byteOffset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        assert(byteOffset >= 0 && static_cast<size_t>(byteOffset) + sizeof(T) <= Size_);
        assert(reinterpret_cast<uintptr_t>(Data_.get() + byteOffset) % alignof(T) == 0);
    }

    std::unique_ptr<uint8_t, TDeleter> Data_{nullptr, TDeleter{}};
    size_t Size_ = 0;
    size_t Capacity_ = 0;
    size_t Alignment_ = alignof(std::max_align_t);
};

} // namespace NIR
} // namespace NQumir

#pragma once

namespace NQumir {

inline int AlignUp(int value, int align) {
    return (value + align - 1) & ~(align - 1);
}

} // namespace NQumir

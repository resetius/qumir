#pragma once

#include <memory>

namespace NQumir {
namespace NAst {

struct TType {
    virtual ~TType() = default;
    virtual std::string ToString() const { return ""; }
    virtual const std::string_view TypeName() const = 0;
};

using TTypePtr = std::shared_ptr<TType>;

std::ostream& operator<<(std::ostream& os, const TType& expr);

} // namespace NAst
} // namespace NQumir
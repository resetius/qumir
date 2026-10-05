#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {

class TDebugPointEmitter {
public:
    TDebugPointEmitter(TModule& module, TBuilder& builder);

    void Before(TOp op, const TInstrDebugInfo& info);
    void After(TOp op, const TInstrDebugInfo& info);
    void Point(std::string_view kind, const TInstrDebugInfo& info, int localId = -1, std::string_view op = {});
    void Declare(TLocal local, const TLocalVarDebugInfo& info);
    void Bind(const TLocation& location, int localId, std::string_view name);

private:
    TModule& Module_;
    TBuilder& Builder_;
    int SymbolId_ = -1;
    bool PendingArgs_ = false;
};

} // namespace NIR
} // namespace NQumir

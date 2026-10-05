#pragma once

namespace NQumir {
namespace NIR {

struct TDebugOptions {
    bool EmitDebugInfo = false;
    bool EmitDebugPoints = false;

    bool CollectMetadata() const {
        return EmitDebugInfo || EmitDebugPoints;
    }
};

} // namespace NIR
} // namespace NQumir

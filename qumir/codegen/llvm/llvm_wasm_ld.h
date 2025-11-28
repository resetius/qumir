#pragma once

namespace NQumir {
namespace NCodeGen {

std::string LinkWasm(const std::string& obj, const std::vector<std::string>& extraArgs = {
    "--no-entry",
    "--export-all",
    "--allow-undefined"
});

} // namespace NCodeGen
} // namespace NQumir

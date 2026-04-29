#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ProcessUtils {

void runCommand(
    const std::filesystem::path &executable,
    const std::vector<std::string> &arguments,
    const std::string &what);

int runCommandAllowFailure(
    const std::filesystem::path &executable,
    const std::vector<std::string> &arguments,
    const std::string &what);

std::string toWslPath(const std::filesystem::path &path);

} // namespace ProcessUtils

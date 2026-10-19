#pragma once
#include <cstdint>
#include <string>

namespace zm {
// Extract/CRC-check while the game runs; only rename complete groups on next launch.
bool stageZip(const std::string& zip, const std::string& pendingRoot, uint64_t freeBytes,
              std::string& summary, std::string& error);
bool activatePending(const std::string& pendingRoot, const std::string& documents,
                     std::string& summary, std::string& error);
bool stadiumName(const std::string& name);
}

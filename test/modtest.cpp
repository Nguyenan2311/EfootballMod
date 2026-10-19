#include "../core/modpack.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
static void check(bool b, const char* m) { if (!b) throw std::runtime_error(m); }
static std::string read(const fs::path& p) { std::ifstream in(p); return std::string(std::istreambuf_iterator<char>(in), {}); }
int main(int argc, char** argv) {
  try {
    check(argc == 2, "fixture root required"); fs::path root(argv[1]);
    std::string pending = (root / "pending").string(), docs = (root / "docs").string(), s, e;
    fs::create_directories(root / "docs/Download");
    std::ofstream(root / "docs/Download/pc3099_mobile_ios.pak") << "old";
    check(zm::stadiumName("pc3099_mobile_ios.pak") && !zm::stadiumName("pc7999_mobile_ios.pak"), "stadium classification");
    for (const char* name : {"traversal", "duplicate", "partial", "empty", "corrupt"}) {
      check(!zm::stageZip((root / (std::string(name) + ".zip")).string(), pending, 1000000000, s, e), name);
      check(read(root / "docs/Download/pc3099_mobile_ios.pak") == "old", "invalid ZIP leaves installed mods intact");
    }
    check(!zm::stageZip((root / "good.zip").string(), pending, 1, s, e), "low disk space");
    check(zm::stageZip((root / "good.zip").string(), pending, 1000000000, s, e), e.c_str());
    check(read(root / "docs/Download/pc3099_mobile_ios.pak") == "old", "staging does not hot-replace loaded containers");
    fs::path stage;
    for (auto& d : fs::directory_iterator(pending)) if (d.is_directory() && d.path().filename().string()[0] != '.') stage = d.path();
    check(!stage.empty(), "staged batch");
    // Simulate termination immediately after the first old file was backed up
    // and its replacement installed. Retry must recover, then complete.
    fs::path backup = root / "docs/ModBackups" / stage.filename(); fs::create_directories(backup / "Download");
    fs::rename(root / "docs/Download/pc3099_mobile_ios.pak", backup / "Download/pc3099_mobile_ios.pak");
    fs::rename(stage / "Download/pc3099_mobile_ios.pak", root / "docs/Download/pc3099_mobile_ios.pak");
    std::ofstream(stage / "applying") << "1";
    check(zm::activatePending(pending, docs, s, e), e.c_str());
    check(read(root / "docs/Download/pc3099_mobile_ios.pak") == "new-pak", "complete group activated");
    check(read(root / "docs/Download/pc3099_mobile_ios.ucas") == "new-ucas", "container activated");
    check(read(backup / "Download/pc3099_mobile_ios.pak") == "old", "original resource backed up");
    check(!fs::exists(stage), "batch consumed once");
    check(zm::activatePending(pending, docs, s, e), "repeated activation is harmless");
    // A destination directory conflict must restore the first replaced file.
    check(zm::stageZip((root / "good.zip").string(), pending, 1000000000, s, e), e.c_str());
    fs::remove(root / "docs/Download/pc3099_mobile_ios.ucas"); fs::create_directory(root / "docs/Download/pc3099_mobile_ios.ucas");
    check(!zm::activatePending(pending, docs, s, e), "destination conflict reported");
    check(read(root / "docs/Download/pc3099_mobile_ios.pak") == "new-pak", "partial activation rolled back");
    std::cout << "mod ZIP validation, deferred activation, backup and recovery passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

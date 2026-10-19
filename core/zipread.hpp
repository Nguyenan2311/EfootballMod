// Minimal streaming ZIP/ZIP64 reader for the data-pack importer (stored + deflate).
// Never loads an entry fully into memory; verifies CRC-32 and (optionally) SHA-256 while writing.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zz {

struct Entry {
  std::string name;
  uint16_t method = 0;     // 0 stored, 8 deflate
  uint32_t crc32 = 0;
  uint64_t compSize = 0;
  uint64_t size = 0;
  uint64_t localOffset = 0;
  bool isDir() const { return !name.empty() && name.back() == '/'; }
};

class Reader {
 public:
  ~Reader();
  bool open(const std::string& path, std::string& err);
  const std::vector<Entry>& entries() const { return list_; }
  const Entry* find(const std::string& name) const;
  // Reads a (small) entry fully into memory.
  bool readAll(const Entry& e, std::string& out, std::string& err);
  // Streams an entry to dstPath; progress(bytesWrittenForThisEntry). sha256Hex receives lowercase hex.
  bool extract(const Entry& e, const std::string& dstPath, std::string& sha256Hex, std::string& err,
               const std::function<bool(uint64_t)>& progress = nullptr);
  uint64_t fileSize() const { return fileSize_; }

 private:
  bool pread(uint64_t off, void* buf, size_t n);
  bool dataOffset(const Entry& e, uint64_t& off, std::string& err);
  bool stream(const Entry& e, const std::function<bool(const uint8_t*, size_t)>& sink, std::string& err,
              const std::function<bool(uint64_t)>& progress);
  int fd_ = -1;
  uint64_t fileSize_ = 0;
  std::vector<Entry> list_;
};

// Streaming SHA-256 (portable implementation, no OS dependency)
struct Sha256 {
  uint32_t h[8];
  uint64_t len = 0;
  uint8_t buf[64];
  size_t bl = 0;
  Sha256();
  void update(const uint8_t* p, size_t n);
  std::string hexdigest();
 private:
  void block(const uint8_t* p);
};

bool sha256File(const std::string& path, std::string& hex, const std::function<bool(uint64_t)>& progress = nullptr);

}  // namespace zz

#include "zipread.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <memory>

namespace zz {

static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t* p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

Reader::~Reader() { if (fd_ >= 0) close(fd_); }

bool Reader::pread(uint64_t off, void* buf, size_t n) {
  uint8_t* b = (uint8_t*)buf;
  while (n) {
    ssize_t r = ::pread(fd_, b, n, (off_t)off);
    if (r <= 0) return false;
    b += r; off += r; n -= r;
  }
  return true;
}

bool Reader::open(const std::string& path, std::string& err) {
  fd_ = ::open(path.c_str(), O_RDONLY);
  if (fd_ < 0) { err = "Không mở được file: " + path; return false; }
  struct stat st;
  fstat(fd_, &st);
  fileSize_ = (uint64_t)st.st_size;
  if (fileSize_ < 22) { err = "File quá nhỏ, không phải ZIP"; return false; }
  // Find End Of Central Directory (max comment 64K)
  size_t tail = (size_t)std::min<uint64_t>(fileSize_, 65557);
  std::vector<uint8_t> t(tail);
  if (!pread(fileSize_ - tail, t.data(), tail)) { err = "Đọc ZIP thất bại"; return false; }
  ssize_t eocd = -1;
  for (ssize_t i = (ssize_t)tail - 22; i >= 0; i--) if (le32(&t[i]) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) { err = "Không tìm thấy End Of Central Directory (ZIP hỏng?)"; return false; }
  uint64_t eocdOff = fileSize_ - tail + (uint64_t)eocd;
  uint64_t cdCount = le16(&t[eocd + 10]);
  uint64_t cdSize = le32(&t[eocd + 12]);
  uint64_t cdOff = le32(&t[eocd + 16]);
  // ZIP64 locator sits right before EOCD
  if (eocdOff >= 20) {
    uint8_t loc[20];
    if (pread(eocdOff - 20, loc, 20) && le32(loc) == 0x07064b50) {
      uint64_t z64 = le64(loc + 8);
      uint8_t z[56];
      if (!pread(z64, z, 56) || le32(z) != 0x06064b50) { err = "ZIP64 EOCD hỏng"; return false; }
      cdCount = le64(z + 32);
      cdSize = le64(z + 40);
      cdOff = le64(z + 48);
    }
  }
  if (cdOff + cdSize > fileSize_) { err = "Central directory vượt quá kích thước file (tải chưa xong?)"; return false; }
  std::vector<uint8_t> cd((size_t)cdSize);
  if (!pread(cdOff, cd.data(), cd.size())) { err = "Đọc central directory thất bại"; return false; }
  size_t p = 0;
  for (uint64_t i = 0; i < cdCount; i++) {
    if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50) { err = "Central directory hỏng"; return false; }
    Entry e;
    e.method = le16(&cd[p + 10]);
    e.crc32 = le32(&cd[p + 16]);
    e.compSize = le32(&cd[p + 20]);
    e.size = le32(&cd[p + 24]);
    uint16_t nl = le16(&cd[p + 28]), xl = le16(&cd[p + 30]), cl = le16(&cd[p + 32]);
    e.localOffset = le32(&cd[p + 42]);
    if (p + 46 + nl + xl + cl > cd.size()) { err = "Central directory hỏng"; return false; }
    e.name.assign((const char*)&cd[p + 46], nl);
    // ZIP64 extra field
    size_t x = p + 46 + nl, xe = x + xl;
    while (x + 4 <= xe) {
      uint16_t id = le16(&cd[x]), sz = le16(&cd[x + 2]);
      if (id == 0x0001) {
        size_t q = x + 4;
        if (e.size == 0xFFFFFFFF && q + 8 <= x + 4 + sz) { e.size = le64(&cd[q]); q += 8; }
        if (e.compSize == 0xFFFFFFFF && q + 8 <= x + 4 + sz) { e.compSize = le64(&cd[q]); q += 8; }
        if (e.localOffset == 0xFFFFFFFF && q + 8 <= x + 4 + sz) { e.localOffset = le64(&cd[q]); q += 8; }
      }
      x += 4 + sz;
    }
    for (auto& c : e.name) if (c == '\\') c = '/';
    list_.push_back(e);
    p += 46 + nl + xl + cl;
  }
  return true;
}

const Entry* Reader::find(const std::string& name) const {
  for (auto& e : list_) if (e.name == name) return &e;
  return nullptr;
}

bool Reader::dataOffset(const Entry& e, uint64_t& off, std::string& err) {
  uint8_t lh[30];
  if (!pread(e.localOffset, lh, 30) || le32(lh) != 0x04034b50) { err = "Local header hỏng: " + e.name; return false; }
  off = e.localOffset + 30 + le16(lh + 26) + le16(lh + 28);
  if (off + e.compSize > fileSize_) { err = "Dữ liệu vượt quá kích thước file: " + e.name; return false; }
  return true;
}

bool Reader::stream(const Entry& e, const std::function<bool(const uint8_t*, size_t)>& sink, std::string& err,
                    const std::function<bool(uint64_t)>& progress) {
  uint64_t off;
  if (!dataOffset(e, off, err)) return false;
  const size_t CH = 1 << 20;
  std::unique_ptr<uint8_t[]> in(new uint8_t[CH]);
  uint32_t crc = crc32(0L, Z_NULL, 0);
  uint64_t done = 0;
  if (e.method == 0) {
    uint64_t left = e.compSize;
    while (left) {
      size_t n = (size_t)std::min<uint64_t>(left, CH);
      if (!pread(off, in.get(), n)) { err = "Đọc thất bại: " + e.name; return false; }
      crc = crc32(crc, in.get(), (uInt)n);
      if (!sink(in.get(), n)) { err = "Ghi thất bại (hết dung lượng?): " + e.name; return false; }
      off += n; left -= n; done += n;
      if (progress && !progress(done)) { err = "Đã hủy"; return false; }
    }
  } else if (e.method == 8) {
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) { err = "zlib init"; return false; }
    std::unique_ptr<uint8_t[]> out(new uint8_t[CH]);
    uint64_t left = e.compSize;
    int r = Z_OK;
    while (r != Z_STREAM_END) {
      if (s.avail_in == 0) {
        if (!left) { inflateEnd(&s); err = "Dữ liệu nén bị cụt: " + e.name; return false; }
        size_t n = (size_t)std::min<uint64_t>(left, CH);
        if (!pread(off, in.get(), n)) { inflateEnd(&s); err = "Đọc thất bại: " + e.name; return false; }
        off += n; left -= n;
        s.next_in = in.get();
        s.avail_in = (uInt)n;
      }
      s.next_out = out.get();
      s.avail_out = (uInt)CH;
      r = inflate(&s, Z_NO_FLUSH);
      if (r != Z_OK && r != Z_STREAM_END) { inflateEnd(&s); err = "Giải nén lỗi: " + e.name; return false; }
      size_t got = CH - s.avail_out;
      if (got) {
        crc = crc32(crc, out.get(), (uInt)got);
        if (!sink(out.get(), got)) { inflateEnd(&s); err = "Ghi thất bại (hết dung lượng?): " + e.name; return false; }
        done += got;
        if (progress && !progress(done)) { inflateEnd(&s); err = "Đã hủy"; return false; }
      }
    }
    inflateEnd(&s);
  } else {
    err = "Phương thức nén không hỗ trợ (" + std::to_string(e.method) + "): " + e.name;
    return false;
  }
  if (done != e.size) { err = "Sai kích thước sau giải nén: " + e.name; return false; }
  if (crc != e.crc32) { err = "Sai CRC-32: " + e.name; return false; }
  return true;
}

bool Reader::readAll(const Entry& e, std::string& out, std::string& err) {
  out.clear();
  if (e.size > (64u << 20)) { err = "Entry quá lớn để đọc vào RAM: " + e.name; return false; }
  out.reserve((size_t)e.size);
  return stream(e, [&](const uint8_t* p, size_t n) { out.append((const char*)p, n); return true; }, err, nullptr);
}

bool Reader::extract(const Entry& e, const std::string& dst, std::string& shaHex, std::string& err,
                     const std::function<bool(uint64_t)>& progress) {
  int fd = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) { err = "Không tạo được file: " + dst; return false; }
  Sha256 sh;
  bool ok = stream(e, [&](const uint8_t* p, size_t n) {
    sh.update(p, n);
    while (n) {
      ssize_t w = ::write(fd, p, n);
      if (w <= 0) return false;
      p += w; n -= w;
    }
    return true;
  }, err, progress);
  if (ok && fsync(fd) != 0) { ok = false; err = "fsync thất bại: " + dst; }
  close(fd);
  if (!ok) { unlink(dst.c_str()); return false; }
  shaHex = sh.hexdigest();
  return true;
}

// ---------------- SHA-256 ----------------
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
static inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
Sha256::Sha256() {
  static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(h, init, sizeof h);
}
void Sha256::block(const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = (p[4 * i] << 24) | (p[4 * i + 1] << 16) | (p[4 * i + 2] << 8) | p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = hh + S1 + ch + K[i] + w[i];
    uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
    uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
void Sha256::update(const uint8_t* p, size_t n) {
  len += n;
  if (bl) {
    size_t k = std::min(n, 64 - bl);
    memcpy(buf + bl, p, k);
    bl += k; p += k; n -= k;
    if (bl == 64) { block(buf); bl = 0; }
  }
  while (n >= 64) { block(p); p += 64; n -= 64; }
  if (n) { memcpy(buf, p, n); bl = n; }
}
std::string Sha256::hexdigest() {
  uint64_t bits = len * 8;
  uint8_t pad = 0x80;
  update(&pad, 1);
  uint8_t z = 0;
  while (bl != 56) update(&z, 1);
  uint8_t L[8];
  for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8 * i));
  update(L, 8);
  static const char* hx = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < 8; i++) for (int k = 3; k >= 0; k--) { uint8_t v = (uint8_t)(h[i] >> (8 * k)); s += hx[v >> 4]; s += hx[v & 15]; }
  return s;
}

bool sha256File(const std::string& path, std::string& hex, const std::function<bool(uint64_t)>& progress) {
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  Sha256 s;
  const size_t CH = 1 << 20;
  std::unique_ptr<uint8_t[]> b(new uint8_t[CH]);
  uint64_t done = 0;
  while (true) {
    ssize_t r = ::read(fd, b.get(), CH);
    if (r < 0) { close(fd); return false; }
    if (r == 0) break;
    s.update(b.get(), (size_t)r);
    done += r;
    if (progress && !progress(done)) { close(fd); return false; }
  }
  close(fd);
  hex = s.hexdigest();
  return true;
}

}  // namespace zz

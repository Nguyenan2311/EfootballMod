#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "jvalue.hpp"

namespace zc {
using Bytes = std::vector<uint8_t>;

extern const uint8_t kAesKey[32];

// msgpack writer identical to OfflineServer.packValue + msgpack-java compact encodings
void packValue(Bytes& out, const zj::Value& v);
// unpackZiruMsgpackJson: INTEGER->Long, FLOAT->Double, BINARY->base64 string, MAP keys as strings
zj::Value unpackZiruValue(const uint8_t* p, size_t n, size_t& pos);
// MessageUnpacker.unpackValue().toJson() (msgpack-java ImmutableValue JSON form)
std::string unpackValueToJson(const uint8_t* p, size_t n, size_t& pos);

// Minimal MessageUnpacker used by the request decoders that walk msgpack directly
struct Unpacker {
  const uint8_t* d; size_t n; size_t p = 0;
  Unpacker(const uint8_t* d_, size_t n_) : d(d_), n(n_) {}
  enum VT { NIL, BOOLEAN, INTEGER, FLOAT, STRING, BINARY, ARRAY, MAP, EXTENSION };
  bool hasNext() const { return p < n; }
  VT nextType() const;
  void unpackNil();
  bool unpackBoolean();
  int64_t unpackLong();
  int32_t unpackInt();
  double unpackDouble();
  std::string unpackString();
  uint32_t unpackBinaryHeader();
  void readPayload(uint8_t* out, size_t len);
  uint32_t unpackArrayHeader();
  uint32_t unpackMapHeader();
  void skipValue();
 private:
  uint8_t u8();
  uint64_t be(int k);
};

Bytes zlibDeflate(const Bytes& in);           // java.util.zip.Deflater default
bool zlibInflate(const uint8_t* p, size_t n, Bytes& out);  // InflaterInputStream
Bytes gzipCompress(const Bytes& in);           // GZIPOutputStream
bool gzipDecompress(const uint8_t* p, size_t n, Bytes& out);

Bytes aesEncryptCbc(const Bytes& plain, const uint8_t iv[16]);      // PKCS7
bool aesDecryptCbc(const uint8_t* ct, size_t n, const uint8_t iv[16], Bytes& out);
void randomBytes(uint8_t* p, size_t n);

std::string base64NoWrap(const uint8_t* p, size_t n);

// Response body: IV || AES(gzip(msgpack(v)))
Bytes encodeResponse(const zj::Value& v);
// Request body: IV || AES(zlib(msgpack)) -> decoded msgpack bytes
bool decodeRequestToMsgpack(const Bytes& body, Bytes& msgpack);
// Client-side helpers for tests
Bytes encodeRequest(const zj::Value& v);
bool decodeResponse(const Bytes& body, zj::Value& v);

std::string hex(const uint8_t* p, size_t n);

// java.util.Random
struct JavaRandom {
  uint64_t seed;
  explicit JavaRandom(int64_t s) { setSeed(s); }
  JavaRandom();  // time-seeded like Java
  void setSeed(int64_t s) { seed = ((uint64_t)s ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); }
  int32_t next(int bits);
  int32_t nextInt();
  int32_t nextInt(int32_t bound);
  int64_t nextLong();
  bool nextBoolean();
  double nextDouble();
};
int64_t currentTimeMillis();
void setClockOverride(int64_t ms);  // tests: fixed clock (0 = real)
void setRandomSeedOverride(int64_t seed, bool enabled);  // tests: deterministic Random()
}  // namespace zc

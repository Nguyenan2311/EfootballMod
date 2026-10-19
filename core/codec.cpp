#include "codec.hpp"

#include <zlib.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <cstring>
#include <stdexcept>

#ifdef __APPLE__
#include <CommonCrypto/CommonCryptor.h>
#include <Security/SecRandom.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace zc {

const uint8_t kAesKey[32] = {0x43, 0x74, 0x09, 0x81, 0x52, 0x3c, 0xdc, 0x17, 0x1e, 0x71, 0xde,
                             0x2c, 0xca, 0xb1, 0xa5, 0xa9, 0xb8, 0x6f, 0x4b, 0x83, 0x31, 0x96,
                             0xc5, 0x5f, 0xac, 0xd4, 0xbd, 0x25, 0x84, 0x6c, 0x33, 0xf5};

// ---------------- msgpack writer ----------------
static void wb(Bytes& o, uint8_t b) { o.push_back(b); }
static void wbe(Bytes& o, uint64_t v, int k) { for (int i = k - 1; i >= 0; i--) o.push_back((uint8_t)(v >> (8 * i))); }

static void packLong(Bytes& o, int64_t v) {
  if (v < -(1LL << 5)) {
    if (v < -(1LL << 15)) {
      if (v < -(1LL << 31)) { wb(o, 0xd3); wbe(o, (uint64_t)v, 8); }
      else { wb(o, 0xd2); wbe(o, (uint64_t)v, 4); }
    } else {
      if (v < -(1 << 7)) { wb(o, 0xd1); wbe(o, (uint64_t)v, 2); }
      else { wb(o, 0xd0); wbe(o, (uint64_t)v, 1); }
    }
  } else if (v < (1 << 7)) {
    wb(o, (uint8_t)v);
  } else {
    if (v < (1LL << 16)) {
      if (v < (1 << 8)) { wb(o, 0xcc); wbe(o, (uint64_t)v, 1); }
      else { wb(o, 0xcd); wbe(o, (uint64_t)v, 2); }
    } else {
      if (v < (1LL << 32)) { wb(o, 0xce); wbe(o, (uint64_t)v, 4); }
      else { wb(o, 0xcf); wbe(o, (uint64_t)v, 8); }
    }
  }
}
static void packString(Bytes& o, const std::string& s) {
  size_t n = s.size();
  if (n < 32) wb(o, (uint8_t)(0xa0 | n));
  else if (n < 256) { wb(o, 0xd9); wbe(o, n, 1); }
  else if (n < 65536) { wb(o, 0xda); wbe(o, n, 2); }
  else { wb(o, 0xdb); wbe(o, n, 4); }
  o.insert(o.end(), s.begin(), s.end());
}
static void packArrayHeader(Bytes& o, size_t n) {
  if (n < 16) wb(o, (uint8_t)(0x90 | n));
  else if (n < 65536) { wb(o, 0xdc); wbe(o, n, 2); }
  else { wb(o, 0xdd); wbe(o, n, 4); }
}
static void packMapHeader(Bytes& o, size_t n) {
  if (n < 16) wb(o, (uint8_t)(0x80 | n));
  else if (n < 65536) { wb(o, 0xde); wbe(o, n, 2); }
  else { wb(o, 0xdf); wbe(o, n, 4); }
}

void packValue(Bytes& o, const zj::Value& v) {
  using zj::Value;
  switch (v.t) {
    case Value::JNULLPTR: case Value::JNULL: wb(o, 0xc0); break;
    case Value::STR: packString(o, v.s); break;
    case Value::INT: case Value::LONG: packLong(o, v.n); break;
    case Value::DOUBLE: { uint64_t b; memcpy(&b, &v.d, 8); wb(o, 0xcb); wbe(o, b, 8); break; }
    case Value::BOOL: wb(o, v.b ? 0xc3 : 0xc2); break;
    case Value::OBJ:
      packMapHeader(o, v.o->kv.size());
      for (auto& p : v.o->kv) { packString(o, p.first); packValue(o, p.second); }
      break;
    case Value::ARR:
      packArrayHeader(o, v.a->v.size());
      for (auto& x : v.a->v) packValue(o, x);
      break;
  }
}

// ---------------- msgpack reader ----------------
uint8_t Unpacker::u8() { if (p >= n) throw std::runtime_error("msgpack EOF"); return d[p++]; }
uint64_t Unpacker::be(int k) { uint64_t v = 0; for (int i = 0; i < k; i++) v = (v << 8) | u8(); return v; }
Unpacker::VT Unpacker::nextType() const {
  if (p >= n) throw std::runtime_error("msgpack EOF");
  uint8_t c = d[p];
  if (c <= 0x7f || c >= 0xe0) return INTEGER;
  if (c <= 0x8f) return MAP;
  if (c <= 0x9f) return ARRAY;
  if (c <= 0xbf) return STRING;
  switch (c) {
    case 0xc0: return NIL;
    case 0xc2: case 0xc3: return BOOLEAN;
    case 0xc4: case 0xc5: case 0xc6: return BINARY;
    case 0xc7: case 0xc8: case 0xc9: case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8: return EXTENSION;
    case 0xca: case 0xcb: return FLOAT;
    case 0xd9: case 0xda: case 0xdb: return STRING;
    case 0xdc: case 0xdd: return ARRAY;
    case 0xde: case 0xdf: return MAP;
    default: return INTEGER;
  }
}
void Unpacker::unpackNil() { if (u8() != 0xc0) throw std::runtime_error("msgpack: expected nil"); }
bool Unpacker::unpackBoolean() { uint8_t c = u8(); if (c == 0xc2) return false; if (c == 0xc3) return true; throw std::runtime_error("msgpack: expected bool"); }
int64_t Unpacker::unpackLong() {
  uint8_t c = u8();
  if (c <= 0x7f) return c;
  if (c >= 0xe0) return (int8_t)c;
  switch (c) {
    case 0xcc: return (int64_t)be(1);
    case 0xcd: return (int64_t)be(2);
    case 0xce: return (int64_t)be(4);
    case 0xcf: { uint64_t v = be(8); if ((int64_t)v < 0) throw std::runtime_error("msgpack: uint64 overflow"); return (int64_t)v; }
    case 0xd0: return (int8_t)be(1);
    case 0xd1: return (int16_t)be(2);
    case 0xd2: return (int32_t)be(4);
    case 0xd3: return (int64_t)be(8);
  }
  throw std::runtime_error("msgpack: expected integer");
}
int32_t Unpacker::unpackInt() {
  int64_t v = unpackLong();
  if (v < INT32_MIN || v > INT32_MAX) throw std::runtime_error("msgpack: int overflow");
  return (int32_t)v;
}
double Unpacker::unpackDouble() {
  uint8_t c = u8();
  if (c == 0xcb) { uint64_t b = be(8); double x; memcpy(&x, &b, 8); return x; }
  if (c == 0xca) { uint32_t b = (uint32_t)be(4); float f; memcpy(&f, &b, 4); return f; }
  throw std::runtime_error("msgpack: expected float");
}
std::string Unpacker::unpackString() {
  uint8_t c = u8();
  size_t len;
  if ((c & 0xe0) == 0xa0) len = c & 0x1f;
  else if (c == 0xd9) len = be(1);
  else if (c == 0xda) len = be(2);
  else if (c == 0xdb) len = be(4);
  else if (c == 0xc4) len = be(1);   // msgpack-java unpackString accepts bin
  else if (c == 0xc5) len = be(2);
  else if (c == 0xc6) len = be(4);
  else throw std::runtime_error("msgpack: expected string");
  if (p + len > n) throw std::runtime_error("msgpack EOF");
  std::string s((const char*)d + p, len);
  p += len;
  return s;
}
uint32_t Unpacker::unpackBinaryHeader() {
  uint8_t c = u8();
  if (c == 0xc4) return (uint32_t)be(1);
  if (c == 0xc5) return (uint32_t)be(2);
  if (c == 0xc6) return (uint32_t)be(4);
  if ((c & 0xe0) == 0xa0) return c & 0x1f;
  if (c == 0xd9) return (uint32_t)be(1);
  if (c == 0xda) return (uint32_t)be(2);
  if (c == 0xdb) return (uint32_t)be(4);
  throw std::runtime_error("msgpack: expected binary");
}
void Unpacker::readPayload(uint8_t* out, size_t len) {
  if (p + len > n) throw std::runtime_error("msgpack EOF");
  memcpy(out, d + p, len);
  p += len;
}
uint32_t Unpacker::unpackArrayHeader() {
  uint8_t c = u8();
  if ((c & 0xf0) == 0x90) return c & 0xf;
  if (c == 0xdc) return (uint32_t)be(2);
  if (c == 0xdd) return (uint32_t)be(4);
  throw std::runtime_error("msgpack: expected array");
}
uint32_t Unpacker::unpackMapHeader() {
  uint8_t c = u8();
  if ((c & 0xf0) == 0x80) return c & 0xf;
  if (c == 0xde) return (uint32_t)be(2);
  if (c == 0xdf) return (uint32_t)be(4);
  throw std::runtime_error("msgpack: expected map");
}
void Unpacker::skipValue() {
  uint64_t cnt = 1;
  while (cnt-- > 0) {
    uint8_t c = u8();
    if (c <= 0x7f || c >= 0xe0 || c == 0xc0 || c == 0xc2 || c == 0xc3) continue;
    if ((c & 0xf0) == 0x80) { cnt += 2 * (c & 0xf); continue; }
    if ((c & 0xf0) == 0x90) { cnt += (c & 0xf); continue; }
    if ((c & 0xe0) == 0xa0) { p += c & 0x1f; continue; }
    switch (c) {
      case 0xcc: case 0xd0: p += 1; break;
      case 0xcd: case 0xd1: p += 2; break;
      case 0xce: case 0xd2: case 0xca: p += 4; break;
      case 0xcf: case 0xd3: case 0xcb: p += 8; break;
      case 0xd9: case 0xc4: p += be(1); break;
      case 0xda: case 0xc5: p += be(2); break;
      case 0xdb: case 0xc6: p += be(4); break;
      case 0xdc: cnt += be(2); break;
      case 0xdd: cnt += be(4); break;
      case 0xde: cnt += 2 * be(2); break;
      case 0xdf: cnt += 2 * be(4); break;
      case 0xd4: p += 2; break;
      case 0xd5: p += 3; break;
      case 0xd6: p += 5; break;
      case 0xd7: p += 9; break;
      case 0xd8: p += 17; break;
      case 0xc7: { size_t l = be(1); p += 1 + l; break; }
      case 0xc8: { size_t l = be(2); p += 1 + l; break; }
      case 0xc9: { size_t l = be(4); p += 1 + l; break; }
      default: throw std::runtime_error("msgpack: bad byte");
    }
    if (p > n) throw std::runtime_error("msgpack EOF");
  }
}

zj::Value unpackZiruValue(const uint8_t* d, size_t n, size_t& pos) {
  Unpacker u(d, n);
  u.p = pos;
  std::function<zj::Value()> rec = [&]() -> zj::Value {
    switch (u.nextType()) {
      case Unpacker::NIL: u.unpackNil(); return zj::Value::null();
      case Unpacker::BOOLEAN: return zj::Value(u.unpackBoolean());
      case Unpacker::INTEGER: return zj::Value((long long)u.unpackLong());
      case Unpacker::FLOAT: return zj::Value(u.unpackDouble());
      case Unpacker::STRING: return zj::Value(u.unpackString());
      case Unpacker::BINARY: {
        uint32_t len = u.unpackBinaryHeader();
        std::vector<uint8_t> b(len);
        if (len) u.readPayload(b.data(), len);
        return zj::Value(base64NoWrap(b.data(), b.size()));
      }
      case Unpacker::ARRAY: {
        uint32_t k = u.unpackArrayHeader();
        zj::ArrP a = zj::Array::make();
        for (uint32_t i = 0; i < k; i++) a->put(rec());
        return zj::Value(a);
      }
      case Unpacker::MAP: {
        uint32_t k = u.unpackMapHeader();
        zj::ObjP o = zj::Object::make();
        for (uint32_t i = 0; i < k; i++) {
          std::string key = u.unpackString();
          o->put(key, rec());
        }
        return zj::Value(o);
      }
      default: u.skipValue(); return zj::Value::null();
    }
  };
  zj::Value v = rec();
  pos = u.p;
  return v;
}

// ImmutableValue.toJson() followed by new JSONObject(...) – build the parsed form directly.
static zj::Value unpackAsParsedJson(Unpacker& u) {
  switch (u.nextType()) {
    case Unpacker::NIL: u.unpackNil(); return zj::Value::null();
    case Unpacker::BOOLEAN: return zj::Value(u.unpackBoolean());
    case Unpacker::INTEGER: {
      if (u.d[u.p] == 0xcf) {
        u.p++;
        uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | u.d[u.p++];
        if ((int64_t)v >= 0) { if (v <= INT32_MAX) return zj::Value((int)v); return zj::Value((long long)v); }
        return zj::Value((double)v);
      }
      int64_t v = u.unpackLong();
      if (v >= INT32_MIN && v <= INT32_MAX) return zj::Value((int)v);
      return zj::Value((long long)v);
    }
    case Unpacker::FLOAT: {
      double x = u.unpackDouble();
      if (std::isnan(x) || std::isinf(x)) return zj::Value::null();
      return zj::Value(x);
    }
    case Unpacker::STRING: return zj::Value(u.unpackString());
    case Unpacker::BINARY: {
      uint32_t len = u.unpackBinaryHeader();
      std::string s((const char*)u.d + u.p, len);
      u.p += len;
      return zj::Value(s);
    }
    case Unpacker::ARRAY: {
      uint32_t k = u.unpackArrayHeader();
      zj::ArrP a = zj::Array::make();
      for (uint32_t i = 0; i < k; i++) a->put(unpackAsParsedJson(u));
      return zj::Value(a);
    }
    case Unpacker::MAP: {
      uint32_t k = u.unpackMapHeader();
      zj::ObjP o = zj::Object::make();
      for (uint32_t i = 0; i < k; i++) {
        zj::Value key = unpackAsParsedJson(u);
        o->put(key.t == zj::Value::STR ? key.s : key.javaToString(), unpackAsParsedJson(u));
      }
      return zj::Value(o);
    }
    default: u.skipValue(); return zj::Value::null();
  }
}
std::string unpackValueToJson(const uint8_t* d, size_t n, size_t& pos) {
  Unpacker u(d, n);
  u.p = pos;
  zj::Value v = unpackAsParsedJson(u);
  pos = u.p;
  std::string out;
  zj::writeValue(out, v);
  return out;
}

// ---------------- zlib / gzip ----------------
static bool runDeflate(const Bytes& in, Bytes& out, int windowBits) {
  z_stream s{};
  if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
  out.resize(deflateBound(&s, in.size()) + 32);
  s.next_in = (Bytef*)in.data();
  s.avail_in = (uInt)in.size();
  s.next_out = out.data();
  s.avail_out = (uInt)out.size();
  int r = deflate(&s, Z_FINISH);
  out.resize(s.total_out);
  deflateEnd(&s);
  return r == Z_STREAM_END;
}
static bool runInflate(const uint8_t* p, size_t n, Bytes& out, int windowBits) {
  z_stream s{};
  if (inflateInit2(&s, windowBits) != Z_OK) return false;
  s.next_in = (Bytef*)p;
  s.avail_in = (uInt)n;
  out.clear();
  uint8_t buf[16384];
  int r;
  do {
    s.next_out = buf;
    s.avail_out = sizeof buf;
    r = inflate(&s, Z_NO_FLUSH);
    if (r != Z_OK && r != Z_STREAM_END) { inflateEnd(&s); return false; }
    out.insert(out.end(), buf, buf + (sizeof buf - s.avail_out));
  } while (r != Z_STREAM_END && (s.avail_in > 0 || s.avail_out == 0));
  inflateEnd(&s);
  // InflaterInputStream on truncated input throws EOFException ("Unexpected end of ZLIB input stream")
  return r == Z_STREAM_END;
}
Bytes zlibDeflate(const Bytes& in) { Bytes o; runDeflate(in, o, 15); return o; }
bool zlibInflate(const uint8_t* p, size_t n, Bytes& out) { return runInflate(p, n, out, 15); }
Bytes gzipCompress(const Bytes& in) {
  Bytes o;
  runDeflate(in, o, 15 + 16);
  if (o.size() > 9) o[9] = 0;  // Java GZIPOutputStream writes OS=0
  return o;
}
bool gzipDecompress(const uint8_t* p, size_t n, Bytes& out) { return runInflate(p, n, out, 15 + 16); }

// ---------------- AES ----------------
Bytes aesEncryptCbc(const Bytes& plain, const uint8_t iv[16]) {
  Bytes out(plain.size() + 16);
#ifdef __APPLE__
  size_t moved = 0;
  CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, kAesKey, 32, iv, plain.data(), plain.size(), out.data(), out.size(), &moved);
  out.resize(moved);
#else
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int l1 = 0, l2 = 0;
  EVP_EncryptInit_ex(c, EVP_aes_256_cbc(), nullptr, kAesKey, iv);
  EVP_EncryptUpdate(c, out.data(), &l1, plain.data(), (int)plain.size());
  EVP_EncryptFinal_ex(c, out.data() + l1, &l2);
  EVP_CIPHER_CTX_free(c);
  out.resize(l1 + l2);
#endif
  return out;
}
bool aesDecryptCbc(const uint8_t* ct, size_t n, const uint8_t iv[16], Bytes& out) {
  out.assign(n + 16, 0);
#ifdef __APPLE__
  size_t moved = 0;
  CCCryptorStatus st = CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, kAesKey, 32, iv, ct, n, out.data(), out.size(), &moved);
  if (st != kCCSuccess) return false;
  out.resize(moved);
  return true;
#else
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int l1 = 0, l2 = 0;
  EVP_DecryptInit_ex(c, EVP_aes_256_cbc(), nullptr, kAesKey, iv);
  bool ok = EVP_DecryptUpdate(c, out.data(), &l1, ct, (int)n) == 1 && EVP_DecryptFinal_ex(c, out.data() + l1, &l2) == 1;
  EVP_CIPHER_CTX_free(c);
  if (!ok) return false;
  out.resize(l1 + l2);
  return true;
#endif
}
void randomBytes(uint8_t* p, size_t n) {
#ifdef __APPLE__
  if (SecRandomCopyBytes(kSecRandomDefault, n, p) != errSecSuccess) for (size_t i = 0; i < n; i++) p[i] = (uint8_t)arc4random();
#else
  RAND_bytes(p, (int)n);
#endif
}

std::string base64NoWrap(const uint8_t* p, size_t n) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  size_t i = 0;
  for (; i + 2 < n; i += 3) {
    uint32_t v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
    o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
  }
  if (i + 1 == n) { uint32_t v = p[i] << 16; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "=="; }
  else if (i + 2 == n) { uint32_t v = (p[i] << 16) | (p[i + 1] << 8); o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '='; }
  return o;
}

Bytes encodeResponse(const zj::Value& v) {
  Bytes mp;
  packValue(mp, v);
  Bytes gz = gzipCompress(mp);
  uint8_t iv[16];
  randomBytes(iv, 16);
  Bytes ct = aesEncryptCbc(gz, iv);
  Bytes out(iv, iv + 16);
  out.insert(out.end(), ct.begin(), ct.end());
  return out;
}
bool decodeRequestToMsgpack(const Bytes& body, Bytes& msgpack) {
  if (body.size() <= 16) return false;
  Bytes plain;
  if (!aesDecryptCbc(body.data() + 16, body.size() - 16, body.data(), plain)) return false;
  return zlibInflate(plain.data(), plain.size(), msgpack);
}
Bytes encodeRequest(const zj::Value& v) {
  Bytes mp;
  packValue(mp, v);
  Bytes z = zlibDeflate(mp);
  uint8_t iv[16];
  randomBytes(iv, 16);
  Bytes ct = aesEncryptCbc(z, iv);
  Bytes out(iv, iv + 16);
  out.insert(out.end(), ct.begin(), ct.end());
  return out;
}
bool decodeResponse(const Bytes& body, zj::Value& v) {
  if (body.size() <= 16) return false;
  Bytes plain, mp;
  if (!aesDecryptCbc(body.data() + 16, body.size() - 16, body.data(), plain)) return false;
  if (!gzipDecompress(plain.data(), plain.size(), mp)) return false;
  size_t pos = 0;
  Unpacker u(mp.data(), mp.size());
  v = unpackZiruValue(mp.data(), mp.size(), pos);
  return true;
}

std::string hex(const uint8_t* p, size_t n) {
  static const char* h = "0123456789abcdef";
  std::string o;
  for (size_t i = 0; i < n; i++) { o += h[p[i] >> 4]; o += h[p[i] & 15]; }
  return o;
}

// ---------------- java.util.Random ----------------
static int64_t g_clock = 0;
static bool g_seedOverride = false;
static int64_t g_seed = 0;
void setClockOverride(int64_t ms) { g_clock = ms; }
void setRandomSeedOverride(int64_t seed, bool enabled) { g_seed = seed; g_seedOverride = enabled; }
int64_t currentTimeMillis() {
  if (g_clock) return g_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
JavaRandom::JavaRandom() {
  if (g_seedOverride) { setSeed(g_seed++); return; }
  static uint64_t uniq = 8682522807148012ULL;
  uniq *= 1181783497276652981ULL;
  int64_t nano = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  setSeed((int64_t)(uniq ^ (uint64_t)nano));
}
int32_t JavaRandom::next(int bits) {
  seed = (seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
  return (int32_t)(int64_t)(seed >> (48 - bits));
}
int32_t JavaRandom::nextInt() { return next(32); }
int32_t JavaRandom::nextInt(int32_t bound) {
  if (bound <= 0) throw std::invalid_argument("bound must be positive");
  int32_t r = next(31);
  int32_t m = bound - 1;
  if ((bound & m) == 0) return (int32_t)(((int64_t)bound * (int64_t)r) >> 31);
  for (int32_t u = r; (int32_t)((uint32_t)u - (uint32_t)(r = u % bound) + (uint32_t)m) < 0; u = next(31)) {}
  return r;
}
int64_t JavaRandom::nextLong() { return ((int64_t)next(32) << 32) + next(32); }
bool JavaRandom::nextBoolean() { return next(1) != 0; }
double JavaRandom::nextDouble() { return (double)(((int64_t)next(26) << 27) + next(27)) * 0x1.0p-53; }

}  // namespace zc

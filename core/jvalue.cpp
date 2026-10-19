#include "jvalue.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace zj {

int32_t d2i(double d) {
  if (std::isnan(d)) return 0;
  if (d >= 2147483647.0) return INT32_MAX;
  if (d <= -2147483648.0) return INT32_MIN;
  return (int32_t)d;
}
int64_t d2l(double d) {
  if (std::isnan(d)) return 0;
  if (d >= 9223372036854775807.0) return INT64_MAX;
  if (d <= -9223372036854775808.0) return INT64_MIN;
  return (int64_t)d;
}

// Shortest round-trip digits, then Java Double.toString layout.
std::string javaDoubleToString(double d) {
  if (std::isnan(d)) return "NaN";
  if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
  if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
  char buf[64];
  int prec = 1;
  for (; prec <= 17; prec++) {
    snprintf(buf, sizeof buf, "%.*e", prec - 1, d);
    if (strtod(buf, nullptr) == d) break;
  }
  // buf = [-]D.DDDDe[+-]XX
  std::string s(buf);
  bool neg = false;
  if (s[0] == '-') { neg = true; s = s.substr(1); }
  size_t e = s.find('e');
  std::string mant = s.substr(0, e);
  int exp10 = atoi(s.c_str() + e + 1);
  std::string digits;
  for (char c : mant) if (c != '.') digits += c;
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
  std::string out;
  double ad = std::fabs(d);
  if (ad >= 1e-3 && ad < 1e7) {
    int pointPos = exp10 + 1;  // digits before decimal point
    if (pointPos <= 0) {
      out = "0.";
      for (int i = 0; i < -pointPos; i++) out += '0';
      out += digits;
    } else if ((int)digits.size() <= pointPos) {
      out = digits;
      for (int i = (int)digits.size(); i < pointPos; i++) out += '0';
      out += ".0";
    } else {
      out = digits.substr(0, pointPos) + "." + digits.substr(pointPos);
    }
  } else {
    out = digits.substr(0, 1) + ".";
    out += digits.size() > 1 ? digits.substr(1) : "0";
    out += "E" + std::to_string(exp10);
  }
  return neg ? "-" + out : out;
}

static bool isJavaWs(char c) { return (unsigned char)c <= ' '; }

bool javaParseDouble(const std::string& in, double& out) {
  size_t b = 0, e = in.size();
  while (b < e && isJavaWs(in[b])) b++;
  while (e > b && isJavaWs(in[e - 1])) e--;
  std::string s = in.substr(b, e - b);
  if (s.empty()) return false;
  std::string t = s;
  if (!t.empty() && (t.back() == 'd' || t.back() == 'D' || t.back() == 'f' || t.back() == 'F')) t.pop_back();
  std::string u = t;
  size_t p = (u[0] == '+' || u[0] == '-') ? 1 : 0;
  std::string rest = u.substr(p);
  if (rest == "NaN") { out = NAN; return true; }
  if (rest == "Infinity") { out = (u[0] == '-') ? -INFINITY : INFINITY; return true; }
  // Reject hex, inf/nan spellings that strtod would accept but Java does not.
  for (char c : rest) {
    if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return false;
  }
  if (rest.empty()) return false;
  char* endp = nullptr;
  out = strtod(t.c_str(), &endp);
  return endp && *endp == 0;
}

int32_t Value::intValue() const {
  switch (t) {
    case INT: case LONG: return (int32_t)n;
    case DOUBLE: return d2i(d);
    default: return 0;
  }
}
int64_t Value::longValue() const {
  switch (t) {
    case INT: case LONG: return n;
    case DOUBLE: return d2l(d);
    default: return 0;
  }
}
double Value::doubleValue() const {
  switch (t) {
    case INT: case LONG: return (double)n;
    case DOUBLE: return d;
    default: return 0;
  }
}

std::string Value::javaToString() const {
  switch (t) {
    case JNULLPTR: return "null";
    case JNULL: return "null";
    case BOOL: return b ? "true" : "false";
    case INT: case LONG: return std::to_string(n);
    case DOUBLE: return javaDoubleToString(d);
    case STR: return s;
    case OBJ: return o->toString();
    case ARR: return a->toString();
  }
  return "";
}

bool Value::javaEquals(const Value& x) const {
  if (t != x.t) return false;
  switch (t) {
    case JNULLPTR: case JNULL: return true;
    case BOOL: return b == x.b;
    case INT: case LONG: return n == x.n;
    case DOUBLE: return std::memcmp(&d, &x.d, sizeof d) == 0 || (d == x.d && d != 0);
    case STR: return s == x.s;
    case OBJ: return o == x.o;
    case ARR: return a == x.a;
  }
  return false;
}

bool toBoolean(const Value& v, bool& out) {
  if (v.t == Value::BOOL) { out = v.b; return true; }
  if (v.t == Value::STR) {
    std::string l;
    for (char c : v.s) l += (char)tolower((unsigned char)c);
    if (l == "true") { out = true; return true; }
    if (l == "false") { out = false; return true; }
  }
  return false;
}
bool toInteger(const Value& v, int32_t& out) {
  if (v.isNumber()) { out = v.intValue(); return true; }
  if (v.t == Value::STR) { double d; if (javaParseDouble(v.s, d)) { out = d2i(d); return true; } }
  return false;
}
bool toLong(const Value& v, int64_t& out) {
  if (v.isNumber()) { out = v.longValue(); return true; }
  if (v.t == Value::STR) { double d; if (javaParseDouble(v.s, d)) { out = d2l(d); return true; } }
  return false;
}
bool toDouble(const Value& v, double& out) {
  if (v.isNumber()) { out = v.doubleValue(); return true; }
  if (v.t == Value::STR) { double d; if (javaParseDouble(v.s, d)) { out = d; return true; } }
  return false;
}
bool toStr(const Value& v, std::string& out) {
  if (v.t == Value::JNULLPTR) return false;
  out = v.javaToString();
  return true;
}

static std::string typeName(const Value& v) {
  switch (v.t) {
    case Value::JNULL: return "org.json.JSONObject$1";
    case Value::BOOL: return "java.lang.Boolean";
    case Value::INT: return "java.lang.Integer";
    case Value::LONG: return "java.lang.Long";
    case Value::DOUBLE: return "java.lang.Double";
    case Value::STR: return "java.lang.String";
    case Value::OBJ: return "org.json.JSONObject";
    case Value::ARR: return "org.json.JSONArray";
    default: return "null";
  }
}
[[noreturn]] static void mismatch(const std::string& where, const Value& v, const std::string& req) {
  if (v.isJavaNull()) throw JSONException("Value at " + where + " is null.");
  throw JSONException("Value " + v.javaToString() + " at " + where + " of type " + typeName(v) + " cannot be converted to " + req);
}

static void checkDouble(double d) {
  if (std::isnan(d) || std::isinf(d)) throw JSONException("Forbidden numeric value: " + javaDoubleToString(d));
}

// ---------------- Object ----------------
void Object::reindex() {
  idx.clear();
  for (size_t i = 0; i < kv.size(); i++) idx[kv[i].first] = i;
}
bool Object::isNull(const std::string& k) const { Value v = opt(k); return v.isJavaNull() || v.isNULL(); }
Value Object::opt(const std::string& k) const {
  auto it = idx.find(k);
  return it == idx.end() ? Value() : kv[it->second].second;
}
Value Object::get(const std::string& k) const {
  auto it = idx.find(k);
  if (it == idx.end()) throw JSONException("No value for " + k);
  return kv[it->second].second;
}
Value Object::remove(const std::string& k) {
  auto it = idx.find(k);
  if (it == idx.end()) return Value();
  Value v = kv[it->second].second;
  kv.erase(kv.begin() + it->second);
  reindex();
  return v;
}
std::vector<std::string> Object::keys() const {
  std::vector<std::string> r;
  r.reserve(kv.size());
  for (auto& p : kv) r.push_back(p.first);
  return r;
}
ObjP Object::put(const std::string& k, const Value& v) {
  if (v.isJavaNull()) { remove(k); return shared_from_this(); }
  if (v.t == Value::DOUBLE) checkDouble(v.d);
  auto it = idx.find(k);
  if (it == idx.end()) { idx[k] = kv.size(); kv.emplace_back(k, v); }
  else kv[it->second].second = v;  // LinkedHashMap keeps original position
  return shared_from_this();
}
ObjP Object::put(const std::string& k, double v) { checkDouble(v); return put(k, Value(v)); }
ObjP Object::accumulate(const std::string& k, const Value& v) {
  Value cur = opt(k);
  if (cur.isJavaNull()) return put(k, v);
  if (cur.t == Value::ARR) { cur.a->put(v); return shared_from_this(); }
  ArrP a = Array::make(); a->put(cur); a->put(v);
  return put(k, a);
}
ObjP Object::copyOf(const ObjP& o, const std::vector<std::string>& names) {
  ObjP r = make();
  for (auto& n : names) { Value v = o->opt(n); if (!v.isJavaNull()) r->put(n, v); }
  return r;
}

bool Object::optBoolean(const std::string& k, bool def) const { bool r; return toBoolean(opt(k), r) ? r : def; }
int32_t Object::optInt(const std::string& k, int32_t def) const { int32_t r; return toInteger(opt(k), r) ? r : def; }
int64_t Object::optLong(const std::string& k, int64_t def) const { int64_t r; return toLong(opt(k), r) ? r : def; }
double Object::optDouble(const std::string& k) const { return optDouble(k, NAN); }
double Object::optDouble(const std::string& k, double def) const { double r; return toDouble(opt(k), r) ? r : def; }
std::string Object::optString(const std::string& k, const std::string& def) const { std::string r; return toStr(opt(k), r) ? r : def; }
ObjP Object::optJSONObject(const std::string& k) const { Value v = opt(k); return v.t == Value::OBJ ? v.o : nullptr; }
ArrP Object::optJSONArray(const std::string& k) const { Value v = opt(k); return v.t == Value::ARR ? v.a : nullptr; }

bool Object::getBoolean(const std::string& k) const { Value v = get(k); bool r; if (!toBoolean(v, r)) mismatch(k, v, "boolean"); return r; }
int32_t Object::getInt(const std::string& k) const { Value v = get(k); int32_t r; if (!toInteger(v, r)) mismatch(k, v, "int"); return r; }
int64_t Object::getLong(const std::string& k) const { Value v = get(k); int64_t r; if (!toLong(v, r)) mismatch(k, v, "long"); return r; }
double Object::getDouble(const std::string& k) const { Value v = get(k); double r; if (!toDouble(v, r)) mismatch(k, v, "double"); return r; }
std::string Object::getString(const std::string& k) const { Value v = get(k); std::string r; if (!toStr(v, r)) mismatch(k, v, "String"); return r; }
ObjP Object::getJSONObject(const std::string& k) const { Value v = get(k); if (v.t != Value::OBJ) mismatch(k, v, "JSONObject"); return v.o; }
ArrP Object::getJSONArray(const std::string& k) const { Value v = get(k); if (v.t != Value::ARR) mismatch(k, v, "JSONArray"); return v.a; }

std::string Object::toString() const { std::string out; writeValue(out, Value(std::const_pointer_cast<Object>(shared_from_this()))); return out; }

// ---------------- Array ----------------
Value Array::get(int i) const {
  if (i < 0 || i >= (int)v.size()) throw JSONException("Index " + std::to_string(i) + " out of range [0.." + std::to_string(v.size()) + ")");
  Value x = v[i];
  if (x.isJavaNull()) throw JSONException("Value at " + std::to_string(i) + " is null.");
  return x;
}
ArrP Array::put(double x) { checkDouble(x); v.push_back(Value(x)); return shared_from_this(); }
ArrP Array::put(int i, const Value& x) {
  if (x.t == Value::DOUBLE) checkDouble(x.d);
  if (i < 0) throw JSONException("Index " + std::to_string(i) + " out of range");
  while ((int)v.size() <= i) v.push_back(Value());
  v[i] = x;
  return shared_from_this();
}
ArrP Array::put(int i, double x) { checkDouble(x); return put(i, Value(x)); }
Value Array::remove(int i) {
  if (i < 0 || i >= (int)v.size()) return Value();
  Value x = v[i];
  v.erase(v.begin() + i);
  return x;
}
bool Array::optBoolean(int i, bool def) const { bool r; return toBoolean(opt(i), r) ? r : def; }
int32_t Array::optInt(int i, int32_t def) const { int32_t r; return toInteger(opt(i), r) ? r : def; }
int64_t Array::optLong(int i, int64_t def) const { int64_t r; return toLong(opt(i), r) ? r : def; }
double Array::optDouble(int i, double def) const { double r; return toDouble(opt(i), r) ? r : def; }
std::string Array::optString(int i, const std::string& def) const { std::string r; return toStr(opt(i), r) ? r : def; }
ObjP Array::optJSONObject(int i) const { Value x = opt(i); return x.t == Value::OBJ ? x.o : nullptr; }
ArrP Array::optJSONArray(int i) const { Value x = opt(i); return x.t == Value::ARR ? x.a : nullptr; }
bool Array::getBoolean(int i) const { Value x = get(i); bool r; if (!toBoolean(x, r)) mismatch(std::to_string(i), x, "boolean"); return r; }
int32_t Array::getInt(int i) const { Value x = get(i); int32_t r; if (!toInteger(x, r)) mismatch(std::to_string(i), x, "int"); return r; }
int64_t Array::getLong(int i) const { Value x = get(i); int64_t r; if (!toLong(x, r)) mismatch(std::to_string(i), x, "long"); return r; }
double Array::getDouble(int i) const { Value x = get(i); double r; if (!toDouble(x, r)) mismatch(std::to_string(i), x, "double"); return r; }
std::string Array::getString(int i) const { Value x = get(i); std::string r; if (!toStr(x, r)) mismatch(std::to_string(i), x, "String"); return r; }
ObjP Array::getJSONObject(int i) const { Value x = get(i); if (x.t != Value::OBJ) mismatch(std::to_string(i), x, "JSONObject"); return x.o; }
ArrP Array::getJSONArray(int i) const { Value x = get(i); if (x.t != Value::ARR) mismatch(std::to_string(i), x, "JSONArray"); return x.a; }
std::string Array::join(const std::string& sep) const {
  std::string out;
  for (size_t i = 0; i < v.size(); i++) { if (i) out += sep; writeValue(out, v[i]); }
  return out;
}
std::string Array::toString() const { std::string out; writeValue(out, Value(std::const_pointer_cast<Array>(shared_from_this()))); return out; }

// ---------------- Writer ----------------
std::string numberToString(const Value& n) {
  if (n.t == Value::INT || n.t == Value::LONG) return std::to_string(n.n);
  double d = n.d;
  checkDouble(d);
  if (d == 0 && std::signbit(d)) return "-0";
  int64_t l = d2l(d);
  if (d == (double)l) return std::to_string(l);
  return javaDoubleToString(d);
}

static void writeString(std::string& out, const std::string& s) {
  out += '"';
  char prev = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    switch (c) {
      case '"': case '\\': out += '\\'; out += c; break;
      case '/': if (prev == '<') out += '\\'; out += c; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\f': out += "\\f"; break;
      default:
        if ((unsigned char)c <= 0x1F) { char b[8]; snprintf(b, sizeof b, "\\u%04x", (unsigned char)c); out += b; }
        else out += c;
    }
    prev = c;
  }
  out += '"';
}
std::string quote(const std::string& s) { std::string o; writeString(o, s); return o; }

void writeValue(std::string& out, const Value& v) {
  switch (v.t) {
    case Value::JNULLPTR: case Value::JNULL: out += "null"; break;
    case Value::BOOL: out += v.b ? "true" : "false"; break;
    case Value::INT: case Value::LONG: case Value::DOUBLE: out += numberToString(v); break;
    case Value::STR: writeString(out, v.s); break;
    case Value::OBJ: {
      out += '{';
      bool first = true;
      for (auto& p : v.o->kv) {
        if (!first) out += ',';
        first = false;
        writeString(out, p.first);
        out += ':';
        writeValue(out, p.second);
      }
      out += '}';
      break;
    }
    case Value::ARR: {
      out += '[';
      for (size_t i = 0; i < v.a->v.size(); i++) { if (i) out += ','; writeValue(out, v.a->v[i]); }
      out += ']';
      break;
    }
  }
}

// ---------------- Tokener (Android JSONTokener) ----------------
namespace {
struct Tok {
  const std::string& in;
  size_t pos = 0;
  explicit Tok(const std::string& s) : in(s) {}
  [[noreturn]] void err(const std::string& m) { throw JSONException(m + " at character " + std::to_string(pos)); }
  int nextClean() {
    while (pos < in.size()) {
      char c = in[pos++];
      switch (c) {
        case '\t': case ' ': case '\n': case '\r': continue;
        case '/': {
          if (pos == in.size()) return c;
          char p = in[pos];
          if (p == '*') {
            pos++;
            size_t e = in.find("*/", pos);
            if (e == std::string::npos) err("Unterminated comment");
            pos = e + 2;
            continue;
          } else if (p == '/') {
            pos++;
            while (pos < in.size() && in[pos] != '\r' && in[pos] != '\n') pos++;
            continue;
          }
          return c;
        }
        case '#':
          while (pos < in.size() && in[pos] != '\r' && in[pos] != '\n') pos++;
          continue;
        default: return (unsigned char)c;
      }
    }
    return -1;
  }
  static void appendUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  }
  uint32_t hex4() {
    if (pos + 4 > in.size()) err("Unterminated escape sequence");
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
      char c = in[pos++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else err("Invalid escape sequence");
    }
    return v;
  }
  std::string nextString(char q) {
    std::string out;
    while (pos < in.size()) {
      char c = in[pos++];
      if (c == q) return out;
      if (c == '\\') {
        if (pos == in.size()) err("Unterminated escape sequence");
        char e = in[pos++];
        switch (e) {
          case 'u': {
            uint32_t cp = hex4();
            if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= in.size() && in[pos] == '\\' && in[pos + 1] == 'u') {
              size_t save = pos; pos += 2;
              uint32_t lo = hex4();
              if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              else pos = save;
            }
            appendUtf8(out, cp);
            break;
          }
          case 't': out += '\t'; break;
          case 'b': out += '\b'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 'f': out += '\f'; break;
          default: out += e;
        }
      } else out += c;
    }
    err("Unterminated string");
  }
  Value readLiteral() {
    size_t start = pos;
    static const char* excl = "{}[]/\\:,=;# \t\f";
    while (pos < in.size()) { char c = in[pos]; if (c == '\r' || c == '\n' || strchr(excl, c)) break; pos++; }
    std::string lit = in.substr(start, pos - start);
    if (lit.empty()) err("Expected literal value");
    std::string low; for (char c : lit) low += (char)tolower((unsigned char)c);
    if (low == "null") return Value::null();
    if (low == "true") return Value(true);
    if (low == "false") return Value(false);
    if (lit.find('.') == std::string::npos) {
      int base = 10;
      std::string num = lit;
      if (num.rfind("0x", 0) == 0 || num.rfind("0X", 0) == 0) { num = num.substr(2); base = 16; }
      else if (num.size() > 1 && num[0] == '0') { num = num.substr(1); base = 8; }
      // Long.parseLong
      bool ok = !num.empty();
      size_t i = 0; bool neg = false;
      if (ok && (num[0] == '-' || num[0] == '+')) { neg = num[0] == '-'; i = 1; if (num.size() == 1) ok = false; }
      unsigned long long acc = 0;
      const unsigned long long lim = neg ? 9223372036854775808ULL : 9223372036854775807ULL;
      for (; ok && i < num.size(); i++) {
        char c = num[i]; int dgt;
        if (c >= '0' && c <= '9') dgt = c - '0';
        else if (c >= 'a' && c <= 'z') dgt = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') dgt = c - 'A' + 10;
        else { ok = false; break; }
        if (dgt >= base) { ok = false; break; }
        if (acc > (lim - dgt) / base) { ok = false; break; }
        acc = acc * base + dgt;
      }
      if (ok) {
        long long lv = neg ? (long long)(0 - acc) : (long long)acc;
        if (lv <= INT32_MAX && lv >= INT32_MIN) return Value((int)lv);
        return Value(lv);
      }
    }
    double d;
    if (javaParseDouble(lit, d)) return Value(d);
    return Value(lit);
  }
  Value nextValue() {
    int c = nextClean();
    switch (c) {
      case -1: err("End of input");
      case '{': return Value(readObject());
      case '[': return Value(readArray());
      case '\'': case '"': return Value(nextString((char)c));
      default: pos--; return readLiteral();
    }
  }
  ObjP readObject() {
    ObjP r = Object::make();
    int first = nextClean();
    if (first == '}') return r;
    if (first != -1) pos--;
    while (true) {
      Value name = nextValue();
      std::string key;
      if (name.t == Value::STR) key = name.s;
      else if (name.isJavaNull()) err("Names cannot be null");
      else key = name.javaToString();
      int sep = nextClean();
      if (sep != ':' && sep != '=') err("Expected ':' after " + key);
      if (pos < in.size() && in[pos] == '>') pos++;
      r->put(key, nextValue());
      int c = nextClean();
      if (c == '}') return r;
      if (c == ';' || c == ',') continue;
      err("Unterminated object");
    }
  }
  ArrP readArray() {
    ArrP r = Array::make();
    bool hasTrailingSep = false;
    while (true) {
      int c = nextClean();
      switch (c) {
        case -1: err("Unterminated array");
        case ']': if (hasTrailingSep) r->put(Value()); return r;
        case ',': case ';': r->put(Value()); hasTrailingSep = true; continue;
        default: pos--;
      }
      r->put(nextValue());
      c = nextClean();
      if (c == ']') return r;
      if (c == ',' || c == ';') { hasTrailingSep = true; continue; }
      err("Unterminated array");
    }
  }
};
}  // namespace

Value parseAny(const std::string& json) { Tok t(json); return t.nextValue(); }
ObjP Object::parse(const std::string& json) {
  Value v = parseAny(json);
  if (v.t != Value::OBJ) mismatch("", v, "JSONObject");
  return v.o;
}
ArrP Array::parse(const std::string& json) {
  Value v = parseAny(json);
  if (v.t != Value::ARR) mismatch("", v, "JSONArray");
  return v.a;
}

}  // namespace zj

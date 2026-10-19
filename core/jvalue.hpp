// Android org.json semantics in C++ (reference semantics, LinkedHashMap ordering,
// Integer/Long/Double typing, opt*/get* conversion rules, Android toString formatting).
#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zj {

struct Object;
struct Array;
using ObjP = std::shared_ptr<Object>;
using ArrP = std::shared_ptr<Array>;

struct JSONException : std::runtime_error {
  explicit JSONException(const std::string& m) : std::runtime_error(m) {}
};

// Java-style helpers
int32_t d2i(double d);   // Java (int) cast
int64_t d2l(double d);   // Java (long) cast
std::string javaDoubleToString(double d);  // Double.toString
bool javaParseDouble(const std::string& s, double& out);  // Double.parseDouble (trimmed)

struct Value {
  enum T : uint8_t { JNULLPTR, JNULL, BOOL, INT, LONG, DOUBLE, STR, OBJ, ARR } t = JNULLPTR;
  bool b = false;
  int64_t n = 0;   // INT (int32 range) or LONG
  double d = 0;
  std::string s;
  ObjP o;
  ArrP a;

  Value() = default;
  static Value null() { Value v; v.t = JNULL; return v; }   // JSONObject.NULL
  Value(bool x) : t(BOOL), b(x) {}
  Value(int x) : t(INT), n(x) {}
  Value(long x) : t(sizeof(long) == 8 ? LONG : INT), n(x) {}
  Value(long long x) : t(LONG), n(x) {}
  Value(unsigned x) = delete;
  Value(double x) : t(DOUBLE), d(x) {}
  Value(const char* x) : t(STR), s(x) {}
  Value(const std::string& x) : t(STR), s(x) {}
  Value(std::string&& x) : t(STR), s(std::move(x)) {}
  Value(ObjP x) { if (x) { t = OBJ; o = std::move(x); } }
  Value(ArrP x) { if (x) { t = ARR; a = std::move(x); } }

  bool isJavaNull() const { return t == JNULLPTR; }
  bool isNULL() const { return t == JNULL; }
  bool isNumber() const { return t == INT || t == LONG || t == DOUBLE; }
  // Number.intValue()/longValue()/doubleValue()
  int32_t intValue() const;
  int64_t longValue() const;
  double doubleValue() const;
  // String.valueOf(this) (Java toString of the boxed object)
  std::string javaToString() const;
  // Java equals() for boxed values (used for contains/equals checks)
  bool javaEquals(const Value& o) const;
};

// JSON.toBoolean/toInteger/... returning false when not convertible
bool toBoolean(const Value& v, bool& out);
bool toInteger(const Value& v, int32_t& out);
bool toLong(const Value& v, int64_t& out);
bool toDouble(const Value& v, double& out);
bool toStr(const Value& v, std::string& out);  // JSON.toString (null -> false)

struct Object : std::enable_shared_from_this<Object> {
  std::vector<std::pair<std::string, Value>> kv;
  std::unordered_map<std::string, size_t> idx;

  static ObjP make() { return std::make_shared<Object>(); }
  static ObjP parse(const std::string& json);   // new JSONObject(String) – throws JSONException
  static ObjP copyOf(const ObjP& o, const std::vector<std::string>& names);  // new JSONObject(o, names)

  int length() const { return (int)kv.size(); }
  bool has(const std::string& k) const { return idx.count(k) != 0; }
  bool isNull(const std::string& k) const;
  Value opt(const std::string& k) const;          // JAVA null if absent
  Value get(const std::string& k) const;          // throws if absent
  Value remove(const std::string& k);
  std::vector<std::string> keys() const;          // insertion order snapshot
  std::vector<std::string> names() const { return keys(); }

  ObjP put(const std::string& k, const Value& v); // Java null removes
  ObjP put(const std::string& k, double v);        // checkDouble
  ObjP put(const std::string& k, int v) { return put(k, Value(v)); }
  ObjP put(const std::string& k, long v) { return put(k, Value(v)); }
  ObjP put(const std::string& k, long long v) { return put(k, Value(v)); }
  ObjP put(const std::string& k, bool v) { return put(k, Value(v)); }
  ObjP put(const std::string& k, const char* v) { return put(k, Value(v)); }
  ObjP put(const std::string& k, const std::string& v) { return put(k, Value(v)); }
  ObjP put(const std::string& k, const ObjP& v) { return put(k, Value(v)); }
  ObjP put(const std::string& k, const ArrP& v) { return put(k, Value(v)); }
  ObjP putOpt(const std::string& k, const Value& v) { if (k.empty() || v.isJavaNull()) return shared_from_this(); return put(k, v); }
  ObjP accumulate(const std::string& k, const Value& v);

  bool optBoolean(const std::string& k, bool def = false) const;
  int32_t optInt(const std::string& k, int32_t def = 0) const;
  int64_t optLong(const std::string& k, int64_t def = 0) const;
  double optDouble(const std::string& k) const;
  double optDouble(const std::string& k, double def) const;
  std::string optString(const std::string& k, const std::string& def = "") const;
  ObjP optJSONObject(const std::string& k) const;
  ArrP optJSONArray(const std::string& k) const;

  bool getBoolean(const std::string& k) const;
  int32_t getInt(const std::string& k) const;
  int64_t getLong(const std::string& k) const;
  double getDouble(const std::string& k) const;
  std::string getString(const std::string& k) const;
  ObjP getJSONObject(const std::string& k) const;
  ArrP getJSONArray(const std::string& k) const;

  std::string toString() const;
 private:
  void reindex();
};

struct Array : std::enable_shared_from_this<Array> {
  std::vector<Value> v;
  static ArrP make() { return std::make_shared<Array>(); }
  static ArrP parse(const std::string& json);
  int length() const { return (int)v.size(); }
  Value opt(int i) const { return (i < 0 || i >= (int)v.size()) ? Value() : v[i]; }
  Value get(int i) const;
  bool isNull(int i) const { Value x = opt(i); return x.isJavaNull() || x.isNULL(); }
  ArrP put(const Value& x) { v.push_back(x); return shared_from_this(); }
  ArrP put(double x);
  ArrP put(int x) { return put(Value(x)); }
  ArrP put(long x) { return put(Value(x)); }
  ArrP put(long long x) { return put(Value(x)); }
  ArrP put(bool x) { return put(Value(x)); }
  ArrP put(const char* x) { return put(Value(x)); }
  ArrP put(const std::string& x) { return put(Value(x)); }
  ArrP put(const ObjP& x) { return put(Value(x)); }
  ArrP put(const ArrP& x) { return put(Value(x)); }
  ArrP put(int i, const Value& x);   // pads with Java null
  ArrP put(int i, double x);
  ArrP put(int i, int x) { return put(i, Value(x)); }
  ArrP put(int i, long x) { return put(i, Value(x)); }
  ArrP put(int i, long long x) { return put(i, Value(x)); }
  ArrP put(int i, bool x) { return put(i, Value(x)); }
  ArrP put(int i, const std::string& x) { return put(i, Value(x)); }
  ArrP put(int i, const char* x) { return put(i, Value(x)); }
  ArrP put(int i, const ObjP& x) { return put(i, Value(x)); }
  ArrP put(int i, const ArrP& x) { return put(i, Value(x)); }
  Value remove(int i);

  bool optBoolean(int i, bool def = false) const;
  int32_t optInt(int i, int32_t def = 0) const;
  int64_t optLong(int i, int64_t def = 0) const;
  double optDouble(int i, double def) const;
  std::string optString(int i, const std::string& def = "") const;
  ObjP optJSONObject(int i) const;
  ArrP optJSONArray(int i) const;
  bool getBoolean(int i) const;
  int32_t getInt(int i) const;
  int64_t getLong(int i) const;
  double getDouble(int i) const;
  std::string getString(int i) const;
  ObjP getJSONObject(int i) const;
  ArrP getJSONArray(int i) const;
  std::string join(const std::string& sep) const;
  std::string toString() const;
};

Value parseAny(const std::string& json);       // JSONTokener.nextValue()
std::string quote(const std::string& s);       // JSONObject.quote
std::string numberToString(const Value& n);    // JSONObject.numberToString
void writeValue(std::string& out, const Value& v);

inline ObjP newObj() { return Object::make(); }
inline ArrP newArr() { return Array::make(); }

}  // namespace zj

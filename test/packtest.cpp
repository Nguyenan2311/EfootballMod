#include <cstdio>
#include "../core/datapack.hpp"
int main(int c, char** v) {
  zd::Layout L{v[2], v[3]};
  zd::Manifest m; std::string err;
  if (!zd::inspectPack(v[1], m, err)) { printf("inspect FAIL %s\n", err.c_str()); return 1; }
  if (!zd::checkCompatible(m, "11.1.1", "11.1.101", "1.0.0", err)) { printf("compat FAIL %s\n", err.c_str()); return 1; }
  uint64_t last = 0;
  bool ok = zd::installPack(v[1], L, 0, [&](const zd::Progress& p) { if (p.done - last > (64u<<20) || p.phase != "extract") { printf("  %s %s %llu/%llu\n", p.phase.c_str(), p.file.c_str(), (unsigned long long)p.done, (unsigned long long)p.total); last = p.done; } return true; }, m, err);
  printf("install %s %s\n", ok ? "OK" : "FAIL", err.c_str());
  printf("active=%s verify=%d %s\n", zd::activeId(L).c_str(), zd::quickVerify(L, zd::activeId(L), err), err.c_str());
  return ok ? 0 : 1;
}

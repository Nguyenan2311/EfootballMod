// zipcheck <zip>: lists entries via the importer's reader and extracts each to /dev/null-like temp, verifying CRC.
#include "../core/zipread.hpp"
#include <cstdio>
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: zipcheck <zip> <tmpfile>\n"); return 2; }
  zz::Reader r; std::string err;
  if (!r.open(argv[1], err)) { printf("OPEN FAIL %s\n", err.c_str()); return 1; }
  for (auto& e : r.entries()) {
    std::string sha;
    if (!r.extract(e, argv[2], sha, err)) { printf("FAIL %s: %s\n", e.name.c_str(), err.c_str()); return 1; }
    printf("%s %llu %s\n", e.name.c_str(), (unsigned long long)e.size, sha.c_str());
  }
  printf("OK %zu entries\n", r.entries().size());
}

// Same TSV protocol as the JVM oracle (Run.java): method \t uri \t base64(body)|-  ->  status \t mime \t base64(body)|-
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "../core/server.hpp"

static std::string b64enc(const std::vector<uint8_t>& d) { return d.empty() ? "-" : zc::base64NoWrap(d.data(), d.size()); }
static std::vector<uint8_t> b64dec(const std::string& s) {
  std::vector<uint8_t> out;
  if (s == "-") return out;
  int val = 0, bits = -8;
  for (unsigned char c : s) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else continue;
    val = (val << 6) + v;
    bits += 6;
    if (bits >= 0) { out.push_back((uint8_t)((val >> bits) & 0xFF)); bits -= 8; }
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: runcpp assets data script out [seed]\n"); return 2; }
  zc::setRandomSeedOverride(argc > 5 ? atoll(argv[5]) : 1000, true);
  zs::setVerbose(getenv("ZLOG") != nullptr);
  zsv::Config cfg;
  cfg.assetsDir = argv[1];
  cfg.dataDir = argv[2];
  zsv::OfflineServer srv(cfg);
  std::ifstream in(argv[3]);
  std::ofstream out(argv[4]);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::vector<std::string> p;
    std::stringstream ss(line);
    std::string f;
    while (std::getline(ss, f, '\t')) p.push_back(f);
    if (p[0] == "CLOCK") { zc::setClockOverride(atoll(p[1].c_str())); continue; }
    zsv::Request r;
    r.method = p[0];
    r.uri = p[1];
    r.body = b64dec(p.size() > 2 ? p[2] : "-");
    if (!r.body.empty()) r.headers["content-length"] = std::to_string(r.body.size());
    zsv::Response resp = srv.serve(r);
    out << resp.status << "\t" << resp.mime << "\t" << b64enc(resp.body) << "\n";
  }
  return 0;
}

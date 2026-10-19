CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wno-unused
CORE = core/jvalue.cpp core/codec.cpp core/store.cpp core/server.cpp core/zipread.cpp
build/gameplaytest: $(CORE) test/gameplaytest.cpp core/*.hpp core/*.inc
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(CORE) test/gameplaytest.cpp -lz -lsqlite3 -lcrypto
build/runcpp: $(CORE) test/runcpp.cpp core/*.hpp core/*.inc
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(CORE) test/runcpp.cpp -lz -lsqlite3 -lcrypto
build/zipcheck: core/zipread.cpp test/zipcheck.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ core/zipread.cpp test/zipcheck.cpp -lz

build/packtest: core/datapack.cpp core/zipread.cpp core/jvalue.cpp test/packtest.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ core/datapack.cpp core/zipread.cpp core/jvalue.cpp test/packtest.cpp -lz

build/modtest: core/modpack.cpp core/datapack.cpp core/zipread.cpp core/jvalue.cpp test/modtest.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ core/modpack.cpp core/datapack.cpp core/zipread.cpp core/jvalue.cpp test/modtest.cpp -lz

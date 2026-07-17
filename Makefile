CXX ?= clang++
OPENSSL_DIR := $(shell brew --prefix openssl 2>/dev/null || echo /usr/local/opt/openssl)
CXXFLAGS = -O2 -std=c++17 -Wall -I$(OPENSSL_DIR)/include
LDFLAGS = -L$(OPENSSL_DIR)/lib -lz -lssl -lcrypto -lpthread

all: boxpack boxrun

boxpack: boxpack.cpp box.h boxpool.h
	$(CXX) $(CXXFLAGS) -o boxpack boxpack.cpp $(LDFLAGS)

boxrun: boxrun.cpp box.h boxpool.h
	$(CXX) $(CXXFLAGS) -o boxrun boxrun.cpp $(LDFLAGS)

clean:
	rm -f boxpack boxrun

install: all
	cp boxpack boxrun /usr/local/bin/
	mkdir -p /usr/local/lib/boxvm
	cp boxvm.html landing.html portal.html sw.js worker.js wasi.js /usr/local/lib/boxvm/

.PHONY: all clean install

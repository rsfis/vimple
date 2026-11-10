CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation
LDLIBS   ?= -lncursesw

rsfvim: src/main.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp -o rsfvim $(LDLIBS)

install: rsfvim
	install -Dm755 rsfvim $(DESTDIR)/usr/local/bin/rsfvim

clean:
	rm -f rsfvim

.PHONY: install clean

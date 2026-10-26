# Makefile for dmk2hfe - builds on Linux and macOS
#
# Usage:
#   make                 build ./dmk2hfe
#   make install         install to $(PREFIX)/bin (default /usr/local)
#   make uninstall       remove the installed binary
#   make clean           remove build products
#
# Override as needed, e.g.:  make CXX=clang++ PREFIX=$$HOME/.local

TARGET  := dmk2hfe
SRC     := dmk2hfe.cpp
OBJ     := $(SRC:.cpp=.o)

# Default compiler: on macOS "c++" is clang, on Linux it is usually g++.
CXX      ?= c++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra

PREFIX  ?= /usr/local
BINDIR  := $(DESTDIR)$(PREFIX)/bin
INSTALL ?= install

.PHONY: all install uninstall clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

install: $(TARGET)
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) -m 755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJ)

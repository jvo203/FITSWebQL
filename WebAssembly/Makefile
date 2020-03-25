VERSION = 2020.03.25.0

INCLUDE=
#../install-1.38/include
LIBRARY=
#../install-1.38/lib
CXXFLAGS=-std=c++11 -O3
LDFLAGS=--llvm-lto 1
#-lIlmImf-2_2 -lIlmThread-2_2 -lIex-2_2 -lHalf -lz --llvm-lto 1

.PHONY=clean

EMFLAGS=
EMFLAGS+=-s ALLOW_MEMORY_GROWTH=1
EMFLAGS+=-s NO_EXIT_RUNTIME=1
EMFLAGS+=-s NO_FILESYSTEM=1
EMFLAGS+=-s MODULARIZE=1
EMFLAGS+=-s EXPORT_NAME="EXR"

# embind
EMFLAGS+=--bind

wasm: main.cc
	em++ $(EMFLAGS) $< -o ../htdocs2/fitswebql/exr$(VERSION).js -I$(INCLUDE) -L$(LIBRARY) $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f exr.js exr.js.mem exr.wasm
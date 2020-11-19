.DEFAULT_GOAL := fitswebql
VERSION = 20.11.19.0

FPZIP=fpzip
SRC=OpenEXR/*.cpp $(FPZIP)/src/*.cpp
INCLUDE=-I./ -I./OpenEXR -I./$(FPZIP)/include -I./$(FPZIP)/src
LIBRARY=
CXXFLAGS=-std=c++11 -O3
LDFLAGS=-lz --llvm-lto 1

.PHONY=clean

EMFLAGS=--bind
EMFLAGS+=-s ALLOW_MEMORY_GROWTH=1
EMFLAGS+=-s NO_EXIT_RUNTIME=1
EMFLAGS+=-s NO_FILESYSTEM=1
#EMFLAGS+=-s USE_PTHREADS=1 -s PROXY_TO_PTHREAD -s WASM_MEM_MAX=2GB
EMFLAGS+=-s USE_ZLIB=1

#EMFLAGS+=-s MODULARIZE=1
#EMFLAGS+=-s EXPORT_NAME="WASM"

FPZIP_FP = FPZIP_FP_FAST
FPZIP_BLOCK_SIZE = 0x1000
DEFS += -DFPZIP_BLOCK_SIZE=$(FPZIP_BLOCK_SIZE) -DFPZIP_FP=$(FPZIP_FP) $(FPZIP_CONV)

wasm:
	em++ $(EMFLAGS) $(DEFS) $(SRC) colourmap.cc main.cc -o ../htdocs/fitswebql/exr.$(VERSION).js --post-js module-post.js $(INCLUDE) -L$(LIBRARY) $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f ../htdocs/fitswebql/exr.*.js ../htdocs/fitswebql/exr.*.js.mem ../htdocs/fitswebql/exr.*.wasm

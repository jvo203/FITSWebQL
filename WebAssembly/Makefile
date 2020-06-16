VERSION = 20.06.16.0

SRC=OpenEXR/*.cpp
INCLUDE=-I./ -I./OpenEXR
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

# this is an offending flag!!!
#EMFLAGS+=-s MODULARIZE=1

#EMFLAGS+=-s EXPORT_NAME="EXR"

wasm:
	em++ $(EMFLAGS) $(SRC) colourmap.cc main.cc -o ../htdocs/fitswebql/exr.$(VERSION).js --post-js module-post.js $(INCLUDE) -L$(LIBRARY) $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f ../htdocs/fitswebql/exr.*.js ../htdocs/fitswebql/exr.*.js.mem ../htdocs/fitswebql/exr.*.wasm

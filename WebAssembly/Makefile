VERSION = 20.03.25.0

SRC=OpenEXR/*.cpp
INCLUDE=-I./ -I./OpenEXR
#install/include
LIBRARY=
#install/lib
CXXFLAGS=-std=c++11 -O3
LDFLAGS=-lz --llvm-lto 1
#-lIlmImf-2_2 -lIlmThread-2_2 -lIex-2_2 -lHalf

.PHONY=clean

EMFLAGS=--bind
EMFLAGS+=-s ALLOW_MEMORY_GROWTH=1
EMFLAGS+=-s NO_EXIT_RUNTIME=1
EMFLAGS+=-s NO_FILESYSTEM=1
EMFLAGS+=-s USE_PTHREADS=1 -s WASM_MEM_MAX=2GB
EMFLAGS+=-s USE_ZLIB=1

# this is an offending flag!!!
#EMFLAGS+=-s MODULARIZE=1

#EMFLAGS+=-s EXPORT_NAME="EXR"

wasm:
	em++ $(EMFLAGS) $(SRC) colourmap.cc main.cc -o ../htdocs2/fitswebql/exr.$(VERSION).js $(INCLUDE) -L$(LIBRARY) $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f ../htdocs2/fitswebql/exr.*.js ../htdocs2/fitswebql/exr.*.js.mem ../htdocs2/fitswebql/exr.*.wasm

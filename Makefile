SRC = src/main.cpp src/fits.cpp src/json.c
INC = -I/usr/include/postgresql
LIBS = -lstdc++fs -lsqlite3 -lcurl -lpq -luWS -lssl -lz -lzfp -lnuma -lpthread -ipp
JEMALLOC = -L`jemalloc-config --libdir` -Wl,-rpath,`jemalloc-config --libdir` -ljemalloc `jemalloc-config --libs`
TARGET=fitswebql

dev:
	ispc -g -O3 --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	icpc -g -O3 -xCORE-AVX2 -mcmodel large -qopenmp -qopt-streaming-stores auto -funroll-loops -ipo -std=c++17 -fp-model fast -DHAVE_INLINE -DDEVELOPMENT -DLOCAL -qopt-report=5 -qopt-report-phase=vec $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) $(JEMALLOC)

llvm:
	ispc -g -O3 --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	clang++ -march=native -g -O3 -std=c++17 -fopenmp=libiomp5 -funroll-loops -ftree-vectorize -Rpass=loop-vectorize -DHAVE_INLINE -DDEVELOPMENT -DLOCAL $(SRC) fits.o -o $(TARGET) $(LIBS) -L/opt/intel/ipp/lib/intel64 -lipps $(JEMALLOC)

gcc:
	ispc -g -O3 --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	g++ -march=native -g -O3 -std=c++17 -fopenmp -funroll-loops -ftree-vectorize -DHAVE_INLINE -DDEVELOPMENT -DLOCAL $(SRC) fits.o -o $(TARGET) $(LIBS) -L/opt/intel/ipp/lib/intel64 -lipps $(JEMALLOC)

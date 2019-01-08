SRC = src/main.cpp src/fits.cpp src/json.c
TARGET=fitswebql

dev:
	icpc -g -O3 -xCORE-AVX2 -mcmodel large -qopenmp -qopt-streaming-stores auto -funroll-loops -ipo -std=c++17 -fp-model fast -DHAVE_INLINE -DDEVELOPMENT -DLOCAL -qopt-report=5 -qopt-report-phase=vec $(SRC) -o $(TARGET) -lstdc++fs -ipp -lsqlite3 -lcurl -luWS -lssl -lz -lnuma -lpthread -L/usr/lib64 -Wl,-rpath,/usr/lib64 -ljemalloc

llvm:
	clang++ -march=native -g -O3 -std=c++17 -fopenmp=libiomp5 -funroll-loops -ftree-vectorize -Rpass=loop-vectorize -DHAVE_INLINE -DDEVELOPMENT -DLOCAL $(SRC) -o $(TARGET) -lstdc++fs -ipp -lsqlite3 -lcurl -luWS -lssl -lz -lnuma -lpthread -L/usr/lib64 -Wl,-rpath,/usr/lib64 -ljemalloc

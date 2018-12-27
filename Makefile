SRC = src/main.cpp

dev:
	icpc -g -O3 -xCORE-AVX2 -mcmodel large -qopenmp -qopt-streaming-stores auto -funroll-loops -ipo -std=c++17 -fp-model fast -DHAVE_INLINE -DDEVELOPMENT -qopt-report=5 -qopt-report-phase=vec $(SRC) -o fitswebql -ipp -lsqlite3 -lnuma -lpthread -L/usr/lib64 -Wl,-rpath,/usr/lib64 -ljemalloc

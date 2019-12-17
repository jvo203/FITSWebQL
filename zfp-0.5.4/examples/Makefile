include ../Config

BINDIR = ../bin
TARGETS = $(BINDIR)/diffusion\
	  $(BINDIR)/inplace\
	  $(BINDIR)/iterator\
	  $(BINDIR)/pgm\
	  $(BINDIR)/simple\
	  $(BINDIR)/speed
LIBS = -L../lib -lzfp
CLIBS = $(LIBS) -lm
CXXLIBS = $(LIBS)

all: $(TARGETS)

$(BINDIR)/diffusion: diffusion.cpp ../lib/$(LIBZFP)
ifeq ($(WITH_IPP), yes)
	$(CXX) $(CXXFLAGS) -I../array diffusion.cpp $(CXXLIBS) -o $@ $(IPPLIBS)
else
	$(CXX) $(CXXFLAGS) -I../array diffusion.cpp $(CXXLIBS) -o $@
endif

$(BINDIR)/inplace: inplace.c ../lib/$(LIBZFP)
ifeq ($(WITH_IPP), yes)
	$(CC) $(CFLAGS) inplace.c $(CLIBS) -o $@ $(IPPLIBS)
else
	$(CC) $(CFLAGS) inplace.c $(CLIBS) -o $@
endif	

$(BINDIR)/iterator: iterator.cpp ../lib/$(LIBZFP)
ifeq ($(WITH_IPP), yes)
	$(CXX) $(CXXFLAGS) -I../array iterator.cpp $(CXXLIBS) -o $@ $(IPPLIBS)
else
	$(CXX) $(CXXFLAGS) -I../array iterator.cpp $(CXXLIBS) -o $@
endif
	
$(BINDIR)/pgm: pgm.c ../lib/$(LIBZFP)
ifeq ($(WITH_IPP), yes)
	$(CC) $(CFLAGS) pgm.c $(CLIBS) -o $@ $(IPPLIBS)
else
	$(CC) $(CFLAGS) pgm.c $(CLIBS) -o $@
endif
	
$(BINDIR)/simple: simple.c ../lib/$(LIBZFP)
ifeq ($(WITH_IPP), yes)
	$(CC) $(CFLAGS) simple.c $(CLIBS) -o $@ $(IPPLIBS)
else
	$(CC) $(CFLAGS) simple.c $(CLIBS) -o $@
endif
	
$(BINDIR)/speed: speed.c ../lib/$(LIBZFP)
ifeq ($(WITH_IPP), yes)
	$(CC) $(CFLAGS) speed.c $(CLIBS) -o $@ $(IPPLIBS)
else
	$(CC) $(CFLAGS) speed.c $(CLIBS) -o $@
endif
	
clean:
	rm -f $(TARGETS)

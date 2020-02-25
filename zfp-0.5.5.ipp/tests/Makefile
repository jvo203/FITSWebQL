include ../Config

BINDIR = ../bin
TARGETS = $(BINDIR)/testzfp
CXXLIBS = -L../lib -lzfp

all: $(TARGETS)

$(BINDIR)/testzfp: testzfp.cpp ../lib/$(LIBZFP)
ifeq ($(WITH_IPP), yes)
	$(CXX) $(CXXFLAGS) -I../array testzfp.cpp $(CXXLIBS) -o $@ $(IPPLIBS)
else
	$(CXX) $(CXXFLAGS) -I../array testzfp.cpp $(CXXLIBS) -o $@
endif

test: $(BINDIR)/testzfp
	$(BINDIR)/testzfp

clean:
	rm -f $(TARGETS)

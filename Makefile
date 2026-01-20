CXXFLAGS=-Wall -O3 -g
OBJECTS=test.o
BINARIES=test

RGB_INCDIR=matrix/include
RGB_LIBDIR=matrix/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) \
          -lrt -lm -lpthread

all : $(BINARIES)

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)

%: %.o $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test.o : test.cpp # Need this explicit declaration cause of git submodule? it cant find the headers without this for whatever reason
#if not reliant on submodule code can just put it in OBJECTS and BINARIES

%.o : %.cpp
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(BINARIES)
	$(MAKE) -C $(RGB_LIBDIR) clean

FORCE:
.PHONY: FORCE
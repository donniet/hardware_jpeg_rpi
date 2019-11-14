ifeq ($(shell uname -m),x86_64)
   CC = /pitools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-g++
else
   CC = g++
endif
LDFLAGS = -L /opt/vc/lib -Wl,--whole-archive -L/opt/vc/lib/ \
   -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread \
   -lrt -L/opt/vc/src/hello_pi/libs/ilclient -lilclient \
   -Wl,--no-whole-archive -rdynamic \
   -Wl,-rpath-link,/opt/vc/lib
BLDDIR = /build
INCDIR = $(BLDDIR)/inc
SRCDIR = $(BLDDIR)/src
OBJDIR = $(BLDDIR)/bin
CFLAGS = -c -Wall -Wno-deprecated --std=c++11 -Wl,-Bstatic -I$(INCDIR) -g -DRASPBERRY_PI  \
   -DSTANDALONE -D__STDC_CONSTANT_MACROS \
   -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC \
   -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 \
   -U_FORTIFY_SOURCE -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT \
   -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST\
   -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -I /opt/vc/include/IL \
   -I /opt/vc/include -I /opt/vc/include/interface/vcos/pthreads \
   -I/opt/vc/include/interface/vmcs_host/linux/ \
   -I/opt/vc/src/hello_pi/libs/ilclient
SRC = $(wildcard $(SRCDIR)/*.cpp)
OBJ = $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SRC))
EXE = $(OBJDIR)/main

all: clean $(EXE) 
    
$(EXE): $(OBJ) 
	$(CC) $(LDFLAGS) $(OBJDIR)/*.o -o $@ 

$(OBJDIR)/%.o : $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm -f $(OBJDIR)/*.o $(EXE)
CXX=g++
CXXFLAGS=-I/opt/local/include -I/usr/local/include -std=c++14 -g -DHAVE_INTTYPES_H -DHAVE_NETINET_IN_H -Wall -Werror
LDFLAGS=-L/opt/local/lib -L/usr/local/lib -std=c++14 -lphosg
INSTALL_DIR=/opt/local

all: tapserver libtapserver.a

install:
	cp libtapserver.a $(INSTALL_DIR)/lib/
	cp -r *.hh $(INSTALL_DIR)/include/
	cp tapserver $(INSTALL_DIR)/bin/

tapserver: MacOSNetworkTapInterface.o MacOSNetworkTapInterfaceServer.o
	$(CXX) $^ $(LDFLAGS) -o $@

libtapserver.a: MacOSNetworkTapInterface.o
	rm -f $@
	ar rcs $@ $^

clean:
	find . -name \*.o -delete
	rm -rf *.dSYM tapserver libtapserver.a gmon.out

.PHONY: clean

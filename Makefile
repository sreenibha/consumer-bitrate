DESTDIR=/
PREFIX=$(DESTDIR)/usr/local
DEPDIR=.deps
bin_PROGRAMS = bitrate

all: $(bin_PROGRAMS)

bitrate: main.o extract.o
	$(CXX) $(LDFLAGS) $^ $(shell pkg-config libcap_utils-0.7 libcap_filter-0.7 conserver-0.7 --libs) -lqd -o $@

clean:
	rm -rf *.o $(bin_PROGRAMS) $(DEPDIR)

$(DEPDIR):
	mkdir -p $@

%.o: %.cpp Makefile $(DEPDIR)
	$(CXX) -Wall -std=c++0x -DHAVE_CONFIG_H $(CFLAGS) $(shell pkg-config libcap_stream-0.7 conserver-0.7 --cflags) -c $< -MD -MF $(DEPDIR)/$(@:.o=.d) -o $@

install: all
	install -m 0755 bitrate $(PREFIX)/bin

-include $(wildcard $(DEPDIR)/*.d)

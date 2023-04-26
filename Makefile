TARGET=kqueue.$(LIB_EXTENSION)
INSTALL?=install

ifdef KQUEUE_COVERAGE
COVFLAGS=--coverage
endif

.PHONY: all preprocess install

all: preprocess
	@$(MAKE) $(TARGET)

preprocess:
	lua ./configure.lua

%.o: %.c
	$(CC) $(CFLAGS) $(WARNINGS) $(COVFLAGS) $(CPPFLAGS) -o $@ -c $<

$(TARGET): $(patsubst %.c,%.o,$(wildcard impl/*.c))
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) $(PLATFORM_LDFLAGS) $(COVFLAGS)

install:
	$(INSTALL) -d $(INST_LIBDIR)
	$(INSTALL) $(TARGET) $(INST_LIBDIR)
	rm -f $(OBJS) $(TARGET) $(GCDAS)


CXX = g++

CXXFLAGS = -Wall -Wextra -O2 -std=c++11 -I/usr/local/include
LDFLAGS = -L/usr/local/lib
LDLIBS = -lrpitx -lstdc++ -lm

TARGET = sendsubghz

SRCS = sendsubghz.cpp

.PHONY: all
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS) $(LDLIBS)
	@echo "Build complete: $(TARGET)"

.PHONY: clean
clean:
	@echo "Cleaning up..."
	rm -f $(TARGET)

# Default installation prefix
PREFIX ?= /usr/local
DESTDIR ?=

.PHONY: install
install: $(TARGET)
	@echo "Installing $(TARGET) to $(DESTDIR)$(PREFIX)/bin/"
	install -d $(DESTDIR)$(PREFIX)/bin/
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/

.PHONY: uninstall
uninstall:
	@echo "Uninstalling $(TARGET) from $(DESTDIR)$(PREFIX)/bin/"
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

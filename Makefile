CXXFLAGS ?= -std=c++14 -I.
LDFLAGS = -lm

sendsubghz: sendsubghz.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lrpitx

clean :
	rm sendsubghz

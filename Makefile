CXX := x86_64-w64-mingw32-g++
WINDRES := x86_64-w64-mingw32-windres
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode
LDFLAGS := -mwindows -lcomctl32

OBJS := MREviz.o stats.o MREviz.res

all: MREviz.exe

MREviz.exe: $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

MREviz.o: MREviz.cpp stats.h resource.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

stats.o: stats.cpp stats.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

app.ico: tools/make_icon.py
	python $<

MREviz.res: MREviz.rc resource.h app.ico
	$(WINDRES) $< -O coff -o $@

clean:
	rm -f *.o *.res MREviz.exe

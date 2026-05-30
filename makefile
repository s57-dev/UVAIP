CXX = g++
WARNINGS = -Wall -Wextra -Wno-overloaded-virtual
CXXFLAGS = $(WARNINGS) -std=c++17 -Iinclude `pkg-config --cflags opencv4`
LDFLAGS = `pkg-config --libs opencv4`

TARGET = out/camera_app

SRC = src/main.cpp \
      src/camera.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean

CC = cc
CFLAGS = -v -mt
TARGET = out

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean

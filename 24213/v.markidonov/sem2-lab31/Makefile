CC = cc
CFLAGS = 
LDFLAGS = 

SRCS = main.c proxy.c cache.c http.c buffer.c
OBJS = $(SRCS:.c=.o)
TARGET = proxy31

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

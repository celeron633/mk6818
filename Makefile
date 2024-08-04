CC := gcc

TARGET := mk6818
OBJS += mk6818.o

%.o: %.c
	$(CC) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $^ -o $@

all: $(TARGET)

clean:
	rm -f $(OBJS);
	rm -f $(TARGET);

.phony: all clean
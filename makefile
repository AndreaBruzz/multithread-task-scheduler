# Compiler
CC = gcc

# Compiler flags
CFLAGS = -pthread -lm

# Executable names
SERVER = server
CLIENT = client

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Object files
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# Default target
all: $(SERVER) $(CLIENT)

# Compile server
$(SERVER): $(SERVER_OBJ)
	$(CC) $(SERVER_OBJ) -o $(SERVER) $(CFLAGS)

# Compile client
$(CLIENT): $(CLIENT_OBJ)
	$(CC) $(CLIENT_OBJ) -o $(CLIENT) $(CFLAGS)

# Compile .c files into .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up object files and executables
clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJ) $(CLIENT_OBJ)

# Rebuild everything
rebuild: clean all

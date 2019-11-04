# Compiler flags
CFLAGS := -w -g -O3 -m64 -D'main(a,b)=main(int argc, char** argv)'

# Compile symbolic execution engine
phantom: phantom.c sase.c mit.c
	$(CXX) $(CFLAGS) $^ -o $@ -lboolector -lbtor2parser -llgl

# Compile selfie.c into selfie executable
selfie:
	$(CC) $(CFLAGS) compiler/selfie.c -o $@

all: phantom selfie

# Clean up
clean:
	rm -rf phantom
	rm -rf selfie
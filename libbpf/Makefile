APP = dowork
BPF_OBJ = $(APP).bpf.o
SKEL_H = $(APP).skel.h

CFLAGS = -g -Wall -O2
INCLUDES = -I/usr/include/$(shell uname -m)-linux-gnu
LDFLAGS = -lbpf -lelf -lz

.PHONY: all clean

all: $(APP)

# Step 1: Compile BPF C -> BPF object file
$(BPF_OBJ): $(APP).bpf.c $(APP).h
	clang -g -O2 -target bpf -D__TARGET_ARCH_x86 \
		-I/usr/include/x86_64-linux-gnu \
		-c $< -o $@

BPFTOOL = /home/gongwei/eBPF/libbpf-bootstrap/examples/c/.output/bpftool/bootstrap/bpftool

# Step 2: Generate skeleton header from BPF object
$(SKEL_H): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

# Step 3: Compile user-space loader + link libbpf
$(APP): $(APP).c $(SKEL_H)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(APP) $(BPF_OBJ) $(SKEL_H)

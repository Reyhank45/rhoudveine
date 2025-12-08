kernel_source_files := $(shell find src/impl/kernel -name *.c)
kernel_object_files := $(patsubst src/impl/kernel/%.c, build/kernel/%.o, $(kernel_source_files))

x86_64_c_source_files := $(shell find src/impl/x86_64 -name *.c)
x86_64_c_object_files := $(patsubst src/impl/x86_64/%.c, build/x86_64/%.o, $(x86_64_c_source_files))

x86_64_asm_source_files := $(shell find src/impl/x86_64 -name *.asm)
x86_64_asm_object_files := $(patsubst src/impl/x86_64/%.asm, build/x86_64/%.o, $(x86_64_asm_source_files))

# build init module (C)

init_c_source := src/impl/init/main.c
init_obj := build/init/init.o
init_elf := build/init/init.elf
init_bin := targets/x86_64/iso/boot/init
init_iso_path := targets/x86_64/iso/System/Rhoudveine/Booter/init

# Embedded init object (use the compiled init object as an embedded fallback)
embedded_init_obj := $(init_obj)

$(init_obj): $(init_c_source)
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c -I src/intf -I includes -ffreestanding -nostdlib -fno-builtin -fno-stack-protector $(init_c_source) -o $(init_obj)

$(init_elf): $(init_obj)
	mkdir -p $(dir $@) && \
	# Link with entry point set to 'main' so C 'main' becomes ELF entry
	x86_64-elf-ld -Ttext 0x40000000 -e main -o $(init_elf) $(init_obj)

$(init_bin): $(init_elf)
	mkdir -p $(dir $@) && \
	cp -f $(init_elf) $(init_bin) || true
	# Also install the init into the ISO tree at the requested path
	mkdir -p $(dir $(init_iso_path)) && \
	cp -f $(init_bin) $(init_iso_path) || true


# (no special objcopy rule — we use the compiled init object directly)

x86_64_object_files := $(x86_64_c_object_files) $(x86_64_asm_object_files)


$(kernel_object_files): build/kernel/%.o : src/impl/kernel/%.c
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c -I src/intf -I includes -ffreestanding $(patsubst build/kernel/%.o, src/impl/kernel/%.c, $@) -o $@

$(x86_64_c_object_files): build/x86_64/%.o : src/impl/x86_64/%.c
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c -I src/intf -I includes -ffreestanding $(patsubst build/x86_64/%.o, src/impl/x86_64/%.c, $@) -o $@

$(x86_64_asm_object_files): build/x86_64/%.o : src/impl/x86_64/%.asm
	mkdir -p $(dir $@) && \
	nasm -f elf64 $(patsubst build/x86_64/%.o, src/impl/x86_64/%.asm, $@) -o $@

.PHONY: build-x86_64
build-x86_64: $(kernel_object_files) $(x86_64_object_files) $(embedded_init_obj)
	mkdir -p dist/x86_64
	x86_64-elf-ld -n -o dist/x86_64/kernel.bin -T targets/x86_64/linker.ld $(kernel_object_files) $(x86_64_object_files) $(embedded_init_obj)
	cp dist/x86_64/kernel.bin targets/x86_64/iso/boot/kernel.bin
	# ensure init module is built and copied into the ISO tree
	$(MAKE) $(init_bin)
	# also install kernel and init into the System/Rhoudveine/Booter folder
	mkdir -p targets/x86_64/iso/System/Rhoudveine/Booter
	cp -f dist/x86_64/kernel.bin targets/x86_64/iso/System/Rhoudveine/Booter/kernel.bin || true
	cp -f $(init_bin) targets/x86_64/iso/System/Rhoudveine/Booter/init || true
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/kernel.iso targets/x86_64/iso
	# write the init binary into a test raw disk image at LBA 2048 so AHCI raw loader can find it
	if [ ! -f disk.img ]; then dd if=/dev/zero of=disk.img bs=512 count=32768; fi
	# write init ELF into disk image at LBA 0 (seek=0) for testing so AHCI raw loader can find it
	# Note: this will overwrite the MBR area in the test image — acceptable for local tests
	if [ -f $(init_bin) ]; then dd if=$(init_bin) of=disk.img bs=512 seek=0 conv=notrunc; fi

kernel_source_files := $(filter-out src/impl/kernel/fat32.c, $(wildcard src/impl/kernel/*.c))
kernel_object_files := $(patsubst src/impl/kernel/%.c, build/kernel/%.o, $(kernel_source_files))

x86_64_c_source_files := $(shell find src/impl/x86_64 -name *.c)
x86_64_c_object_files := $(patsubst src/impl/x86_64/%.c, build/x86_64/%.o, $(x86_64_c_source_files))

# Find all assembly files, excluding the obsolete 64-bit bootloader
all_asm_sources := $(filter-out src/impl/x86_64/boot.asm, $(shell find src/impl/x86_64 -name *.asm))
x86_64_asm_object_files := $(patsubst src/impl/x86_64/%.asm, build/x86_64/%.o, $(all_asm_sources))

# build init module (C)

# discover all C sources under init/ (including subdirs)
init_c_source_files := $(shell find init -name '*.c')
init_c_object_files := $(patsubst init/%.c, build/init/%.o, $(init_c_source_files))
init_core_object := build/init/init.o

$(init_c_object_files): build/init/%.o : init/%.c
	mkdir -p $(dir $@)
	x86_64-elf-gcc -c -I src/intf -I includes -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -O0 -mno-red-zone $< -o $@

x86_64_object_files := $(x86_64_c_object_files) $(x86_64_asm_object_files)


$(kernel_object_files): build/kernel/%.o : src/impl/kernel/%.c
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c -I src/intf -I includes -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -O0 -mno-red-zone $< -o $@

$(x86_64_c_object_files): build/x86_64/%.o : src/impl/x86_64/%.c
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c -I src/intf -I includes -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -O0 -mno-red-zone $< -o $@

# A single rule to compile all assembly files into 64-bit objects.
# The `bits 32` directive inside the boot files ensures correct code generation.
$(x86_64_asm_object_files): build/x86_64/%.o : src/impl/x86_64/%.asm
	mkdir -p $(dir $@) && \
	nasm -f elf64 $< -o $@
.PHONY: build-x86_64
build-x86_64: $(kernel_object_files) $(x86_64_object_files) $(init_core_object)
	mkdir -p dist/x86_64
	x86_64-elf-ld -o dist/x86_64/rhoudveine -T targets/x86_64/linker.ld $(kernel_object_files) $(x86_64_object_files) $(init_core_object)
	cp dist/x86_64/rhoudveine targets/x86_64/iso/boot/rhoudveine
	mkdir -p targets/x86_64/iso/System/Rhoudveine/Booter
	cp -f dist/x86_64/rhoudveine targets/x86_64/iso/System/Rhoudveine/Booter/rhoudveine || true
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/kernel.iso targets/x86_64/iso

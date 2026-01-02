# Configuration
KCONFIG := Kconfig
CONFIG_MK := config.mk
AUTOCONF_H := src/impl/kernel/include/autoconf.h
DOT_CONFIG := .config

-include $(CONFIG_MK)

.PHONY: menuconfig genconfig tools

tools/menuconfig: tools/menuconfig.c
	gcc tools/menuconfig.c -o tools/menuconfig

tools/genconfig: tools/genconfig.c
	gcc tools/genconfig.c -o tools/genconfig

menuconfig: tools/menuconfig
	./tools/menuconfig

genconfig: $(DOT_CONFIG) tools/genconfig
	./tools/genconfig $(KCONFIG) $(DOT_CONFIG) $(AUTOCONF_H) $(CONFIG_MK)

$(CONFIG_MK): $(DOT_CONFIG) tools/genconfig
	./tools/genconfig $(KCONFIG) $(DOT_CONFIG) $(AUTOCONF_H) $(CONFIG_MK)

$(AUTOCONF_H): $(DOT_CONFIG) tools/genconfig
	./tools/genconfig $(KCONFIG) $(DOT_CONFIG) $(AUTOCONF_H) $(CONFIG_MK)

# Force config generation if missing
$(DOT_CONFIG):
	@echo "No .config found, running menuconfig..."
	$(MAKE) menuconfig

kernel_source_files := $(shell find src/impl/kernel -name '*.c' -not -name 'fat32.c')
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


# Flags
CFLAGS := -I src/intf -I includes -I src/impl/kernel -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -O0 -mno-red-zone -MMD -MP

$(init_c_object_files): build/init/%.o : init/%.c $(AUTOCONF_H)
	@mkdir -p $(dir $@)
	@echo "  CC      $@"
	@x86_64-elf-gcc -c $(CFLAGS) $< -o $@

x86_64_object_files := $(x86_64_c_object_files) $(x86_64_asm_object_files)


$(kernel_object_files): build/kernel/%.o : src/impl/kernel/%.c $(AUTOCONF_H)
	@mkdir -p $(dir $@)
	@echo "  CC      $@"
	@x86_64-elf-gcc -c $(CFLAGS) $< -o $@

$(x86_64_c_object_files): build/x86_64/%.o : src/impl/x86_64/%.c $(AUTOCONF_H)
	@mkdir -p $(dir $@)
	@echo "  CC      $@"
	@x86_64-elf-gcc -c $(CFLAGS) $< -o $@

# Include dependencies
-include $(kernel_object_files:.o=.d)
-include $(x86_64_c_object_files:.o=.d)
-include $(init_c_object_files:.o=.d)

# A single rule to compile all assembly files into 64-bit objects.
# The `bits 32` directive inside the boot files ensures correct code generation.
$(x86_64_asm_object_files): build/x86_64/%.o : src/impl/x86_64/%.asm
	@mkdir -p $(dir $@)
	@echo "  NASM    $@"
	@nasm -f elf64 $< -o $@
.PHONY: build-x86_64
build-x86_64: $(kernel_object_files) $(x86_64_object_files) $(init_core_object)
	@mkdir -p dist/x86_64
	@echo "  LD      dist/x86_64/rhoudveine"
	@x86_64-elf-ld -o dist/x86_64/rhoudveine -T targets/x86_64/linker.ld $(kernel_object_files) $(x86_64_object_files) $(init_core_object)
	@cp dist/x86_64/rhoudveine targets/x86_64/iso/boot/rhoudveine
	@mkdir -p targets/x86_64/iso/System/Rhoudveine/Booter
	@cp -f dist/x86_64/rhoudveine targets/x86_64/iso/System/Rhoudveine/Booter/rhoudveine || true
	@echo "  ISO     dist/x86_64/kernel.iso"
	@grub-mkrescue -o dist/x86_64/kernel.iso targets/x86_64/iso > /dev/null 2>&1 

clean:
	@echo "  CLEAN"
	@rm -rf build dist targets/x86_64/iso/boot/rhoudveine targets/x86_64/iso/System/Rhoudveine/Booter/rhoudveine 


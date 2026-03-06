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

# Find all assembly files, excluding the obsolete bootloader and start code
all_asm_sources := $(filter-out src/impl/x86_64/boot.asm src/impl/x86_64/boot/%.asm src/impl/x86_64/main64.asm, $(shell find src/impl/x86_64 -name *.asm))
x86_64_asm_object_files := $(patsubst src/impl/x86_64/%.asm, build/x86_64/%.o, $(all_asm_sources))

# build init module (C)

# discover all C sources under init/ (including subdirs)
init_c_source_files := $(shell find init -name '*.c')
init_c_object_files := $(patsubst init/%.c, build/init/%.o, $(init_c_source_files))
init_core_object := build/init/init.o


# Flags
CFLAGS := -I src/intf -I includes -I src/impl/kernel -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -O0 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -MMD -MP

$(init_c_object_files): build/init/%.o : init/%.c $(AUTOCONF_H)
	@mkdir -p $(dir $@)
	@echo "  CC      $@"
	@x86_64-elf-gcc -c $(CFLAGS) $< -o $@

x86_64_object_files := $(x86_64_c_object_files) $(x86_64_asm_object_files)

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
	make -C limine

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
build-x86_64: $(kernel_object_files) $(x86_64_object_files) $(init_core_object) limine
	@mkdir -p dist/x86_64
	@echo "  LD      dist/x86_64/rhoudveine"
	@x86_64-elf-ld -o dist/x86_64/rhoudveine -T targets/x86_64/linker.ld $(kernel_object_files) $(x86_64_object_files) $(init_core_object)
	@rm -rf build/iso_root
	@mkdir -p build/iso_root/boot
	@mkdir -p build/iso_root/boot/limine
	@mkdir -p build/iso_root/System/Rhoudveine/Booter
	@cp dist/x86_64/rhoudveine build/iso_root/boot/rhoudveine
	@cp targets/x86_64/limine.conf build/iso_root/boot/limine/limine.conf
	@cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin build/iso_root/boot/limine/
	@cp init/init.elf build/iso_root/System/Rhoudveine/Booter/init || cp build/init/init.o build/iso_root/System/Rhoudveine/Booter/init || true
	@echo "  ISO     dist/x86_64/kernel.iso"
	@xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build/iso_root -o dist/x86_64/kernel.iso > /dev/null 2>&1
	@./limine/limine bios-install dist/x86_64/kernel.iso

clean:
	@echo "  CLEAN"
	@rm -rf build dist targets/x86_64/iso/boot/rhoudveine targets/x86_64/iso/System/Rhoudveine/Booter/rhoudveine
	@# rm -rf limine # Don't clean limine folder to avoid re-downloading


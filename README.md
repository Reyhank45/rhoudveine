# Rhoudveine Kernel

Rhoudveine is a custom, 64-bit operating system kernel written in C and Assembly for the x86_64 architecture. It is an experimental project designed to explore OS concepts from scratch, unincumbered by decades of legacy compatibility layers.

## Philosophy

**"POSIX is for people who like constraints."**

Rhoudveine is explicitly **Non-POSIX**. While we respect the history of Unix, we believe that strict adherence to POSIX standards often holds back innovation and forces modern hardware to behave like 1970s teletypes.

*   **Custom Interfaces**: We define our own syscalls and interfaces (`userlib`) that make sense for this specific kernel.
*   **Safety & Simplicity**: We prioritize clean, readable code over supporting every obscure standard.
*   **Modern First**: We focus on modern hardware features (ACPI, PCI-E, UEFI-aware) rather than maintaining support for 386-era hardware.

## Features

*   **Virtual File System (VFS)**: Robust abstraction with support for:
    *   **FAT32**: Read support (Write in progress).
    *   **RamFS**: Voltage-volatile in-memory filesystem.
    *   **DevFS & ProcFS**: Kernel interfaces exposed as files.
*   **Hardware Abstraction**:
    *   **VRAY**: Advanced PCI system using binary search on a 20,000+ device database.
    *   **ACPI**: Modern power management (Shutdown, Reboot, SMP detection).
*   **Drivers**:
    *   **AHCI**: SATA disk support.
    *   **xHCI**: USB 3.0 Host Controller support.
    *   **PS/2**: Legacy keyboard/mouse fallback.
    *   **Graphics**: High-resolution framebuffer console with double buffering capabilities.
*   **User Space**: Custom `libc` and `init` process loading from the filesystem.

## Build Requirements

To build Rhoudveine, you need a cross-compilation toolchain for `x86_64-elf`.

**Dependencies:**
*   `x86_64-elf-gcc` (Targeting x86_64-elf)
*   `nasm` (Netwide Assembler)
*   `make`
*   `grub-common` & `grub-pc-bin` (For ISO generation)
*   `xorriso`
*   `mtools`

**Docker (Recommended):**
You can use the `randomdude/gcc-cross-x86_64-elf` image which contains the compiler:
```bash
docker run -it -v $(pwd):/root/env randomdude/gcc-cross-x86_64-elf
# Inside container:
apt-get update && apt-get install -y grub-common grub-pc-bin xorriso mtools nasm
```

## Building

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/Reyhank45/rhoudveine.git
    cd rhoudveine
    ```

2.  **Compile the kernel and create ISO:**
    ```bash
    make clean
    make build-x86_64
    ```
    The kernel image will be located at `dist/x86_64/rhoudveine`.
    Also there will be a conviniently crafted iso image for quick testing that will be located at `dist/x86_64/kernel.iso`.

## Running

It is **highly recommended** to run Rhoudveine using UEFI firmware (e.g., OVMF) rather than legacy BIOS, as the kernel is optimized for modern hardware standards.

```bash
# Example with OVMF (UEFI)
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom dist/x86_64/kernel.iso -serial stdio -m 2G -smp 4
```

*   `-serial stdio`: Redirects kernel logging to your terminal.
*   `-m 2G`: Allocates 2GB of RAM.
*   `-smp 4`: Simulates a 4-core processor (tested with ACPI SMP).

## Architecture Stack

Rhoudveine utilizes a unique, layered driver stack designed for modularity and dependency management:

1.  **VRAY (Top Layer)**: The Hardware Abstract Layer / PCI Subsystem. This is the foundation that enumerates and initializes all PCI/PCI-E devices.
2.  **VNODE**: Layered below VRAY. VNODE manages major hardware controllers found on the bus, such as USB Controllers (xHCI), SATA Controllers (AHCI), and other high-bandwidth bridges.
3.  **NVNODE**: The specialized peripheral layer. This handles specific end-devices connected to the system or controllers, such as PS/2 keyboards/mice, I2C devices, and individual USB devices.

This hierarchy ensures a logical flow: Bus (VRAY) -> Controllers (VNODE) -> Peripherals (NVNODE).

## Roadmap

*   [ ] **Filesystem**: Implement Write support for FAT32.
*   [ ] **Networking**: Implement a basic TCP/IP stack and drivers for Realtek/Intel NICs.
*   [ ] **Shell**: Expand the `init` process into a capable interactive shell.
*   [ ] **Audio**: Intel HDA / AC'97 driver support.
*   [ ] **EFI**: Native UEFI bootloader support (moving away from GRUB/Multiboot2 legacy).
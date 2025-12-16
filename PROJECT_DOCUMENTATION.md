# Operating Systems Course Project: FAT12 Bootloader Implementation

## Executive Summary

This project demonstrates a practical implementation of a **bootloader and kernel loader** that showcases fundamental OS-level concepts including:

- **Real-mode x86 assembly programming** for bootloader development
- **FAT12 filesystem** interaction and file reading
- **Disk I/O operations** using BIOS interrupts
- **LBA to CHS address conversion** for disk sector access
- **Boot parameter block (BPB)** and extended boot parameter block (EBPB) structures
- **Creating bootable floppy disk images** formatted with FAT12

This is an educational implementation designed to explain the actual boot process and how modern operating systems load kernels from disk storage.

---

## Project Overview

### Goal
Understand and implement the low-level mechanisms that allow a computer to:
1. Execute bootloader code from a floppy disk
2. Read the bootloader configuration (BPB/EBPB)
3. Load a kernel file from a FAT12 filesystem
4. Hand over control to the kernel

### What Makes This Project Important
- **Demystifies the boot process**: Shows how firmware hands control to your code
- **Implements real filesystem interaction**: Not just reading raw sectors, but navigating a real FAT12 filesystem
- **Bridges hardware and software**: Demonstrates how low-level assembly interfaces with higher-level C code
- **Practical OS knowledge**: Essential foundation for any OS development

---

## Architecture Overview

```
┌─────────────────────────────────────────┐
│      Floppy Disk Image (main_floppy.img)│
├─────────────────────────────────────────┤
│  Boot Sector (512 bytes)                │
│  ├─ Bootloader Code (boot.asm)          │
│  ├─ BPB (Boot Parameter Block)          │
│  └─ EBPB (Extended BPB)                 │
├─────────────────────────────────────────┤
│  File Allocation Table (FAT12)          │
│  ├─ Sector 1-9 (from BPB)               │
│  └─ Links file clusters together        │
├─────────────────────────────────────────┤
│  Root Directory                         │
│  ├─ Directory Entries (32 bytes each)   │
│  └─ Contains kernel.bin, test.txt       │
├─────────────────────────────────────────┤
│  Data Clusters                          │
│  ├─ kernel.bin (executable)             │
│  └─ test.txt (sample file)              │
└─────────────────────────────────────────┘
```

---

## Component Details

### 1. Bootloader (`src/bootloader/boot.asm`)

The bootloader is a 512-byte program that executes when the computer boots from the floppy disk.

#### Key Responsibilities:

**a) Boot Parameter Block (BPB)**
```assembly
bpb_oem_id:              db "MSWIN4.1"       ; 8 Bytes
bpb_bytes_per_sector:    dw 0x0200           ; 512 bytes per sector
bpb_sectors_per_cluster: db 0x1              ; 1 sector per cluster
bpb_reserved_sectors:    dw 0x1              ; 1 reserved sector (the boot sector itself)
bpb_num_of_FATs:         db 0x2              ; 2 FAT copies (main + backup)
bpb_num_of_root_entries: dw 0xe0             ; 224 root directory entries
bpb_total_sectors:       dw 2880             ; 1440 KiB floppy = 2880 sectors
bpb_media_descriptor:    db 0xF0             ; 3.5" floppy disk
bpb_sectors_per_fat:     dw 9                ; Each FAT is 9 sectors
bpb_sectors_per_track:   dw 18               ; CHS geometry
bpb_heads:               dw 2                ; CHS geometry
```

The BPB describes the disk layout so the bootloader knows where to find files.

**b) Disk I/O Operations**

The bootloader implements two critical disk operations:

1. **LBA to CHS Conversion** (`lba_to_chs`)
   - **Why needed**: BIOS interrupt 0x13 (disk read) uses CHS (Cylinder, Head, Sector) addressing
   - **Challenge**: Modern systems use LBA (Logical Block Addressing) which is simpler
   - **Solution**: Convert LBA to CHS using the geometry from BPB

   ```
   Formulas:
   Sector    = (LBA mod SectorsPerTrack) + 1
   Cylinder  = (LBA / SectorsPerTrack) / Heads
   Head      = (LBA / SectorsPerTrack) mod Heads
   ```

   **CX Register Layout (for BIOS INT 0x13)**
   ```
   CX = [Cylinder (10 bits)] [Sector (6 bits)]
   ```

2. **Disk Read** (`disk_read`)
   - Calls `lba_to_chs` to convert sector number to CHS
   - Uses BIOS interrupt 0x13 (AH=0x02) to read sectors
   - Implements **retry logic**: Up to 3 attempts with disk reset on failure
   - Reads data into specified memory buffer

**c) String Output** (`puts`)
   - Prints null-terminated strings using BIOS TTY mode (INT 0x10)
   - Essential for debugging and user feedback

**d) Boot Sequence**
```
1. Set up CPU segments and stack
2. Store drive number from BIOS
3. Read kernel from disk using LBA=34 (after bootloader and FAT)
4. Display welcome message
5. Display loaded kernel content
6. Halt CPU
```

#### Technical Details:
- **Mode**: Real mode (16-bit)
- **Origin**: 0x7C00 (standard BIOS bootloader location)
- **Size**: 510 bytes maximum (512 - 2 byte signature)
- **Bootable Signature**: 0xAA55 at end of sector

---

### 2. FAT12 Driver (`tools/fat/fat.c`)

A C implementation of a FAT12 filesystem driver that reads files from the disk image.

#### Why We Need This:
The bootloader can read raw sectors, but to load files with meaningful names, we need to:
1. Parse the FAT12 filesystem structure
2. Navigate the directory tree
3. Follow cluster chains in the FAT
4. Return file contents to the user

#### Key Data Structures:

**Boot Sector Structure**
```c
typedef struct {
    uint8_t boot_jump_instr[3];      // JMP instruction
    uint8_t oem_id[8];               // Disk identification
    uint16_t bytes_per_sector;       // 512 bytes
    uint8_t sectors_per_cluster;     // 1 sector
    uint16_t reserved_sectors;       // Boot sector count
    uint8_t num_of_FATs;             // FAT copies
    uint16_t num_of_root_entries;    // Max files in root
    uint16_t total_sectors;          // Total disk sectors
    uint8_t media_descriptor_type;   // Disk type
    uint16_t sectors_per_fat;        // FAT size
    uint16_t sectors_per_track;      // CHS geometry
    uint16_t head;                   // CHS geometry
    uint32_t hidden_sectors;         // Hidden sectors
    uint32_t large_sector_count;     // Large sector count
    // Extended Boot Record
    uint8_t drive_number;
    uint8_t _reserved;
    uint8_t signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t system_id[8];
} __attribute__((packed)) BootSector;
```

**Directory Entry Structure**
```c
typedef struct {
    uint8_t name[11];                // 8.3 filename format
    uint8_t attributes;              // File/directory flags
    uint8_t _reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_high;     // Start cluster (high word)
    uint16_t modified_date;
    uint16_t modified_time;
    uint16_t first_cluster_low;      // Start cluster (low word)
    uint32_t size;                   // File size in bytes
} __attribute__((packed)) DirectoryEntry;
```

#### Core Functions:

1. **`readBootSector(FILE* disk)`**
   - Reads and parses the boot sector into the BootSector structure

2. **`readSectors(FILE* disk, uint32_t lba, uint32_t count, void* bufferOut)`**
   - Generic disk read function
   - Seeks to LBA position and reads sectors

3. **`readFat(FILE* disk)`**
   - Loads the entire FAT into memory
   - FAT is a linked list: each entry points to the next cluster containing the file

4. **`readRootDirectory(FILE* disk)`**
   - Loads root directory entries into memory
   - Calculates root directory location from BPB

5. **`findFile(const char* name)`**
   - Searches root directory for file by name
   - Linear search through directory entries

6. **`readFile(DirectoryEntry* fileEntry, FILE* disk, uint8_t* outputBuffer)`**
   - **Critical function**: Reads a file by following cluster chain in FAT
   - **FAT12 Special Handling**: 12-bit entries (1.5 bytes each) packed in FAT array
     - Even cluster index: use lower 12 bits
     - Odd cluster index: use upper 12 bits
   - Continues reading until reaching EOF marker (0xFF8 or higher)

#### FAT12 Cluster Chain Example:
```
Cluster 2: contains data, FAT[2] points to cluster 3
Cluster 3: contains data, FAT[3] points to cluster 4
Cluster 4: contains data, FAT[4] = 0xFFF (EOF)
```

---

### 3. Build System (`Makefile`)

Orchestrates compilation of all components:

```makefile
all: floppy_image tools_fat

# Bootloader: boot.asm → bootloader.bin
bootloader: $(BUILD_DIR)/bootloader.bin
$(BUILD_DIR)/bootloader.bin:
    nasm $(SRC_DIR)/bootloader/boot.asm -f bin -o $(BUILD_DIR)/bootloader.bin

# Kernel: kernel.asm → kernel.bin
kernel: $(BUILD_DIR)/kernel.bin
$(BUILD_DIR)/kernel.bin:
    nasm $(SRC_DIR)/kernel/main.asm -f bin -o $(BUILD_DIR)/kernel.bin

# FAT12 Driver: fat.c → fat executable
tools_fat: $(BUILD_DIR)/tools/fat
$(BUILD_DIR)/tools/fat:
    gcc -g -o $(BUILD_DIR)/tools/fat $(TOOLS_DIR)/fat/fat.c

# Floppy Image: Create FAT12 disk and populate
floppy_image: $(BUILD_DIR)/main_floppy.img
$(BUILD_DIR)/main_floppy.img: bootloader kernel
    # Create 1440 KiB floppy image
    dd if=/dev/zero of=$(BUILD_DIR)/main_floppy.img bs=512 count=2880
    # Format with FAT12
    mkfs.fat -F 12 -n "NBOS" $(BUILD_DIR)/main_floppy.img
    # Write bootloader to first sector
    dd if=$(BUILD_DIR)/bootloader.bin of=$(BUILD_DIR)/main_floppy.img conv=notrunc
    # Copy kernel and test files to FAT12 filesystem
    mcopy -i $(BUILD_DIR)/main_floppy.img $(BUILD_DIR)/kernel.bin "::kernel.bin"
    mcopy -i $(BUILD_DIR)/main_floppy.img test.txt "::test.txt"
```

---

## Boot Process Flowchart

```
┌──────────────────────────┐
│  Computer Powers On      │
└────────────┬─────────────┘
             │
             ↓
┌──────────────────────────────────────────────┐
│  BIOS Loads MBR/Boot Sector from LBA 0       │
│  Boot sector is placed at 0x0000:0x7C00      │
└────────────┬─────────────────────────────────┘
             │
             ↓
┌──────────────────────────────────────────────┐
│  boot.asm Executes:                          │
│  1. Set up CPU segments (DS, ES, SS)         │
│  2. Initialize stack at 0x7C00               │
└────────────┬─────────────────────────────────┘
             │
             ↓
┌──────────────────────────────────────────────┐
│  Read BPB from bootloader to understand      │
│  disk layout (sector size, FAT location, etc)│
└────────────┬─────────────────────────────────┘
             │
             ↓
┌──────────────────────────────────────────────┐
│  Convert LBA → CHS (lba_to_chs function)     │
│  Use geometry from BPB                       │
└────────────┬─────────────────────────────────┘
             │
             ↓
┌──────────────────────────────────────────────┐
│  Read Kernel from Disk (disk_read function)  │
│  - Call INT 0x13 with CHS coordinates       │
│  - Retry up to 3 times on failure            │
│  - Load kernel.bin into memory               │
└────────────┬─────────────────────────────────┘
             │
             ↓
┌──────────────────────────────────────────────┐
│  Display Boot Messages                       │
│  Print kernel content to verify load         │
└────────────┬─────────────────────────────────┘
             │
             ↓
┌──────────────────────────────────────────────┐
│  Halt and Wait                               │
│  (Full kernel would take over here)          │
└──────────────────────────────────────────────┘
```

---

## Building and Running the Project

### Prerequisites
```bash
# Install required tools
sudo apt-get install nasm          # x86 assembler
sudo apt-get install gcc           # C compiler
sudo apt-get install mtools        # FAT filesystem tools
sudo apt-get install qemu-system   # Emulator (optional)
sudo apt-get install bochs         # Alternative emulator (optional)
```

### Build
```bash
make clean
make all
```

This generates:
- `build/bootloader.bin` - The bootloader (512 bytes)
- `build/kernel.bin` - The kernel code
- `build/main_floppy.img` - Complete FAT12 floppy image
- `build/tools/fat` - FAT12 driver utility

### Test the FAT12 Driver
```bash
./build/tools/fat build/main_floppy.img "KERNEL BIN"
./build/tools/fat build/main_floppy.img "TEST    TXT"
```

### Run in Emulator
```bash
# Using QEMU
qemu-system-i386 -fda build/main_floppy.img

# Using Bochs (with bochs_config file)
bochs -f bochs_config
```

---

## Key Technical Insights

### 1. Real Mode Memory Layout
```
0x00000 - 0x00FFF: IVT (Interrupt Vector Table)
0x01000 - 0x06BFF: Conventional memory (available)
0x07C00 - 0x07DFF: Boot sector (bootloader loads here)
0x07E00 - 0x7FFFF: Free memory (kernel loads here)
```

### 2. LBA to CHS Conversion Complexity
The conversion involves:
- **Division operations** (16-bit DX:AX ÷ 16-bit divisor)
- **Bit shifting** to pack cylinder bits into CX register
- **Off-by-one error prevention** (sectors are 1-indexed, not 0-indexed)

This is error-prone in assembly, making it a learning opportunity.

### 3. FAT12 Cluster Chain Following
FAT12 uses 12-bit entries, which means:
- Each FAT entry occupies 1.5 bytes
- Entries are packed: 3 bytes hold 2 cluster numbers
- Reading requires careful bit masking:
  ```
  if (cluster_num is even)
      next_cluster = FAT[index] & 0x0FFF
  else
      next_cluster = FAT[index] >> 4
  ```

### 4. Disk I/O Reliability
The bootloader implements:
- **Retry logic**: Up to 3 attempts per read
- **Disk reset**: Reset controller on failure
- **Error handling**: Clear error messages

This demonstrates why real OSes don't just do single reads—disks are inherently unreliable.

### 5. Firmware Interaction
BIOS interrupts used:
- **INT 0x10**: Video output (printing text)
- **INT 0x13**: Disk I/O (reading sectors)
- **INT 0x16**: Keyboard input (waiting for key)

These form the bridge between bare metal and high-level operations.

---

## Learning Outcomes

This project teaches:

1. **Boot Process Fundamentals**
   - How computers bootstrap from firmware
   - Why boot sectors must be small and efficient
   - The role of BIOS in early boot

2. **Filesystem Implementation**
   - How filesystems organize data on disk
   - Cluster allocation and FAT tables
   - Directory structures and file discovery

3. **Assembly Language Proficiency**
   - Real-mode x86 assembly
   - Interrupt handling and BIOS calls
   - Register manipulation and memory addressing

4. **Systems-Level Problem Solving**
   - Dealing with hardware limitations
   - Implementing robust error handling
   - Optimizing for space constraints (512 bytes!)

5. **Low-Level Debugging**
   - Working without high-level language abstractions
   - Testing disk I/O operations
   - Validating filesystem structures

---

## Future Enhancements

Possible extensions for deeper learning:

1. **Protected Mode Boot**: Transition from 16-bit to 32-bit mode
2. **Kernel Shell**: Implement a simple command-line interface
3. **File Write Support**: Extend FAT12 driver to create/modify files
4. **Error Recovery**: Implement more sophisticated disk error handling
5. **Multiboot Standard**: Support loading via GRUB bootloader
6. **Memory Management**: Implement basic paging/segmentation
7. **Interrupt Handlers**: Create custom interrupt handlers for keyboard/timer

---

## Conclusion

This project successfully demonstrates the fundamental mechanisms of OS bootstrapping. While simplified compared to production operating systems, it implements real, working code that:

- ✅ Boots from a real disk format (FAT12)
- ✅ Navigates a real filesystem structure
- ✅ Loads executable code from disk
- ✅ Handles low-level hardware interaction

By studying and extending this project, you gain practical understanding of how operating systems actually work at the metal level—knowledge that forms the foundation for all systems programming.

---

## References

- Intel x86 Architecture Reference
- FAT12 Filesystem Specification (Microsoft)
- BIOS Interrupt Specification
- OSDev.org Community Resources
- "The Little Book About OS Development" by Erik Svensson

---

**Project Status**: ✅ Complete bootloader and FAT12 driver implementation for educational purposes.

**Course**: Operating Systems

**Last Updated**: December 2025

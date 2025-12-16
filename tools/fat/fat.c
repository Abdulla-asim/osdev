#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct
{
    uint8_t boot_jump_instr[3];
    uint8_t oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;				
    uint8_t num_of_FATs;			
    uint16_t num_of_root_entries;
    uint16_t total_sectors;		    
    uint8_t media_descriptor_type;	
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head;
    uint32_t hidden_sectors;
    uint32_t large_sector_count;

    // Extended Boot Record
    uint8_t drive_number;
    uint8_t _reserved;
    uint8_t signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t system_id[8];

} __attribute__((packed)) BootSector;

typedef struct {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t _reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t modified_date;
    uint16_t modified_time;
    uint16_t first_cluster_low;
    uint32_t size;

} __attribute__((packed)) DirectoryEntry;

//
// Global Variables
//
BootSector g_BootSector;                    // Boot Parameter Block
uint8_t* g_Fat = NULL;                      // File Allocation Table
DirectoryEntry* g_RootDirectory = NULL;     // Root Directory
uint32_t g_RootDirectoryEnd;                // Saves Sector Number of where Root directory ends 

// Read the boot sector into the global bootSector structure
bool readBootSector(FILE* disk)
{
    return fread(&g_BootSector, sizeof(g_BootSector), 1, disk) > 0;
}

// Read disk sectors (lba = sector number)g_DirectoryEntry
bool readSectors(FILE* disk, uint32_t lba, uint32_t count, void* bufferOut)
{
    bool ok = true;     // Flag for successful seek/read
    ok = ok && (fseek(disk, lba*g_BootSector.bytes_per_sector, SEEK_SET) == 0);     // sets position to "StartOfFile(SEEK_SET) + Offset"
    ok = ok && (fread(bufferOut, g_BootSector.bytes_per_sector, count, disk) == count);         // Read 'count' units into bufferOut

    return ok;
}

// Read FAT(file allocation table) into Memory
bool readFat(FILE* disk)
{
    // Allocate enough memory for FAT
    g_Fat = (uint8_t*) malloc(g_BootSector.sectors_per_fat * g_BootSector.bytes_per_sector);

    // Read the fat and return true or false 
    return readSectors(disk, g_BootSector.reserved_sectors, g_BootSector.sectors_per_fat, g_Fat);
}

// Read Root directory
bool readRootDirectory(FILE* disk) 
{
    bool ok = true;

    // Get location of root directory (sector number == lba)
    uint32_t lba = g_BootSector.reserved_sectors + g_BootSector.sectors_per_fat * g_BootSector.num_of_FATs;
    uint32_t size = sizeof(DirectoryEntry) * g_BootSector.num_of_root_entries;
    uint32_t sectors = size / g_BootSector.bytes_per_sector;

    if (size % g_BootSector.bytes_per_sector > 0)
        sectors++;

    g_RootDirectoryEnd = lba + sectors;         // Save so we dont have tp compute again.

    g_RootDirectory = (DirectoryEntry*)malloc(sectors * g_BootSector.bytes_per_sector);

    return readSectors(disk, lba, sectors, g_RootDirectory);
}

// Find files from root directory
DirectoryEntry* findFile(const char* name)
{
    for (int i = 0; i < g_BootSector.num_of_root_entries; i++)
        if (memcmp(name, g_RootDirectory[i].name, 11) == 0)
            return &g_RootDirectory[i];

    return NULL;
}

// Read File 
bool readFile(DirectoryEntry* fileEntry, FILE* disk, uint8_t* outputBuffer)
{
    bool ok = true;
    uint16_t currentCluster = fileEntry->first_cluster_low;

    do {
        uint32_t lba = g_RootDirectoryEnd + (currentCluster - 2) * g_BootSector.sectors_per_cluster;
        ok = ok && readSectors(disk, lba, g_BootSector.sectors_per_cluster, outputBuffer);
        outputBuffer += g_BootSector.sectors_per_cluster * g_BootSector.bytes_per_sector;

        uint32_t fatIndex = currentCluster * 3 / 2;
        if (currentCluster % 2 == 0)
            currentCluster = *((uint16_t*)(g_Fat + fatIndex)) & 0x0FFF;     // Get the lower 12 bits in case of even cluster num
        else 
            currentCluster = *((uint16_t*)(g_Fat + fatIndex)) >> 4;         // Get the hiser 12 bits in case of odd cluster num

    } while (ok && currentCluster < 0xFF8);

    return ok;
}

// MAIN
int main(int argc, char** argv) 
{
    // Usage Error
    if (argc < 3) {
        printf("Usage: %s <disk_image> <file_name>\n", argv[0]);
        return -1;
    }

    // Open the Disk (FAT12)
    FILE* disk = fopen(argv[1], "rb");
    if (!disk) {
        fprintf(stderr, "Could not open disk image %s", argv[1]);
        return -2;
    }

    // Read the Boot Sector (512 bytes) into g_BootSector
    if (!readBootSector(disk)) {
        fprintf(stderr, "Could not read boot sector\n");
        return -3;
    }

    // Read the file allocation table into memory(heap)
    if (!readFat(disk)) {
        fprintf(stderr, "Could not read file allocation table\n");
        free(g_Fat);
        return -4;
    }

    // Read the root directory into g_RootSectors (array of directory entries)
    if (!readRootDirectory(disk)) {
        fprintf(stderr, "Could not read Root Directory!\n");
        free(g_Fat);
        free(g_RootDirectory);
        return -5;
    }

    // Get the file specified at the cmd argument index 2
    DirectoryEntry* fileEntry = findFile(argv[2]);
    if (!fileEntry) {
        fprintf(stderr, "File %s not found!\n", argv[2]);
        free(g_Fat);
        free(g_RootDirectory);
        return -6;
    }

    // Read the file
    uint8_t* outBuffer = (uint8_t *) malloc(fileEntry->size + g_BootSector.bytes_per_sector);    // allocating extra sector for safety
    if (!readFile(fileEntry, disk, outBuffer)) {
        fprintf(stderr, "Could not read file!\n");
        free(g_Fat);
        free(g_RootDirectory);
        return -7;
        free(outBuffer);
    }

    // Loop over each char in buffer and print it if printable, otherwise print its hex
    for (size_t i=0; i < fileEntry->size; i++) {
        if (isprint(outBuffer[i])) putc(outBuffer[i], stdout);
        else printf("<%02x>", outBuffer[i]);
    }

    printf("\n");

    free(g_Fat);
    free(g_RootDirectory);
    free(outBuffer);
    return 0;
}
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

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
uint8_t* g_fat = NULL;                      // File Allocation Table
DirectoryEntry* g_DirectoryEntry = NULL;    // Root Directory


// Read the boot sector into the global bootSector structure
bool readBootSector(FILE* disk)
{
    return fread(&g_BootSector, sizeof(g_BootSector), 1, disk) > 0;
}

// Read disk sectors (lba = sector number)
bool readSectors(FILE* disk, uint32_t lba, uint32_t count, void* bufferOut)
{
    bool ok = true;     // Flag for successful seek/read
    ok = ok && (fseek(disk, lba*g_BootSector.bytes_per_sector, SEEK_SET) == 0);     // sets position to "StartOfFile(SEEK_SET) + Offset"
    ok = ok && (fread(bufferOut, sizeof(bufferOut), count, disk) == count);         // Read 'count' units into bufferOut

    return ok;
}

// Read FAT(file allocation table) into Memory
bool readFat(FILE* disk)
{
    // Allocate enough memory for FAT
    g_fat = (uint8_t*) malloc(g_BootSector.sectors_per_fat * g_BootSector.bytes_per_sector);

    // Read the fat and return true or false 
    return readSectors(disk, g_BootSector.reserved_sectors, g_BootSector.sectors_per_fat, g_fat);
}


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
        free(g_fat);
        return -4;
    }


    free(g_fat);
    return 0;
}
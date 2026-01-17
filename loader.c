#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "loader.h"
#include "tables.h"

extern struct Memory M[MEMORY_SIZE];
static int memory_count = 0;

typedef struct {
    char opcode[3];
    int bytes;
} OpcodeSize;

static OpcodeSize opcodeSizes[] = {
    {"A1", 3}, {"A2", 2}, {"A3", 3}, {"A4", 2},
    {"E1", 3}, {"E2", 2}, {"F1", 3},
    {"C1", 3}, {"B4", 3}, {"B1", 3}, {"B2", 3}, {"B3", 3},
    {"D2", 1}, {"D1", 1}, {"C2", 1}, {"FE", 1}
};
static int opcodeSizesCount = sizeof(opcodeSizes) / sizeof(OpcodeSize);

int getInstructionSizeFromOpcode(const char *opcode_hex) {
    for (int i = 0; i < opcodeSizesCount; i++) {
        if (strcmp(opcodeSizes[i].opcode, opcode_hex) == 0) {
            return opcodeSizes[i].bytes;
        }
    }
    return 1; //Data byte
}

static int isKnownOpcode(const char *opcode_hex) {
    for (int i = 0; i < opcodeSizesCount; i++) {
        if (strcmp(opcodeSizes[i].opcode, opcode_hex) == 0) {
            return 1;
        }
    }
    return 0;
}

//1033 -> "10" "33"
void intToTwoCharString(int value, char *high, char *low) {
    sprintf(high, "%02d", value / 100);
    sprintf(low, "%02d", value % 100);
}

static void addToMemory(int address, const char *byte_str) {
    for (int i = 0; i < memory_count; i++) {
        if (M[i].address == address) {
            strncpy(M[i].symbol, byte_str, sizeof(M[i].symbol) - 1);
            M[i].symbol[sizeof(M[i].symbol) - 1] = '\0';
            return;
        }
    }
    
    if (memory_count < MEMORY_SIZE) {
        M[memory_count].address = address;
        strncpy(M[memory_count].symbol, byte_str, sizeof(M[memory_count].symbol) - 1);
        M[memory_count].symbol[sizeof(M[memory_count].symbol) - 1] = '\0';
        memory_count++;
    }
}

static const char* getByteAtAddress(int addr) {
    for (int i = 0; i < memory_count; i++) {
        if (M[i].address == addr) {
            return M[i].symbol;
        }
    }
    return NULL;
}

static void loadExeFile(const char *exe_file, int loadpoint) {
    FILE *fp = fopen(exe_file, "r");
    if (!fp) return;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) < 1) continue;
        
        int lc;
        char tok1[16], tok2[16], tok3[16];
        int n = sscanf(line, "%d %s %s %s", &lc, tok1, tok2, tok3);
        if (n < 2) continue;
        
        int mem_addr = loadpoint + lc;
        
        unsigned int first_val = 0;
        sscanf(tok1, "%x", &first_val);
        char first_str[4];
        snprintf(first_str, sizeof(first_str), "%02X", first_val & 0xFF);
        
        if (isKnownOpcode(first_str)) {
            addToMemory(mem_addr, first_str);
            
            int inst_size = getInstructionSizeFromOpcode(first_str);
            
            if (inst_size == 2 && n >= 3) {
                unsigned int val = 0;
                sscanf(tok2, "%x", &val);
                char str[4];
                snprintf(str, sizeof(str), "%02d", val & 0xFF);
                addToMemory(mem_addr + 1, str);
            }
            else if (inst_size == 3 && n >= 4) {
                unsigned int val1 = 0, val2 = 0;
                sscanf(tok2, "%x", &val1);
                sscanf(tok3, "%x", &val2);
                
                int full_addr = (val1 << 8) | val2;
                char high[4], low[4];
                intToTwoCharString(full_addr, high, low);
                
                addToMemory(mem_addr + 1, high);
                addToMemory(mem_addr + 2, low);
            }
        }
        else {
            char str[8];
            snprintf(str, sizeof(str), "%d", first_val & 0xFF);
            addToMemory(mem_addr, str);
        }
    }
    fclose(fp);
}

//DAT'taki her adres için 16-bit değere loadpoint ekle.
void applyRelocation(int loadpoint, int *dat_addresses, int dat_count) {
    for (int i = 0; i < dat_count; i++) {
        int lc = dat_addresses[i];
        int mem_addr = loadpoint + lc;
        
        const char *high_str = getByteAtAddress(mem_addr);
        const char *low_str = getByteAtAddress(mem_addr + 1);
        
        if (high_str && low_str) {
            int high_val = atoi(high_str);
            int low_val = atoi(low_str);
            
            int original_addr = high_val * 100 + low_val;
            
            int new_addr = loadpoint + original_addr;
            
            char new_high[4], new_low[4];
            intToTwoCharString(new_addr, new_high, new_low);
            
            addToMemory(mem_addr, new_high);
            addToMemory(mem_addr + 1, new_low);
        }
    }
}

void printMemoryArray(int start_addr, int end_addr) {
    for (int i = 0; i < memory_count - 1; i++) {
        for (int j = 0; j < memory_count - i - 1; j++) {
            if (M[j].address > M[j + 1].address) {
                struct Memory temp = M[j];
                M[j] = M[j + 1];
                M[j + 1] = temp;
            }
        }
    }
    
    if (memory_count == 0) return;
    
    int min_addr = M[0].address;
    int max_addr = M[memory_count - 1].address;
    int processed[1000] = {0};
    
    int addr = min_addr;
    while (addr <= max_addr) {
        if (start_addr != -1 && addr < start_addr) { addr++; continue; }
        if (end_addr != -1 && addr > end_addr) { break; }

        const char *byte = getByteAtAddress(addr);
        if (!byte) { addr++; continue; }
        
        int idx = addr - min_addr;
        if (idx >= 0 && idx < 1000 && processed[idx]) { addr++; continue; }
        
        char opcode[4] = {0};
        strncpy(opcode, byte, 3);
        int inst_size = getInstructionSizeFromOpcode(opcode);
        
        printf("%d %s", addr, opcode);
        
        if (inst_size >= 2) {
            const char *b2 = getByteAtAddress(addr + 1);
            if (b2) {
                printf(" %s", b2);
                int i2 = (addr + 1) - min_addr;
                if (i2 >= 0 && i2 < 1000) processed[i2] = 1;
            }
        }
        if (inst_size >= 3) {
            const char *b3 = getByteAtAddress(addr + 2);
            if (b3) {
                printf(" %s", b3);
                int i3 = (addr + 2) - min_addr;
                if (i3 >= 0 && i3 < 1000) processed[i3] = 1;
            }
        }
        printf("\n");
        
        if (idx >= 0 && idx < 1000) processed[idx] = 1;
        addr += inst_size;
    }
}

void runLoader(const char *exe_file, const char *t_file) {
    memory_count = 0;
    memset(M, 0, sizeof(M));
    
    unsigned int loadpoint;
    printf("Loadpoint giriniz (decimal veya hex): ");
    
    char input[20];
    if (scanf("%s", input) != 1) return;
    
    if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) {
        sscanf(input, "%x", &loadpoint);
    } else {
        loadpoint = atoi(input);
    }
    
    loadExeFile(exe_file, loadpoint);
    
    int dat_addresses[MAX_DAT];
    int dat_count = 0;
    
    FILE *fp = fopen(t_file, "r");
    if (fp) {
        int in_dat = 0;
        char line[256];
        
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = 0;
            
            if (strstr(line, "=== DIRECT ADDRESS TABLE (DAT) ===")) {
                in_dat = 1;
                continue;
            }
            if (strstr(line, "===") && in_dat) break;
            
            if (in_dat && strlen(line) > 0) {
                int addr = atoi(line);
                if (addr >= 0 && dat_count < MAX_DAT) {
                    dat_addresses[dat_count++] = addr;
                }
            }
        }
        fclose(fp);
    }
    
    if (dat_count > 0) {
        applyRelocation(loadpoint, dat_addresses, dat_count);
    }
    
    printMemoryArray(-1, -1);
}

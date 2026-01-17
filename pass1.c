#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pass1.h"

//Opcode tablosu
typedef struct {
    char mnemonic[10];
    char opcode[3];
    int bytes;
    int isImmediate;
} OpcodeEntry;

OpcodeEntry opcodeMap[] = {
    {"ADD", "A1", 3, 0}, {"ADD", "A2", 2, 1},
    {"SUB", "A3", 3, 0}, {"SUB", "A4", 2, 1},
    {"LDA", "E1", 3, 0}, {"LDA", "E2", 2, 1},
    {"STA", "F1", 3, 0},
    {"CLL", "C1", 3, 0},
    {"JMP", "B4", 3, 0},
    {"BEQ", "B1", 3, 0},
    {"BGT", "B2", 3, 0},
    {"BLT", "B3", 3, 0},
    {"INC", "D2", 1, 0},
    {"DEC", "D1", 1, 0},
    {"RET", "C2", 1, 0},
    {"HLT", "FE", 1, 0}
};

int opcodeMapSize = sizeof(opcodeMap) / sizeof(OpcodeEntry);

//Relative addressing mi?
int isRelativeAddressing(const char *opcode) {
    return (strcmp(opcode, "BEQ") == 0 || 
            strcmp(opcode, "BGT") == 0 || 
            strcmp(opcode, "BLT") == 0);
}

//Mnemonic + addressing mode -> opcode hex
void opcodeToHex(const char *opcode, const char *operand, char *hexOpcode) {
    int isImmediate = (operand && operand[0] == '#');
    
    for (int i = 0; i < opcodeMapSize; i++) {
        if (strcmp(opcodeMap[i].mnemonic, opcode) == 0) {
            if (opcodeMap[i].isImmediate == isImmediate || opcodeMap[i].bytes == 1) {
                strcpy(hexOpcode, opcodeMap[i].opcode);
                return;
            }
        }
    }
    strcpy(hexOpcode, "??"); //Bulunamadı
}

//Operand sayı mı?
int isNumeric(const char *str) {
    if (!str || strlen(str) == 0) return 0;
    if (str[0] == '#') {
        //Immediate: #5 gibi
        for (int i = 1; str[i]; i++) {
            if (!isdigit(str[i])) return 0;
        }
        return 1;
    }
    //Direkt sayı: 10, 70 gibi
    for (int i = 0; str[i]; i++) {
        if (!isdigit(str[i])) return 0;
    }
    return 1;
}

//Operand'dan sayıyı al
int getNumericValue(const char *operand) {
    if (!operand) return 0;
    if (operand[0] == '#') {
        return atoi(operand + 1);
    }
    return atoi(operand);
}

//ST güncelle
void updateSymbolTable(ParsedLine *pl, int LC) {
    //Label varsa Symbol Table'a ekle
    if (strlen(pl->label) > 0) {
        int existing = findInSymbolTable(pl->label);
        if (existing == -1) {
            //Yeni symbol
            addToSymbolTable(pl->label, LC);
        } else {
            //Symbol zaten var, hata olabilir
            printf("UYARI: Symbol '%s' zaten tanımlı (eski: %d, yeni: %d)\n", 
                   pl->label, existing, LC);
        }
    }
}

//FRT güncelle
void updateFRT(ParsedLine *pl, int LC) {
    //Tanımsız sembol görürsek FRT'ye al
    if (strlen(pl->operand) > 0 && !isNumeric(pl->operand)) {
        //Immediate değilse (# ile başlamıyorsa)
        if (pl->operand[0] != '#') {
            //External değilse ve ST'de yoksa forward ref
            if (!isExternalReference(pl->operand)) {
                int found = findInSymbolTable(pl->operand);
                if (found == -1) {
                    //Symbol henüz tanımlı değil, forward reference
                    //3-byte: adres alanı LC+1
                    int addrOffset = (pl->size == 3) ? 1 : 0;
                    addToForwardRefTable(LC + addrOffset, pl->operand);
                }
            }
        }
    }
}

//DAT güncelle
void updateDAT(ParsedLine *pl, int LC) {
    (void)LC; //şu an kullanılmıyor
    //BYTE/WORD: veri, DAT'a girmez
    if (strcmp(pl->opcode, "BYTE") == 0 || strcmp(pl->opcode, "WORD") == 0) {
        return;
    }
    
    //Direct addressing kullanan instruction'lar için
    //Operand sayısal bir adres ise (örn: STA 70, STA 10)
    if (strlen(pl->operand) > 0 && isNumeric(pl->operand) && pl->operand[0] != '#') {
        int addr = getNumericValue(pl->operand);
        addToDirectAdrTable(addr);
    }
}

//HDRM güncelle
void updateHDRM(ParsedLine *pl, int LC) {
    if (strcmp(pl->opcode, "EXTREF") == 0) {
        //EXTREF: R kayıtları
        char *operand_copy = strdup(pl->operand);
        char *token = strtok(operand_copy, ",");
        while (token != NULL) {
            while (*token == ' ' || *token == '\t') token++;
            addToHDRMTable('R', token, 0);
            token = strtok(NULL, ",");
        }
        free(operand_copy);
    }
    
    if (strcmp(pl->opcode, "ENTRY") == 0) {
        //ENTRY: D kayıtları (adres Pass1 sonunda güncellenir)
        char *operand_copy = strdup(pl->operand);
        char *token = strtok(operand_copy, ",");
        while (token != NULL) {
            while (*token == ' ' || *token == '\t') token++;
            int addr = findInSymbolTable(token);
            if (addr == -1) addr = 0; //Henüz bilinmiyor
            addToHDRMTable('D', token, addr);
            token = strtok(NULL, ",");
        }
        free(operand_copy);
    }
    
    if (pl->size == 3 && strlen(pl->operand) > 0) {
        if (!isNumeric(pl->operand) && pl->operand[0] != '#') {
            addToHDRMTable('M', pl->operand, LC + 1);
        }
    }
}

//.s üret
void generatePartialCode(ParsedLine *pl, int LC, FILE *sfp) {
    if (!sfp) return;
    
    //BYTE
    if (strcmp(pl->opcode, "BYTE") == 0) {
        int val = getNumericValue(pl->operand);
        fprintf(sfp, "%d %02X\n", LC, val & 0xFF);
        return;
    }
    
    //WORD
    if (strcmp(pl->opcode, "WORD") == 0) {
        int val = getNumericValue(pl->operand);
        fprintf(sfp, "%d %02X\n", LC, val & 0xFF);
        return;
    }
    
    char hexOpcode[3];
    opcodeToHex(pl->opcode, pl->operand, hexOpcode);
    
    fprintf(sfp, "%d %s ", LC, hexOpcode);
    
    //Operand için partial code
    if (pl->size == 1) {
        fprintf(sfp, "\n");
    } else if (pl->size == 2) {
        if (pl->operand[0] == '#') {
            int val = getNumericValue(pl->operand);
            fprintf(sfp, "%02X\n", val & 0xFF);
        } else {
            fprintf(sfp, "??\n");
        }
    } else if (pl->size == 3) {
        if (isNumeric(pl->operand) && pl->operand[0] != '#') {
            int addr = getNumericValue(pl->operand);
            
            if (isRelativeAddressing(pl->opcode)) {
                int displacement = addr - (LC + 3);
                fprintf(sfp, "%02X %02X\n", (displacement >> 8) & 0xFF, displacement & 0xFF);
            } else {
                fprintf(sfp, "%02X %02X\n", (addr >> 8) & 0xFF, addr & 0xFF);
            }
        } else if (pl->operand[0] == '#') {
            fprintf(sfp, "?? ??\n");
        } else {
            int symbolAddr = findInSymbolTable(pl->operand);
            if (symbolAddr != -1) {
                if (isRelativeAddressing(pl->opcode)) {
                    int displacement = symbolAddr - (LC + 3);
                    fprintf(sfp, "%02X %02X\n", (displacement >> 8) & 0xFF, displacement & 0xFF);
                } else {
                    fprintf(sfp, "%02X %02X\n", (symbolAddr >> 8) & 0xFF, symbolAddr & 0xFF);
                }
            } else {
                fprintf(sfp, "?? ??\n");
            }
        }
    } else {
        fprintf(sfp, "\n");
    }
}

void processPass1(ParsedLine *pl, int LC, FILE *sfp) {
    if (strcmp(pl->opcode, "EXTREF") == 0 || strcmp(pl->opcode, "ENTRY") == 0) {
        updateHDRM(pl, LC);
        return;
    }
    
    updateSymbolTable(pl, LC);
    updateFRT(pl, LC);
    updateDAT(pl, LC);
    updateHDRM(pl, LC);
    
    generatePartialCode(pl, LC, sfp);
}


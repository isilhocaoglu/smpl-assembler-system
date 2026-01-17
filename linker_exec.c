#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//Minimal linker for the existing assembler/loader pipeline.
//Usage:
//  ./linker_exec <out_prefix> <mod1.o> <mod1.t> [<mod2.o> <mod2.t> ...]
//Outputs:
//  <out_prefix>.exe   (linked, still relocatable)
//  <out_prefix>.t     (DAT entries for loader relocation)

#define MAX_LINE 512
#define MAX_SYMBOLS 256
#define MAX_MRECS 512
#define MAX_DRECS 256
#define MAX_MODULES 32
#define MAX_DAT_OUT 4096

typedef struct {
    char name[32];
    int value; //absolute address in linked executable (LC space)
} SymEntry;

typedef struct {
    int addr;          //module-relative address of the address-field high byte (lc+1)
    char symbol[32];   //referenced symbol
} MRec;

typedef struct {
    char symbol[32];   //defined/exported symbol
    int addr;          //module-relative address
} DRec;

typedef struct {
    char o_path[256];
    char t_path[256];
    char prog_name[32]; //from H record
    int base;           //base LC in linked executable
    int length;         //module length in bytes

    //Local symbol table (from .t file's SYMBOL TABLE section)
    SymEntry locals[MAX_SYMBOLS];
    int locals_count;

    //HDRM-derived lists
    MRec mrecs[MAX_MRECS];
    int mrecs_count;

    DRec drecs[MAX_DRECS];
    int drecs_count;
} Module;

typedef struct {
    SymEntry entries[MAX_SYMBOLS * 4];
    int count;
} SymMap;

static void trim_crlf(char *s) {
    s[strcspn(s, "\r\n")] = 0;
}

static int symmap_find(const SymMap *m, const char *name) {
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->entries[i].name, name) == 0) return m->entries[i].value;
    }
    return -1;
}

static void symmap_put(SymMap *m, const char *name, int value) {
    //Replace if exists
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->entries[i].name, name) == 0) {
            m->entries[i].value = value;
            return;
        }
    }
    if (m->count < (int)(sizeof(m->entries) / sizeof(m->entries[0]))) {
        strncpy(m->entries[m->count].name, name, sizeof(m->entries[m->count].name) - 1);
        m->entries[m->count].name[sizeof(m->entries[m->count].name) - 1] = '\0';
        m->entries[m->count].value = value;
        m->count++;
    }
}

static int module_find_local(const Module *mod, const char *name) {
    for (int i = 0; i < mod->locals_count; i++) {
        if (strcmp(mod->locals[i].name, name) == 0) return mod->locals[i].value; //module-relative
    }
    return -1;
}

static const MRec* module_find_mrec_at(const Module *mod, int addr) {
    for (int i = 0; i < mod->mrecs_count; i++) {
        if (mod->mrecs[i].addr == addr) return &mod->mrecs[i];
    }
    return NULL;
}

typedef struct {
    const char *op;
    int bytes;
} OpcodeSize;

static OpcodeSize opcode_sizes[] = {
    {"A1", 3}, {"A2", 2}, {"A3", 3}, {"A4", 2},
    {"E1", 3}, {"E2", 2}, {"F1", 3},
    {"C1", 3}, {"B4", 3}, {"B1", 3}, {"B2", 3}, {"B3", 3},
    {"D2", 1}, {"D1", 1}, {"C2", 1}, {"FE", 1}
};

static int is_known_opcode(const char *tok) {
    for (size_t i = 0; i < sizeof(opcode_sizes) / sizeof(opcode_sizes[0]); i++) {
        if (strcmp(opcode_sizes[i].op, tok) == 0) return 1;
    }
    return 0;
}

static int opcode_len_bytes(const char *tok) {
    for (size_t i = 0; i < sizeof(opcode_sizes) / sizeof(opcode_sizes[0]); i++) {
        if (strcmp(opcode_sizes[i].op, tok) == 0) return opcode_sizes[i].bytes;
    }
    return 1; //data byte
}

static void parse_t_file(Module *mod) {
    FILE *fp = fopen(mod->t_path, "r");
    if (!fp) {
        fprintf(stderr, "HATA: .t dosyasi acilamadi: %s\n", mod->t_path);
        return;
    }

    mod->locals_count = 0;
    mod->mrecs_count = 0;
    mod->drecs_count = 0;
    mod->prog_name[0] = '\0';

    enum { SEC_NONE, SEC_SYMTAB, SEC_HDRM } section = SEC_NONE;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), fp)) {
        trim_crlf(line);
        if (strlen(line) == 0) continue;

        if (strstr(line, "=== SYMBOL TABLE ===")) { section = SEC_SYMTAB; continue; }
        if (strstr(line, "=== HDRM TABLE ===")) { section = SEC_HDRM; continue; }
        if (strstr(line, "===")) { section = SEC_NONE; continue; }

        if (section == SEC_SYMTAB) {
            //Symbol: LOOP       Address: 0
            char sym[32];
            int addr = 0;
            if (sscanf(line, "Symbol: %31s Address: %d", sym, &addr) == 2) {
                if (mod->locals_count < MAX_SYMBOLS) {
                    strncpy(mod->locals[mod->locals_count].name, sym, sizeof(mod->locals[mod->locals_count].name) - 1);
                    mod->locals[mod->locals_count].name[sizeof(mod->locals[mod->locals_count].name) - 1] = '\0';
                    mod->locals[mod->locals_count].value = addr;
                    mod->locals_count++;
                }
            }
        } else if (section == SEC_HDRM) {
            //H MAIN 0
            //D AD5 0
            //M XX 1
            char code = 0;
            char sym[32];
            int addr = 0;
            if (sscanf(line, " %c %31s %d", &code, sym, &addr) == 3) {
                if (code == 'H') {
                    strncpy(mod->prog_name, sym, sizeof(mod->prog_name) - 1);
                    mod->prog_name[sizeof(mod->prog_name) - 1] = '\0';
                } else if (code == 'D') {
                    if (mod->drecs_count < MAX_DRECS) {
                        strncpy(mod->drecs[mod->drecs_count].symbol, sym, sizeof(mod->drecs[mod->drecs_count].symbol) - 1);
                        mod->drecs[mod->drecs_count].symbol[sizeof(mod->drecs[mod->drecs_count].symbol) - 1] = '\0';
                        mod->drecs[mod->drecs_count].addr = addr;
                        mod->drecs_count++;
                    }
                } else if (code == 'M') {
                    if (mod->mrecs_count < MAX_MRECS) {
                        mod->mrecs[mod->mrecs_count].addr = addr;
                        strncpy(mod->mrecs[mod->mrecs_count].symbol, sym, sizeof(mod->mrecs[mod->mrecs_count].symbol) - 1);
                        mod->mrecs[mod->mrecs_count].symbol[sizeof(mod->mrecs[mod->mrecs_count].symbol) - 1] = '\0';
                        mod->mrecs_count++;
                    }
                }
            }
        }
    }

    fclose(fp);
}

static int compute_module_length_from_o(const Module *mod) {
    FILE *fp = fopen(mod->o_path, "r");
    if (!fp) return 0;

    char line[MAX_LINE];
    int max_end = 0;

    while (fgets(line, sizeof(line), fp)) {
        trim_crlf(line);
        if (strlen(line) == 0) continue;

        int lc = 0;
        char tok1[32] = {0}, tok2[32] = {0}, tok3[32] = {0};
        int n = sscanf(line, "%d %31s %31s %31s", &lc, tok1, tok2, tok3);
        if (n < 2) continue;

        int size = 1;
        if (is_known_opcode(tok1)) size = opcode_len_bytes(tok1);

        int end = lc + size;
        if (end > max_end) max_end = end;
    }

    fclose(fp);
    return max_end;
}

static void dat_add_unique(int *dat_out, int *dat_count, int value) {
    for (int i = 0; i < *dat_count; i++) {
        if (dat_out[i] == value) return;
    }
    if (*dat_count < MAX_DAT_OUT) {
        dat_out[*dat_count] = value;
        (*dat_count)++;
    }
}

static int cmp_int(const void *a, const void *b) {
    const int ia = *(const int *)a;
    const int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static void link_modules(const char *out_prefix, Module *mods, int mod_count, const SymMap *estab) {
    char exe_path[300];
    char t_path[300];
    snprintf(exe_path, sizeof(exe_path), "%s.exe", out_prefix);
    snprintf(t_path, sizeof(t_path), "%s.t", out_prefix);

    FILE *exe = fopen(exe_path, "w");
    if (!exe) {
        fprintf(stderr, "HATA: .exe yazilamadi: %s\n", exe_path);
        return;
    }

    int dat_out[MAX_DAT_OUT];
    int dat_count = 0;

    for (int mi = 0; mi < mod_count; mi++) {
        const Module *mod = &mods[mi];
        FILE *fp = fopen(mod->o_path, "r");
        if (!fp) {
            fprintf(stderr, "HATA: .o acilamadi: %s\n", mod->o_path);
            continue;
        }

        char line[MAX_LINE];
        while (fgets(line, sizeof(line), fp)) {
            trim_crlf(line);
            if (strlen(line) == 0) continue;

            int lc = 0;
            char tok1[32] = {0}, tok2[32] = {0}, tok3[32] = {0};
            int n = sscanf(line, "%d %31s %31s %31s", &lc, tok1, tok2, tok3);
            if (n < 2) continue;

            int global_lc = mod->base + lc;

            int size = 1;
            if (is_known_opcode(tok1)) size = opcode_len_bytes(tok1);

            if (size == 3 && n >= 4) {
                const MRec *mrec = module_find_mrec_at(mod, lc + 1);
                if (mrec) {
                    //Always compute the correct linked address for this symbol.
                    int target = symmap_find(estab, mrec->symbol);
                    if (target < 0) {
                        int local = module_find_local(mod, mrec->symbol);
                        if (local >= 0) target = mod->base + local;
                    }

                    if (target >= 0) {
                        int high = (target >> 8) & 0xFF;
                        int low = target & 0xFF;
                        fprintf(exe, "%d %s %02X %02X\n", global_lc, tok1, high, low);
                        //DAT holds the LC of the address-field high byte in the linked executable.
                        dat_add_unique(dat_out, &dat_count, global_lc + 1);
                    } else {
                        //Unresolved; keep as-is.
                        fprintf(exe, "%d %s %s %s\n", global_lc, tok1, tok2, tok3);
                    }
                } else {
                    fprintf(exe, "%d %s %s %s\n", global_lc, tok1, tok2, tok3);
                }
            } else if (size == 2 && n >= 3) {
                fprintf(exe, "%d %s %s\n", global_lc, tok1, tok2);
            } else {
                fprintf(exe, "%d %s\n", global_lc, tok1);
            }
        }

        fclose(fp);
    }

    fclose(exe);

    //Write DAT file for loader
    FILE *tfp = fopen(t_path, "w");
    if (!tfp) {
        fprintf(stderr, "HATA: .t yazilamadi: %s\n", t_path);
        return;
    }

    qsort(dat_out, dat_count, sizeof(int), cmp_int);

    fprintf(tfp, "=== DIRECT ADDRESS TABLE (DAT) ===\n");
    for (int i = 0; i < dat_count; i++) {
        fprintf(tfp, "%d\n", dat_out[i]);
    }
    fclose(tfp);
}

int main(int argc, char **argv) {
    if (argc < 4 || ((argc - 2) % 2) != 0) {
        fprintf(stderr, "Kullanim: %s <out_prefix> <mod1.o> <mod1.t> [<mod2.o> <mod2.t> ...]\n", argv[0]);
        return 1;
    }

    const char *out_prefix = argv[1];

    int mod_count = (argc - 2) / 2;
    if (mod_count > MAX_MODULES) mod_count = MAX_MODULES;

    Module mods[MAX_MODULES];
    memset(mods, 0, sizeof(mods));

    int argi = 2;
    for (int i = 0; i < mod_count; i++) {
        strncpy(mods[i].o_path, argv[argi++], sizeof(mods[i].o_path) - 1);
        strncpy(mods[i].t_path, argv[argi++], sizeof(mods[i].t_path) - 1);
        mods[i].o_path[sizeof(mods[i].o_path) - 1] = '\0';
        mods[i].t_path[sizeof(mods[i].t_path) - 1] = '\0';
    }

    //Parse .t files and compute lengths
    for (int i = 0; i < mod_count; i++) {
        parse_t_file(&mods[i]);
        mods[i].length = compute_module_length_from_o(&mods[i]);
    }

    //Assign bases: main first, then others follow immediately
    int running = 0;
    for (int i = 0; i < mod_count; i++) {
        mods[i].base = running;
        running += mods[i].length;
    }

    //Build ESTAB from H/D records
    SymMap estab;
    memset(&estab, 0, sizeof(estab));
    for (int i = 0; i < mod_count; i++) {
        if (mods[i].prog_name[0] != '\0') {
            symmap_put(&estab, mods[i].prog_name, mods[i].base);
        }
        for (int d = 0; d < mods[i].drecs_count; d++) {
            symmap_put(&estab, mods[i].drecs[d].symbol, mods[i].base + mods[i].drecs[d].addr);
        }
    }

    link_modules(out_prefix, mods, mod_count, &estab);
    return 0;
}



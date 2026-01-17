# SMPL Assembler-Linker-Loader System

A complete two-pass assembler, linker, and relocating loader implementation for the SMPL (Simple) assembly language.

## 📚 Course CSE 232 - Systems Programming

## 👥 Contributors

This is a **group project**. Each member was responsible for specific modules:

| Name | GitHub | Responsibility | Status |
|------|--------|----------------|--------|
| **Işıl Hocaoğlu** | | Loader, Integration & Makefile |
| **Nisa Of** | [@Nisaof](https://github.com/Nisaof) | Assembler Pass 1 - Parser |
| **Yiğit Can Turan** | [@Yigit1708](https://github.com/Yigit1708) | Assembler Pass 1 - Tables & Partial Code |
| **Efe Demirci** | [@efedemirci04](https://github.com/efedemirci04) | Assembler Pass 2 |
| **Deniz Baltaş** | [@denizbaltas](https://github.com/denizbaltas) | Linker |

### My Contribution

I was responsible for:
- **Loader Module** (`loader.c`, `loader.h`, `loader_main.c`)
  - Loading executable and DAT files into memory
  - Implementing relocation logic based on user-provided load point
  - Memory array management and display
- **System Integration** - Ensuring all modules work together seamlessly
- **Makefile** - Build system configuration
- **Group Leadership** - Coordinating team efforts and submissions

## 📋 Project Overview

This project implements a complete assembly language processing toolchain:

```
.asm file → Assembler → .o & .t files → Linker → .exe & .t files → Loader → Memory Array M[]
```

### Components

1. **Two-Pass Assembler**
   - **Pass 1**: Parses source code, builds Symbol Table (ST), Forward Reference Table (FRT), Direct Address Table (DAT), and generates partial object code
   - **Pass 2**: Resolves forward references and generates final relocatable object code (.o file)

2. **Linker**
   - Creates External Symbol Table (ESTAB)
   - Resolves external references between modules
   - Generates executable code (.exe file)

3. **Loader**
   - Accepts load point from user input
   - Loads executable into memory array with relocation
   - Displays final memory dump

## 🛠️ SMPL Instruction Set

| Mnemonic | Opcode | Bytes | Mode | Description |
|----------|--------|-------|------|-------------|
| ADD | A1/A2 | 3/2 | direct/immediate | AC ← AC + M[] or AC + n |
| SUB | A3/A4 | 3/2 | direct/immediate | AC ← AC - M[] or AC - n |
| LDA | E1/E2 | 3/2 | direct/immediate | AC ← M[] or n |
| STA | F1 | 3 | direct | M[] ← AC |
| BEQ | B1 | 3 | relative | Branch if AC = 0 |
| BGT | B2 | 3 | relative | Branch if AC > 0 |
| BLT | B3 | 3 | relative | Branch if AC < 0 |
| JMP | B4 | 3 | direct | Unconditional jump |
| CLL | C1 | 3 | direct | Call subroutine |
| RET | C2 | 1 | implied | Return |
| INC | D2 | 1 | implied | AC ← AC + 1 |
| DEC | D1 | 1 | implied | AC ← AC - 1 |
| HLT | FE | 1 | implied | Halt |

## 🔧 Build & Run

### Requirements
- GCC compiler
- Linux environment (tested on Ubuntu)

### Compilation

```bash
make all
```

### Usage

```bash
# Run the complete pipeline
./main <input.asm>

# Run loader separately
./loader_main <program.exe> <program.t>
```

## 📁 Project Structure

```
├── main.c           # Main entry point
├── parser.c/.h      # Line parsing (label/opcode/operand)
├── tables.c/.h      # Symbol Table, FRT, DAT, HDRM Table management
├── pass1.c/.h       # Assembler Pass 1
├── pass2.c/.h       # Assembler Pass 2
├── linker_exec.c    # Linker implementation
├── loader.c/.h      # Loader implementation
├── loader_main.c    # Loader standalone entry
├── Makefile         # Build configuration
├── test_main.asm    # Test: Main program
├── sub.asm          # Test: Subroutine module
└── data.asm         # Test: Data module
```

## 📝 Example

**Input (Main Program):**
```asm
PROG MAIN
EXTREF AD5,XX,ZZ
START
LOOP: LDA XX
      CLL AD5
      ADD ZZ
      CLL AD5
      STA 70
      LDA ZZ
      SUB #1
      BLT EX
      JMP LOOP
EX:   HLT
END
```

**Memory Output (loadpoint = 1000):**
```
1000 E1 10 33
1003 C1 10 27
1006 A1 10 35
1009 C1 10 27
1012 F1 00 70
1015 E1 10 35
1018 A4 01
1020 B3 10 26
1023 B4 10 00
1026 FE
1027 A2 05
1029 F1 10 34
1032 C2
1033 20
1034 0
1035 3
```

## 📄 This project was developed for educational purposes as part of CSE 232 coursework.

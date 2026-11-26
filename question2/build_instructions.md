# Assembly-Based Sensor Data Validation and Analysis

## Build and Execution Instructions

### Environment
- Architecture: x86-64
- Operating System: Linux (Ubuntu / WSL2)
- Assembler: NASM (Netwide Assembler)
- Linker: GNU Linker (`ld`)

### Build Instructions

Open terminal in the directory containing `sensor_analysis.asm` and `sensor_readings.txt`.

**1. Assemble source file:**

```bash
nasm -f elf64 sensor_analysis.asm -o sensor_analysis.o
```

**2. Link object file:**

```bash
ld sensor_analysis.o -o sensor_analysis
```

**3. Execute program:**

```bash
./sensor_analysis
```

### Expected Output

```text
Total records: 11
Valid measurements: 6
Invalid measurements: 5
Sum of valid measurements: 215
Minimum measurement: 0
Maximum measurement: 100
```

### Notes

The program reads `sensor_readings.txt` from the current working directory. Ensure that the input file is located in the same directory when executing the program.


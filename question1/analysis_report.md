# Investigating and Reverse Engineering an ELF Executable

## Overview

This project investigates how a C application is compiled into an ELF executable and how Linux loads and executes it. A bank transaction processing program was developed and examined using `readelf`, `objdump`, `strace`, and GDB.

The investigation covers the executable's ELF structure, static and dynamic linking, assembly instructions, system calls, and runtime memory management.

## Program Development

The application simulates a bank transaction system using a `Transaction` structure, a global initial balance, and three user-defined functions in addition to `main()`. Five transactions were dynamically allocated and processed:

| Transaction ID | Type | Amount |
|---|---|---:|
| 1001 | Deposit | $100 |
| 1002 | Withdrawal | $200 |
| 1003 | Deposit | $300 |
| 1004 | Withdrawal | $400 |
| 1005 | Deposit | $500 |

The program uses loops and conditional statements to process transactions, calculate deposits and withdrawals, and update the balance. It also releases dynamically allocated memory using `free()`.

**Execution results:**

- Initial balance: $1,000.00
- Total deposits: $900.00
- Total withdrawals: $600.00
- Final balance: $1,300.00

The program was compiled into stripped and debug versions:

```bash
gcc -Wall -O0 -fno-inline -o program program.c
strip program
gcc -Wall -O0 -g -fno-inline -o program_debug program.c
```

## ELF Static Analysis and Reverse Engineering

### ELF Header and Program Structure

Using `readelf -h`, the executable was identified as:

- ELF class: ELF64
- Data encoding: Little Endian
- Architecture: AMD x86-64
- ELF type: DYN (Position-Independent Executable)
- Entry point: `0x1100`
- Program headers: 13
- Section headers: 29

The executable uses the dynamic linker `/lib64/ld-linux-x86-64.so.2`. Its `GNU_STACK` segment is readable and writable but not executable, while `GNU_RELRO` provides additional protection for relocation-related memory.

### ELF Sections

The executable contains several important sections:

| Section | Purpose |
|---|---|
| `.text` | Executable machine instructions |
| `.data` | Initialized global and static variables |
| `.bss` | Uninitialized global and static variables |
| `.rodata` | Read-only constants and strings |
| `.plt` | Procedure Linkage Table for external function calls |
| `.got` | Global Offset Table used in dynamic linking |

The global variable `initial_balance` is stored in `.data` at ELF address `0x4010`. The `.bss` section begins at `0x4020`.

### Dynamic Linking

The dynamic section identifies `libc.so.6` as a required shared library. It also contains entries associated with the PLT and GOT, including `PLTGOT` and `PLTREL`.

External functions such as `malloc()`, `printf()`, and `free()` are accessed through dynamically linked library routines. The PLT and GOT support the resolution of these external function addresses at runtime.

### Assembly Analysis

The disassembled executable revealed the following user-defined functions:

| Function | Address |
|---|---|
| `initialize_transactions()` | `0x11e9` |
| `process_transactions()` | `0x12ae` |
| `display_summary()` | `0x13b5` |
| `main()` | `0x145c` |

The assembly instructions show how the compiler translates the program's logic into machine instructions.

For example, `initialize_transactions()` assigns transaction IDs using arithmetic instructions and determines transaction types through comparisons. The following conditional instruction selects the appropriate branch:

```asm
cmp    BYTE PTR [rbp-0x9],0x44
jne    ...
```

The loop in the same function uses a counter, comparison, and backward jump:

```asm
129a: add    DWORD PTR [rbp-0x4],0x1
129e: mov    eax,DWORD PTR [rbp-0x4]
12a1: cmp    eax,DWORD PTR [rbp-0x1c]
12a4: jl     1204
```

In `process_transactions()`, conditional branches distinguish deposits from withdrawals, while `addsd` performs floating-point addition.

The `main()` function contains calls to `malloc@plt`, the three user-defined functions, and `free@plt`. The allocation instruction reserves `0x78` bytes (120 bytes), enough for five transaction records of 24 bytes each. A conditional check verifies whether the allocation succeeded.

These observations demonstrate how high-level C structures, loops, conditions, and function calls are represented in x86-64 assembly.

## System Call Analysis Using `strace`

The program was traced using `strace` to examine interactions between the application and the Linux kernel.

The trace revealed several system calls:

| System call | Purpose |
|---|---|
| `execve` | Starts the executable |
| `openat` | Opens required files |
| `read` / `pread64` | Reads program and library data |
| `mmap` | Maps memory regions |
| `brk` | Adjusts the process heap |
| `mprotect` | Changes memory protection |
| `write` | Writes output to standard output |
| `exit_group` | Terminates the process |

These calls represent different stages of execution, including program startup, memory management, input/output, and termination.

The C library function `malloc()` manages dynamic memory and may request additional memory from the kernel through calls such as `brk()` or `mmap()`. However, the trace does not establish that a particular `brk()` call was exclusively caused by the program's 120-byte allocation.

Similarly, `printf()` formats output in the C library before the resulting data is written through the `write()` system call.

An excerpt from the trace shows the program's summary being written to standard output:

```text
write(1, "BANK TRANSACTION SUMMARY\n", 25) = 25
write(1, "Initial Balance:   $1000.00\n", 28) = 28
write(1, "Total Deposits:    $900.00\n", 27) = 27
write(1, "Total Withdrawals: $600.00\n", 27) = 27
write(1, "Final Balance:     $1300.00\n", 28) = 28
```

The process terminates successfully, as shown by:

```text
exit_group(0) = ?
+++ exited with 0 +++
```

## 5. Dynamic Analysis Using GDB

GDB was used to examine the program during execution. Breakpoints were set at `_start`, `main()`, and `process_transactions()` to observe program startup and function execution.

The call stack at `process_transactions()` showed the function being called from `main()`:

```text
#0 process_transactions (...) at program.c:25
#1 ... in main () at program.c:59
```

Memory inspection confirmed the locations and contents of different variables:

- **Global memory:** `initial_balance` contained `1000` and was located at runtime address `0x555555558010`, corresponding to ELF address `0x4010`.
- **Heap memory:** The `transactions` pointer referenced dynamically allocated memory at `0x5555555592a0`. Inspection showed the five transaction records and their values.
- **Stack memory:** The local loop variable `i` was inspected at address `0x7fffffffd67c`.

The investigation demonstrated how global variables, dynamically allocated records, and local variables occupy different memory regions during execution.

## Conclusion

This project demonstrated the relationship between C source code, ELF executable structure, assembly instructions, Linux system calls, and runtime memory management. Using `readelf`, `objdump`, `strace`, and GDB provided practical insight into how Linux loads, executes, and manages a compiled application.

The complete command outputs, disassembly, system call traces, and debugging evidence are available in the project's GitHub repository under the `evidence` directory.


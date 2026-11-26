# Linux Programming Summative

This repository presents five Linux programming projects investigating executable formats and reverse engineering, assembly-based sensor data processing, Python and C integration, multithreaded programming, and TCP client-server communication. The implementations demonstrate practical applications of C programming, x86-64 Assembly, Linux system calls, memory management, POSIX threads, synchronization, and network programming.

[View Project Demo](https://canva.link/gdhp8qicscnwexe)

---

## Compilation & Execution

### Question 1: Investigating and Reverse Engineering an ELF Executable

```bash
cd question1

gcc -Wall -O0 -fno-inline -o program program.c
strip program

gcc -Wall -O0 -g -fno-inline -o program_debug program.c

./program
```

To investigate the executable:

```bash
readelf -h program
readelf -l program
readelf -S program
readelf -d program

objdump -d -M intel program

strace ./program

gdb ./program_debug
```

The program simulates bank transactions and is examined to understand ELF structure, dynamic linking, assembly instructions, system calls, and runtime memory organisation.

For detailed findings, see [Question 1 Analysis Report](question1/analysis_report.md) and `question1/evidence/`.

### Question 2: Assembly-Based Sensor Data Validation and Analysis

```bash
cd question2

nasm -f elf64 sensor_analysis.asm -o sensor_analysis.o
ld sensor_analysis.o -o sensor_analysis

./sensor_analysis
```

The program reads sensor data using Linux system calls, validates readings within the range of 0–100, and calculates summary statistics. It also handles invalid records, empty values, different line endings, and file-reading errors.

For additional compilation instructions, see [Question 2 Build Instructions](question2/build_instructions.txt). Execution results are available in `question2/execution_evidence.txt`.

### Question 3: Python C Extension for Sensor Data Processing

```bash
cd question3

python3 setup.py build_ext --inplace
python3 test_processor.py
```

The project builds a native C extension using the Python/C API and tests its integration with Python. The tests cover sensor data processing, input validation, and error handling.

For detailed implementation and testing information, see [Question 3 README](question3/README.md).

### Question 4: Concurrent University Library Book Reservation System

```bash
cd question4

gcc -Wall -Wextra -Werror -pedantic -std=c11 -pthread reservation.c -o reservation

./reservation
```

To save the execution output as evidence:

```bash
mkdir -p evidence
./reservation | tee evidence/execution_output.txt
```

The program uses POSIX threads and a bounded shared queue to coordinate concurrent library reservation requests. It demonstrates producer-consumer coordination, queue capacity management, synchronization, and protection of shared resources.

For detailed implementation, design decisions, and execution information, see [Question 4 README](question4/README.md).

### Question 5: TCP-Based University Event Registration System

Compile the server and client:

```bash
cd question5

gcc -Wall -Wextra -Werror -pthread server.c -o server
gcc -Wall -Wextra -Werror client.c -o client
```

Start the server:

```bash
./server
```

In a separate terminal, run the automated tests:

```bash
cd question5
./run_tests.sh
```

The project implements a TCP client-server application for university event registration. It demonstrates student authentication, concurrent client handling, shared resource protection, duplicate registration prevention, and event capacity enforcement.

For detailed protocol information, client and server usage, testing procedures, and implementation details, see [Question 5 README](question5/README.md).

---

## Summary of Projects

### Question 1: Investigating and Reverse Engineering an ELF Executable

- Develops a C-based bank transaction processing application using structures, global variables, dynamic memory allocation, and user-defined functions.
- Examines ELF headers, program headers, sections, dynamic linking, and machine instructions using `readelf` and `objdump`.
- Uses `strace` and GDB to investigate system calls, function execution, the call stack, and memory organisation.

### Question 2: Assembly-Based Sensor Data Validation and Analysis

- Implements a sensor data validation and analysis program using NASM x86-64 Assembly and Linux system calls.
- Reads and parses sensor readings, validates values within the range of 0–100, and calculates the number of records, valid and invalid readings, sum, minimum, and maximum.
- Handles empty records, non-numeric input, out-of-range values, LF and CRLF line endings, and file-reading errors.
- Tests the implementation using different input scenarios, including invalid records and larger input files.

**Disclosure:** Question 2 was highly guided by external technical and academic sources. The implementation and its underlying concepts were developed with reference to established literature on algorithms, data representation, programming, and information processing. The specific sources included in the research references are listed below.

### Question 3: Python C Extension for Sensor Data Processing

- Develops a native C extension module that integrates C functionality with Python through the Python/C API.
- Implements sensor data processing and statistical operations through the compiled extension.
- Tests functional correctness, input validation, and error handling.
- Compares native C processing with Python-based processing through performance experiments.

### Question 4: Concurrent University Library Book Reservation System

- Implements a multithreaded library reservation system using POSIX threads.
- Uses three worker threads and a bounded queue with a capacity of five to coordinate reservation requests.
- Demonstrates producer-consumer coordination, mutex synchronization, shared resource protection, and queue management.
- Processes 15 reservation requests and verifies that the queue is emptied after processing.

### Question 5: TCP-Based University Event Registration System

- Implements a client-server application using TCP sockets for university event registration.
- Uses student authentication and concurrent request handling to support multiple clients.
- Protects shared registration data and enforces event capacity constraints.
- Tests event listing, registration approval and rejection, duplicate requests, concurrent registrations, persistence, and server behaviour.

---

## AI Assistance Used

AI tools were used as supplementary learning and development aids during the projects. Their use included:

- Clarifying programming concepts, system interfaces, and technical documentation.
- Supporting debugging and troubleshooting during implementation.
- Supporting the interpretation of test results and performance observations.
- Improving technical documentation and report organisation.

For Question 2, external technical and academic sources played a particularly significant role in guiding the development and understanding of the implementation.

All implementations were compiled and tested in a Linux environment. Execution results and supporting evidence are provided in the respective project directories.

---

## Resources

The following technical documentation and academic references were consulted during the development and investigation of the projects.

**Technical documentation**

- [Linux man-pages](https://man7.org/linux/man-pages/)
- [GNU Compiler Collection (GCC) Documentation](https://gcc.gnu.org/onlinedocs/)
- [GNU Binutils Documentation](https://sourceware.org/binutils/docs/binutils/)
- [NASM Documentation](https://www.nasm.us/doc/)
- [Python/C API Reference Manual](https://docs.python.org/3/c-api/)
- [System V AMD64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI)
- [POSIX Specifications](https://pubs.opengroup.org/onlinepubs/9699919799/)

**Academic references used in Question 2**

[1] T. H. Cormen, C. E. Leiserson, R. L. Rivest, and C. Stein, *Introduction to Algorithms*, 4th ed. MIT Press, 2022.

[2] D. A. Huffman, “A Method for the Construction of Minimum-Redundancy Codes,” *Proceedings of the IRE*, vol. 40, no. 9, pp. 1098–1101, 1952. doi: 10.1109/JRPROC.1952.273898.

[3] B. W. Kernighan and D. M. Ritchie, *The C Programming Language*, 2nd ed. Prentice Hall, 1988.

[4] V. I. Levenshtein, “Binary Codes Capable of Correcting Deletions, Insertions, and Reversals,” *Soviet Physics Doklady*, vol. 10, pp. 707–710, 1966.

---

## License

This project is under the MIT License.

---

## Author

Monica Dhieu


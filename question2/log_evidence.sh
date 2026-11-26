#!/bin/bash

EVIDENCE_FILE="execution_evidence.txt"
BACKUP=$(mktemp)

cleanup() {
    # remove temp dir if read-error test was interrupted
    if [ -d sensor_readings.txt ]; then
        rmdir sensor_readings.txt 2>/dev/null
    fi
    # restore orig input file
    if [ -f "$BACKUP" ]; then
        cp "$BACKUP" sensor_readings.txt
        rm -f "$BACKUP"
    fi
    rm -f sensor_readings.temp
}

trap cleanup EXIT

exec > >(tee "$EVIDENCE_FILE") 2>&1

if ! cp sensor_readings.txt "$BACKUP"; then
    echo "ERROR: Could not back up sensor_readings.txt"
    exit 1
fi

run_program() {
    ./sensor_analysis
    STATUS=$?
    echo "Exit status: $STATUS"
    echo
}

echo
echo "ASSEMBLY-BASED SENSOR DATA VALIDATION"
echo
echo "Architecture: x86-64"
echo "Operating System: Linux"
echo "Assembler: NASM"
echo "Linker: GNU ld"
echo

echo
echo "BUILD: ASSEMBLING AND LINKING"
echo
echo '$ nasm -f elf64 sensor_analysis.asm -o sensor_analysis.o'
nasm -f elf64 sensor_analysis.asm -o sensor_analysis.o
ASSEMBLY_STATUS=$?

echo '$ ld sensor_analysis.o -o sensor_analysis'
if [ "$ASSEMBLY_STATUS" -eq 0 ]; then
    ld sensor_analysis.o -o sensor_analysis
    LINK_STATUS=$?
else
    LINK_STATUS=1
fi

echo "Assembly exit status: $ASSEMBLY_STATUS"
echo "Linker exit status: $LINK_STATUS"

if [ "$ASSEMBLY_STATUS" -ne 0 ] || [ "$LINK_STATUS" -ne 0 ]; then
    echo "BUILD FAILED! Tests were not executed."
    exit 1
fi

echo "Build successful :)"
echo

echo ""
echo "TEST 1: NORMAL INPUT"
echo "valid, invalid & empty records"
echo ""
echo '$ ./sensor_analysis'
run_program

echo ""
echo "TEST 2: LARGE INPUT"
echo "buffer boundary handling (3047 records)"
echo ""
echo "Generating large input..."
python3 -c "open('sensor_readings.txt','wb').write(b'1\n'*2046 + b'25\n' + b'30\n'*1000)"
echo '$ ./sensor_analysis'
run_program

echo ""
echo "TEST 3: WINDOWS CRLF LINE ENDINGS"
echo ""
echo "Generating CRLF input..."
python3 -c "open('sensor_readings.txt','wb').write(b'25\r\ninvalid\r\n\r\n100\r\n')"
echo '$ ./sensor_analysis'
run_program

echo ""
echo "TEST 4: NO VALID MEASUREMENTS"
echo ""
printf 'invalid\n101\n-5\n\n' > sensor_readings.txt
echo '$ ./sensor_analysis'
run_program

echo ""
echo "TEST 5: MISSING INPUT FILE"
echo ""
rm -f sensor_readings.txt
echo '$ ./sensor_analysis'
run_program

echo ""
echo "TEST 6: RESTORED ORIGINAL INPUT"
echo ""
cp "$BACKUP" sensor_readings.txt
echo '$ ./sensor_analysis'
run_program

echo "Original input is preserved."

echo ""
echo "TEST 7: FILE READ ERROR HANDLING"
echo ""
echo "Testing with directory instead of regular file..."
rm -f sensor_readings.txt
mkdir sensor_readings.txt
echo '$ ./sensor_analysis'
run_program

# remove directory & restore orig input
rmdir sensor_readings.txt
cp "$BACKUP" sensor_readings.txt

echo ""
echo "TEST 8: FINAL EXECUTION AFTER RESTORATION"
echo ""
echo "Verifying original input still works..."
echo '$ ./sensor_analysis'
run_program

echo ""
echo "TESTS COMPLETED"
echo "Original sensor_readings.txt restored."
echo ""


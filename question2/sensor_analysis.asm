; -------------------------------------------------------------------
; Project: Assembly-Based Sensor Data Validation and Analysis
; Architecture: x86-64 (Linux)
; Assembler: NASM
; Calling Convention: Linux x86-64 System Call Interface
; -------------------------------------------------------------------

section .data
    filename        db "sensor_readings.txt", 0

    ; Console output labels
    lbl_total       db "Total records: "
    len_total       equ $ - lbl_total

    lbl_valid       db "Valid measurements: "
    len_valid       equ $ - lbl_valid

    lbl_invalid     db "Invalid measurements: "
    len_invalid     equ $ - lbl_invalid

    lbl_sum         db "Sum of valid measurements: "
    len_sum         equ $ - lbl_sum

    lbl_min         db "Minimum measurement: "
    len_min         equ $ - lbl_min

    lbl_max         db "Maximum measurement: "
    len_max         equ $ - lbl_max

    ; Error and edge-case messages
    err_open        db "Error: Could not open sensor_readings.txt", 10
    len_err_open    equ $ - err_open

    err_read        db "Error: Failed to read sensor_readings.txt", 10
    len_err_read    equ $ - err_read

    msg_na          db "N/A", 10
    len_na          equ $ - msg_na

section .bss
    BUFFER_SIZE     equ 4096
    buffer          resb BUFFER_SIZE
    num_buf         resb 32

    ; 64-bit counters and statistical accumulators
    total_records   resq 1
    valid_count     resq 1
    invalid_count   resq 1
    valid_sum       resq 1
    min_val         resq 1
    max_val         resq 1

section .text
    global _start

_start:
    ; Initialize min and max outside valid range
    mov qword [min_val], 101
    mov qword [max_val], -1

    ; FILE HANDLING: Open sensor_readings.txt
    mov rax, 2                  ; sys_open
    mov rdi, filename
    xor rsi, rsi                ; O_RDONLY
    xor rdx, rdx
    syscall

    test rax, rax
    js handle_open_error

    mov r12, rax                ; Preserve file descriptor

    ; Initialize parser state across buffer reads:
    ; rbx  = current numeric value
    ; r10  = digit count (preserved across Linux syscalls)
    ; r13  = line validity flag (1 = valid so far)
    ; r14  = pending record flag
    ; r15  = skip-next-LF flag after CR
    xor rbx, rbx
    xor r10, r10
    mov r13, 1
    xor r14, r14
    xor r15, r15

    ; FILE READING AND CHARACTER-BY-CHARACTER TRAVERSAL
read_chunk_loop:
    mov rax, 0                  ; sys_read
    mov rdi, r12
    mov rsi, buffer
    mov rdx, BUFFER_SIZE
    syscall

    ; Retry if read was interrupted
    cmp rax, -4                 ; EINTR
    je read_chunk_loop

    test rax, rax
    js handle_read_error
    jz eof_reached              ; Zero bytes means EOF

    mov rsi, buffer
    lea r8, [buffer + rax]      ; End of current buffer

char_loop:
    cmp rsi, r8
    jae read_chunk_loop         ; Read another chunk when exhausted

    movzx rax, byte [rsi]       ; Load one character
    inc rsi

    ; Handle CRLF: ignore LF immediately following CR
    test r15, r15
    jz check_line_endings

    xor r15, r15
    cmp al, 10
    je char_loop

check_line_endings:
    cmp al, 10                  ; Unix LF
    je handle_newline

    cmp al, 13                  ; Windows CR
    je handle_cr

    ; NUMERIC CONVERSION AND VALIDATION
    mov r14, 1                  ; Current line contains a character

    test r13, r13
    jz char_loop                ; Ignore further characters if already invalid

    ; Accept only ASCII digits
    cmp al, '0'
    jl mark_invalid

    cmp al, '9'
    jg mark_invalid

    ; Convert ASCII digit into its numeric value
    sub al, '0'
    movzx r9, al

    ; Check the accumulator BEFORE multiplication
    ; A valid next value cannot exceed 100
    cmp rbx, 10
    ja mark_invalid
    jne accumulate_digit

    ; If accumulator is 10, only digit 0 can produce a valid value
    test r9, r9
    jnz mark_invalid

accumulate_digit:
    ; Accumulate: value = (value * 10) + digit
    imul rbx, rbx, 10
    add rbx, r9
    inc r10                     ; Count digits

    jmp char_loop

mark_invalid:
    xor r13, r13                ; Mark current record invalid
    jmp char_loop

handle_cr:
    mov r15, 1                  ; Ignore following LF if present

handle_newline:
    call evaluate_record

    ; Reset parser state for the next record
    xor rbx, rbx
    xor r10, r10
    mov r13, 1
    xor r14, r14

    jmp char_loop

eof_reached:
    ; Close input file
    mov rax, 3                  ; sys_close
    mov rdi, r12
    syscall

    ; Process last record if it did not end with a newline
    test r14, r14
    jz print_results

    call evaluate_record
    jmp print_results

; SUBROUTINE: evaluate_record
; Updates counters and statistics for one completed record
evaluate_record:
    inc qword [total_records]

    ; Empty records are invalid
    test r10, r10
    jz .invalid

    ; Reject records containing non-digit characters
    test r13, r13
    jz .invalid

    ; Confirm value is within range 0-100
    cmp rbx, 0
    jl .invalid

    cmp rbx, 100
    jg .invalid

    ; Update valid measurement statistics
    inc qword [valid_count]
    add qword [valid_sum], rbx

    ; Update minimum
    cmp rbx, [min_val]
    jge .check_max
    mov [min_val], rbx

.check_max:
    ; Update maximum
    cmp rbx, [max_val]
    jle .done
    mov [max_val], rbx

.done:
    ret

.invalid:
    inc qword [invalid_count]
    ret

; DISPLAY STATISTICS
print_results:
    mov rsi, lbl_total
    mov rdx, len_total
    call print_stdout

    mov rax, [total_records]
    call print_uint_ln

    mov rsi, lbl_valid
    mov rdx, len_valid
    call print_stdout

    mov rax, [valid_count]
    call print_uint_ln

    mov rsi, lbl_invalid
    mov rdx, len_invalid
    call print_stdout

    mov rax, [invalid_count]
    call print_uint_ln

    ; Handle files with no valid measurements
    cmp qword [valid_count], 0
    je .no_valid_data

    mov rsi, lbl_sum
    mov rdx, len_sum
    call print_stdout

    mov rax, [valid_sum]
    call print_uint_ln

    mov rsi, lbl_min
    mov rdx, len_min
    call print_stdout

    mov rax, [min_val]
    call print_uint_ln

    mov rsi, lbl_max
    mov rdx, len_max
    call print_stdout

    mov rax, [max_val]
    call print_uint_ln

    jmp exit_ok

.no_valid_data:
    mov rsi, lbl_sum
    mov rdx, len_sum
    call print_stdout

    xor rax, rax
    call print_uint_ln

    mov rsi, lbl_min
    mov rdx, len_min
    call print_stdout

    mov rsi, msg_na
    mov rdx, len_na
    call print_stdout

    mov rsi, lbl_max
    mov rdx, len_max
    call print_stdout

    mov rsi, msg_na
    mov rdx, len_na
    call print_stdout

exit_ok:
    mov rax, 60                 ; sys_exit
    xor rdi, rdi
    syscall

; FILE ERROR HANDLING
handle_open_error:
    mov rsi, err_open
    mov rdx, len_err_open
    call print_stderr
    jmp exit_fail

handle_read_error:
    ; Close file before reporting the read error
    mov rax, 3                  ; sys_close
    mov rdi, r12
    syscall

    mov rsi, err_read
    mov rdx, len_err_read
    call print_stderr

exit_fail:
    mov rax, 60                 ; sys_exit
    mov rdi, 1
    syscall

; OUTPUT HELPERS
; Handles partial writes and interrupted write operations
; Input: rdi = file descriptor, rsi = buffer, rdx = byte count
print_stdout:
    mov rdi, 1
    jmp write_all

print_stderr:
    mov rdi, 2
    jmp write_all

write_all:
.write_loop:
    test rdx, rdx
    jz .done

    mov rax, 1                  ; sys_write
    syscall

    test rax, rax
    jg .write_progress

    cmp rax, -4                 ; EINTR
    je .write_loop

    ret                         ; Stop on other write errors

.write_progress:
    add rsi, rax
    sub rdx, rax
    jmp .write_loop

.done:
    ret

; SUBROUTINE: print_uint_ln
; Converts unsigned integer in RAX to decimal ASCII and prints it
print_uint_ln:
    push rbx

    mov rcx, 10
    lea rsi, [num_buf + 31]
    mov byte [rsi], 10          ; Append newline

    mov rbx, 1                  ; Initial length includes newline

.div_loop:
    xor rdx, rdx
    div rcx                     ; Divide by 10

    add dl, '0'
    dec rsi
    mov [rsi], dl
    inc rbx

    test rax, rax
    jnz .div_loop

    mov rdx, rbx
    call print_stdout

    pop rbx
    ret


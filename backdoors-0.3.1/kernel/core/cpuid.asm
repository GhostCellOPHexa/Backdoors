; SPDX-License-Identifier: GPL-3.0-or-later
;
; Suporte a CPUID para i386/i686.
;   uint32_t cpu_has_cpuid(void)                      (1 se CPUID existe)
;   void     cpu_cpuid(uint32_t leaf, uint32_t out[4]) (EAX, EBX, ECX, EDX)

BITS 32

global cpu_has_cpuid
global cpu_cpuid

section .text

; CPUID existe se o bit 21 (ID) do EFLAGS puder ser alterado.
cpu_has_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x00200000
    push eax
    popfd
    pushfd
    pop eax
    push ecx                     ; restaura EFLAGS original
    popfd
    xor eax, ecx
    test eax, eax
    setnz al
    movzx eax, al
    ret

cpu_cpuid:
    push ebx
    push esi
    mov eax, [esp + 12]          ; leaf
    cpuid
    mov esi, [esp + 16]          ; out
    mov [esi + 0], eax
    mov [esi + 4], ebx
    mov [esi + 8], ecx
    mov [esi + 12], edx
    pop esi
    pop ebx
    ret

section .note.GNU-stack noalloc noexec nowrite progbits

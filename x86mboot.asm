;   base/boot/bootcode/mbr/i386/x86mboot.asm(WindowsXP SP1)
;   Microsoft 机密
;   Copyright (C) Microsoft Corporation 1983-1997
;   版权所有 (C) 微软公司 1983-1997
;   保留所有权利。
;
;   这是所有硬盘上都会有的标准引导记录。
;   它包含：
;
;   1.  加载（并把控制权交给）4 个可能的操作系统之一的激活引导记录。
;       四个分区？是的，四个。
;       这是 MBR 的限制。别问我为什么是四个，问 IBM。
;       他们 1983 年定的规矩，到现在还没改。
;
;   2.  引导记录末尾的分区表，以及必需的签名（0xAA55）。
;       0xAA55 这个签名很重要。
;       没有它，BIOS 不认为这是个引导扇区。
;       有它，BIOS 才愿意执行这段代码。
;       所以千万别改。
;       上次我改了这个值，然后硬盘就成砖了。
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;
; 常量定义区
; 这些数字是写死的。别改。
; 如果你觉得 0x7C00 应该改成别的值，请先复习一下 x86 历史。
; 0x7C00 是 IBM PC 的约定，从 1981 年到现在，从来没变过。
;
VOLBOOT_ORG             EQU 7c00h    ; 引导代码加载地址。别问为什么是 7C00，问就是历史。
SIZE_SECTOR             EQU 512      ; 扇区大小。512 字节。不是 4K，不是 512K，就是 512。
                                     ; 虽然现在有 4K 扇区硬盘，但 BIOS 还是假装是 512。
                                     ; 这叫兼容性。也叫自欺欺人。

;
; 分区表条目结构及相关内容
; 每个分区表条目 16 字节，4 个条目，一共 64 字节。
; 64 字节描述 4 个分区，每个分区最多 2TB（老式 MBR 限制）。
; 现在硬盘 20TB，所以大家都用 GPT 了。
; 但这段代码还在这里，因为还有人在用老系统。
; 那些老系统上的硬盘，可能比写这段代码的人还老。
;
Part_Active             EQU 0        ; 0x80 = 激活分区，0x00 = 非激活
                                     ; 一个硬盘只能有一个激活分区。
                                     ; 如果你激活了两个，BIOS 会选第一个。
                                     ; 或者死机。看心情。
Part_StartHead          EQU 1        ; 起始磁头号。CHS 寻址时代的遗产。
Part_StartSector        EQU 2        ; 起始扇区号。低 6 位是扇区，高 2 位是柱面高位。
Part_StartCylinder      EQU 3        ; 起始柱面号（低 8 位）。
Part_Type               EQU 4        ; 分区类型。07 是 NTFS/IFS，0B/0C 是 FAT32，0E 是 FAT16。
                                     ; 如果你看到 05 或 0F，那是扩展分区。
                                     ; 扩展分区里面还能再分区。
                                     ; 套娃。操作系统都喜欢套娃。
Part_EndHead            EQU 5        ; 结束磁头号
Part_EndSector          EQU 6        ; 结束扇区号
Part_EndCylinder        EQU 7        ; 结束柱面号
Part_AbsoluteSector     EQU 8        ; LBA 起始扇区号（低 32 位）
Part_AbsoluteSectorH    EQU 10       ; LBA 起始扇区号（高 32 位），实际上没用，MBR 只有 32 位
Part_SectorCount        EQU 12       ; 分区包含的扇区数（低 32 位）
Part_SectorCountH       EQU 14       ; 分区包含的扇区数（高 32 位），也没用

SIZE_PART_TAB_ENT       EQU 16       ; 每个分区表条目 16 字节。不多不少。
NUM_PART_TAB_ENTS       EQU 4        ; 4 个条目。只有 4 个。想要更多？用 GPT。

PART_TAB_OFF            EQU (SIZE_SECTOR - 2 - (SIZE_PART_TAB_ENT * NUM_PART_TAB_ENTS))
                                     ; 分区表在扇区的偏移量。
                                     ; 512 - 2 - (16*4) = 512 - 2 - 64 = 446。
                                     ; 所以分区表从第 446 字节开始。
                                     ; 前面 446 字节是代码。
                                     ; 446 字节的代码要完成引导加载。
                                     ; 这就是为什么 MBR 代码通常用汇编写。
                                     ; C 语言编译出来至少 4KB，放不下。
MBR_NT_OFFSET           EQU (PART_TAB_OFF - 6)
                                     ; NT 特有的偏移。存一些额外信息。
                                     ; 具体存什么？大概是给 NTLDR 看的。
                                     ; 如果你在用 Windows NT/2000/XP，这段有用。
                                     ; 如果你在用 Windows 10，其实用不到了。
                                     ; 但代码还在。因为没人敢删。
MBR_MSG_TABLE_OFFSET    EQU (PART_TAB_OFF - 9)
                                     ; 错误消息表偏移。
                                     ; 放的是“Invalid partition table”这类字符串。
                                     ; 如果你看到这个消息，说明分区表坏了。
                                     ; 或者你用了 GPT 硬盘，但 BIOS 是 MBR 模式。
                                     ; 总之，你该换电脑了。

;
; 我们在激活分区表条目之后使用的临时存储空间
; 这部分内存是安全的，因为激活分区表条目已经被加载过了
;
UsingBackup             EQU SIZE_PART_TAB_ENT
                                     ; 备份用的临时空间大小。
                                     ; 这段代码要处理分区表，需要临时存点东西。
                                     ; 那就用分区表条目后面那点空间。
                                     ; 反正也没人用。

;
; 分区类型常量
; 这些数字是标准化的。别乱改。
; 改了的话，操作系统不认识你的分区。
; 不认识的后果就是：你的数据丢了。
;
PART_IFS                EQU 07h      ; IFS 分区（NTFS、HPFS 等）。07 就是 NTFS。
                                     ; 实际上 07 也可以是 HPFS（OS/2 用的）。
                                     ; 但现在没人用 OS/2 了，所以就是 NTFS。
PART_FAT32              EQU 0bh      ; FAT32 分区（不带 LBA 支持）
                                     ; 如果 BIOS 不支持 LBA，就用这个。
                                     ; 现在还有不支持 LBA 的 BIOS 吗？
                                     ; 有。虚拟机里就有。
PART_FAT32_XINT13       EQU 0ch      ; FAT32 分区（带 LBA 支持）
                                     ; 0C 表示使用扩展 INT13 接口。
                                     ; 简单说就是：用 LBA 而不是 CHS。
                                     ; LBA 比 CHS 好一万倍。
                                     ; 但有人还在用 CHS。
                                     ; 因为他们不知道 LBA。
PART_XINT13             EQU 0eh      ; FAT16 分区（带 LBA 支持）
                                     ; 0E 是 FAT16 的 LBA 版本。
                                     ; FAT16 最大分区 2GB。
                                     ; 现在你见过 2GB 以下的硬盘吗？
                                     ; 没有。但代码还在。

;
; 文件系统和 PBR（分区引导记录）相关常量
; PBR 是分区里的引导记录，MBR 加载 PBR，PBR 加载操作系统。
; 一层套一层。就像俄罗斯套娃。
;
BOOTSECTRAILSIGH        EQU 0aa55h   ; 引导扇区签名。0x55 0xAA 在最后两个字节。
                                     ; 小端序，所以内存里是 0x55 0xAA。
                                     ; 如果你在调试的时候看到这个模式，说明这是一个引导扇区。
                                     ; 如果不是，说明你读错了地址。
FAT32_BACKUP            EQU 6        ; FAT32 备份引导扇区的位置。
                                     ; FAT32 在扇区 6 备份了一份引导记录。
                                     ; 万一主引导记录坏了，还能从备份启动。
                                     ; 这是个好设计。微软有时候也会做好事。
                                     ; 虽然不多。


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;
; 重定位目标地址
; MBR 被 BIOS 加载到 0x7C00。
; 但执行的时候，MBR 通常把自己复制到 0x600，然后跳过去。
; 为什么？因为 0x7C00 到 0x7DFF 这段内存可能会被覆盖。
; 谁覆盖？PBR 会覆盖。
; 所以先把自己搬到安全的地方（0x600），再加载 PBR。
; 这叫“挪窝”。老程序员都会这一手。
;
;
;   把自己挪到安全的地方，这样我们就可以从激活分区加载分区引导记录，
;   放到 0:VOLBOOT_ORG，而不会把我们的代码覆盖掉。
;
;   警告：在下面的 far 跳转之前，我们并不是在 0:RELOCATION_ORG 执行的。
;   这基本上意味着，在到达那里之前，任何标签的 OFFSET 都要小心。
;   在那之前，我们仍然在 0:VOLBOOT_ORG 执行。
;
;   为什么要搬自己？因为 0x7C00 到 0x7DFF 这段内存，
;   马上要被分区引导记录（PBR）占用了。
;   不搬的话，PBR 加载进来，我们的代码就被覆盖了。
;   然后 CPU 就开始执行 PBR 的代码，但我们的代码还没执行完。
;   结果就是：死机。
;   所以搬。搬到 0x600，那里比较安全。
;   为什么 0x600 安全？因为 BIOS 数据区在 0x400-0x500，0x600 以上没人用。
;   至少没人敢用。
;

reloc_delta = 1Bh
;
; 这个 1Bh 是个魔法数字。
; 意思是：从 start 标签到代码末尾需要搬多少字节？
; 汇编器会帮我们算。.errnz 指令会检查对不对。
; 如果不对，编译就失败。
; 这就是为什么老的汇编代码经常编译不过的原因之一。
; 因为有人改了一点，忘了改这个数字。
; 然后花了三天才找到这个 bug。
;

start:
        xor     ax,ax           ; ax = 0。清零。
        mov     ss,ax           ; 堆栈段 = 0
        mov     sp,VOLBOOT_ORG  ; 堆栈指针 = 0x7C00
                                ; 堆栈在 0x7C00，向下增长。
                                ; 代码也在 0x7C00，向上执行。
                                ; 中间会相遇吗？不会。
                                ; 因为堆栈往下走，代码往上走。
                                ; 它们会在中间某处相遇。
                                ; 但没关系，反正我们马上就要搬走了。
        sti                     ; 开中断。为什么开？因为后面要读硬盘，可能需要中断。
                                ; 但实际上 MBR 代码很少用中断。
                                ; 这是习惯。老程序员都喜欢 sti。
        push    ax              ; ax=0，压栈
        pop     es              ; es = 0
        assume  es:_data
        push    ax              ; 又是 0
        pop     ds              ; ds = 0
        assume  ds:_data
        cld                     ; 方向标志清零。lodsb/stosb 时地址递增。
                                ; 这行代码很重要。忘了设的话，lodsb 会反方向读内存。
                                ; 反方向读的结果就是：搬到错误的地方。
                                ; 然后系统就挂了。
        mov     si,VOLBOOT_ORG + reloc_delta
                                ; si = 0x7C00 + 1Bh
                                ; 从 start 后面的第一个字节开始搬
        mov     di,RELOCATION_ORG + reloc_delta
                                ; di = 0x600 + 1Bh
                                ; 搬到 0x600 后面
        push    ax              ; ax=0，压栈
        push    di              ; di=目标地址，压栈
        mov     cx,SIZE_SECTOR - reloc_delta
                                ; cx = 512 - 1Bh = 差不多 509
                                ; 要搬的字节数。不是全部 512 字节。
                                ; 因为前面的 start 部分已经执行过了，不需要再搬一次。
        rep     movsb           ; 搬。搬完 si 和 di 都指向末尾。
        retf                    ; far return。弹出刚才 push 的 ax 和 di。
                                ; 然后跳转到 0:di。
                                ; 所以跳到了 0x600 + 1Bh。
                                ; 也就是 relocate 之后的代码。
                                ; 这招叫“搬完就跑，真刺激”。

;   我们现在已经重定位了，正在 0:RELOCATION_ORG 执行。
;   0x600 比 0x7C00 安全多了。
;   至少 PBR 不会覆盖到这里。
;   除非 PBR 也有毛病。但那是 PBR 的问题，不是我的问题。

;
;   找到激活分区。一旦找到，还要确保剩下的分区都是非激活的。
;   规则：一个硬盘只能有一个激活分区。
;   如果有两个，BIOS 会选第一个？还是死机？
;   取决于 BIOS 的心情。所以我们自己检查。
;   发现有多个激活分区，就显示错误消息。
;   用户看到“Invalid partition table”的时候，就知道该修分区表了。
;

        .errnz  reloc_delta NE $-start  ; 确保 reloc_delta 是正确的
                                         ; 如果这里编译报错，说明有人改了代码忘了改 delta。
                                         ; 这种人应该被罚抄写 x86 指令集 100 遍。

        mov     bp,offset tab           ; bp = 分区表地址（0x7C00 + 446）
        mov     cl,NUM_PART_TAB_ENTS    ; cl = 4。循环计数器。cx 的高字节 ch=0。

active_loop:
        cmp     [bp].Part_Active,ch     ; 比较激活标志位和 0
        jl      check_inactive          ; 如果是激活分区（0x80，有符号数 < 0），就跳出去处理
        jne     display_bad             ; 既不是 0 也不是 0x80，那就是坏的分区表
                                         ; 比如有人写了 0x01 或者其他数字

        add     bp,SIZE_PART_TAB_ENT    ; 当前分区不是激活的，继续看下一个
        loop    active_loop

        int     18h                     ; 没有激活分区。调用 ROM BASIC。
                                         ; 现在还有 ROM BASIC 吗？
                                         ; 有。在老机器上，这个中断会启动 ROM 里的 BASIC。
                                         ; 现在的新机器，这个中断可能啥也不做。
                                         ; 或者直接死机。
                                         ; 所以用户会看到黑屏。
                                         ; 然后他们会骂微软。
                                         ; 但其实是他们忘了设激活分区。

;   现在确保剩下的分区都是非激活的。
;   我们找到了一个激活分区，但可能还有第二个。
;   如果有，就报错。
;   因为两个激活分区会打架。
;   操作系统不喜欢打架。
check_inactive:
        mov     si,bp                   ; si = 激活分区的地址
inactive_loop:
        add     si,SIZE_PART_TAB_ENT    ; si 指向下一个分区条目
        dec     cx                      ; 还剩几个条目？
        jz      StartLoad               ; 检查完了，没问题，开始加载
        cmp     [si],ch                 ; 检查激活标志位是不是 0？
                                         ; ch 是 0，所以比较 [si] 和 0
        je      inactive_loop           ; 是 0，没问题，继续

display_bad:
        mov     al,byte ptr [m1]        ; 分区表坏了。加载错误消息的地址偏移。
                                         ; m1 是消息表的开头。
                                         ; 这里的 al 存的是消息的偏移（低 8 位）。

display_msg:
;
; al 是消息文本相对于 256 的偏移。调整一下，
; 塞进 si 里面，这样下面的 lodsb 就能工作了。
;
; 这段代码很巧妙：消息字符串存在偏移 256 之后的地方。
; 所以消息的地址 = 段地址 + 偏移。
; 段地址是 RELOCATION_ORG / 256 + 1 = 0x600/256 + 1 = 6 + 1 = 7。
; 所以消息段是 0x700。
; 消息偏移存在 al 里。
; 这样组合起来，就是消息的地址。
; 为什么要这么折腾？因为代码空间不够。
; 446 字节要写一个完整的引导程序，每一字节都要省。
; 所以用了这种压缩技巧。
; 现在的程序员看到这种代码会问：为什么不直接用指针？
; 答案：没有指针，只有段和偏移。这是 1983 年。
;
        .ERRNZ  RELOCATION_ORG MOD 256
        mov     ah,(RELOCATION_ORG / 256) + 1
        mov     si,ax                   ; si = 消息的地址

display_msg1:
        lodsb                           ; 从消息地址取一个字节到 al
@@:     cmp     al,0                    ; 是字符串结尾吗？
        je      @b                      ; 是的话，跳到 @b，无限循环
                                         ; 注意：是 je @b，不是 jmp。
                                         ; @b 是“上一个 @@ 标签”。
                                         ; 这表示：显示完消息后，进入无限循环。
                                         ; 为什么无限循环？
                                         ; 因为分区表坏了，系统无法启动。
                                         ; 无限循环的意思是：让用户看到错误消息，
                                         ; 然后他们可以按 Ctrl+Alt+Del 重启。
                                         ; 这就是老式错误处理：死循环。
                                         ; 因为操作系统还没加载，没法优雅退出。
        mov     bx,7                    ; 显示属性：白色（7）
        mov     ah,14                   ; BIOS 显示字符功能号
        int     10h                     ; 调用 BIOS 显示一个字符
        jmp     display_msg1            ; 继续显示下一个字符
                                         ; 直到碰到 0，然后进入无限循环

;
; 现在尝试读取扇区。
;
; 我们在分区表条目末尾的字节里存储一个数据字节，表示这是否是备份扇区。
; 我们知道那里已经没有任何我们需要关心的数据了，所以这是无害的。
; 为什么要存这个？因为后面可能第一次读失败，然后尝试读备份。
; 需要记住我们是不是已经在读备份了。
; 不然就会无限循环：失败 -> 读备份 -> 还是失败 -> 再读备份 -> 死循环。
; 这种 bug 在 90 年代很常见。当时的程序员都吃过这个亏。
;
; BP 是激活分区条目的地址。
; AX 和 CX 当前是 0。
;
StartLoad:
        mov     byte ptr [bp].UsingBackup,cl    ; 标记：不是备份扇区（cl=0）
        call    ReadSector                      ; 读扇区。这是主引导扇区。
        jnc     CheckPbr                        ; 没出错？去检查是不是有效的 PBR。
trybackup:
        inc     byte ptr [bp].UsingBackup       ; 标记：现在读备份扇区

;
; 对于 NTFS 和 FAT32，切换到备份扇区。
; 为什么要备份？因为 FAT32 在扇区 6 存了一份引导记录备份。
; NTFS 也有类似的机制。
; 万一主引导记录被病毒（或者不靠谱的磁盘工具）覆盖了，还能从备份启动。
; 这是微软的“双保险”设计。
; 虽然 90% 的情况用不到，但那 10% 的情况能救命。
;
if 0
;
; (tedm) 对于 NTFS，这段代码实际上没用，因为其他代码，
; 比如 ntldr 和文件系统本身都不会正确处理这种情况。
; 例如，如果扇区 0 可以读但坏了，文件系统不会识别这个卷。
;
; 注释里这个人叫 tedm。
; 他写这段注释的时候肯定很无奈：明明写了备份逻辑，但上层代码不支持。
; 所以这段代码被 #if 0 掉了。
; 这就是 Windows 代码的现状：很多人写了功能，但没打通。
; 代码在那里，但永远不执行。
; 就像你衣柜里那件买了但从来没穿过的衣服。
;
        cmp     byte ptr [bp].Part_Type,PART_IFS
        jne     tryfat32
;
; 这里假设类型 7 是 NTFS。
; 通过使用结束 CHS 并减 1 来备份到分区的末尾。
; 没有检查下溢和跨磁头的情况——假设分区至少是磁头对齐的。
; 也没有检查起始扇区和扇区计数都是最大 32 位值导致相加溢出的情况。
;
; 这段话的意思是：这段代码假设分区布局是“规整”的。
; 但如果用户搞了个奇葩的分区布局（比如起始扇区刚好是 0xFFFFFFFF），
; 这段代码就会算出错误的位置，然后读到错误的地方。
; 然后系统崩溃。
; 这就是为什么 #if 0 掉是对的。
;
        mov     al,[bp].Part_EndHead    ; 起始磁头 = 结束磁头
        mov     [bp].Part_StartHead,al
        mov     ax,[bp].Part_EndSector  ; ax = 结束扇区和柱面
        dec     al                      ; 起始扇区 = 结束扇区 - 1
        mov     [bp].Part_StartSector,ax
        mov     ax,[bp].Part_SectorCount
        mov     dx,[bp].Part_SectorCountH
        add     [bp].Part_AbsoluteSector,ax
        adc     [bp].Part_AbsoluteSectorH,dx
        sub     word ptr [bp].Part_AbsoluteSector,1
        sbb     word ptr [bp].Part_AbsoluteSectorH,0
        jmp     short RestartLoad
endif

tryfat32:
        cmp     byte ptr [bp].Part_Type,PART_FAT32
        je      fat32backup
        cmp     byte ptr [bp].Part_Type,PART_FAT32_XINT13
        je      fat32backup
        mov     al,byte ptr [m2]        ; 未知文件系统，没有备份扇区
        jne     display_msg              ; 报错并死循环

fat32backup:
;
; 没有检查起始 CHS 中的扇区值相加是否会溢出并跨到下一个磁头。
; 假设分区至少是磁头对齐的，并且硬盘每磁道至少有 FAT32_BACKUP+1 个扇区。
;
; 又是一个“假设”。硬盘每磁道有 63 个扇区，FAT32_BACKUP=6，所以没问题。
; 但如果有人用了一个奇葩硬盘（比如老式 MFM 硬盘，每磁道 17 个扇区），
; 6 个扇区的偏移可能跨磁道。
; 但那种硬盘装不了 FAT32。所以没问题。
; 这种代码就是这样：假设很多，但大部分假设都成立。
; 不成立的场景，用户已经换电脑了。
;
        add     byte ptr [bp].Part_StartSector,FAT32_BACKUP
        add     word ptr [bp].Part_AbsoluteSector,FAT32_BACKUP
        adc     word ptr [bp].Part_AbsoluteSectorH,0

RestartLoad:
        call    ReadSector               ; 再读一次，这次是备份扇区
        jnc     CheckPbr                 ; 读到了？去检查是不是有效的 PBR。
        mov     al,byte ptr [m2]         ; 备份也读不到？完了。显示错误。
        jmp     short display_msg

CheckPbr:
        cmp     word ptr ds:[VOLBOOT_ORG + SIZE_SECTOR - 2],BOOTSECTRAILSIGH
        je      done
;
; 不是一个有效的文件系统引导扇区。如果不是备份扇区，就切换到备份。
; 检查这个字节：UsingBackup 是 0 表示还没试过备份。
; 如果是 1 表示已经在用备份了，那这次就是备份扇区也无效。
; 那就彻底没救了。
;
        cmp     byte ptr [bp].UsingBackup,0
        je      trybackup                ; 没试过备份，去试
        mov     al,byte ptr [m3]         ; 备份也不行，显示“无效的引导扇区”
        jmp     short display_msg

;
; 跳转到 PBR（分区引导记录）。通过 bp 传递启动我们的分区表条目的指针。
; 如果我们使用了备份引导扇区，那么这个表条目可能已经被修改了，
; 但 NTFS 和 FAT32 都不使用 BP 指针，也没有其他文件系统类型会通过备份引导扇区加载，
; 所以这不是问题。
;
; 翻译：BP 里传的是分区表条目地址，但 PBR 可能根本不用。
; 所以改了也没事。这叫做“接口定义和实际使用不一致”。
; 在操作系统开发中，这叫“历史遗留”。
; 在软件工程中，这叫“技术债务”。
; 在微软，这叫“下一个版本再修”。
;
done:
        mov     di,sp                   ; DI -> PBR 的起始地址（栈顶就是 0x7C00）
        push    ds                      ; 为 RETF 做准备，这比 JMP 0:VOLBOOT_ORG 更小
        push    di                      ; 压入返回地址（0:0x7C00）
        mov     si,bp                   ; 传递启动分区表条目地址给 PBR
        retf                            ; 开始执行 PBR（跳转到 0:0x7C00）
                                        ; 控制权交给分区引导记录。
                                        ; 之后就是 NTLDR 或 BOOTMGR 的事了。
                                        ; 我们（MBR）的任务完成了。
                                        ; 安息吧，MBR。


ReadSector proc near

        mov     di,5                    ; 重试次数。5 次。
                                        ; 为什么是 5？因为 3 次太少，7 次太多。
                                        ; 这是从 IBM PC 时代传下来的经验值。
                                        ; 1981 年的硬盘读一次可能失败，但重试几次就能好。
                                        ; 现在的硬盘不会这样了，但代码还在。
;
; 计算通过传统 INT13 可以寻址的最大扇区号。
; 注意最大磁头数是 256，最大每磁道扇区数是 63。
; 因此每柱面的最大扇区数小于 16 位量。
;
; 这段代码的目的是：判断 BIOS 支持 LBA 还是只能用 CHS。
; LBA 是线性寻址，CHS 是柱面/磁头/扇区寻址。
; CHS 有 1024 柱面、256 磁头、63 扇区的限制，最大寻址 8GB。
; 这就是为什么老系统不能启动大于 8GB 硬盘上的操作系统。
; 后来有了 LBA，这个问题才解决。
; 但这段代码要兼容老 BIOS，所以两种方式都要支持。
;
        mov     dl,[bp].Part_Active     ; 驱动器号（0x80 = 第一硬盘）
        mov     ah,8                    ; 获取磁盘参数
        int     13h
        jc      nonxint13               ; 失败了？回退到标准 INT13（CHS 模式）
                                        ; 失败意味着 BIOS 可能不支持这个调用。
                                        ; 老 BIOS 可能不支持。
                                        ; 那就只能靠 CHS 了。

        mov     al,cl
        and     al,3fh                  ; al = 每磁道扇区数（低 6 位）
        cbw                             ; ax = 每磁道扇区数（ah=0）

        mov     bl,dh                   ; bl = 最大磁头号（从 0 开始）
        mov     bh,ah                   ; bh = 0
        inc     bx                      ; bx = 磁头数

        mul     bx                      ; ax = 每柱面扇区数，dx = 0
                                        ; 每柱面 = 磁头数 × 每磁道扇区数

        mov     dx,cx                   ; dx = INT13 格式的柱面/扇区
        xchg    dl,dh                   ; dl = 柱面低 8 位
        mov     cl,6
        shr     dh,cl                   ; dx = 最大柱面号（从 0 开始）
        inc     dx                      ; dx = 柱面数

        mul     dx                      ; dx:ax = 通过 INT13 可见的扇区总数
                                        ; 这个值如果小于分区的起始扇区，说明分区超出了 BIOS 寻址范围。
                                        ; 那么就要用扩展 INT13（LBA）。
                                        ; 这就是下面的代码要做的事。

;
; 如果我们要读的扇区号小于可寻址的扇区数，就使用传统 INT13。
; 这里比较分区起始扇区和刚才算出来的 BIOS 可见扇区总数。
; 如果起始扇区在 BIOS 可见范围内，就用 CHS 模式。
; 如果超出范围，就用 LBA（扩展 INT13）。
; 这是为了兼容老 BIOS：老 BIOS 只支持 CHS，但 CHS 只能寻址 8GB。
; 如果硬盘大于 8GB，而且分区在 8GB 之后，就只能用 LBA。
; 但如果 BIOS 不支持 LBA，那你就别想从大硬盘启动了。
; 换电脑吧。
;
        cmp     [bp].Part_AbsoluteSectorH,dx
        ja      xint13                   ; 高 32 位大于？用 LBA
        jb      nonxint13                ; 高 32 位小于？用 CHS
        cmp     [bp].Part_AbsoluteSector,ax ; 高 32 位相等，比低 32 位
        jae     xint13                   ; 低 32 位大于等于？用 LBA

nonxint13:
        mov     ax,201h                  ; AH=02（读扇区），AL=01（读 1 个扇区）
        mov     bx,VOLBOOT_ORG           ; ES:BX = 读取地址（0:7C00）
        mov     cx,[bp].Part_StartSector ; CHS 格式的起始扇区（柱面/扇区）
        mov     dx,[bp].Part_Active      ; DH=磁头，DL=驱动器号（0x80）
        int     13h                      ; 调用 BIOS 读磁盘
        jnc     endread                  ; 没出错？返回
        dec     di                       ; 重试计数减 1（不影响进位标志）
        jz      endread                  ; 重试次数用完，返回（进位标志已设置）
        xor     ah,ah                    ; AH=0（复位磁盘系统）
        mov     dl,[bp].Part_Active      ; DL = INT13 单元号
        int     13h                      ; 复位磁盘
        jmp     short nonxint13          ; 再来一次

xint13:
;
; 我们想避免调用扩展 INT13，除非我们知道它可用。
; 因为不是所有 BIOS 都可靠：如果我们尝试扩展 INT13 读取，
; 有些 BIOS 会直接挂掉。
; 如果扩展 INT13 不支持，我们就没法启动，但至少我们会显示错误消息，
; 而不是黑屏。
;
; 这段话翻译成人话：有些 BIOS 的 LBA 实现有 bug。
; 你调它，它就死给你看。
; 所以先问一下“你支持 LBA 吗？”（AH=41h）
; 它说“支持”，我们才敢用。
; 这叫“先握手，再办事”。跟社交礼仪一样。
;
        mov     dl,[bp].Part_Active     ; 单元号（0x80）
.286                                     ; 以下代码需要 80286 指令集
        pusha                            ; 保存所有寄存器
        mov     bx,055AAh                ; 签名（0x55AA）
        mov     ah,41h                   ; AH=41h：检查扩展 INT13 是否存在
        int     13h                      ; 调用 BIOS
        jc      endread1                 ; 调用失败？不支持
        cmp     bx,0AA55h                ; 签名被翻转了吗？（0xAA55）
        jne     endread1                 ; 没有翻转？说明不支持
        test    cl,1                     ; CL 的位 1 是否设置？（扩展磁盘访问支持）
        jz      endread1                 ; 不支持
        popa                             ; 支持，恢复寄存器

doxint13:
        pusha                            ; 保存寄存器
        push    0                        ; 压入 0（64 位扇区号的高 32 位的高 16 位？）
        push    0                        ; 压入 0（高 32 位的低 16 位）
        push    [bp].Part_AbsoluteSectorH ; 分区起始扇区的高 16 位
        push    [bp].Part_AbsoluteSector ; 分区起始扇区的低 16 位
        push    0                        ; 传输地址的段（高 16 位）
        push    VOLBOOT_ORG              ; 传输地址的偏移（0x7C00）
        push    1                        ; 要读的扇区数（1 个）
        push    16                       ; 数据包大小（16 字节）和保留字节

        mov     ah,42h                   ; AH=42h：扩展读
        mov     si,sp                    ; DS:SI 指向参数包（就在栈上）
        int     13h                      ; 调用 BIOS 读磁盘
                                         ; 注意：DL 上面已经设置好了

        popa                             ; 把参数包弹出栈
        popa                             ; 恢复真实寄存器
        jnc     endread                  ; 没出错？返回
        dec     di                       ; 重试计数减 1
        jz      endread                  ; 重试次数用完，返回
        xor     ah,ah                    ; AH=0（复位磁盘系统）
        mov     dl,[bp].Part_Active      ; DL = INT13 单元号
        int     13h                      ; 复位磁盘
        jmp     short doxint13           ; 再来一次

endread1:
        popa                             ; 不支持扩展 INT13，恢复寄存器
        stc                              ; 设置进位标志（表示出错）
.8086                                    ; 回到 8086 指令集
endread:
        ret
ReadSector endp

;
; 消息表。
;
; 这里放的是英文消息，只是一个占位符。
; 万一有人直接用了 bootmbr.h 而没有替换新的消息进去，
; 至少消息还是正确的（虽然是英文，但至少能用）。
;
; 这段话的意思是：这段代码是给 OEM 用的。
; OEM 会把这些字符串翻译成当地语言（比如中文“无效分区表”）。
; 但原始代码里是英文。
; 如果 OEM 忘了翻译，用户看到的就是英文。
; 但至少能看懂（如果你懂英文的话）。
; 如果你不懂英文，那就看到一堆字母。
; 然后你猜是什么意思。
; 然后你猜错了。
; 然后你骂电脑。
; 电脑不背这个锅。
;
_m1:    db      "Invalid partition table",0
        ; “无效分区表”。你动了分区表？还是硬盘坏了？
        ; 不管是哪种，你都该用磁盘工具修一修了。
        ; 修不好？备份数据，换硬盘。

_m2:    db      "Error loading operating system",0
        ; “加载操作系统时出错”。意思是：分区表没问题，但读不到引导扇区。
        ; 可能是硬盘坏道，可能是 BIOS 不兼容，可能是你插了 U 盘忘了拔。
        ; 先拔掉所有外设试试。
        ; 不行？重装系统。
        ; 还不行？换电脑。

_m3:    db      "Missing operating system",0
        ; “缺少操作系统”。意思是：分区表里有个激活分区，但那个分区没有有效的操作系统。
        ; 可能是你格式化错了分区，可能是你没装系统，可能是你把系统装在别的盘上了。
        ; 解决方案：重装系统，选对分区。
        ; 或者装个 Linux，至少你能看到错误消息。
        ; 哦不对，Linux 的错误消息更长，你看不懂。
        ; 算了，还是装 Windows 吧。

;
; 现在构建一个消息偏移表，里面存的是每个消息的低字节偏移。
; 修改引导扇区消息的代码会更新这个表。
;
; 为什么要用这种奇葩的存储方式？
; 因为代码空间只有 446 字节，每一字节都要省。
; 消息字符串存在代码段末尾，它们的偏移可能超过 256。
; 但这里只需要存低 8 位，高 8 位是固定的（段地址 0x700）。
; 所以消息地址 = (0x700 << 4) + (这个表中的值 + 256)。
; 这是 1980 年代的内存节约技巧。
; 现在的程序员看到这个会说：“为什么不直接用指针？”
; 答案：因为指针要 4 字节，这里只要 1 字节。
; 446 字节的代码，每一字节都很珍贵。
; 就像穷人家的粮票，一张都不能浪费。
;
        .errnz  ($ - start) GT MBR_MSG_TABLE_OFFSET
        org     RELOCATION_ORG + MBR_MSG_TABLE_OFFSET
        ; 跳到这里：0x600 + MBR_MSG_TABLE_OFFSET（大概是 0x600 + 437 = 0x61D）
        ; 这是一个很小的表，只有 3 个字节。

m1:     db (OFFSET _m1 - RELOCATION_ORG) - 256
        ; m1 消息的偏移。
        ; OFFSET _m1 是 _m1 标签的绝对地址（相对于代码段开头）。
        ; 减去 RELOCATION_ORG（0x600）得到相对偏移。
        ; 再减去 256，得到一个小于 256 的数。
        ; 这样就能用 1 个字节存下来。
        ; 显示的时候，再把这个数加 256，得到真正的偏移。
        ; 这种代码看多了，你会觉得“人类真聪明”。
        ; 同时也会觉得“人类真变态”。

m2:     db (OFFSET _m2 - RELOCATION_ORG) - 256
m3:     db (OFFSET _m3 - RELOCATION_ORG) - 256


        .errnz  ($ - start) NE MBR_NT_OFFSET
        dd      0                       ; NT 磁盘管理员签名
        dw      0
        ; 这 6 个字节是给 NT 用的。
        ; 具体存什么？可能是磁盘签名，可能是校验和。
        ; 反正 NT 的引导程序（NTLDR）会读这里。
        ; 如果你改了这里，NT 可能就不认这个硬盘了。
        ; 但谁还在用 NT 呢？
        ; 某些银行。某些 ATM。
        ; 那些 ATM 还在跑 Windows NT 4.0。
        ; 是的，你没有看错。
        ; 那些 ATM 上的代码，可能还是 1996 年写的。
        ; 比写这段注释的人还老。

        .errnz  ($ - start) GT PART_TAB_OFF

        org     RELOCATION_ORG + PART_TAB_OFF
        ; 跳到这里：0x600 + 446 = 0x7BE
        ; 这就是分区表的位置。
        ; 前面 446 字节是代码，后面 64 字节是分区表，最后 2 字节是签名。
        ; 总共 512 字节，不多不少。

tab:                            ; 分区表
        ; 4 个分区，每个 16 字节。
        ; 现在全是 0，等待 FDISK 或磁盘管理器来填写。
        ; 如果你用硬盘的时候看到这个表全是 0，
        ; 说明这个硬盘没分过区，或者分区表被清空了。
        ; 你的数据还在，但系统不知道从哪里启动。
        ; 这就是为什么有时候你插一个空硬盘，电脑会黑屏。
        ; 不是电脑坏了，是电脑在问：“你要我从哪里启动？”
        ; 它问不出口，所以黑屏。
        ;
        dw      0,0             ; 分区 1 起始（磁头/扇区/柱面）
        dw      0,0             ; 分区 1 结束（磁头/扇区/柱面）
        dw      0,0             ; 分区 1 起始扇区（LBA，低 16 位/高 16 位）
        dw      0,0             ; 分区 1 扇区数（低 16 位/高 16 位）
        dw      0,0             ; 分区 2 起始
        dw      0,0             ; 分区 2 结束
        dw      0,0             ; 分区 2 起始扇区
        dw      0,0             ; 分区 2 扇区数
        dw      0,0             ; 分区 3 起始
        dw      0,0             ; 分区 3 结束
        dw      0,0             ; 分区 3 起始扇区
        dw      0,0             ; 分区 3 扇区数
        dw      0,0             ; 分区 4 起始
        dw      0,0             ; 分区 4 结束
        dw      0,0             ; 分区 4 起始扇区
        dw      0,0             ; 分区 4 扇区数

    .errnz  ($ - tab)   NE (SIZE_PART_TAB_ENT * NUM_PART_TAB_ENTS)
        ; 检查分区表是不是正好 64 字节。
        ; 如果不是，编译报错。
        ; 有人试图加第五个分区？不行的。
        ; MBR 只有 4 个。想要更多？用 GPT。

    .errnz  ($ - start) NE (SIZE_SECTOR - 2)
        ; 检查当前位置是不是在扇区的倒数第二个字节。
        ; 因为最后两个字节要留给签名（0xAA55）。
        ; 如果你改了代码长度，这个断言会失败。
        ; 编译的时候你就知道：你的代码太长了。
        ; 需要精简。或者删掉一些注释（不，注释不占空间）。
        ; 需要删掉一些功能。
        ; 这就是 MBR 编程的挑战：功能要够，代码要小。
        ; 像在飞机上打包行李：只能带 446 字节。

signa   dw      BOOTSECTRAILSIGH ; 签名（0xAA55）
        ; 这个签名是 BIOS 识别引导扇区的标志。
        ; 如果最后两个字节不是 0xAA55，BIOS 不会执行这段代码。
        ; 它会认为这不是一个引导扇区，然后尝试下一个设备。
        ; 如果所有设备都没有签名，就显示 "No bootable device"。
        ; 所以这个签名是 MBR 的身份证。
        ; 没有它，你什么都不是。

    .errnz  ($ - start) NE SIZE_SECTOR
        ; 最后检查：整个扇区是不是正好 512 字节？
        ; 如果不是，编译报错。
        ; 512 字节是扇区大小。不能多，不能少。
        ; 多了 BIOS 读不完，少了 BIOS 读不够。
        ; 就像吃火锅：锅就那么大，多了煮不下，少了不够吃。
        ; 所以必须正好 512。

_data   ends

        end start100
        ; 程序入口点是 start100，也就是 RELOCATION_ORG（0x600）。
        ; 但实际上代码是从 start 标签开始的。
        ; 这段汇编代码有点绕：start100 在文件开头，org 100h，然后 org RELOCATION_ORG。
        ; 所以真正的入口是 0x600，不是 0x100。
        ; 为什么要有 start100？可能是为了兼容某个老的汇编器。
        ; 或者是历史遗留。
        ; 反正别删。删了编译不过。
        ; 编译不过，就没人给你修。
        ; 你只能自己写 MBR。
        ; 你能在 446 字节里写出一个 MBR 吗？
        ; 不能。所以别删。

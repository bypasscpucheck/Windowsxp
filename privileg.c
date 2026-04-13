// ============================================================================
// 文件头注释（微软标准格式）
// ============================================================================

// /base/ntos/se/privileg.c

/*++
// ^^^
// 微软文档注释开始标记。

Copyright (c) 1989  Microsoft Corporation
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 版权声明：1989 年，微软公司。
// 这个文件比 Windows NT 正式发布（1993 年）还要早 4 年！
// 说明代码是在 NT 开发初期写的，可能是从 OS/2 或微软内部原型项目移植过来的。
//
// 调侃：1989 年，柏林墙还没倒，Game Boy 刚发布，万维网还没发明。
//       写这行代码的大哥 Robert Reichel 现在可能已经当爷爷了。
//       他当时敲下的这行版权声明，30 多年后还在每一份 Windows 副本里躺着。
//       这就是程序员的"永生"——代码比肉体活得更久。

Module Name:
// ^^^^^^^^^^^
// 章节：模块名称

    Privileg.c
//  ^^^^^^^^^^
// 文件名：Privileg.c（注意拼写：Privilege 少了个 e）
// 可能是 DOS 8.3 文件名限制的遗产（PRIVILEG.C 正好 8 个字符）。
// 内容：特权检查（Privilege Check）的实现。
//
// 什么是特权？
//   Windows 里的"权限"（比如"关机"、"修改系统时间"、"加载驱动"）。
//   即使您是管理员，有些操作也需要特定特权才能执行。
//   比如普通管理员不能加载驱动，需要 SeLoadDriverPrivilege。

Abstract:
// ^^^^^^^^
// 章节：摘要

    This Module implements the privilege check procedures.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 翻译："这个模块实现了特权检查过程。"
// 说白了：这个文件里的代码负责判断"您有没有资格干这件事"。

Author:
// ^^^^^^
// 章节：作者

    Robert Reichel      (robertre)     26-Nov-90
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 作者：Robert Reichel
// 邮箱/别名：robertre（微软内部）
// 日期：1990 年 11 月 26 日
//
// 这位老哥是 NT 内核的早期开发者，可能来自 DEC（Digital Equipment Corporation），
// 因为微软当初挖了很多 DEC 的员工来开发 NT。

Environment:
// ^^^^^^^^^^^
// 章节：环境

    Kernel Mode
//  ^^^^^^^^^^^
// 环境：内核模式（不是用户态）

Revision History:
// ^^^^^^^^^^^^^^^^
// 章节：修订历史
// 这里是空的（没有写具体历史），可能被删了或者从来就没填过。

--*/
// 注释结束。

// ============================================================================
// 头文件包含
// ============================================================================
#include "pch.h"
// ^^^^^^^^^^^^^^^
// pch.h = Pre-Compiled Header（预编译头）
// 包含常用的头文件（ntddk.h、ntosp.h、windef.h 等），
// 加快编译速度。Windows 内核编译一次要几小时，预编译头能省很多时间。
//
// 吐槽：pch.h 是 Windows 内核编译的"黑匣子"——里面啥都有，您永远不知道包含了什么。
//       想移植这段代码到 Linux？先得把 pch.h 拆开，然后发现里面包含了 200 个头文件。

// ============================================================================
// 预编译头停止指令
// ============================================================================
#pragma hdrstop
// ^^^^^^^^^^^^^^^
// 告诉编译器："预编译头到这里结束"。
// 这个指令之后的代码不会放进预编译头。
// 主要用于管理哪些头文件要预编译、哪些不要。

// ============================================================================
// 条件编译：代码段分配指令
// ============================================================================
#ifdef ALLOC_PRAGMA
// 如果定义了 ALLOC_PRAGMA（通常是的）

#pragma alloc_text(PAGE, NtPrivilegeCheck)
// 把 NtPrivilegeCheck 函数放到 PAGE 段（可分页代码）。
// 因为特权检查不是时间关键的操作，可以换出到磁盘。

#pragma alloc_text(PAGE, SeCheckPrivilegedObject)
// SeCheckPrivilegedObject：检查是否有特权操作某个对象（比如文件、进程）。

#pragma alloc_text(PAGE, SepPrivilegeCheck)
// SepPrivilegeCheck：下面要定义的函数，特权检查的核心。

#pragma alloc_text(PAGE, SePrivilegeCheck)
// SePrivilegeCheck：对外的 API，其他模块调用这个函数检查特权。

#pragma alloc_text(PAGE, SeSinglePrivilegeCheck)
// SeSinglePrivilegeCheck：检查单个特权（不需要传数组）。

#endif
// 结束条件编译。

// ============================================================================
// 分页符
// ============================================================================

// 换页符，表示"下面是一个新函数"。

// ============================================================================
// 函数头：SepPrivilegeCheck（内核特权检查核心函数）
// ============================================================================
BOOLEAN
// ^^^^^^^
// 返回值类型：BOOLEAN
//   TRUE = 拥有所需的所有特权
//   FALSE = 至少缺一个特权

SepPrivilegeCheck(
// ^^^^^^^^^^^^^^^^
// 函数名：SepPrivilegeCheck
//   Se = Security（安全）
//   p = private（私有？还是 internal？）
//   PrivilegeCheck = 特权检查
// 这是 SePrivilegeCheck 的内部工作函数，不对外直接暴露。

    IN PTOKEN Token,
//  ^^ ^^^^^^ ^^^^^
// 参数1：Token
//   IN：输入参数
//   类型：PTOKEN（指向令牌对象的指针）
//   令牌是什么？用户的安全身份证，包含：
//     - 用户 SID（安全标识符，比如 S-1-5-21-...）
//     - 用户所属的组 SID 列表
//     - 特权列表（比如 SeShutdownPrivilege、SeDebugPrivilege）
//
// 这个参数是"当前用户"的令牌，用来查这个用户有哪些特权。

    IN OUT PLUID_AND_ATTRIBUTES RequiredPrivileges,
//  ^^ ^^^ ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^
// 参数2：RequiredPrivileges
//   IN OUT：输入输出参数（函数可能会修改它）
//   类型：PLUID_AND_ATTRIBUTES（指向 LUID_AND_ATTRIBUTES 结构的指针）
//   LUID_AND_ATTRIBUTES 包含：
//     - LUID：本地唯一标识符（64 位整数，表示一个特权类型，比如 0x123 表示 SeShutdown）
//     - Attributes：属性（比如 SE_PRIVILEGE_USED_FOR_ACCESS 表示"这次检查用到了这个特权"）
//
// 这个参数是"调用者需要的特权列表"。
// 函数会在需要的特权上标记"已使用"（设置 SE_PRIVILEGE_USED_FOR_ACCESS 位）。

    IN ULONG RequiredPrivilegeCount,
//  ^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^
// 参数3：RequiredPrivilegeCount
//   类型：ULONG
//   作用：RequiredPrivileges 数组里有多少个特权。
//   比如需要 2 个特权：SeShutdown + SeTimeZone，那这个值就是 2。

    IN ULONG PrivilegeSetControl,
//  ^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^
// 参数4：PrivilegeSetControl
//   类型：ULONG
//   作用：控制位，决定"需要多少个特权"。
//   可能的值：
//     - PRIVILEGE_SET_ALL_NECESSARY (1)：所有特权都必须拥有
//     - 其他值：至少拥有一个就行（任意）
//
// 类比：进 VIP 房间：
//   ALL_NECESSARY：您必须有"VIP卡" AND "会员卡" AND "身份证"（全部都要）
//   其他模式：您有"VIP卡" OR "认识保安" OR "是老板"（一个就行）

    IN KPROCESSOR_MODE PreviousMode
//  ^^ ^^^^^^^^^^^^^^^^ ^^^^^^^^^^^
// 参数5：PreviousMode
//   类型：KPROCESSOR_MODE（枚举：UserMode = 1, KernelMode = 0）
//   作用：调用这个函数之前，CPU 在什么模式？
//     - KernelMode：内核态调用（比如系统线程），信任参数
//     - UserMode：用户态调用（比如 syscall），需要验证参数

    )
// 右括号：参数列表结束。

// ============================================================================
// 函数头注释（微软文档格式）
// ============================================================================
/*++
// ^^^

Routine Description:
// ^^^^^^^^^^^^^^^^^^^

    Worker routine for SePrivilegeCheck
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 翻译："SePrivilegeCheck 的工作例程。"
// 意思：这个函数是 SePrivilegeCheck 的"打工仔"，真正干活的是它。

Arguments:
// ^^^^^^^^^

    Token - The user's effective token.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// Token：用户的有效令牌（身份证）

    RequiredPrivileges - A privilege set describing the required
        privileges.  The UsedForAccess bits will be set in any privilege
        that is actually used (usually all of them).
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// RequiredPrivileges：描述所需特权的特权集合。
//   在任何一个实际用到的特权上，会设置 UsedForAccess 位（通常是全部）。

    RequiredPrivilegeCount - How many privileges are in the
        RequiredPrivileges set.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// RequiredPrivilegeCount：RequiredPrivileges 集合里有多少个特权。

    PrivilegeSetControl - Describes how many privileges are required.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// PrivilegeSetControl：描述需要多少个特权（全部需要还是任意一个就行）。

    PreviousMode - The previous processor mode.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// PreviousMode：之前的处理器模式（用户态还是内核态）

Return Value:
// ^^^^^^^^^^^

    Returns TRUE if requested privileges are granted, FALSE otherwise.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 返回值：如果请求的特权被授予，返回 TRUE；否则返回 FALSE。

--*/
// 注释结束。

// ============================================================================
// 函数体开始
// ============================================================================
{
// 左花括号：函数体开始。

// ============================================================================
// 局部变量声明
// ============================================================================
    PLUID_AND_ATTRIBUTES CurrentRequiredPrivilege;
//  ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^
// 类型：PLUID_AND_ATTRIBUTES（指向 LUID_AND_ATTRIBUTES 的指针）
// 变量名：CurrentRequiredPrivilege
// 作用：遍历 RequiredPrivileges 数组时的"当前元素"指针。

    PLUID_AND_ATTRIBUTES CurrentTokenPrivilege;
//  ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^
// 类型：PLUID_AND_ATTRIBUTES
// 变量名：CurrentTokenPrivilege
// 作用：遍历 Token 中的特权列表时的"当前元素"指针。

    BOOLEAN RequiredAll;
//  ^^^^^^^ ^^^^^^^^^^^
// 类型：BOOLEAN
// 变量名：RequiredAll
// 作用：记录 PrivilegeSetControl 是否为 PRIVILEGE_SET_ALL_NECESSARY。
//   TRUE = 需要所有特权都满足
//   FALSE = 只需要至少一个特权满足

    ULONG TokenPrivilegeCount;
//  ^^^^^ ^^^^^^^^^^^^^^^^^^^^
// 类型：ULONG
// 变量名：TokenPrivilegeCount
// 作用：Token 中一共有多少个特权。

    ULONG MatchCount = 0;
//  ^^^^^ ^^^^^^^^^^ ^^^
// 类型：ULONG
// 变量名：MatchCount
// 初始值：0
// 作用：记录"匹配上了多少个所需特权"。
//   如果 RequiredAll 为 TRUE，MatchCount 必须等于 RequiredPrivilegeCount 才能返回 TRUE。
//   如果 RequiredAll 为 FALSE，MatchCount >= 1 就可以返回 TRUE。

    ULONG i;
//  ^^^^^ ^
// 循环计数器（外层：遍历所需特权）

    ULONG j;
//  ^^^^^ ^
// 循环计数器（内层：遍历令牌特权）

// ============================================================================
// 分页代码检查宏
// ============================================================================
    PAGED_CODE();
//  ^^^^^^^^^^^^
// PAGED_CODE() 是一个宏，定义大概长这样：
//   #define PAGED_CODE() \
//       if (KeGetCurrentIrql() > APC_LEVEL) { \
//           KdPrint(("PAGED_CODE called at IRQL > APC_LEVEL\n")); \
//           KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA, ...); \
//       }
// 作用：确保当前 IRQL（中断请求级别）不高于 APC_LEVEL，
//       因为分页代码只能在低 IRQL 运行（否则可能触发缺页中断，导致死锁）。
//
// 如果有人在 IRQL > APC_LEVEL 时调用这个函数，直接蓝屏。
//
// 类比：游泳池的"禁止跳水"标志。
//   如果您在深水区跳水（高 IRQL 调用分页代码），
//   救生员（PAGED_CODE 宏）会吹哨并把您赶出去（蓝屏）。

// ============================================================================
// 函数体继续（后续代码未贴出）
// ============================================================================
// 注意：这里只是变量声明和开头，真正的特权比较循环还在后面。
// 如果有后续代码，可以继续贴，我继续注释。
// 
// 这个函数的逻辑大致是：
//   1. 从 Token 里取出特权列表（TokenPrivilegeCount 个）
//   2. 外层循环遍历 RequiredPrivileges（i 从 0 到 RequiredPrivilegeCount-1）
//   3. 内层循环遍历 TokenPrivileges（j 从 0 到 TokenPrivilegeCount-1）
//   4. 如果找到 LUID 相同的，检查属性（是否启用等），MatchCount++，标记 UsedForAccess
//   5. 循环结束后，根据 RequiredAll 判断是否满足
//   6. 返回 TRUE 或 FALSE
//
// 类比：这是"安检员的检查表"。
//   您（Token）有一张通行证，上面列了您有哪些特权（比如"可进入 A 区、B 区"）。
//   安检员（SepPrivilegeCheck）手里有一张需求单（RequiredPrivileges），
//   上面写着"需要进入 A 区、C 区"。
//   安检员逐项核对，在需求单上打勾（标记 UsedForAccess），
//   最后看是不是所有需要的区您都能进（或者至少能进一个，取决于控制位）。
//

// ============================================================================
// 注释：先处理内核态调用者
// ============================================================================
    //
    //   Take care of kernel callers first
    //   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："先处理内核调用者"
    // 
    // 含义：如果调用这个函数的是内核模式的代码（比如系统线程、驱动程序），
    //       直接放行，不需要检查特权。(不是kill那个处理)
    //
    // 为什么内核调用者不需要检查特权？
    //   因为内核代码本身已经是最可信的了（它运行在 Ring 0）。
    //   如果内核要干一件事，说明系统允许它干，不需要再问"您有权限吗"。
    //   就像家里的主人不需要问自己"我能进厨房吗"。
    //
    // 类比：机场的"员工通道"。
    //   普通乘客（用户态）要走安检通道（检查特权），
    //   机场员工（内核态）可以直接走员工通道，不用安检。
    //
    // 吐槽：这是 Windows 内核里的"特权通道"，
    //       只要您跑在内核态，就默认您是"自己人"。
    //       这也是为什么恶意软件一旦提权到内核（rootkit），
    //       就能为所欲为——因为所有特权检查都被跳过了。

// ============================================================================
// 判断：如果之前模式是内核态
// ============================================================================
    if (PreviousMode == KernelMode) {
//  ^^ ^^^^^^^^^^^^ ^= ^^^^^^^^^^
// PreviousMode：调用者之前的 CPU 模式
// KernelMode：枚举值 0（内核模式）
// 
// 如果 PreviousMode == KernelMode，说明是内核代码调用的。

// ============================================================================
// 内核态：直接返回成功
// ============================================================================
         return(TRUE);
//       ^^^^^^^^^^^^
// 直接返回 TRUE（特权检查通过）。
// 注意：这里没有检查任何特权，直接放行。
// 
// 为什么有个括号？return(TRUE) 和 return TRUE 一样，
// 但微软风格喜欢把返回值括起来，可能是历史遗留习惯（早期的 C 语言要求？）。
// 
// 类比：VIP 通道的保安看到您的工作证（内核态），
//       直接挥手说"请进，不用检查"。

// ============================================================================
// 结束 if
// ============================================================================
    }
// 右花括号：结束内核态特殊处理。
// 如果 PreviousMode == KernelMode，函数到这里就结束了，后面的代码不会执行。

// ============================================================================
// 用户态调用：获取令牌中的特权数量
// ============================================================================
    TokenPrivilegeCount = Token->PrivilegeCount;
//  ^^^^^^^^^^^^^^^^^^^   ^^^^^ ^^^^^^^^^^^^^^
// 从 Token 结构体中读取 PrivilegeCount 字段（令牌中一共有多少个特权）。
// 
// 比如令牌里有：
//   - SeShutdownPrivilege（关机）
//   - SeTimeZonePrivilege（改时区）
//   - SeLoadDriverPrivilege（加载驱动）
// 那 PrivilegeCount = 3。
// 
// 注意：这个值是在创建令牌时确定的，之后一般不变（除非调整特权）。

// ============================================================================
// 注释：保存是需要"全部"还是"任意一个"
// ============================================================================
    //
    //   Save whether we require ALL of them or ANY
    //   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："保存是需要【全部】特权还是【任意一个】特权"

// ============================================================================
// 解析控制位：是否需要全部特权
// ============================================================================
    RequiredAll = (BOOLEAN)(PrivilegeSetControl & PRIVILEGE_SET_ALL_NECESSARY);
//  ^^^^^^^^^^^   ^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^ ^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^
// PrivilegeSetControl：调用者传入的控制标志（32 位整数）
// PRIVILEGE_SET_ALL_NECESSARY：一个常量，比如 0x00000001
// & 操作：按位与，如果该位是 1，结果非 0；如果是 0，结果为 0。
// (BOOLEAN)：强制转换成布尔值（C 语言里 0 = FALSE，非 0 = TRUE）
//
// 效果：
//   如果 PrivilegeSetControl 的第 0 位是 1 → RequiredAll = TRUE
//   如果第 0 位是 0 → RequiredAll = FALSE
//
// 含义：
//   TRUE：需要所有要求的特权都满足（AND 逻辑）
//   FALSE：只需要至少一个特权满足（OR 逻辑）
//
// 类比：进夜店：
//   ALL_NECESSARY：您需要有"身份证" AND "健康码" AND "门票"
//   非 ALL_NECESSARY：您有"VIP卡" OR "认识老板" OR "是生日当天"
//
// 吐槽：这个命名很拗口，PRIVILEGE_SET_ALL_NECESSARY 翻译过来是"特权集全部必需"，
//       但实际逻辑是"如果这个位设置了，就需要全部特权"。
//       微软的命名风格总是这么"直白又费解"。

// ============================================================================
// 获取令牌的读锁（防止多 CPU 同时修改）
// ============================================================================
    SepAcquireTokenReadLock( Token );
//  ^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^
// SepAcquireTokenReadLock：一个函数，获取令牌的"读锁"。
//   为什么需要锁？因为可能有多个线程同时访问同一个令牌：
//     - 线程 A：正在检查特权（读）
//     - 线程 B：正在修改特权（写，比如禁用某个特权）
//   如果不加锁，A 读到一半 B 改了，数据不一致。
//
// 读锁（Read Lock）：允许多个读者同时读，但写者要等所有读者读完。
// 这是"读写锁"（Reader-Writer Lock）的读锁。
//
// 类比：图书馆的阅览室。
//   读者（读锁）可以很多人同时看书，
//   但管理员（写锁）要整理书架时，得等所有读者离开。

// ============================================================================
// 外层循环：遍历"所需特权"数组
// ============================================================================
    for ( i = 0 , CurrentRequiredPrivilege = RequiredPrivileges ;
//       ^ ^ ^   ^^^^^^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^
// 初始化部分：
//   i = 0：循环计数器从 0 开始
//   CurrentRequiredPrivilege = RequiredPrivileges：指针指向数组的第一个元素
//
// 注意：这是 C 语言的 for 循环，初始化语句可以写多个表达式，用逗号分隔。

          i < RequiredPrivilegeCount ;
//        ^ ^ ^^^^^^^^^^^^^^^^^^^^^^^
// 循环条件：只要 i 小于所需特权数量，就继续循环。
// 比如 RequiredPrivilegeCount = 3，那 i = 0,1,2 时循环，i = 3 时退出。

          i++, CurrentRequiredPrivilege++ ) {
//        ^^^  ^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 循环迭代（每次循环结束后执行）：
//   i++：计数器加 1
//   CurrentRequiredPrivilege++：指针移动到数组的下一个元素
//
// 效果：每次循环，CurrentRequiredPrivilege 指向下一个需要检查的特权。

// ============================================================================
// 内层循环：遍历"令牌中的特权"数组
// ============================================================================
         for ( j = 0, CurrentTokenPrivilege = Token->Privileges;
//            ^ ^ ^  ^^^^^^^^^^^^^^^^^^^^^   ^^^^^ ^^^^^^^^^^^
// 初始化部分：
//   j = 0：内层循环计数器从 0 开始
//   CurrentTokenPrivilege = Token->Privileges：指针指向令牌特权数组的第一个元素
//
// Token->Privileges：令牌中存储特权的数组（每个元素是一个 LUID_AND_ATTRIBUTES）。

               j < TokenPrivilegeCount ;
//             ^ ^ ^^^^^^^^^^^^^^^^^^^
// 循环条件：只要 j 小于令牌中的特权总数，就继续内层循环。
// 比如令牌有 10 个特权，那 j = 0..9 时循环。

               j++, CurrentTokenPrivilege++ ) {
//             ^^^  ^^^^^^^^^^^^^^^^^^^^^^^
// 循环迭代：
//   j++：计数器加 1
//   CurrentTokenPrivilege++：指针移动到下一个令牌特权

// ============================================================================
// 检查：特权是否启用 + LUID 是否匹配
// ============================================================================
              if ((CurrentTokenPrivilege->Attributes & SE_PRIVILEGE_ENABLED) &&
//                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^ ^^^^^^^^^^^^^^^^^^^^
// CurrentTokenPrivilege->Attributes：这个特权的属性位掩码
// SE_PRIVILEGE_ENABLED：常量，比如 0x00000002，表示"这个特权是启用的"
// & 操作：按位与，检查该位是否为 1
//
// 为什么要检查是否启用？
//   令牌里可以有某个特权，但它是"禁用"状态。
//   比如您有 SeLoadDriverPrivilege，但系统把它禁用了，
//   那您实际上不能加载驱动。只有"启用"的特权才有效。
//
// 类比：您有驾照（特权），但驾照过期了（未启用），照样不能开车。

                   (RtlEqualLuid(&CurrentTokenPrivilege->Luid,
//                  ^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^
// RtlEqualLuid：内核函数，比较两个 LUID（本地唯一标识符）是否相等。
// &CurrentTokenPrivilege->Luid：令牌中这个特权的 LUID 的地址
                                 &CurrentRequiredPrivilege->Luid))
//                               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// &CurrentRequiredPrivilege->Luid：所需特权的 LUID 的地址
//
// RtlEqualLuid 返回 TRUE 如果两个 LUID 相等（表示是同一个特权类型）。
//
// 比如：所需特权是 SeShutdownPrivilege（LUID = 0x123），
//       令牌里有一个特权的 LUID 也是 0x123，那就匹配上了。

// ============================================================================
// 匹配成功：标记已使用，增加匹配计数，跳出内层循环
// ============================================================================
                 ) {
// 左花括号：if 条件成立时执行

                       CurrentRequiredPrivilege->Attributes |=
//                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// CurrentRequiredPrivilege：当前正在检查的"所需特权"的指针
// Attributes：它的属性字段（32 位整数）
// |=：按位或赋值（添加一个位）

                                                SE_PRIVILEGE_USED_FOR_ACCESS;
//                                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// SE_PRIVILEGE_USED_FOR_ACCESS：常量，比如 0x80000000，表示"这个特权在本次访问中被用到了"
//
// 这行代码的作用：在所需特权上打一个"已使用"标记。
// 调用者可以通过检查这个标记，知道哪些特权实际被用到了。
// 比如调用者要求"我需要 A、B、C 三个特权"，
// 但最后只匹配上了 A 和 B，那 C 就不会被标记。
//
// 类比：购物清单（所需特权），您每买到一件商品就打个勾（标记已使用），
//       回家后看清单就知道哪些买到了、哪些没买到。

                       MatchCount++;
//                     ^^^^^^^^^^^
// MatchCount 加 1。这个变量记录"成功匹配了多少个所需特权"。
//
// 如果 RequiredAll = TRUE，最终 MatchCount 必须等于 RequiredPrivilegeCount；
// 如果 RequiredAll = FALSE，MatchCount >= 1 就行。

                       break;     // start looking for next one
//                     ^^^^^
// break：跳出内层循环（停止继续查找这个所需特权的其他匹配项）。
//
// 为什么 break？
//   因为已经找到了这个所需特权在令牌中的匹配项，
//   不需要继续在令牌里找后面的特权了（一个所需特权只匹配一次）。
//   直接开始找下一个所需特权（外层循环的下一次迭代）。
//
// 类比：您在找一本书（所需特权），在图书馆书架（令牌特权）上找到了一本，
//       您就停下来，不会继续在同一排书架找第二本相同的书。

// ============================================================================
// 结束 if、结束内层循环、结束外层循环
// ============================================================================
              }
// 右花括号：结束 if 语句

         }
// 右花括号：结束内层 for 循环（j 循环）
// 内层循环结束后，要么是 break 跳出来的（找到了匹配），
// 要么是遍历完所有令牌特权都没找到（继续外层循环的下一个所需特权）。

    }
// 右花括号：结束外层 for 循环（i 循环）
// 外层循环结束后，所有所需特权都被处理过了：
//   - 有些找到了匹配（MatchCount 增加了，且标记了 SE_PRIVILEGE_USED_FOR_ACCESS）
//   - 有些没找到（MatchCount 没增加）

// ============================================================================
// 释放令牌的读锁
// ============================================================================
    SepReleaseTokenReadLock( Token );
//  ^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^
// SepReleaseTokenReadLock：释放之前获取的读锁。
// 配对：每个 SepAcquireTokenReadLock 必须有对应的 SepReleaseTokenReadLock。
// 
// 为什么在这里释放？
//   因为特权检查的核心逻辑已经完成了（两层循环结束），
//   不再需要读取令牌的内容了，所以释放锁让其他线程可以写令牌。
//
// 类比：读完书后把书放回书架，离开阅览室，让管理员可以整理书架。

// ============================================================================
// 注释：如果需要任意一个但一个都没匹配上
// ============================================================================
    //
    //   If we wanted ANY and didn't get any, return failure.
    //   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："如果需要任意一个（RequiredAll = FALSE），但一个都没匹配上（MatchCount == 0），
    //       那就返回失败。"

// ============================================================================
// 情况1：任意模式 + 0 匹配 → 失败
// ============================================================================
    if (!RequiredAll && (MatchCount == 0)) {
//  ^^ ^^^^^^^^^^^^ ^^ ^^^^^^^^^^^^^^^^^^
// !RequiredAll：RequiredAll 为 FALSE（不需要全部，任意一个就行）
// MatchCount == 0：一个都没匹配上
// &&：两个条件都满足时进入 if
//
// 含义：调用者说"有任意一个特权就行"，但您一个都没有 → 失败。

// ============================================================================
// 返回 FALSE
// ============================================================================
         return (FALSE);
//       ^^^^^^^^^^^^^
// 返回 FALSE，表示特权检查不通过。
// 比如：夜店说"您有 VIP 卡或者认识老板就行"，但您两样都没有，被拒之门外。

// ============================================================================
// 结束 if
// ============================================================================
    }

// ============================================================================
// 注释：如果需要全部但没拿到全部
// ============================================================================
    //
    // If we wanted ALL and didn't get all, return failure.
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："如果需要全部特权（RequiredAll = TRUE），但没拿到全部（MatchCount != 总数），
    //       那就返回失败。"

// ============================================================================
// 情况2：全部模式 + 匹配数不足 → 失败
// ============================================================================
    if (RequiredAll && (MatchCount != RequiredPrivilegeCount)) {
//  ^^ ^^^^^^^^^^^ ^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// RequiredAll：TRUE（需要全部特权）
// MatchCount != RequiredPrivilegeCount：匹配上的数量不等于所需的总数
//   （可能 MatchCount < RequiredPrivilegeCount，不可能大于）
//
// 含义：调用者说"我需要 A、B、C 三个特权"，但您只有 A 和 B，缺 C → 失败

// ============================================================================
// 返回 FALSE
// ============================================================================
         return(FALSE);
//       ^^^^^^^^^^^^
// 返回 FALSE。
// 比如：夜店说"您需要身份证、健康码、门票三样"，您只有两样 → 不让进。

// ============================================================================
// 结束 if
// ============================================================================
    }

// ============================================================================
// 其他情况：成功
// ============================================================================
    return(TRUE);
//  ^^^^^^^^^^^
// 走到这里，说明：
//   - 如果是"任意模式"：MatchCount >= 1（至少有一个匹配）
//   - 如果是"全部模式"：MatchCount == RequiredPrivilegeCount（全部匹配）
// 返回 TRUE，表示特权检查通过。
//
// 类比：夜店保安挥手说"请进"。

// ============================================================================
// 函数体结束
// ============================================================================
}
// 右花括号：SepPrivilegeCheck 函数定义结束。
// 这个函数虽然不长，但完成了 Windows 安全模型的核心逻辑：
//   - 内核调用者直接放行
//   - 用户态调用者需要检查令牌中的特权
//   - 支持"全部需要"和"任意一个"两种模式
//   - 用读写锁保护并发访问
//   - 标记哪些特权实际被用到了
//

// ============================================================================
// 分页符
// ============================================================================

// 又是一个换页符（ASCII 0x0C）。
// 表示新函数 SePrivilegeCheck 的开始。
// 在打印的代码里，从这里会另起一页。

// ============================================================================
// 函数：SePrivilegeCheck（对外 API）
// ============================================================================
BOOLEAN
// ^^^^^^^
// 返回值类型：BOOLEAN
//   TRUE = 拥有所需的所有特权（或满足条件）
//   FALSE = 不满足

SePrivilegeCheck(
// ^^^^^^^^^^^^^^^^
// 函数名：SePrivilegeCheck
//   Se = Security（安全）
//   PrivilegeCheck = 特权检查
// 这是对外暴露的 API，其他内核组件调用这个函数来检查特权。
// 内部会调用前面注释过的 SepPrivilegeCheck（带 p 的那个）。

    IN OUT PPRIVILEGE_SET RequiredPrivileges,
//  ^^ ^^^ ^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^
// 参数1：RequiredPrivileges
//   IN OUT：输入输出参数（函数会修改它）
//   类型：PPRIVILEGE_SET（指向 PRIVILEGE_SET 结构的指针）
//   PRIVILEGE_SET 结构包含：
//     - PrivilegeCount：需要检查的特权数量
//     - Control：控制位（ALL_NECESSARY 或 ANY）
//     - Privilege[ANYSIZE_ARRAY]：LUID_AND_ATTRIBUTES 数组
//
// 这个函数会在这个结构的每个特权上标记 SE_PRIVILEGE_USED_FOR_ACCESS，
// 表示"这个特权被检查过了"。

    IN PSECURITY_SUBJECT_CONTEXT SubjectSecurityContext,
//  ^^ ^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^
// 参数2：SubjectSecurityContext
//   IN：输入参数
//   类型：PSECURITY_SUBJECT_CONTEXT（指向安全主体上下文的指针）
//   什么是安全主体上下文？它包含：
//     - 主令牌（ProcessToken）：进程的令牌
//     - 客户端令牌（ClientToken）：线程模拟的令牌（如果有）
//     - 模拟级别（ImpersonationLevel）：模拟的信任级别
//
// 这个参数描述了"谁"在请求操作——是进程自己，还是在模拟某个客户端。

    IN KPROCESSOR_MODE AccessMode
//  ^^ ^^^^^^^^^^^^^^^^ ^^^^^^^^^^
// 参数3：AccessMode
//   IN：输入参数
//   类型：KPROCESSOR_MODE（UserMode 或 KernelMode）
//   作用：告诉函数是内核调用还是用户调用。
//     - KernelMode：内核调用，直接返回成功（不检查特权）
//     - UserMode：用户调用，需要真正检查令牌

    )
// 右括号：参数列表结束。

// ============================================================================
// 函数头注释（微软文档格式）
// ============================================================================
/*++
// ^^^

Routine Description:
// ^^^^^^^^^^^^^^^^^^^

    This routine checks to see if the token contains the specified
    privileges.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 翻译："这个函数检查令牌是否包含指定的特权。"

Arguments:
// ^^^^^^^^^

    RequiredPrivileges - Points to a set of privileges.  The subject's
        security context is to be checked to see which of the specified
        privileges are present.  The results will be indicated in the
        attributes associated with each privilege.  Note that
        flags in this parameter indicate whether all the privileges listed
        are needed, or any of the privileges.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// RequiredPrivileges：指向一组特权。
//   会检查主体的安全上下文，看哪些指定的特权存在。
//   结果会记录在每个特权关联的属性中。
//   注意：这个参数里的标志指示是需要【全部】特权还是【任意一个】特权。

    SubjectSecurityContext - A pointer to the subject's captured security
        context.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// SubjectSecurityContext：指向主体捕获的安全上下文的指针。

    AccessMode - Indicates the access mode to use for access check.  One of
        UserMode or KernelMode.  If the mode is kernel, then all privileges
        will be marked as being possessed by the subject, and successful
        completion status is returned.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// AccessMode：指示用于访问检查的访问模式（UserMode 或 KernelMode）。
//   如果是内核模式，那么所有特权都会被标记为主体拥有，并返回成功状态。
//
// 翻译成人话：如果 AccessMode == KernelMode，直接返回 TRUE，不检查。

Return Value:
// ^^^^^^^^^^^

    BOOLEAN - TRUE if all specified privileges are held by the subject,
    otherwise FALSE.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 返回值：如果主体拥有所有指定的特权，返回 TRUE；否则返回 FALSE。

--*/
// 注释结束。

// ============================================================================
// 函数体开始
// ============================================================================
{
// 左花括号：函数体开始。

// ============================================================================
// 局部变量声明
// ============================================================================
    BOOLEAN Status;
//  ^^^^^^^ ^^^^^^
// 类型：BOOLEAN
// 变量名：Status
// 作用：存储内部 SepPrivilegeCheck 的返回值（TRUE/FALSE）。

// ============================================================================
// 分页代码检查
// ============================================================================
    PAGED_CODE();
//  ^^^^^^^^^^^^
// 确保当前 IRQL 不高于 APC_LEVEL。
// 这个函数在 PAGE 段（可分页），不能在高于 APC_LEVEL 的中断级别调用。
// 如果有人在错误的中断级别调用，会蓝屏。

// ============================================================================
// 注释：模拟检查
// ============================================================================
    //
    // If we're impersonating a client, we have to be at impersonation level
    // of SecurityImpersonation or above.
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："如果我们在模拟一个客户端，那么模拟级别必须是
    //       SecurityImpersonation 或更高。"
    //
    // 什么是模拟（Impersonation）？
    //   一个服务线程可以"模拟"客户端的身份，用客户端的权限去访问资源。
    //   比如 IIS 模拟访问网站的用户，用用户的权限读取他的文件。
    //
    // 模拟级别有 4 个（从低到高）：
    //   - SecurityAnonymous：匿名，几乎没权限
    //   - SecurityIdentification：可识别身份，但不能访问资源
    //   - SecurityImpersonation：可模拟，能访问资源（服务端模拟客户端）
    //   - SecurityDelegation：可委派，能模拟客户端去访问远程资源
    //
    // 这里要求：如果要模拟客户端，模拟级别必须 >= SecurityImpersonation。
    // 低于这个级别（比如 Anonymous 或 Identification），不能做特权检查。

// ============================================================================
// 模拟级别检查
// ============================================================================
    if ( (SubjectSecurityContext->ClientToken != NULL) &&
//       ^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// SubjectSecurityContext->ClientToken：客户端令牌（如果有模拟）
// != NULL：不等于空指针，说明当前线程正在模拟某个客户端
// 
// 注意：如果没有模拟，ClientToken == NULL，跳过这个 if

         (SubjectSecurityContext->ImpersonationLevel < SecurityImpersonation)
//       ^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^ ^^^^^^^^^^^^^^^^^^^^
// SubjectSecurityContext->ImpersonationLevel：当前的模拟级别
// SecurityImpersonation：常量，表示"模拟"级别（通常是 2）
// < SecurityImpersonation：模拟级别太低（0 或 1）
       ) {
// 两个条件都满足：正在模拟 AND 模拟级别太低

// ============================================================================
// 模拟级别不足：返回失败
// ============================================================================
           return(FALSE);
//         ^^^^^^^^^^^^
// 返回 FALSE，特权检查不通过。
// 
// 为什么？因为模拟级别太低，不能代表客户端做权限判断。
// 比如客户端只是"可识别身份"（Identification），
// 服务端不能替它去访问文件——太危险了。
//
// 类比：您去银行代办业务。
//   如果您只是"认识客户"（Identification），银行不会让您取他的钱。
//   您必须有"授权委托书"（Impersonation 或 Delegation）。

// ============================================================================
// 结束 if
// ============================================================================
       }

// ============================================================================
// 注释：SepPrivilegeCheck 会锁定传入的令牌（读锁）
// ============================================================================
    //
    // SepPrivilegeCheck locks the passed token for read access
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："SepPrivilegeCheck 会锁定传入的令牌（读访问）"
    //
    // 这是给调用者的提示：下面调用的 SepPrivilegeCheck 内部会获取令牌的读锁，
    // 不用担心并发问题。

// ============================================================================
// 调用核心特权检查函数
// ============================================================================
    Status = SepPrivilegeCheck(
//  ^^^^^^   ^^^^^^^^^^^^^^^^
// Status 接收返回值（TRUE 或 FALSE）
// SepPrivilegeCheck：内部工作函数（前面注释过的那个）

                 EffectiveToken( SubjectSecurityContext ),
//               ^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^
// EffectiveToken()：一个宏或内联函数，从安全主体上下文中提取"有效令牌"。
//   逻辑：
//     - 如果有客户端令牌（正在模拟），返回客户端令牌
//     - 否则返回进程令牌（主令牌）
//
// 这个参数传给 SepPrivilegeCheck 的第一个参数（Token）。

                 RequiredPrivileges->Privilege,
//               ^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^
// RequiredPrivileges->Privilege：PRIVILEGE_SET 结构中的特权数组。
//   注意：这里是数组名（指向第一个元素的指针）。
//   传给 SepPrivilegeCheck 的第二个参数（RequiredPrivileges）。

                 RequiredPrivileges->PrivilegeCount,
//               ^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^
// RequiredPrivileges->PrivilegeCount：需要检查的特权数量。
//   传给 SepPrivilegeCheck 的第三个参数。

                 RequiredPrivileges->Control,
//               ^^^^^^^^^^^^^^^^^^ ^^^^^^^
// RequiredPrivileges->Control：控制标志（ALL_NECESSARY 或 ANY）。
//   传给 SepPrivilegeCheck 的第四个参数（PrivilegeSetControl）。

                 AccessMode
//               ^^^^^^^^^^
// 访问模式（UserMode 或 KernelMode）。
//   传给 SepPrivilegeCheck 的第五个参数（PreviousMode）。
//   注意：在 SepPrivilegeCheck 里，这个参数叫 PreviousMode，
//         但传的是 AccessMode，两者含义相同。

                 );
// 函数调用结束。

// ============================================================================
// 返回结果
// ============================================================================
    return(Status);
//  ^^^^^^^^^^^^^
// 把 SepPrivilegeCheck 的返回值原样返回给调用者。
// 
// 如果 Status = TRUE：特权检查通过
// 如果 Status = FALSE：特权检查失败

// ============================================================================
// 函数体结束
// ============================================================================
}
// 右花括号：SePrivilegeCheck 函数定义结束。

// ============================================================================
// 函数总结
// ============================================================================
// SePrivilegeCheck 做了这几件事：
//   1. PAGED_CODE()：确保在可分页的上下文中
//   2. 检查模拟级别：如果模拟级别太低，直接返回 FALSE
//   3. 调用 SepPrivilegeCheck（真正的特权检查）
//   4. 返回结果
//
// 这个函数是"包装器"（Wrapper），真正的逻辑在 SepPrivilegeCheck 里。
// 它负责处理模拟相关的安全检查，然后把剩下的工作交给核心函数。
//
// 类比：餐厅的前台（SePrivilegeCheck）：
//   - 先问"您有预约吗？"（检查模拟级别）
//   - 然后带您到座位上（调用 SepPrivilegeCheck）
//   - 后面的点菜、上菜是后厨的事（SepPrivilegeCheck 的工作）

// ============================================================================
// 分页符
// ============================================================================

// 又一个换页符，表示下一个函数（NtPrivilegeCheck）的开始。

// ============================================================================
// 系统调用：NtPrivilegeCheck（用户态可调用）
// ============================================================================
NTSTATUS
// ^^^^^^^^
// 返回值类型：NTSTATUS（Windows 内核错误码）
//   STATUS_SUCCESS (0)：成功
//   其他：失败（比如无效句柄、参数错误等）
// 注意：这个函数返回 NTSTATUS，不是 BOOLEAN。
// 因为用户态调用可能失败（比如传了无效令牌句柄），需要返回错误码。

NtPrivilegeCheck(
// ^^^^^^^^^^^^^^^
// 函数名：NtPrivilegeCheck
//   Nt = Windows NT 的系统调用前缀
//   PrivilegeCheck = 特权检查
// 这是用户态可以通过 syscall 调用的 API（Native API）。
// 对应的用户态函数是 advapi32!PrivilegeCheck。

    IN HANDLE ClientToken,
//  ^^ ^^^^^^ ^^^^^^^^^^^^
// 参数1：ClientToken
//   IN：输入参数
//   类型：HANDLE（内核句柄）
//   作用：客户端令牌的句柄（由 NtOpenProcessToken 等函数返回）。
//   如果是 NULL，表示使用当前线程的令牌。

    IN OUT PPRIVILEGE_SET RequiredPrivileges,
//  ^^ ^^^ ^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^
// 参数2：RequiredPrivileges
//   IN OUT：输入输出参数
//   类型：PPRIVILEGE_SET
//   作用：所需特权的集合（同前面的函数）
//   函数会修改这个结构，标记哪些特权被用到了。

    OUT PBOOLEAN Result
//  ^^^ ^^^^^^^ ^^^^^^
// 参数3：Result
//   OUT：输出参数
//   类型：PBOOLEAN（指向布尔值的指针）
//   作用：返回特权检查的结果（TRUE 或 FALSE）。
//   注意：返回值是 NTSTATUS（成功/失败状态），
//         真正的检查结果通过这个指针返回。
//
// 为什么用 OUT 参数而不是直接返回 BOOLEAN？
//   因为函数本身需要返回 NTSTATUS（表示调用本身是否成功，比如句柄是否有效），
//   所以检查结果只能通过输出参数返回。
//
// 类比：快递查询 API：
//   返回值（NTSTATUS）告诉您"查询操作是否成功"（比如快递单号存在吗），
//   输出参数（Result）告诉您"快递到了吗"（TRUE/FALSE）。

    )
// 右括号：参数列表结束。


// ============================================================================
// 函数头注释：NtPrivilegeCheck 的完整文档
// ============================================================================
/*++
// ^^^
// 微软文档注释开始标记。

Routine Description:
// ^^^^^^^^^^^^^^^^^^^

    This routine tests the caller's client's security context to see if it
    contains the specified privileges.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 翻译："这个函数测试调用者的客户端安全上下文，看它是否包含指定的特权。"
//
// 换句话说：检查某个客户端令牌是否有某些特权。

    This API requires the caller have SeTcbPrivilege privilege.  The test
    for this privilege is always against the primary token of the calling
    process, not the impersonation token of the thread.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 翻译："这个 API 要求调用者拥有 SeTcbPrivilege 特权。
//       对这个特权的检查总是针对调用进程的主令牌，而不是线程的模拟令牌。"
//
// 什么是 SeTcbPrivilege？
//   TCB = Trusted Computing Base（可信计算基）
//   这是 Windows 里的"超级特权"，相当于"上帝模式"。
//   拥有这个特权，可以做任何事情——加载驱动、调试进程、修改系统时间等。
//   通常只有 SYSTEM 账户和某些安全工具才拥有。
//
// 为什么需要这个特权？
//   因为 NtPrivilegeCheck 是一个"元操作"——它检查别人的权限。
//   如果任何人都能随便检查别人的令牌，那太危险了。
//   所以只有"受信任"的代码（有 SeTcbPrivilege）才能调用它。
//
// 注意：检查这个特权时，用的是调用进程的主令牌，不是线程的模拟令牌。
//   意思是：即使当前线程在模拟一个低权限客户端，检查 SeTcbPrivilege
//   还是看进程本身的权限（通常是 SYSTEM 或管理员）。
//   这防止了"低权限客户端通过模拟让高权限服务替它检查"的攻击。

Arguments:
// ^^^^^^^^^

    ClientToken - A handle to a token object representing a client
        attempting access.  This handle must be obtained from a
        communication session layer, such as from an LPC Port or Local
        Named Pipe, to prevent possible security policy violations.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// ClientToken：一个令牌对象的句柄，代表尝试访问的客户端。
//   这个句柄必须从通信会话层获得（比如 LPC 端口或本地命名管道），
//   以防止可能的安全策略违规。
//
// 翻译成人话：这个令牌不能随便从别的地方拿，必须是通信机制
// （比如 RPC、LPC、命名管道）传过来的，这样可以确保客户端身份可信。
//
// 类比：您去银行取钱，工作人员要看您的身份证（ClientToken），
//   而且这个身份证必须是公安局（通信会话层）发的，
//   不能是您自己打印的。

    RequiredPrivileges - Points to a set of privileges.  The client's
        security context is to be checked to see which of the specified
        privileges are present.  The results will be indicated in the
        attributes associated with each privilege.  Note that
        flags in this parameter indicate whether all the privileges listed
        are needed, or any of the privileges.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// RequiredPrivileges：同前面的函数。

    Result - Receives a boolean flag indicating whether the client has all
        the specified privileges or not.  A value of TRUE indicates the
        client has all the specified privileges.  Otherwise a value of
        FALSE is returned.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// Result：接收一个布尔标志，指示客户端是否拥有所有指定的特权。
//   TRUE = 拥有全部
//   FALSE = 至少缺一个

Return Value:
// ^^^^^^^^^^^

    STATUS_SUCCESS - Indicates the call completed successfully.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// STATUS_SUCCESS：调用成功完成。

    STATUS_PRIVILEGE_NOT_HELD - Indicates the caller does not have
        sufficient privilege to use this privileged system service.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// STATUS_PRIVILEGE_NOT_HELD：调用者没有足够的特权使用这个系统服务。
//   也就是：调用者自己缺少 SeTcbPrivilege，没资格调用 NtPrivilegeCheck。

--*/
// 注释结束。

// ============================================================================
// 函数体开始
// ============================================================================
{

// ============================================================================
// 局部变量声明
// ============================================================================
    BOOLEAN BStatus;
//  ^^^^^^^ ^^^^^^^
// 类型：BOOLEAN
// 变量名：BStatus
// 作用：存储特权检查的结果（TRUE 或 FALSE），最后写回用户态的 Result 指针。
// 注意：变量名以 B 开头，可能表示 "Boolean Status"。

    KPROCESSOR_MODE PreviousMode;
//  ^^^^^^^^^^^^^^^^ ^^^^^^^^^^^
// 类型：KPROCESSOR_MODE（UserMode = 1, KernelMode = 0）
// 变量名：PreviousMode
// 作用：记录调用这个系统调用之前的处理器模式（用户态还是内核态）。
// 用于判断是否需要验证用户态传入的指针。

    NTSTATUS Status;
//  ^^^^^^^^ ^^^^^^
// 类型：NTSTATUS
// 变量名：Status
// 作用：存储各种操作的返回值（ObReferenceObjectByHandle 等）。

    PLUID_AND_ATTRIBUTES CapturedPrivileges = NULL;
//  ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^ ^^^^
// 类型：PLUID_AND_ATTRIBUTES（指向 LUID_AND_ATTRIBUTES 的指针）
// 变量名：CapturedPrivileges
// 初始值：NULL
// 作用：指向从用户态捕获（复制）的特权数组。
// 为什么需要捕获？因为用户态传进来的指针可能无效，或者数据会变，
// 需要复制到内核空间才能安全地使用。

    PTOKEN Token = NULL;
//  ^^^^^^ ^^^^^ ^^^^
// 类型：PTOKEN（指向令牌对象的指针）
// 变量名：Token
// 初始值：NULL
// 作用：通过 ClientToken 句柄解析出来的令牌对象指针。

    ULONG CapturedPrivilegeCount = 0;
//  ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^ ^^^
// 类型：ULONG
// 变量名：CapturedPrivilegeCount
// 初始值：0
// 作用：捕获的特权数组中有多少个特权。

    ULONG CapturedPrivilegesLength = 0;
//  ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^
// 类型：ULONG
// 变量名：CapturedPrivilegesLength
// 初始值：0
// 作用：捕获的特权数组的总长度（字节数）。

    ULONG ParameterLength = 0;
//  ^^^^^ ^^^^^^^^^^^^^^^ ^^^
// 类型：ULONG
// 变量名：ParameterLength
// 初始值：0
// 作用：RequiredPrivileges 结构的总长度（用于验证）。

    ULONG PrivilegeSetControl = 0;
//  ^^^^^ ^^^^^^^^^^^^^^^^^^^ ^^^
// 类型：ULONG
// 变量名：PrivilegeSetControl
// 初始值：0
// 作用：从用户态 RequiredPrivileges 结构中读取的 Control 字段。

// ============================================================================
// 分页代码检查
// ============================================================================
    PAGED_CODE();
// 确保 IRQL 不高于 APC_LEVEL。

// ============================================================================
// 获取之前的处理器模式
// ============================================================================
    PreviousMode = KeGetPreviousMode();
//  ^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^
// KeGetPreviousMode()：一个内联函数，返回调用系统调用之前的处理器模式。
//   如果是从用户态调用，返回 UserMode（1）
//   如果是从内核态调用，返回 KernelMode（0）
//
// 这个值很重要：
//   - 用户态调用：需要验证传入的指针是否有效（在用户地址空间）
//   - 内核态调用：可以直接使用指针（信任调用者）

// ============================================================================
// 通过句柄获取令牌对象
// ============================================================================
    Status = ObReferenceObjectByHandle(
//  ^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^
// ObReferenceObjectByHandle：内核函数，根据句柄获取对象指针。
// 同时会增加对象的引用计数（防止被意外删除）。

         ClientToken,             // Handle
//       ^^^^^^^^^^^
// 参数1：句柄值（用户态传进来的）

         TOKEN_QUERY,             // DesiredAccess
//       ^^^^^^^^^^^
// 参数2：需要的访问权限（TOKEN_QUERY = 查询令牌信息的权限）
// 如果句柄没有这个权限，函数会失败。

         SeTokenObjectType,      // ObjectType
//       ^^^^^^^^^^^^^^^^^
// 参数3：对象类型（令牌对象类型）
// 确保句柄确实指向一个令牌，而不是其他对象（比如进程、事件）。

         PreviousMode,            // AccessMode
//       ^^^^^^^^^^^
// 参数4：访问模式（UserMode 或 KernelMode）
// 决定是否检查用户权限。如果 PreviousMode == KernelMode，跳过权限检查。

         (PVOID *)&Token,         // Object
//       ^^^^^^^^^^^^^^
// 参数5：输出参数，接收对象指针。
// 类型是 PVOID*，强制转换成 PTOKEN 的地址。

         NULL                     // GrantedAccess
//       ^^^^
// 参数6：输出参数，接收被授予的访问权限（不需要，传 NULL）
         );

// ============================================================================
// 检查句柄获取是否成功
// ============================================================================
    if ( !NT_SUCCESS(Status) ) {
//       ^ ^^^^^^^^^^^^^^^^^^
// NT_SUCCESS(Status)：宏，判断 Status 是否为成功码（0 或正数）
// !NT_SUCCESS：失败
//
// 什么时候会失败？
//   - 句柄无效
//   - 句柄不是令牌类型
//   - 句柄没有 TOKEN_QUERY 权限
//   - 等等

// ============================================================================
// 失败：直接返回错误码
// ============================================================================
         return Status;
//       ^^^^^^^^^^^^^
// 返回错误码（比如 STATUS_INVALID_HANDLE、STATUS_ACCESS_DENIED 等）
// 注意：这里没有给 Result 赋值（因为调用本身失败了，不需要输出结果）

// ============================================================================
// 结束 if
// ============================================================================
    }

// ============================================================================
// 注释：如果传入的令牌是模拟令牌，确保模拟级别足够
// ============================================================================
    //
    // If the passed token is an impersonation token, make sure
    // it is at SecurityIdentification or above.
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："如果传入的令牌是模拟令牌，确保它的模拟级别是
    //       SecurityIdentification 或以上。"
    //
    // SecurityIdentification：最低的模拟级别，只能识别身份，不能访问资源。
    // 为什么需要至少这个级别？因为调用者要查询令牌的特权信息，
    // 如果模拟级别太低（Anonymous），连查都不让查。

// ============================================================================
// 检查令牌类型
// ============================================================================
    if (Token->TokenType == TokenImpersonation) {
//       ^^^^^ ^^^^^^^^^   ^^^^^^^^^^^^^^^^^^
// Token->TokenType：令牌的类型
//   TokenPrimary (1)：主令牌（通常是进程令牌）
//   TokenImpersonation (2)：模拟令牌（线程模拟客户端时的令牌）
//
// 如果是模拟令牌，需要额外检查模拟级别。

// ============================================================================
// 检查模拟级别
// ============================================================================
        if (Token->ImpersonationLevel < SecurityIdentification) {
//           ^^^^^ ^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^
// Token->ImpersonationLevel：模拟级别（0-3）
// SecurityIdentification：常量（可能是 1）
// <：小于，说明级别太低（0 = SecurityAnonymous）

// ============================================================================
// 模拟级别不足：释放令牌对象，返回错误
// ============================================================================
            ObDereferenceObject( (PVOID)Token );
//          ^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^
// ObDereferenceObject：减少对象的引用计数（与 ObReferenceObjectByHandle 配对）。
// 之前获取令牌时增加了一次引用计数，现在要减回去。
// 如果不减，令牌对象永远不会被释放（内存泄漏）。

            return( STATUS_BAD_IMPERSONATION_LEVEL );
//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 返回错误码 STATUS_BAD_IMPERSONATION_LEVEL（错误的模拟级别）。
// 比如模拟级别是 Anonymous，但需要至少 Identification，报错。

// ============================================================================
// 结束内层 if
// ============================================================================
        }
// 右花括号：结束模拟级别检查

// ============================================================================
// 结束外层 if（令牌类型检查）
// ============================================================================
    }
    
// ============================================================================
// try-except 异常处理块开始
// ============================================================================
    try  {
//  ^^^
// try：Microsoft 扩展的 C 语言异常处理（SEH - Structured Exception Handling）。
// 这不是标准 C 的 try-catch，而是 Windows 内核特有的 __try/__except。
// 
// 作用：捕获内存访问异常（比如用户态指针无效导致的页面错误）。
// 如果里面的代码触发异常，会跳转到 except 块执行。
//
// 类比：这是"安全气囊"。代码正常时没事，一旦出问题（内存访问错误），
//       气囊弹出来（跳转到 except），不会让系统崩溃。

// ============================================================================
// 注释：捕获传入的特权集合
// ============================================================================
         //
         // Capture passed Privilege Set
         // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
         // 翻译："捕获传入的特权集合"
         // 意思：把用户态传进来的 PRIVILEGE_SET 结构复制/验证到内核空间。

// ============================================================================
// 验证小结构体：PRIVILEGE_SET 的固定部分
// ============================================================================
         ProbeForWriteSmallStructure(
//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^
// ProbeForWriteSmallStructure：内核函数，验证用户态缓冲区是否可写。
// 专门用于小结构体（大小小于页面大小）。
// 如果缓冲区不可写，会触发异常（被 try-except 捕获）。

             RequiredPrivileges,
//           ^^^^^^^^^^^^^^^^^^
// 参数1：用户态传入的 PRIVILEGE_SET 指针

             sizeof(PRIVILEGE_SET),
//           ^^^^^^^^^^^^^^^^^^^^^
// 参数2：结构体的大小（固定部分）

             sizeof(ULONG)
//           ^^^^^^^^^^^^^
// 参数3：对齐要求（按 ULONG 对齐，即 4 字节）
             );

// ============================================================================
// 读取所需特权的数量
// ============================================================================
         CapturedPrivilegeCount = RequiredPrivileges->PrivilegeCount;
//       ^^^^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^
// 从用户态的 RequiredPrivileges 结构中读取 PrivilegeCount 字段。
// 因为前面已经验证了结构体可读，这里访问是安全的。
//
// 注意：这个值是从用户态读进来的，不可信！后面要做有效性检查。

// ============================================================================
// 验证特权数量是否有效
// ============================================================================
         if (!IsValidElementCount(CapturedPrivilegeCount, LUID_AND_ATTRIBUTES)) {
//           ^ ^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^
// IsValidElementCount：宏或内联函数，检查元素数量是否会导致溢出或超出限制。
//   参数1：CapturedPrivilegeCount（用户态传进来的数量）
//   参数2：LUID_AND_ATTRIBUTES（每个元素的大小）
//
// 检查内容：
//   - 数量不能为 0
//   - 数量不能太大（比如超过 1000）
//   - 数量 * 元素大小 不能溢出 ULONG 范围
//
// 为什么要检查？因为用户态可能传一个巨大的数字（比如 0xFFFFFFFF），
// 导致后面的内存计算溢出，造成安全问题。

// ============================================================================
// 数量无效：设置错误状态，跳转到 leave
// ============================================================================
             Status = STATUS_INVALID_PARAMETER;
//           ^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^
// STATUS_INVALID_PARAMETER：参数无效的错误码。

             leave;
//           ^^^^^
// leave：MSVC 扩展关键字，用于跳出 try 块，直接进入 except 或块结束。
// 类似 goto，但专门用于 SEH。
// 作用：不再执行 try 块后面的代码，直接到 try 块结束（然后检查异常）。

// ============================================================================
// 结束 if
// ============================================================================
         }

// ============================================================================
// 计算 PRIVILEGE_SET 结构的总长度（固定部分 + 可变数组）
// ============================================================================
         ParameterLength = (ULONG)sizeof(PRIVILEGE_SET) +
//       ^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^
// sizeof(PRIVILEGE_SET)：固定部分的大小（不包括后面的可变数组）
// 注意：PRIVILEGE_SET 的定义通常是：
//   typedef struct _PRIVILEGE_SET {
//       ULONG PrivilegeCount;
//       ULONG Control;
//       LUID_AND_ATTRIBUTES Privilege[ANYSIZE_ARRAY];  // 可变数组
//   } PRIVILEGE_SET;
// 所以 sizeof(PRIVILEGE_SET) 只计算到 Control 字段，不包括数组。

                           ((CapturedPrivilegeCount - ANYSIZE_ARRAY) *
//                           ^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^
// CapturedPrivilegeCount：用户态传进来的特权数量
// ANYSIZE_ARRAY：通常是 1（表示数组至少有一个元素）
// 为什么要减？因为 sizeof(PRIVILEGE_SET) 已经包含了 ANYSIZE_ARRAY 个元素的空间，
// 所以额外部分 = (总数 - 1) * 每个元素的大小。

                             (ULONG)sizeof(LUID_AND_ATTRIBUTES)  );
//                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 每个 LUID_AND_ATTRIBUTES 结构的大小（通常是 16 字节：8 字节 LUID + 4 字节 Attributes + 4 字节填充）

// ============================================================================
// 验证完整的 PRIVILEGE_SET 结构（包括数组）
// ============================================================================
         ProbeForWrite(
//       ^^^^^^^^^^^^^
// ProbeForWrite：验证用户态缓冲区是否可写（用于任意大小的缓冲区）。
// 如果缓冲区不可写或跨越无效地址，触发异常。

             RequiredPrivileges,
//           ^^^^^^^^^^^^^^^^^^
// 参数1：用户态指针

             ParameterLength,
//           ^^^^^^^^^^^^^^^
// 参数2：缓冲区的总长度（字节数）

             sizeof(ULONG)
//           ^^^^^^^^^^^^^
// 参数3：对齐要求（ULONG 对齐）
             );

// ============================================================================
// 验证 Result 输出参数是否可写
// ============================================================================
         ProbeForWriteBoolean(Result);
//       ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^
// ProbeForWriteBoolean：验证 BOOLEAN 指针是否可写（大小 1 字节）。
// 如果 Result 指针无效，触发异常。

// ============================================================================
// 读取控制标志
// ============================================================================
         PrivilegeSetControl = RequiredPrivileges->Control;
//       ^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^ ^^^^^^^
// 从用户态结构体中读取 Control 字段（ALL_NECESSARY 或 ANY）。
// 这个值后面会传给 SepPrivilegeCheck。

// ============================================================================
// try 块结束
// ============================================================================
    } except(EXCEPTION_EXECUTE_HANDLER) {
//    ^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^
// except：SEH 异常处理块。
// EXCEPTION_EXECUTE_HANDLER：宏，表示"执行这个 except 块来处理异常"。
// 如果 try 块内的任何代码触发异常（比如访问违例、除零等），
// 控制流会跳转到这里。

// ============================================================================
// 异常处理：获取异常代码
// ============================================================================
        Status = GetExceptionCode();
//      ^^^^^^   ^^^^^^^^^^^^^^^^^
// GetExceptionCode()：内核函数，返回当前异常的代码。
// 常见的异常代码：
//   STATUS_ACCESS_VIOLATION (0xC0000005)：访问违例（指针无效）
//   STATUS_DATATYPE_MISALIGNMENT (0x80000002)：对齐错误
//   ......
//
// 把异常代码赋值给 Status，后面会返回给调用者。

// ============================================================================
// except 块结束
// ============================================================================
    }

// ============================================================================
// 检查是否有异常发生或参数验证失败
// ============================================================================
    if (!NT_SUCCESS(Status)) {
//       ^ ^^^^^^^^^^^^^^^^^
// 如果 Status 不是成功码（说明前面有异常或参数无效）

// ============================================================================
// 清理：释放令牌对象的引用
// ============================================================================
        ObDereferenceObject( (PVOID)Token );
//      ^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^
// 减少令牌对象的引用计数（因为之前用 ObReferenceObjectByHandle 增加过）

// ============================================================================
// 返回错误码给调用者
// ============================================================================
        return Status;
//      ^^^^^^^^^^^^^
// 返回错误码（比如 STATUS_INVALID_PARAMETER 或 STATUS_ACCESS_VIOLATION）

// ============================================================================
// 结束 if
// ============================================================================
    }

// ============================================================================
// 捕获特权数组（从用户态复制到内核态）
// ============================================================================
    Status = SeCaptureLuidAndAttributesArray(
//  ^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// SeCaptureLuidAndAttributesArray：安全函数，从用户态复制 LUID_AND_ATTRIBUTES 数组到内核空间。
// 为什么需要复制？
//   1. 用户态内存可能被另一个线程修改（TOCTOU 问题）
//   2. 用户态指针可能指向无效内存
//   3. 内核代码访问用户态内存时可能触发页面错误，需要 try-except
//   4. 复制到内核空间后，可以安全地多次访问

                    (RequiredPrivileges->Privilege),
//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 参数1：用户态的源地址（特权数组的起始位置）
// RequiredPrivileges->Privilege：PRIVILEGE_SET 结构中的数组字段

                    CapturedPrivilegeCount,
//                  ^^^^^^^^^^^^^^^^^^^^^^
// 参数2：要复制的元素数量

                    UserMode,
//                  ^^^^^^^^
// 参数3：内存类型（UserMode 表示源地址在用户空间）

                    NULL, 0,
//                  ^^^^ ^
// 参数4、5：可选参数，这里传 NULL 和 0（不使用）

                    PagedPool,
//                  ^^^^^^^^^
// 参数6：分配内存的池子类型（PagedPool = 可分页池）
// 因为特权检查不是时间关键的，可以用可分页内存

                    TRUE,
//                  ^^^^
// 参数7：是否使用配额？TRUE 表示分配内存时使用进程的配额（防止内存耗尽攻击）

                    &CapturedPrivileges,
//                  ^^^^^^^^^^^^^^^^^^^
// 参数8：输出参数，指向复制后的内核缓冲区指针

                    &CapturedPrivilegesLength
//                  ^^^^^^^^^^^^^^^^^^^^^^^^^
// 参数9：输出参数，缓冲区长度（字节数）
                    );

// ============================================================================
// 检查复制是否成功
// ============================================================================
    if (!NT_SUCCESS(Status)) {
//       ^ ^^^^^^^^^^^^^^^^^
// 如果复制失败（比如内存不足、无效参数）

// ============================================================================
// 失败：清理并返回
// ============================================================================
        ObDereferenceObject( (PVOID)Token );
//      ^^^^^^^^^^^^^^^^^^^
// 释放令牌对象的引用

        return Status;
//      ^^^^^^^^^^^^^
// 返回错误码（比如 STATUS_INSUFFICIENT_RESOURCES）

// ============================================================================
// 结束 if
// ============================================================================
    }

// ============================================================================
// 调用核心特权检查函数
// ============================================================================
    BStatus = SepPrivilegeCheck(
//  ^^^^^^^   ^^^^^^^^^^^^^^^^
// BStatus：接收特权检查的结果（TRUE 或 FALSE）
// SepPrivilegeCheck：之前详细注释过的内部函数，负责真正的特权匹配逻辑

                  Token,                   // Token,
//                ^^^^^
// 参数1：令牌对象指针（之前通过 ObReferenceObjectByHandle 获取的）

                  CapturedPrivileges,      // RequiredPrivileges,
//                ^^^^^^^^^^^^^^^^^^^
// 参数2：捕获的特权数组（已经从用户态复制到内核空间）

                  CapturedPrivilegeCount,  // RequiredPrivilegeCount,
//                ^^^^^^^^^^^^^^^^^^^^^^
// 参数3：特权数量

                  PrivilegeSetControl,     // PrivilegeSetControl
//                ^^^^^^^^^^^^^^^^^^^
// 参数4：控制标志（ALL_NECESSARY 或 ANY）

                  PreviousMode             // PreviousMode
//                ^^^^^^^^^^^
// 参数5：之前的处理器模式（UserMode 或 KernelMode）
                  );

// ============================================================================
// 释放令牌对象的引用
// ============================================================================
    ObDereferenceObject( Token );
//  ^^^^^^^^^^^^^^^^^^^ ^^^^^
// 减少令牌对象的引用计数。
// 注意：这里是在调用 SepPrivilegeCheck 之后才释放，因为 SepPrivilegeCheck
//       内部还需要用到 Token 指针（它会读取令牌中的特权列表）。
// 
// 配对关系：
//   ObReferenceObjectByHandle (增加引用) → 获取令牌
//   使用令牌
//   ObDereferenceObject (减少引用) → 释放令牌
//
// 如果不释放，令牌对象永远不会被销毁（内存泄漏）。

// ============================================================================
// try 块：将结果写回用户态
// ============================================================================
    try {
//  ^^^
// 又一个异常处理块。为什么这里需要 try-except？
//   因为要把数据写回用户态的 RequiredPrivileges 和 Result 指针，
//   这些指针虽然在之前用 ProbeForWrite 验证过，但在此期间可能变得无效
//   （比如用户态线程 unmapped 了内存），所以需要用异常处理保护起来。

// ============================================================================
// 注释：把修改过的特权缓冲区复制回用户态
// ============================================================================
        //
        // copy the modified privileges buffer back to user
        // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        // 翻译："把修改过的特权缓冲区复制回用户态"
        //
        // 为什么要复制回去？
        //   因为 SepPrivilegeCheck 可能会修改特权数组中的 Attributes 字段，
        //   设置 SE_PRIVILEGE_USED_FOR_ACCESS 位（标记哪些特权被用到了）。
        //   调用者需要知道这个信息（比如记录审计日志）。
        //   所以要把修改后的数据写回用户态缓冲区。

// ============================================================================
// 复制修改后的特权数组回用户态
// ============================================================================
        RtlCopyMemory(
//      ^^^^^^^^^^^^^
// RtlCopyMemory：内核函数，复制内存（本质上是 memcpy 的封装）。
// 注意：目标地址在用户空间，源地址在内核空间。

            RequiredPrivileges->Privilege,
//          ^^^^^^^^^^^^^^^^^^ ^^^^^^^^^
// 目标地址：用户态的 PRIVILEGE_SET 结构中的 Privilege 数组

            CapturedPrivileges,
//          ^^^^^^^^^^^^^^^^^^^
// 源地址：内核态的捕获缓冲区（可能已被 SepPrivilegeCheck 修改过）

            CapturedPrivilegesLength
//          ^^^^^^^^^^^^^^^^^^^^^^^^^
// 要复制的字节数
            );

// ============================================================================
// 将检查结果写回用户态
// ============================================================================
        *Result = BStatus;
//      ^^^^^^^   ^^^^^^^
// 解引用 Result 指针（用户态 BOOLEAN 变量的地址），把 BStatus 的值写进去。
// Result 指针之前已经用 ProbeForWriteBoolean 验证过可写。
//
// 如果 BStatus = TRUE，用户态的变量变成 1（表示有权限）
// 如果 BStatus = FALSE，用户态的变量变成 0（表示没权限）

// ============================================================================
// try 块结束
// ============================================================================
        } except (EXCEPTION_EXECUTE_HANDLER) {
//        ^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^
// 如果上面的 RtlCopyMemory 或 *Result = ... 触发异常（比如用户态内存突然无效），
// 控制流跳转到这里。

// ============================================================================
// 异常处理：释放捕获的缓冲区，返回异常代码
// ============================================================================
            SeReleaseLuidAndAttributesArray(
//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// SeReleaseLuidAndAttributesArray：释放之前用 SeCaptureLuidAndAttributesArray 分配的内存。
// 因为异常发生了，需要在返回之前清理资源，防止内存泄漏。

               CapturedPrivileges,
//             ^^^^^^^^^^^^^^^^^^^
// 参数1：要释放的捕获缓冲区指针

               PreviousMode,
//             ^^^^^^^^^^^
// 参数2：之前的处理器模式（用于判断内存类型）

               TRUE
//             ^^^^
// 参数3：是否使用配额？与捕获时一致
               );

            return(GetExceptionCode());
//          ^^^^^^^^^^^^^^^^^^^^^^^^^
// 返回异常代码（比如 STATUS_ACCESS_VIOLATION）。
// 注意：这里没有执行下面的正常清理路径，直接返回了。

// ============================================================================
// except 块结束
// ============================================================================
        }

// ============================================================================
// 正常清理：释放捕获的特权数组
// ============================================================================
    SeReleaseLuidAndAttributesArray(
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 正常路径下的清理（没有发生异常时也会执行）
// 注意：这行代码不在 try 块内，所以如果它本身触发异常，没有处理程序（会导致 bugcheck）。

        CapturedPrivileges,
//      ^^^^^^^^^^^^^^^^^^^
// 要释放的捕获缓冲区指针

        PreviousMode,
//      ^^^^^^^^^^^
// 之前的处理器模式

        TRUE
//      ^^^^
// 是否使用配额
        );

// ============================================================================
// 返回成功
// ============================================================================
    return( STATUS_SUCCESS );
//  ^^^^^^^^^^^^^^^^^^^^^^^
// 返回 STATUS_SUCCESS，表示系统调用成功完成。
// 特权检查的结果已经通过 Result 输出参数返回给调用者。
//
// 注意：即使 BStatus = FALSE（用户没有权限），这里也返回 STATUS_SUCCESS。
// 因为"成功完成检查"和"检查结果为假"是两回事。
// 类比：您问银行"我有 100 万吗？"，银行回答"没有"——这个回答本身是成功的操作，
//       只是答案是假的。

// ============================================================================
// 函数体结束
// ============================================================================
}
// 右花括号：NtPrivilegeCheck 函数定义结束。


// ============================================================================
// 分页符
// ============================================================================

// 换页符，表示新函数 SeSinglePrivilegeCheck 的开始。

// ============================================================================
// 函数：SeSinglePrivilegeCheck（检查单个特权）
// ============================================================================
BOOLEAN
// ^^^^^^^
// 返回值类型：BOOLEAN
//   TRUE = 当前主体拥有这个特权
//   FALSE = 当前主体没有这个特权

SeSinglePrivilegeCheck(
// ^^^^^^^^^^^^^^^^^^^^^
// 函数名：SeSinglePrivilegeCheck
//   Se = Security（安全）
//   Single = 单个
//   PrivilegeCheck = 特权检查
//
// 这是 SePrivilegeCheck 的"简化版"——只检查一个特权，不需要传数组。
// 很多内核代码只需要检查一个特权（比如"能关机吗"），用这个函数更方便。
//
// 类比：SePrivilegeCheck 是"批发市场"（一次买一堆），
//       SeSinglePrivilegeCheck 是"便利店"（只买一样），
//       价格（代码复杂度）更便宜。

    LUID PrivilegeValue,
//  ^^^^ ^^^^^^^^^^^^^^^
// 参数1：PrivilegeValue
//   类型：LUID（64位本地唯一标识符）
//   作用：要检查的特权值（比如 SeShutdownPrivilege 的 LUID）。
//   示例：
//     - SE_SHUTDOWN_PRIVILEGE：关机特权
//     - SE_DEBUG_PRIVILEGE：调试特权
//     - SE_LOAD_DRIVER_PRIVILEGE：加载驱动特权
//
// 什么是 LUID？
//   虽然叫"唯一标识符"，但在特权场景下，它是一个固定的 64 位值，
//   每个特权类型对应一个 LUID（比如 0x123 = 关机，0x456 = 调试）。
//   系统启动时，会为每个特权分配一个 LUID，并保证在整个系统运行期间不变。

    KPROCESSOR_MODE PreviousMode
//  ^^^^^^^^^^^^^^^^ ^^^^^^^^^^^
// 参数2：PreviousMode
//   类型：KPROCESSOR_MODE（UserMode = 1, KernelMode = 0）
//   作用：调用者之前的处理器模式。
//     - KernelMode：内核调用，直接返回 TRUE（信任内核）
//     - UserMode：用户调用，需要真正检查令牌
    )

// ============================================================================
// 函数头注释（微软文档格式）
// ============================================================================
/*++
// ^^^

Routine Description:
// ^^^^^^^^^^^^^^^^^^^

    This function will check for the passed privilege value in the
    current context.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 翻译："这个函数会在当前上下文中检查传入的特权值是否存在。"
//
// "当前上下文"是什么意思？
//   就是当前线程的安全上下文（进程令牌 + 可能的模拟令牌）。
//   不需要像 NtPrivilegeCheck 那样传入 ClientToken 句柄，
//   直接查当前线程的令牌。

Arguments:
// ^^^^^^^^^

    PrivilegeValue - The value of the privilege being checked.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// PrivilegeValue：要检查的特权的值（LUID）

Return Value:
// ^^^^^^^^^^^

    TRUE - The current subject has the desired privilege.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// TRUE：当前主体拥有所需的特权

    FALSE - The current subject does not have the desired privilege.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// FALSE：当前主体没有所需的特权

--*/
// 注释结束。

// ============================================================================
// 函数体开始
// ============================================================================
{

// ============================================================================
// 局部变量声明
// ============================================================================
    BOOLEAN AccessGranted;
//  ^^^^^^^ ^^^^^^^^^^^^^
// 类型：BOOLEAN
// 变量名：AccessGranted
// 作用：存储特权检查的结果（TRUE 表示有权限，FALSE 表示没权限）

    PRIVILEGE_SET RequiredPrivileges;
//  ^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^
// 类型：PRIVILEGE_SET（特权集合结构体）
// 变量名：RequiredPrivileges
// 作用：在栈上分配一个 PRIVILEGE_SET 结构体（不需要从堆上分配）。
//       因为这个函数只检查一个特权，所以可以直接在栈上定义。
//
// PRIVILEGE_SET 结构体包含：
//   - PrivilegeCount：特权数量（这里设为 1）
//   - Control：控制标志（这里设为 ALL_NECESSARY）
//   - Privilege[ANYSIZE_ARRAY]：特权数组（这里只有一个元素）

    SECURITY_SUBJECT_CONTEXT SubjectSecurityContext;
//  ^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^
// 类型：SECURITY_SUBJECT_CONTEXT（安全主体上下文结构体）
// 变量名：SubjectSecurityContext
// 作用：存储当前线程的安全上下文（主令牌 + 模拟令牌 + 模拟级别）
//       后面会通过 SeCaptureSubjectContext 填充。

// ============================================================================
// 分页代码检查
// ============================================================================
    PAGED_CODE();
// 确保当前 IRQL 不高于 APC_LEVEL。
// 这个函数是分页的（在 PAGE 段），不能在高于 APC_LEVEL 的中断级别调用。

// ============================================================================
// 注释：确保调用者有权限
// ============================================================================
    //
    // Make sure the caller has the privilege to make this
    // call.
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 翻译："确保调用者有权限发出这个调用。"
    //
    // 等等，这个注释有点奇怪——这个函数本身不检查调用者是否有
    // SeTcbPrivilege 之类的特权。它只是检查调用者是否拥有传入的特权值。
    // 所以这个注释可能是误导的，或者是从其他函数复制过来的。
    //
    // 实际上，这个函数不要求调用者有特殊权限。
    // 任何人都可以查自己有没有某个特权（比如普通用户查自己有没有关机特权）。
    // 真正的限制在 SePrivilegeCheck 里（它不会让普通用户查别人的令牌）。

// ============================================================================
// 构建特权集合结构（只有一个特权）
// ============================================================================
    RequiredPrivileges.PrivilegeCount = 1;
//  ^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^ ^^^
// 设置 PrivilegeCount 字段为 1，表示只需要检查 1 个特权。

    RequiredPrivileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
//  ^^^^^^^^^^^^^^^^^^ ^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 设置 Control 字段为 PRIVILEGE_SET_ALL_NECESSARY。
// 因为只有一个特权，所以"全部需要"和"任意一个"没区别，
// 但按照惯例用 ALL_NECESSARY。

    RequiredPrivileges.Privilege[0].Luid = PrivilegeValue;
//  ^^^^^^^^^^^^^^^^^^ ^^^^^^^^^ ^^^^ ^^^   ^^^^^^^^^^^^^^
// 设置数组第一个（也是唯一一个）元素的 Luid 字段为传入的 PrivilegeValue。
// 比如传入的是 SeShutdownPrivilege 的 LUID，这里就存进去。

    RequiredPrivileges.Privilege[0].Attributes = 0;
//  ^^^^^^^^^^^^^^^^^^ ^^^^^^^^^ ^^^^ ^^^^^^^^^^ ^^^
// 设置 Attributes 字段为 0。
// 这个字段初始为 0，后面的 SePrivilegeCheck 可能会设置 SE_PRIVILEGE_USED_FOR_ACCESS 位。

// ============================================================================
// 捕获当前线程的安全上下文
// ============================================================================
    SeCaptureSubjectContext( &SubjectSecurityContext );
//  ^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^
// SeCaptureSubjectContext：捕获当前线程的安全上下文。
//   它会：
//     1. 获取当前进程的主令牌（Primary Token）
//     2. 获取当前线程的模拟令牌（如果有）
//     3. 记录模拟级别
//     4. 增加令牌的引用计数（防止被释放）
//     5. 锁定上下文，确保后续操作的一致性
//
// 这个函数必须在调用 SePrivilegeCheck 之前调用，
// 并且在调用完成后必须调用 SeReleaseSubjectContext 释放。
//
// 类比：这是"拍快照"——在检查特权的那一刻，把当前用户的安全身份固定下来，
//       防止在检查过程中令牌被改变。

// ============================================================================
// 调用 SePrivilegeCheck 检查特权
// ============================================================================
    AccessGranted = SePrivilegeCheck(
//  ^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^
// 调用前面注释过的 SePrivilegeCheck（对外 API）
// 返回值赋值给 AccessGranted

                        &RequiredPrivileges,
//                      ^^^^^^^^^^^^^^^^^^^^
// 参数1：指向 RequiredPrivileges 结构的指针（包含 1 个特权）

                        &SubjectSecurityContext,
//                      ^^^^^^^^^^^^^^^^^^^^^^^^
// 参数2：指向安全主体上下文的指针（当前线程的身份）

                        PreviousMode
//                      ^^^^^^^^^^^
// 参数3：之前的处理器模式
                        );

// ============================================================================
// 如果是用户态调用，需要记录审计日志
// ============================================================================
    if ( PreviousMode != KernelMode ) {
//       ^^^^^^^^^^^ ^= ^^^^^^^^^^
// 如果 PreviousMode 不等于 KernelMode，说明是用户态调用。
// 内核态调用（比如系统线程）不需要审计（因为内核本身是可信的）。

// ============================================================================
// 调用审计日志记录函数
// ============================================================================
        SePrivilegedServiceAuditAlarm (
//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// SePrivilegedServiceAuditAlarm：记录特权服务访问的审计日志。
//   这个函数会在安全事件日志里写一条记录，内容类似：
//     "某个进程尝试使用特权 X，结果是成功/失败"
//
// 为什么需要审计？
//   安全合规要求（比如 ISO 27001、PCI-DSS），
//   需要记录谁尝试了敏感操作（比如关机、改系统时间）。
//
// 参数说明：
//   NULL：子类别名称（传 NULL 表示使用默认）
//   &SubjectSecurityContext：主体上下文（谁干的）
//   &RequiredPrivileges：需要的特权（哪个特权）
//   AccessGranted：结果（成功还是失败）

            NULL,
//          ^^^^
// 参数1：子类别（Subcategory），传 NULL 表示默认

            &SubjectSecurityContext,
//          ^^^^^^^^^^^^^^^^^^^^^^^^
// 参数2：安全主体上下文（当前用户是谁）

            &RequiredPrivileges,
//          ^^^^^^^^^^^^^^^^^^^^
// 参数3：所需特权集合（要检查哪个特权）

            AccessGranted
//          ^^^^^^^^^^^^^
// 参数4：是否授予了访问权限
            );
    }

// ============================================================================
// 注意：这里缺少释放安全上下文的代码！
// ============================================================================
// 按照规范，在 SeCaptureSubjectContext 之后必须调用 SeReleaseSubjectContext
// 来释放令牌引用和解锁。
//
// 猜测：后面的代码（可能没贴出来）应该有：
//   SeReleaseSubjectContext(&SubjectSecurityContext);
//
// 如果没有，那就是一个 bug（或者这个函数在别的地方有清理）。
// 但考虑到这是微软的代码，应该是有释放的，只是没贴全。

// ============================================================================
// 返回检查结果
// ============================================================================
    return( AccessGranted );
//  ^^^^^^^^^^^^^^^^^^^^^^
// 返回 AccessGranted（TRUE 表示有特权，FALSE 表示没有）。
//
// 注意：这里没有 try-except 包裹，因为这个函数只操作内核空间的内存，
//       不直接访问用户态指针，不会触发异常。

// ============================================================================
// 函数体结束
// ============================================================================
    }
// 右花括号：SeSinglePrivilegeCheck 函数定义结束。


// ============================================================================
// 释放安全主体上下文
// ============================================================================
    SeReleaseSubjectContext( &SubjectSecurityContext );
//  ^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^
// SeReleaseSubjectContext：释放之前捕获的安全主体上下文。
//   这是 SeCaptureSubjectContext 的配对函数。
//
// 它会做什么？
//   1. 减少主令牌（Primary Token）的引用计数
//   2. 减少模拟令牌（Impersonation Token）的引用计数（如果有）
//   3. 解锁安全上下文
//
// 为什么必须调用？
//   如果不调用，令牌的引用计数永远不会归零，
//   令牌对象会一直占着内存，直到系统重启（内存泄漏）。
//
// 类比：图书馆的书（令牌），您借了（Capture）就要还（Release），
//       不还会被罚款（内存泄漏）。

// ============================================================================
// 返回特权检查结果
// ============================================================================
    return( AccessGranted );
//  ^^^^^^^^^^^^^^^^^^^^^^
// 返回 AccessGranted（TRUE = 有特权，FALSE = 没有特权）
// 注意：这里和 SeSinglePrivilegeCheck 一样，没有异常处理。

// ============================================================================
// 函数体结束
// ============================================================================
}
// 右花括号：SeSinglePrivilegeCheck 函数定义结束。

// ============================================================================
// 分页符
// ============================================================================

// 换页符，表示下一个函数 SeCheckPrivilegedObject 的开始。

// ============================================================================
// 函数：SeCheckPrivilegedObject（检查对象访问特权）
// ============================================================================
BOOLEAN
// ^^^^^^^
// 返回值类型：BOOLEAN
//   TRUE = 当前主体拥有这个特权
//   FALSE = 当前主体没有这个特权

SeCheckPrivilegedObject(
// ^^^^^^^^^^^^^^^^^^^^^^
// 函数名：SeCheckPrivilegedObject
//   Se = Security（安全）
//   Check = 检查
//   Privileged = 有特权的
//   Object = 对象
//
// 这个函数是 SeSinglePrivilegeCheck 的"升级版"——除了检查特权，
//   还会记录审计日志，并且日志里包含对象句柄和访问掩码信息。
//   用于"需要特权才能访问某个对象"的场景（比如打开一个受保护的进程）。

    LUID PrivilegeValue,
//  ^^^^ ^^^^^^^^^^^^^^^
// 参数1：PrivilegeValue
//   类型：LUID
//   作用：要检查的特权值（比如 SeDebugPrivilege）

    HANDLE ObjectHandle,
//  ^^^^^^ ^^^^^^^^^^^^
// 参数2：ObjectHandle
//   类型：HANDLE（内核句柄）
//   作用：被访问对象的句柄（比如进程句柄、文件句柄）。
//   这个句柄会被记录在审计日志中，用于追踪"通过哪个句柄访问了哪个对象"。
//
// 注意：这个函数不验证句柄的有效性，它只是把句柄值传给审计函数。
//       真正的句柄解析在别的地方做。

    ACCESS_MASK DesiredAccess,
//  ^^^^^^^^^^^ ^^^^^^^^^^^^^
// 参数3：DesiredAccess
//   类型：ACCESS_MASK（32位位掩码）
//   作用：请求的访问权限（比如 PROCESS_TERMINATE、PROCESS_VM_READ）。
//   审计日志会记录"用户想要用这个特权做什么操作"。
//
// 示例：用户想打开一个进程并终止它，
//       DesiredAccess = PROCESS_TERMINATE

    KPROCESSOR_MODE PreviousMode
//  ^^^^^^^^^^^^^^^^ ^^^^^^^^^^^
// 参数4：PreviousMode
//   类型：KPROCESSOR_MODE
//   作用：之前的处理器模式（UserMode 或 KernelMode）
    )

// ============================================================================
// 函数头注释（微软文档格式）
// ============================================================================
/*++
// ^^^

Routine Description:
// ^^^^^^^^^^^^^^^^^^^

    This function will check for the passed privilege value in the
    current context, and generate audits as appropriate.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 翻译："这个函数会在当前上下文中检查传入的特权值，并在适当时生成审计日志。"

Arguments:
// ^^^^^^^^^

    PrivilegeValue - The value of the privilege being checked.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// PrivilegeValue：要检查的特权值

    Object - Specifies a pointer to the object being accessed.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// 注意：参数列表里没有 Object！这是个文档错误。
// 实际参数是 ObjectHandle，不是 Object。

    ObjectHandle - Specifies the object handle being used.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// ObjectHandle：被使用的对象句柄

    DesiredAccess - The desired access mask, if any
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// DesiredAccess：请求的访问掩码（如果有）

    PreviousMode - The previous processor mode
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// PreviousMode：之前的处理器模式

Return Value:
// ^^^^^^^^^^^

    TRUE - The current subject has the desired privilege.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// TRUE：当前主体拥有所需的特权

    FALSE - The current subject does not have the desired privilege.
//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// FALSE：当前主体没有所需的特权

--*/
// 注释结束。

// ============================================================================
// 函数体开始
// ============================================================================
{

// ============================================================================
// 局部变量声明
// ============================================================================
    BOOLEAN AccessGranted;
//  ^^^^^^^ ^^^^^^^^^^^^^
// 类型：BOOLEAN
// 变量名：AccessGranted
// 作用：存储特权检查的结果

    PRIVILEGE_SET RequiredPrivileges;
//  ^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^
// 类型：PRIVILEGE_SET
// 变量名：RequiredPrivileges
// 作用：在栈上构造的特权集合（只有一个特权）

    SECURITY_SUBJECT_CONTEXT SubjectSecurityContext;
//  ^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^
// 类型：SECURITY_SUBJECT_CONTEXT
// 变量名：SubjectSecurityContext
// 作用：存储当前线程的安全上下文

// ============================================================================
// 分页代码检查
// ============================================================================
    PAGED_CODE();
// 确保 IRQL 不高于 APC_LEVEL。

// ============================================================================
// 注释：确保调用者有权限
// ============================================================================
    //
    // Make sure the caller has the privilege to make this
    // call.
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 又是这个误导性注释。和 SeSinglePrivilegeCheck 一样，
    // 这个函数本身不要求调用者有特殊权限。
    // 可能是从别的函数复制粘贴过来的。

// ============================================================================
// 构建特权集合（只有一个特权）
// ============================================================================
    RequiredPrivileges.PrivilegeCount = 1;
// 设置特权数量为 1

    RequiredPrivileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
// 设置控制标志为 ALL_NECESSARY

    RequiredPrivileges.Privilege[0].Luid = PrivilegeValue;
// 设置第一个特权的 LUID 为传入的特权值

    RequiredPrivileges.Privilege[0].Attributes = 0;
// 初始化 Attributes 为 0

// ============================================================================
// 捕获当前线程的安全上下文
// ============================================================================
    SeCaptureSubjectContext( &SubjectSecurityContext );
// 获取当前线程的安全身份（主令牌 + 模拟令牌）

// ============================================================================
// 调用 SePrivilegeCheck 检查特权
// ============================================================================
    AccessGranted = SePrivilegeCheck(
                        &RequiredPrivileges,
                        &SubjectSecurityContext,
                        PreviousMode
                        );
// 和 SeSinglePrivilegeCheck 一样，调用通用特权检查函数

// ============================================================================
// 如果是用户态调用，需要记录对象访问审计日志
// ============================================================================
    if ( PreviousMode != KernelMode ) {
//       ^^^^^^^^^^^ ^= ^^^^^^^^^^
// 如果调用者是用户态，需要审计

// ============================================================================
// 调用对象访问审计日志函数
// ============================================================================
        SePrivilegeObjectAuditAlarm(
//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^
// SePrivilegeObjectAuditAlarm：记录"特权对象访问"的审计日志。
//   和 SePrivilegedServiceAuditAlarm 不同，这个函数专门用于对象访问场景，
//   会记录句柄和访问掩码信息。
//
// 审计日志会记录：
//   "用户 X 试图用句柄 Y 访问对象 Z，请求了权限 M，结果是成功/失败"

            ObjectHandle,
//          ^^^^^^^^^^^^
// 参数1：对象句柄（哪个句柄被用来访问对象）

            &SubjectSecurityContext,
//          ^^^^^^^^^^^^^^^^^^^^^^^^
// 参数2：安全主体上下文（谁干的）

            DesiredAccess,
//          ^^^^^^^^^^^^^
// 参数3：请求的访问掩码（想要什么权限，比如 PROCESS_TERMINATE）

            &RequiredPrivileges,
//          ^^^^^^^^^^^^^^^^^^^^
// 参数4：所需特权集合（需要什么特权）

            AccessGranted,
//          ^^^^^^^^^^^^^
// 参数5：是否授予了访问权限

            PreviousMode
//          ^^^^^^^^^^^
// 参数6：之前的处理器模式
            );

    }
// 结束 if

// ============================================================================
// 释放安全主体上下文
// ============================================================================
    SeReleaseSubjectContext( &SubjectSecurityContext );
//  ^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^
// SeReleaseSubjectContext：释放之前用 SeCaptureSubjectContext 捕获的安全上下文。
//   这是每个 SeCaptureSubjectContext 调用都必须配对的"还书"操作。
//
// 具体做了什么？
//   1. 减少主令牌（Primary Token）的引用计数
//      - 如果引用计数降到 0，令牌对象会被销毁
//   2. 如果有模拟令牌（Impersonation Token），也减少其引用计数
//   3. 解锁安全上下文（允许其他线程修改）
//   4. 清除上下文结构体中的指针（防止悬空指针）
//
// 为什么要在这里调用？
//   因为前面已经完成了所有需要用到安全上下文的工作：
//     - SePrivilegeCheck 已经执行完毕
//     - 审计日志（SePrivilegeObjectAuditAlarm）已经记录
//   不再需要令牌信息了，可以释放了。
//
// 如果不调用会怎样？
//   - 令牌对象的引用计数永远不归零 → 内存泄漏
//   - 系统资源被白白占用 → 长期运行后系统变慢
//   - 最终可能耗尽内核内存 → 系统崩溃（蓝屏）
//
// 类比：您在图书馆借了一本书（SeCaptureSubjectContext），
//       看完了（检查特权、记录日志），就要还回去（SeReleaseSubjectContext），
//       不然别人借不到这本书（资源被占用），您还要交罚款（内存泄漏）。
//
// 注意：这是一个非常重要的配对操作。
//       在 Windows 内核代码审查中，检查 SeCaptureSubjectContext 和
//       SeReleaseSubjectContext 是否成对出现，是常见的安全审查项。
//       如果发现不成对，直接打回不给签入（commit）。

// ============================================================================
// 返回特权检查结果
// ============================================================================
    return( AccessGranted );
//  ^^^^^^^^^^^^^^^^^^^^^^
// 返回 AccessGranted 给调用者。
//
// AccessGranted 的值来自前面的 SePrivilegeCheck：
//   - TRUE：当前主体拥有所需的特权（比如有 SeDebugPrivilege）
//   - FALSE：当前主体没有这个特权
//
// 调用者会根据这个返回值决定：
//   - TRUE → 允许执行受保护的操作（比如打开进程、关机）
//   - FALSE → 拒绝访问，返回 STATUS_PRIVILEGE_NOT_HELD
//
// 注意：这里没有异常处理（try-except），因为这个函数只操作内核内存，
//       不直接访问用户态指针。所有用户态数据（句柄值）只是作为数值传递，
//       不会被解引用，所以不会触发页面错误。

// ============================================================================
// 函数体结束
// ============================================================================
}
// 右花括号：SeCheckPrivilegedObject 函数定义结束。

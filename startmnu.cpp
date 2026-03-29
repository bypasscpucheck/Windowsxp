/*
 * 开始菜单宿主实现。
 * 这个文件负责 Windows 开始菜单的核心逻辑。
 * 是的，你每天点那个“开始”按钮，背后就是这段代码在工作。
 * 虽然 Windows 改了好多遍，但内核还是这套。
 * 就像你换了新衣服，但人还是那个人。
 */

#include "cabinet.h"      //  cabinet 相关。别问，问就是历史。
#include "rcids.h"        //  资源 ID。字符串、图标、对话框。
#include <shguidp.h>      //  Shell GUID。微软的 UUID 大杂烩。
#include <lmcons.h>       //  LAN Manager 常量。NT 时代的遗产。
#include "bandsite.h"     //  工具栏站点接口。开始菜单其实是个工具栏。
#include "shellp.h"       //  Shell 私有头文件。微软不让看的部分。
#include "shdguid.h"      //  更多 GUID。看到 GUID 就头疼？你不是一个人。
#include <regstr.h>       //  注册表字符串常量。注册表路径。
#include "startmnu.h"     //  开始菜单私有头。真正的黑魔法在这里。
#include "trayp.h"        //  托盘私有头。WMTRAY_* 常量的老家。
#include "tray.h"         //  托盘实现。系统托盘那堆图标。
#include "util.h"         //  工具函数。什么都往里塞的垃圾桶头文件。

/*
 * 获取静态开始菜单。
 * 如果 fEdit 是 TRUE，返回的是可编辑版本（用于自定义菜单）。
 * 否则返回只读版本。
 *
 * 这个函数定义在别处。可能是 startmnu.c 或者 startmnui.c。
 * 反正你找的时候会发现：头文件里有声明，实现藏得很深。
 * 微软代码的传统：声明和实现永远不在一起。
 * 让你找 bug 的时候多翻几个文件，锻炼身体。
 */
HMENU GetStaticStartMenu(BOOL fEdit);

// *** IUnknown 方法 ***
// COM 接口的三大件：QueryInterface、AddRef、Release。
// 写 COM 组件就是写这三个函数，然后抄无数遍。
// 每次抄的时候都会想：为什么 C++ 没有自动生成这个？
// 后来 C++ 有了，但微软的代码是 C++ 出来之前写的。
// 所以还在手写。就像现在还有人用手写支票一样。
//
STDMETHODIMP CStartMenuHost::QueryInterface (REFIID riid, LPVOID * ppvObj)
{
    /*
     * QITAB（QueryInterface Table）是个宏魔法。
     * 它把一个接口映射表展开成代码。
     * 这样你就不用写 20 个 if (IsEqualIID(...)) 了。
     * 你只需要写这个表，QISearch 帮你遍历。
     *
     * 好处：代码少。
     * 坏处：出 bug 的时候你根本不知道 QISearch 干了什么。
     * 所以调试的时候你只能相信它。或者不信，然后自己写 20 个 if。
     * 大部分人会选择相信。
     */
    static const QITAB qit[] =
    {
        /*
         * QITABENTMULTI 的意思是：这个类继承自多个接口。
         * 同一个指针，可以当作 IOleWindow 用，也可以当作 IMenuPopup 用。
         * 这是多重继承在 COM 里的体现。
         * C++ 的多重继承被骂了三十年，COM 用得更欢。
         */
        QITABENTMULTI(CStartMenuHost, IOleWindow, IMenuPopup),
        QITABENTMULTI(CStartMenuHost, IDeskBarClient, IMenuPopup),
        QITABENT(CStartMenuHost, IMenuPopup),
        QITABENT(CStartMenuHost, ITrayPriv),
        QITABENT(CStartMenuHost, IShellService),
        QITABENT(CStartMenuHost, IServiceProvider),
        QITABENT(CStartMenuHost, IOleCommandTarget),
        QITABENT(CStartMenuHost, IWinEventHandler),
        { 0 },  // 表结束标记。QISearch 看到这个就知道到头了。
    };

    /*
     * QISearch：在 qit 表里找 riid 对应的接口。
     * 找到了就返回指针，加引用计数。
     * 找不到就返回 E_NOINTERFACE。
     *
     * 这个函数是微软自己的 helper。不是 Windows API。
     * 你要用的话，得自己实现或者抄 shlwapi.lib。
     * 大部分人选择抄。因为抄比链接快。
     */
    return QISearch(this, qit, riid, ppvObj);
}

/*
 * AddRef：增加引用计数。
 * 返回值是新的引用计数。
 *
 * 为什么用 ++_cRef 而不是 _cRef++？
 * 因为前置递增比后置递增少一个临时变量。
 * 在 COM 的世界里，每一个 CPU 周期都很重要。
 * 重要个屁。就是写代码的人习惯。
 */
STDMETHODIMP_(ULONG) CStartMenuHost::AddRef ()
{
    return ++_cRef;
}

/*
 * Release：减少引用计数。
 * 当引用计数降到 0 时，自己把自己删掉。
 *
 * 注意这个 ASSERT：_cRef > 0。
 * 如果 _cRef 已经是 0 还 Release，说明有人多 Release 了一次。
 * 这是 COM 编程最常见的 bug 之一。
 * 你会在调试版里看到断言失败，然后花两小时找谁多调了一次 Release。
 * 然后发现是某个同事的代码。但他已经离职了。
 * 所以你就成了这个 bug 的主人。
 * 然后你改 bug，引入新 bug。
 * 这就是软件开发。
 */
STDMETHODIMP_(ULONG) CStartMenuHost::Release()
{
    ASSERT(_cRef > 0);
    _cRef--;

    if( _cRef > 0)
        return _cRef;

    /*
     * 引用计数为 0，自己把自己干掉。
     * 注意：delete this 是合法的，只要你保证这个对象是在堆上分配的。
     * 如果有人在栈上创建了这个对象然后调用 Release，你就完了。
     * 但 COM 规定：所有对象都应该是堆分配的。
     * 所以你不用担心。如果你担心，说明你的同事不守规矩。
     */
    delete this;
    return 0;
}

/*----------------------------------------------------------
用途：ITrayPriv::ExecItem 方法
实现 ITrayPriv 接口的命令执行功能。

谁调用这个方法？系统托盘（tray）在用户点击某个图标时调用。
用户点击开始菜单里的某个项目，最终会走到这里。
然后 ShellExecute 被调用，程序就启动了。

所以从点击开始菜单到程序启动，中间经历了：
用户点击 → 窗口消息 → 托盘处理 → ITrayPriv → 这里 → ShellExecute
中间有十几层调用栈，每一层都可能出 bug。
但 Windows 就这样跑了二十多年，也没全坏。
奇迹。
----------------------------------------------------------*/
STDMETHODIMP CStartMenuHost::ExecItem (IShellFolder* psf, LPCITEMIDLIST pidl)
{
    /*
     * ShellExecute 会显示错误（如果有的话）。
     * 所以这里不需要再显示错误。
     *
     * 这句话翻译成人话：如果 SHInvokeDefaultCommand 失败了，
     * 它会自己弹个对话框告诉你“找不到文件”或者“没有权限”。
     * 我们不用再弹一个。弹多了用户会烦。
     *
     * 而且我们也不知道怎么处理错误。
     * 文件找不到？我们能怎么办？变一个出来？
     * 所以让 Shell 自己处理吧。
     *
     * SHInvokeDefaultCommand 的 v_hwndTray 是托盘的窗口句柄。
     * 这样错误对话框的父窗口是托盘，看起来像是开始菜单弹出来的。
     * 用户以为是开始菜单的问题，其实是文件没了。
     * 用户骂开始菜单，开始菜单骂 Shell，Shell 骂文件系统。
     * 文件系统骂硬盘。
     * 硬盘说：“我是好的，是你的文件被删了。”
     * 用户说：“我没删。”
     * 然后开始吵架。
     * 这就是为什么技术支持电话那么多。
     */
    return SHInvokeDefaultCommand(v_hwndTray, psf, pidl);
}

/*----------------------------------------------------------
用途：ITrayPriv::GetFindCM 方法
获取“查找”功能的上下文菜单。

这个方法返回一个 IContextMenu 接口，用来显示“查找文件”、“查找计算机”等菜单项。
谁需要这个？系统托盘。当用户点击开始菜单里的“查找”时，托盘会调用这个。
然后托盘显示一个子菜单，里面有“文件或文件夹”、“计算机”之类的选项。

现在 Windows 10/11 里还有这个吗？有，但藏得很深。
微软想让你用搜索框，而不是“查找”菜单。
但老用户还在用，所以代码还在。
就像你奶奶还在用 IE，微软不敢删。
----------------------------------------------------------*/
STDMETHODIMP CStartMenuHost::GetFindCM(HMENU hmenu, UINT idFirst, UINT idLast, IContextMenu** ppcmFind)
{
    /*
     * SHFind_InitMenuPopup：初始化“查找”菜单的弹出菜单。
     * 参数：
     *   hmenu - 父菜单句柄
     *   v_hwndTray - 托盘窗口句柄（错误对话框的父窗口）
     *   TRAY_IDM_FINDFIRST, TRAY_IDM_FINDLAST - 菜单项的 ID 范围
     *
     * 这个函数会创建一个 IContextMenu 对象，里面包含了“查找”相关的命令。
     * 为什么是“FIRST”和“LAST”？因为菜单项 ID 是连续的。
     * 比如 100 到 105，FIRST=100，LAST=105。
     * 这样在循环里就能判断 ID 是否在这个范围内。
     * 这是 Win32 菜单编程的传统艺能：用 ID 范围表示一组相关的菜单项。
     */
    *ppcmFind = SHFind_InitMenuPopup(hmenu, v_hwndTray, TRAY_IDM_FINDFIRST, TRAY_IDM_FINDLAST);
    
    /*
     * 如果成功创建了，返回 NOERROR（S_OK）。
     * 否则返回 E_FAIL。
     *
     * 为什么不是 E_OUTOFMEMORY？因为 SHFind_InitMenuPopup 可能因为各种原因失败。
     * 我们不知道具体原因，所以返回一个模糊的“失败了”。
     * 调用者也不关心为什么失败，只知道没拿到菜单。
     * 然后它可能什么都不显示，或者显示个灰的菜单项。
     * 用户看到灰的，以为不能用。
     * 其实是内存不够了。
     * 但用户不知道。
     * 这就是“错误处理哲学”：用户不需要知道为什么，只需要知道不行。
     */
    if(*ppcmFind)
        return NOERROR;
    else
        return E_FAIL;
}


/*----------------------------------------------------------
用途：ITrayPriv::GetStaticStartMenu 方法
获取静态开始菜单的句柄。

“静态开始菜单”是什么？就是 Windows 经典模式的那个开始菜单。
不是 Windows XP 那个，也不是 Windows 7 那个，更不是 Windows 10 那个。
是 Windows 95/98/NT 4.0 那个。
是的，这个代码从 Windows 95 就有了。
微软一直留着，因为有些企业客户还在用经典模式。
这些企业客户付的钱比普通用户多，所以他们的需求就是圣旨。
----------------------------------------------------------*/
STDMETHODIMP CStartMenuHost::GetStaticStartMenu(HMENU* phmenu)
{
    /*
     * ::GetStaticStartMenu(TRUE) 是全局函数。
     * 参数 TRUE 表示返回可编辑版本（允许自定义）。
     * FALSE 表示只读版本（不允许改）。
     *
     * 为什么用 TRUE？因为开始菜单本来就应该让用户自定义。
     * 用户可以添加删除程序、重命名、排序。
     * 这些都是通过这个可编辑菜单实现的。
     *
     * 注意：这个函数是全局的，不是成员函数。
     * 在 C++ 里，:: 表示全局作用域。
     * 如果没写 ::，编译器会找 CStartMenuHost::GetStaticStartMenu。
     * 那就会无限递归。然后堆栈溢出。然后蓝屏。
     * 所以 :: 很重要。就像你发微信时 @ 人一样。
     * 不 @ 就没人知道你在叫谁。
     */
    *phmenu = ::GetStaticStartMenu(TRUE);

    if(*phmenu)
        return NOERROR;
    else
        return E_FAIL;
}

// *** IServiceProvider ***
// 服务提供者接口。COM 用来查询其他接口的一种方式。
// QueryInterface 是按接口查，QueryService 是按服务查。
// 区别？没什么区别。就是多了一层抽象。
// 微软喜欢加抽象层。就像洋葱，剥开一层还有一层。
// 剥到最后你会哭，因为辣眼睛。
//
STDMETHODIMP CStartMenuHost::QueryService (REFGUID guidService, REFIID riid, void ** ppvObject)
{
    /*
     * 如果请求的服务是 SID_SMenuPopup（开始菜单弹出服务），
     * 就调用 QueryInterface 返回 IMenuPopup 接口。
     *
     * 否则返回 E_NOINTERFACE（不支持这个服务）。
     *
     * 这段代码的意思是：这个对象只提供一种服务，就是把自己当 IMenuPopup 用。
     * 你要别的服务？没有。去找别人。
     */
    if(IsEqualGUID(guidService,SID_SMenuPopup))
        return QueryInterface(riid,ppvObject);
    else
        return E_NOINTERFACE;
}


// *** IShellService ***
// Shell 服务接口。IShellService 只有一个方法：SetOwner。
// 设置这个服务的所有者。谁调用？一般是 Shell 自己。
// 但这里返回 E_NOTIMPL（未实现）。
// 说明这个功能没做完。或者做完了但没人用。
// 微软代码里到处都是 E_NOTIMPL。就像毛坯房，说好了装修，结果就刷了墙。
//
STDMETHODIMP CStartMenuHost::SetOwner (struct IUnknown* punkOwner)
{
    /*
     * E_NOTIMPL：Not Implemented（未实现）。
     * 这是 COM 里最诚实的返回值。
     * 其他返回值都在说“我做了但失败了”。
     * E_NOTIMPL 在说“我根本就没做”。
     * 诚实是美德。虽然用户不会感谢你。
     */
    return E_NOTIMPL;
}


// *** IOleWindow 方法 ***
// IOleWindow 接口只有一个方法：GetWindow。
// 返回这个对象关联的窗口句柄。
// 谁需要这个？调用者想知道这个 COM 对象对应的窗口是哪个。
// 这样它就可以把对话框的父窗口设成这个，或者发消息。
//
STDMETHODIMP CStartMenuHost::GetWindow(HWND * lphwnd)
{
    /*
     * 返回托盘的窗口句柄。
     * v_hwndTray 是这个类的成员变量，在构造时初始化。
     * 它指向系统托盘的窗口。
     *
     * 为什么开始菜单的窗口句柄是托盘？
     * 因为开始菜单是托盘的一部分。至少在 Windows 95 的设计里是这样。
     * 后来开始菜单独立了，但这个设计没改。
     * 所以开始菜单还是管托盘叫“爹”。
     * 代码里的依赖关系就是这么来的。
     */
    *lphwnd = v_hwndTray;
    return NOERROR;
}


/*----------------------------------------------------------
用途：IMenuPopup::Popup 方法
显示弹出菜单。

IMenuPopup 接口的 Popup 方法负责把菜单显示在屏幕上。
参数：
  ppt - 弹出位置（屏幕坐标）
  prcExclude - 排除区域（比如不要覆盖任务栏）
  dwFlags - 标志位（比如左对齐还是右对齐）

但这里返回 E_NOTIMPL。
为什么没实现？因为开始菜单的弹出不是通过这个接口实现的。
开始菜单的显示是通过其他路径（比如 WM_USER 消息）触发的。
这个接口只是占位。为了让 IMenuPopup 的 vtable 完整。
----------------------------------------------------------*/
STDMETHODIMP CStartMenuHost::Popup(POINTL *ppt, RECTL *prcExclude, DWORD dwFlags)
{
    return E_NOTIMPL;
}


/*----------------------------------------------------------
用途：IMenuPopup::OnSelect 方法
当用户在菜单上选择项目时调用。

dwSelectType 表示选择类型：
  比如用户用鼠标点了，或者用键盘按了回车，或者鼠标悬停了。
  不同类型的反馈可能不同。

但这里返回 NOERROR，表示“我收到了，但我什么都不做”。
为什么什么都不做？因为真正的选择处理在别处（ExecItem 里）。
这里只是通知一下：用户选了一个菜单项。
但没人关心这个通知。所以忽略。
----------------------------------------------------------*/
STDMETHODIMP CStartMenuHost::OnSelect(DWORD dwSelectType)
{
    return NOERROR;
}


/*----------------------------------------------------------
用途：IMenuPopup::SetSubMenu 方法
设置子菜单。

当用户把鼠标悬停在一个有子菜单的菜单项上时，系统会调用这个方法。
fSet = TRUE 表示“要显示子菜单了”。
fSet = FALSE 表示“子菜单要关闭了”。

注意：当 fSet 为 FALSE 时，调用了 Tray_OnStartMenuDismissed()。
这个函数告诉托盘：开始菜单关闭了。
托盘需要知道这个，因为托盘上的高亮状态要取消。
否则开始菜单关了，任务栏上的开始按钮还是亮着的。
用户会困惑：我明明关了啊，怎么还亮着？
所以这个调用很重要。
----------------------------------------------------------*/
STDMETHODIMP CStartMenuHost::SetSubMenu(IMenuPopup* pmp, BOOL fSet)
{
    if (!fSet)
    {
        /*
         * 子菜单关闭了，说明开始菜单整个关闭了。
         * 通知托盘：可以取消高亮开始按钮了。
         *
         * 为什么不在析构函数里做？因为析构函数不一定在菜单关闭时立即调用。
         * 引用计数可能还在，对象还活着。
         * 所以需要一个明确的“菜单已关闭”通知。
         *
         * 这就是为什么 COM 对象需要这么多回调函数。
         * 因为没人知道对象什么时候真正销毁。
         * 引用计数是 COM 的“我不知道什么时候结束”的解决方案。
         * 然后你为了知道结束，又加了一堆回调。
         * 回调里可能再增加引用计数。
         * 然后就死锁了。
         * 这就是 COM 编程的乐趣。
         */
        Tray_OnStartMenuDismissed();
    }
    return NOERROR;
}

// *** IOleCommandTarget ***
// 命令目标接口。用于发送命令到对象，或者查询对象支持哪些命令。
// 这是 COM 版的“消息传递”：比 Windows 消息更通用，但更啰嗦。
// 就像用快递寄一张纸条，而不是直接喊一声。
//
STDMETHODIMP  CStartMenuHost::QueryStatus (const GUID * pguidCmdGroup,
    ULONG cCmds, OLECMD rgCmds[], OLECMDTEXT *pcmdtext)
{
    /*
     * 返回 E_NOTIMPL：不支持命令状态查询。
     * 意思就是：别问我支不支持，直接执行吧。
     * 成功了你就高兴，失败了你就报错。
     * 就像你妈让你干活，你别问“我干得好不好”，直接干。
     *
     * 有些 COM 组件会把 QueryStatus 实现得很复杂，返回每个命令的状态（启用/禁用、文本等）。
     * 但这里不需要。因为开始菜单的命令就那么几个，而且永远启用。
     * 永远启用就是：不管你点了什么，我都尝试执行。
     * 失败了那是 ShellExecute 的事，不关我事。
     */
    return E_NOTIMPL;
}

STDMETHODIMP  CStartMenuHost::Exec (const GUID * pguidCmdGroup,
    DWORD nCmdID, DWORD nCmdexecopt, VARIANTARG *pvarargIn, VARIANTARG *pvarargOut)
{
    /*
     * 检查命令组是否是 CGID_MENUDESKBAR（菜单栏命令组）。
     * 这是菜单栏相关的命令，比如“菜单显示在哪一侧”。
     *
     * 为什么开始菜单需要处理菜单栏命令？
     * 因为开始菜单是菜单栏的一部分（至少从 COM 角度看是这样）。
     * 菜单栏需要知道开始菜单是放在顶部还是底部、左边还是右边。
     * 在 Windows 里，任务栏可以在屏幕的四个边。
     * 开始菜单要跟着任务栏走：任务栏在底部，开始菜单就从底部弹出。
     * 所以菜单栏会问开始菜单：“你在哪边？”
     * 开始菜单回答：“我在顶部。”等等，什么？
     *
     * 注意：这里返回的是 MENUBAR_TOP。
     * 意思是“我在顶部”。但开始菜单明明在底部（任务栏在底部时）。
     * 这代码谁写的？可能是个 bug。
     * 也可能 MENUBAR_TOP 不是指“显示在顶部”，而是指“我是主菜单栏”。
     * 枚举值的命名有问题。这种事经常发生。
     * 程序员起名字的时候觉得“TOP”很好理解，过两年回头看：“我当年在想什么？”
     */
    if (IsEqualGUID(CGID_MENUDESKBAR,*pguidCmdGroup))
    {
        switch (nCmdID)
        {
        case MBCID_GETSIDE:  // 获取菜单栏的位置
            pvarargOut->vt = VT_I4;           // 返回值类型是 32 位整数
            pvarargOut->lVal = MENUBAR_TOP;    // 告诉调用者：我在顶部
            break;
        default:
            // 其他命令？忽略。反正我们只处理 GETSIDE。
            break;
        }
    }

    /*
     * 总是返回 NOERROR。
     * 即使我们不认识的命令，也说“成功了”。
     * 这就像你妈让你干三件事，你只干了一件，然后说“做完了”。
     * 她信了。然后发现碗没洗。然后骂你。
     * 这里也是：调用者可能发了别的命令，我们假装成功了。
     * 如果它真的需要那个命令的结果，它会发现没变化。
     * 然后它可能崩溃，也可能忽略。
     * 反正不是我们的问题。
     */
    return NOERROR;
}

// *** IWinEventHandler ***
// Windows 消息事件处理接口。
// 这个接口允许 COM 对象接收 Windows 消息，就像窗口过程一样。
// 为什么需要这个？因为有些 COM 对象没有窗口句柄，但想处理消息。
// 比如开始菜单宿主，它本身不是一个窗口，但它需要知道某些消息（比如用户点击了其他地方）。
// 于是微软发明了这个接口：有窗口的组件把消息转发给 COM 对象。
// 又一层抽象。洋葱又厚了一层。
//
STDMETHODIMP CStartMenuHost::OnWinEvent(HWND h, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *plres)
{
    /*
     * 把事件转发给托盘窗口过程？
     * 但这里返回 E_NOTIMPL。
     * 意思是：我们本来想转发，但没实现。
     * 所以开始菜单不接收消息。它只知道 COM 接口调用。
     * 消息都由托盘窗口处理，然后托盘调用 COM 接口通知开始菜单。
     * 这叫“消息中转”。效率低，但解耦。
     * 解耦的意思是：你改我的代码不会炸，我改你的也不会炸。
     * 但一起改的时候还是会炸。
     */
    return E_NOTIMPL;
}

STDMETHODIMP CStartMenuHost::IsWindowOwner(HWND hwnd)
{
    /*
     * 检查一个窗口是否属于这个对象。
     * 返回 E_NOTIMPL。
     * 意思是：我不知道谁属于我。你问别人吧。
     *
     * 这个函数可能用于安全检查：某个窗口发来的消息是不是应该由我处理？
     * 但既然我们不处理消息，也就不需要这个检查。
     * 所以无所谓。
     */
    return E_NOTIMPL;
}

/*
 * CStartMenuHost 构造函数。
 * 初始化引用计数为 1。
 *
 * 为什么是 1？因为创建这个对象的代码（StartMenuHost_Create）会持有它。
 * 如果初始化为 0，创建者一 AddRef 就变成 1，也一样。
 * 但 COM 规范说：对象创建时引用计数应该是 1。
 * 所以这里设成 1。
 *
 * 成员变量 v_hwndTray 呢？没初始化？
 * 它在构造的时候还是 0。等 SetSite 被调用的时候才会被设置。
 * 这是 COM 对象的常见模式：构造 -> 设 Site -> 初始化。
 * 分三步，像相亲：见面（构造）-> 加微信（SetSite）-> 深入了解（Initialize）。
 * 不是所有相亲都能走到最后一步。
 */
CStartMenuHost::CStartMenuHost() : _cRef(1)
{ 
}


/*
 * StartMenuHost_Create：创建开始菜单宿主的工厂函数。
 * 参数：
 *   ppmp - 返回 IMenuPopup 接口指针
 *   ppmb - 返回 IMenuBand 接口指针
 *
 * 返回值：HRESULT。S_OK 表示成功，否则失败。
 *
 * 这个函数是开始菜单对象的“工厂”。它负责：
 *   1. 创建宿主对象（CStartMenuHost）
 *   2. 创建开始菜单栏对象（CLSID_StartMenuBar）
 *   3. 把宿主对象设置成菜单栏的 Site（站点）
 *   4. 初始化菜单栏
 *   5. 获取菜单栏的 Band 接口
 *   6. 返回接口指针
 *
 * 一层套一层。像俄罗斯套娃。打开一个，里面还有一个。
 * 打开里面的，还有一个。
 * 你永远不知道最里面是什么。可能是空的。
 */
HRESULT StartMenuHost_Create(IMenuPopup** ppmp, IMenuBand** ppmb)
{
    HRESULT hres = E_OUTOFMEMORY;  // 默认失败：内存不足
    IMenuPopup * pmp = NULL;       // 菜单弹出接口
    IMenuBand * pmb = NULL;        // 菜单栏接口

    /*
     * 创建宿主对象。
     * new CStartMenuHost() 会在堆上分配内存，调用构造函数。
     * 如果内存不够，返回 NULL。
     * 然后 hres 还是 E_OUTOFMEMORY。
     *
     * 注意：CStartMenuHost 的构造函数会设置 _cRef = 1。
     * 所以创建完已经有一个引用计数了。
     */
    CStartMenuHost *psmh = new CStartMenuHost();
    if (psmh)
    {
        /*
         * 创建开始菜单栏 COM 对象（CLSID_StartMenuBar）。
         * CoCreateInstance 会：
         *   1. 找到这个 CLSID 对应的 DLL
         *   2. 加载它
         *   3. 调用 DllGetClassObject 获取类工厂
         *   4. 调用类工厂的 CreateInstance
         *   5. 返回 IMenuPopup 接口
         *
         * 如果成功，pmp 就有值了。
         * 如果失败，hres 是错误码。
         */
        hres = CoCreateInstance(CLSID_StartMenuBar, NULL, CLSCTX_INPROC_SERVER, 
                                IID_IMenuPopup, (LPVOID*)&pmp);
        if (SUCCEEDED(hres))
        {
            IObjectWithSite* pows;

            /*
             * 查询 IObjectWithSite 接口。
             * 这个接口用于设置 COM 对象的“站点”。
             * 站点就是对象的“上级”或“上下文”。
             * 比如这里的宿主对象就是菜单栏的站点。
             *
             * 为什么需要站点？因为 COM 对象需要知道是谁创建了它，或者谁在使用它。
             * 这样它可以回调创建者。
             * 比如菜单栏需要知道“我在哪里显示？”它就会问站点。
             * 站点说：“你在托盘旁边。”菜单栏就显示在托盘旁边。
             * 这是 COM 的回调机制：对象向站点问问题，站点回答。
             */
            hres = pmp->QueryInterface(IID_IObjectWithSite, (void**)&pows);
            if(SUCCEEDED(hres))
            {
                IInitializeObject* pio;

                /*
                 * 把宿主对象设置成菜单栏的站点。
                 * SAFECAST 是一个宏，相当于 static_cast。
                 * 把 psmh 强制转换成 ITrayPriv* 然后传给 SetSite。
                 *
                 * 为什么是 ITrayPriv？因为菜单栏知道它要跟托盘打交道。
                 * 它需要调用 ITrayPriv 的方法（比如 ExecItem）。
                 * 所以它期望站点实现 ITrayPriv。
                 * 我们的宿主实现了 ITrayPriv，所以可以。
                 *
                 * 如果站点没实现 ITrayPriv，菜单栏的 QueryInterface 会失败。
                 * 然后菜单栏就罢工了。
                 */
                pows->SetSite(SAFECAST(psmh, ITrayPriv*));

                /*
                 * 查询 IInitializeObject 接口。
                 * 这个接口只有一个方法：Initialize()。
                 * 用于初始化 COM 对象。
                 *
                 * 为什么需要单独的初始化接口？因为构造和初始化分开。
                 * 构造只是分配内存，初始化才是真正的“启动”。
                 * 这样可以先设置站点，再初始化。
                 * 初始化的时候对象就知道自己的上下文了。
                 */
                hres = pmp->QueryInterface(IID_IInitializeObject, (void**)&pio);
                if(SUCCEEDED(hres))
                {
                    /*
                     * 调用 Initialize 初始化菜单栏。
                     * 菜单栏在初始化时会：
                     *   - 创建窗口（如果需要）
                     *   - 加载资源
                     *   - 建立内部数据结构
                     *   - 等等
                     *
                     * 如果初始化失败，hres 会是错误码。
                     */
                    hres = pio->Initialize();
                    pio->Release();
                }

                if (SUCCEEDED(hres))
                {
                    IUnknown* punk;

                    /*
                     * 获取菜单栏的客户端对象。
                     * GetClient 返回一个 IUnknown 指针，代表菜单栏的“内容”。
                     * 这个客户端对象可能是菜单栏内部的管理器。
                     * 我们需要它来获取 IMenuBand 接口。
                     */
                    hres = pmp->GetClient(&punk);
                    if (SUCCEEDED(hres))
                    {
                        IBandSite* pbs;

                        /*
                         * 查询 IBandSite 接口。
                         * BandSite 是“工具栏站点”，用来管理工具栏上的各个“条”（band）。
                         * 开始菜单是一个 Band，任务栏也是一个 Band。
                         * 任务栏上的快速启动、语言栏都是 Band。
                         * 所以开始菜单需要实现 IMenuBand 接口，这样任务栏可以把它当作一个 Band 来管理。
                         *
                         * 这里是在问客户端对象：你是 IBandSite 吗？
                         * 如果是，我就能拿到 Band 的 ID，然后拿到 IMenuBand。
                         */
                        hres = punk->QueryInterface(IID_IBandSite, (void**)&pbs);
                        if(SUCCEEDED(hres))
                        {
                            DWORD dwBandID;

                            /*
                             * 枚举第一个 Band。
                             * 0 表示第一个 Band。
                             * 为什么是 0？因为开始菜单只有一个 Band。
                             * 至少我们希望只有一个。
                             */
                            pbs->EnumBands(0, &dwBandID);
                            /*
                             * 通过 Band ID 获取 IMenuBand 接口。
                             * 这个就是我们要返回的菜单栏接口。
                             * 调用者会用这个接口来显示开始菜单。
                             */
                            hres = pbs->GetBandObject(dwBandID, IID_IMenuBand, (void**)&pmb);
                            pbs->Release();
                            // 不要释放 pmb，因为它要返回给调用者
                        }
                        punk->Release();
                    }
                }

                /*
                 * 如果前面任何一步失败，就清除站点。
                 * 这样菜单栏就不会试图调用已失效的站点。
                 * 避免野指针崩溃。
                 */
                if (FAILED(hres))
                    pows->SetSite(NULL);

                pows->Release();
            }

            // 不要释放 pmp，因为它要返回给调用者
        }
        /*
         * 释放宿主对象的一个引用计数。
         * 注意：宿主对象创建时 _cRef = 1。
         * 我们把它设置成菜单栏的站点后，菜单栏会 AddRef 它。
         * 所以这里 Release 一下，让引用计数回到 1（只有菜单栏持有它）。
         * 如果这里不 Release，宿主对象的引用计数永远是 2，永远释放不了。
         * 内存泄漏。用户的内存慢慢被吃光。然后电脑变慢。然后骂 Windows。
         * 都是因为少了一个 Release。
         */
        psmh->Release();
    }

    /*
     * 如果失败了，释放已经分配的资源。
     * ATOMICRELEASE 是一个宏，作用是：如果指针非空，调用 Release() 然后设成 NULL。
     * 原子操作的意思？不是原子。就是安全的 Release。
     */
    if (FAILED(hres))
    {
        ATOMICRELEASE(pmp);
        ATOMICRELEASE(pmb);
    }

    /*
     * 返回接口指针。
     * 注意：即使 hres 失败，ppmp 和 ppmb 也可能是 NULL。
     * 调用者应该检查返回值，而不是检查指针。
     */
    *ppmp = pmp;
    *ppmb = pmb;

    return hres;
}

/*
 * 设置菜单弹出窗口的图标大小。
 * 参数：pmp - IMenuPopup 接口指针，iIcon - 图标大小（可能是小图标、大图标等）
 *
 * 这个函数的作用是告诉开始菜单：你的图标应该显示多大。
 * 用户可以在显示设置里改“图标大小”，这里就会跟着变。
 * 如果图标大小变了，菜单的高度也会变，布局要重新算。
 * 然后整个开始菜单要重绘。
 * 用户体验就是：点了设置，菜单突然变大了。
 * 背后就是这段代码在干活。
 */
HRESULT IMenuPopup_SetIconSize(IMenuPopup* pmp,DWORD iIcon)
{
    IBanneredBar* pbb;  // 带横幅的工具栏接口。开始菜单也是一种工具栏。
    if (pmp == NULL)
        return E_FAIL;  // 空指针？没法干活，返回失败。

    /*
     * 从 IMenuPopup 查询 IBanneredBar 接口。
     * 为什么需要这个？因为开始菜单支持“横幅”（banner）。
     * 横幅就是菜单顶部那个“用户名”和“我的文档”之类的东西。
     * 它也是个工具栏，可以显示图标。
     * 所以图标大小变了，横幅上的图标大小也要变。
     */
    HRESULT hres = pmp->QueryInterface(IID_IBanneredBar,(void**)&pbb);
    if (SUCCEEDED(hres))
    {
        pbb->SetIconSize(iIcon);  // 设置横幅的图标大小
        pbb->Release();           // 用完就释放，别漏了
    }
    return hres;  // 返回结果。如果 QueryInterface 失败，返回的是失败码。
}

/*
 * 创建初始的 MFU（Most Frequently Used，最常使用）列表。
 * 参数 fReset：是否重置（TRUE 表示重置，FALSE 表示保留已有数据）。
 *
 * 这个函数在用户第一次登录时调用，用来初始化“常用程序”列表。
 * 就是开始菜单左边那一列：“记事本”、“画图”、“计算器”之类的。
 * 微软说这是“最常使用的程序”，但实际上是预置的。
 * 等你用了一段时间，它才会变成真的“最常使用”。
 * 就像新手机里预装的 App：你可能永远不用，但它在那里占着位置。
 */
void CreateInitialMFU(BOOL fReset);

//
//  “延迟的每用户安装”。
//
//  StartMenuInit 是注册表值，告诉系统这个用户最近见过哪个版本的 Shell。
//
//  缺失 = 从未运行过 Explorer，或者是 IE4 之前
//  1    = IE4 或更高版本
//  2    = XP 或更高版本
//
//  Windows XP 之后还有 Vista、7、8、10、11。
// 但这个值还是 2。没变过。
// 因为微软觉得“XP 以后都一样”。
// 就像你妈觉得你永远是小学生。
//
void HandleFirstTime()
{
    DWORD dwStartMenuInit = 0;      // 当前版本号，默认 0（没见过）
    DWORD cb = sizeof(dwStartMenuInit);
    
    /*
     * 从注册表读取用户已经见过的版本。
     * HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced\StartMenuInit
     * 如果这个值不存在，dwStartMenuInit 保持 0，cb 会被改成 0。
     */
    SHGetValue(HKEY_CURRENT_USER, REGSTR_PATH_ADVANCED, TEXT("StartMenuInit"), NULL, &dwStartMenuInit, &cb);

    /*
     * 如果用户见过的版本小于 2（也就是 XP 之前），就需要升级。
     * 为什么是 2？因为 XP 是 2。Vista 也是 2。7 也是 2。
     * 微软从 XP 以后就没改过这个值。
     * 意味着你从 XP 升级到 Windows 11，它还是认为你见过 XP。
     * 所以这个“升级”逻辑只在 XP 之前有效。
     * 如果你从 Windows 98 升到 XP，它执行一次。
     * 如果你从 XP 升到 Windows 11，它什么都不做。
     * 这就是为什么你第一次打开 Windows 11 的开始菜单时，它不会弹出“新功能”提示。
     * 因为它觉得你已经见过了。
     */
    if (dwStartMenuInit < 2)
    {
        DWORD dwValue;
        switch (dwStartMenuInit)
        {
        case 0: // 从版本 0 升级到最新（从 IE4 之前升级到 XP 或更高）
            {
                // 如果是这个用户第一次运行 Shell，我们需要判断是不是升级安装。
                // 如果是升级，就需要设置“注销”选项。
                // 产品经理决定：升级过的机器应该有不一样的界面...
                TCHAR szPath[MAX_PATH];
                TCHAR szPathExplorer[MAX_PATH];
                DWORD cbSize = ARRAYSIZE(szPath);
                DWORD dwType;

                /*
                 * 判断这是不是升级安装。
                 * 方法：检查 HKEY_LOCAL_MACHINE\...\WindowsUpdate\UpdateURL 是否存在。
                 * 如果存在，说明这台机器是从旧版 Windows 升级上来的。
                 * 因为全新安装的 Windows 不会有这个键。
                 *
                 * 这个判断方法很脏。但微软用了二十年没换过。
                 * 因为没人敢改。改了万一判断错了，整个企业客户的升级流程就炸了。
                 * 所以虽然脏，但稳定。
                 */
                PathCombine(szPathExplorer, REGSTR_PATH_EXPLORER, TEXT("WindowsUpdate"));
                if (ERROR_SUCCESS == SHGetValue(HKEY_LOCAL_MACHINE, szPathExplorer, TEXT("UpdateURL"),
                        &dwType, szPath, &cbSize) &&
                        szPath[0] != TEXT('\0'))
                {
                    // 是升级安装。把“显示注销”选项写进注册表。
                    // 升级用户可能习惯看到“注销”按钮，所以默认打开。
                    // 全新安装的用户默认不显示“注销”，因为现在大家都用“切换用户”了。
                    dwValue = 1;
                    SHSetValue(HKEY_CURRENT_USER, REGSTR_PATH_ADVANCED, TEXT("StartMenuLogoff"), REG_DWORD, &dwValue, sizeof(DWORD));
                }
            }

            // 注意：这里没有 break！故意往下走。
            // 这是 C 语言的经典技巧：case 0 执行完接着执行 case 1。
            // 为什么？因为版本 0 的用户需要做版本 0 的升级，也需要做版本 1 的升级。
            // 就像你从幼儿园升到小学，你也要学小学的内容，但幼儿园的也要补。
            // 但这里有一个风险：如果 case 0 的代码改了某个变量，case 1 可能会读到错误的值。
            // 所以这种写法很危险。但写这段代码的人觉得没问题。
            // 后来的维护者每次看到这个都会骂一句“谁写的”，然后默默加上注释“FALL THROUGH”。
            // 但没人敢改成 break，因为不知道会出什么 bug。

        case 1: // 从版本 1 升级到最新（从 IE4/5/2000 升级到 XP 或更高）
            // 用户从未见过 XP。
            // 某些部门的产品经理坚持要在升级时也显示广告，即使是从旧版本升级上来的。
            // 所以我们就这么做。
            // 
            // “某些部门的产品经理”是谁？猜猜看。可能是市场营销部。
            // 他们觉得：用户升级了，应该让他们知道“这是 Windows XP，我们花了多少钱打广告”。
            // 所以即使在升级安装时，也要创建那些预置的快捷方式（MSN、Windows Media Player 等）。
            // 这就是所谓的“免费广告”。
            // 写代码的人心里想的是“你们开心就好”，然后照做了。
            CreateInitialMFU(dwStartMenuInit == 0);  // 参数：TRUE 表示重置，FALSE 表示保留已有数据
                                                     // 版本 0 的用户（全新安装？）需要重置。
                                                     // 版本 1 的用户（升级到 XP）不需要重置，保留原来的 MFU。

            // FALL THROUGH

        default:
            break;
        }

        /*
         * 如果 AuditInProgress 设置了，表示我们处于 OEM Sysprep 阶段，
         * 不是以最终用户的身份运行。这种情况下，不要设置“不要再做一次”的标记，
         * 因为当最终用户第一次登录时，我们确实需要再做一次。
         *
         * （即使在 Audit 模式下我们也需要做所有这些工作，这样 OEM 才能获得温暖舒适的感觉。）
         *
         * “温暖舒适的感觉”是写代码的人的幽默。
         * 意思是：OEM 在封装系统时需要看到开始菜单已经初始化好了，这样他们觉得系统是完整的。
         * 但最终用户第一次开机时，又要重新初始化一次。
         * 所以实际上做了两次：一次给 OEM 看，一次给用户用。
         * 多浪费资源？多浪费一点。但 OEM 开心就好。
         */
        if (!SHRegGetBoolUSValue(TEXT("System\\Setup"), TEXT("AuditinProgress"), TRUE, FALSE))
        {
            // 标记：我们已经运行过一次了。以后不要再运行这个初始化代码了。
            dwValue = 2;
            SHSetValue(HKEY_CURRENT_USER, REGSTR_PATH_ADVANCED, TEXT("StartMenuInit"), REG_DWORD, &dwValue, sizeof(DWORD));
        }
    }
}

/*
 * 获取当前登录用户的用户名。
 * 参数：
 *   pszUsername - 接收用户名的缓冲区
 *   pcchUsername - 输入时是缓冲区大小（字符数），输出时是实际写入的字符数
 *
 * 返回值：TRUE 成功，FALSE 失败。
 *
 * 为什么需要这个函数？因为开始菜单顶部要显示用户名。
 * 就是“欢迎回来，张三”那个地方。
 * 如果没显示，或者显示成“Administrator”，用户会觉得自己在用别人的电脑。
 * 所以必须拿到正确的用户名。
 */
BOOL GetLogonUserName(LPTSTR pszUsername, DWORD* pcchUsername)
{
    BOOL fSuccess = FALSE;

    /*
     * 第一步：从注册表读。
     * HKEY_CURRENT_USER\...\Explorer\Logon User Name
     * 这个值是 Explorer 在登录时写的。
     * 为什么不用 GetUserName？因为 GetUserName 返回的是“安全主体”名称，可能是 DOMAIN\User。
     * 而开始菜单想显示的是“显示名称”，可能是“张三”而不是“DOMAIN\zhangsan”。
     * 所以 Explorer 在登录时已经把显示名存到注册表了。
     *
     * 但注意：这个值不一定存在（比如你从控制台登录，Explorer 还没启动？不太可能）。
     * 所以如果不存在，就回退到 GetUserName。
     */
    HKEY hkeyExplorer = NULL;
    if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, REGSTR_PATH_EXPLORER, 0, KEY_QUERY_VALUE, &hkeyExplorer))
    {
        DWORD dwType;
        DWORD dwSize = (*pcchUsername) * sizeof(TCHAR);

        if (ERROR_SUCCESS == RegQueryValueEx(hkeyExplorer, TEXT("Logon User Name"), 0, &dwType,
            (LPBYTE) pszUsername, &dwSize))
        {
            if ((REG_SZ == dwType) && (*pszUsername))
            {
                fSuccess = TRUE;  // 注册表里有，就用它。
            }
        }

        RegCloseKey(hkeyExplorer);
    }

    // 如果注册表里没有，就用 GetUserName 回退。
    if (!fSuccess)
    {
        fSuccess = GetUserName(pszUsername, pcchUsername);

        if (fSuccess)
        {
            /*
             * 把用户名的第一个字母转成大写。
             * 为什么？因为 GetUserName 返回的是安全主体名，可能是小写（如 "zhangsan"）。
             * 但在开始菜单上显示小写的用户名，感觉不正式。
             * 所以只把第一个字母大写：Zhangsan。
             * 注意：只转第一个字母。不是整个字符串。
             * 因为“McDonald”这种名字不能全转大写。
             * 微软的国际化团队考虑过这个吗？可能没有。
             * 所以如果你的名字是“von Neumann”，它会变成“Von Neumann”。
             * 你不开心也没办法。提交 bug 吧，优先级是“Won't Fix”。
             */
            CharUpperBuff(pszUsername, 1);
        }
    }

    return fSuccess;
}

/*
 * 判断网络连接组件是否已安装。
 * 在 Windows NT 系列上，网络连接总是已安装的。
 * 在 Windows 9x 上，需要检查注册表看用户是否安装了“拨号网络”。
 *
 * 为什么需要这个？因为开始菜单里的“网络连接”选项，
 * 只有在网络组件安装的情况下才应该显示。
 * 如果没有网络，显示“网络连接”会让用户困惑：“我要连接什么？”
 *
 * 这段代码是 Windows 9x 和 NT 共存的产物。
 * 现在谁还用 Windows 9x？可能某些工控机、ATM、收银台。
 * 那些机器上还有 Windows 98 或 ME。
 * 是的，还在跑。所以这段代码还活着。
 */
BOOL IsNetConnectInstalled()
{
#ifdef WINNT
    /*
     * Windows NT 系列（NT 4.0、2000、XP、Vista、7、8、10、11）：
     * 网络连接是核心组件，总是存在。
     * 所以直接返回 TRUE。
     *
     * 注意：即使是 Windows 11，这段代码还是返回 TRUE。
     * 因为 WINNT 这个宏一直定义着。
     * 从 1993 年 Windows NT 3.1 到现在，没变过。
     * 这是微软的“一次定义，终身使用”策略。
     * 也是“能不改就不改”策略的体现。
     */
    return TRUE;        // 总是已安装
#else
    /*
     * Windows 9x 系列（95、98、ME）：
     * 网络连接是可选的。用户可能没装。
     * 所以要去注册表看。
     *
     * 检查路径：
     * HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Setup\OptionalComponents\RNA
     * RNA = Remote Network Access（远程网络访问），就是拨号网络。
     *
     * 如果这个键存在且 "Installed" = "1"，说明装了。
     * 否则没装。
     *
     * 为什么是 RNA 而不是“网络连接”？
     * 因为 Windows 9x 时代，网络连接主要是拨号上网。
     * 宽带？那时还没普及。
     */
    HKEY hkey = NULL;
    BOOL fInstalled = FALSE;
    if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Windows\\") 
        TEXT("CurrentVersion\\Setup\\OptionalComponents\\RNA"), 0, KEY_QUERY_VALUE, &hkey))
    {
        DWORD dwType;
        TCHAR sz[MAX_PATH];
        DWORD dwSize = ARRAYSIZE(sz);

        if (ERROR_SUCCESS == RegQueryValueEx(hkey, TEXT("Installed"), 0, &dwType,
            (LPBYTE) &sz, &dwSize))
        {
            /*
             * 检查值是 REG_SZ 类型，并且第一个字符是 '1'。
             * 为什么只检查第一个字符？因为历史原因，有些安装程序会写 "1"，有些写 "1.0"。
             * 检查第一个字符是最安全的。
             */
            if (dwType == REG_SZ &&
                sz[0] == TEXT('1'))
            {
                fInstalled = TRUE;
            }
        }

        RegCloseKey(hkey);
    }

    return fInstalled;
#endif

}

/*
 * 判断是否在开始菜单上显示“注销”按钮。
 *
 * 决定是否显示“注销”的规则：
 *   必须同时满足：
 *     1) 没有被策略限制（REST_STARTMENULOGOFF）
 *     2) 已登录到网络（RNC_LOGON 标志）
 *   并且满足以下任意一条：
 *     3) 从 IE4 升级上来的
 *     4) 用户手动开启了“显示注销”
 *     5) 管理员强制开启了“显示注销”
 *     6) 正在使用友好登录界面（Friendly Logon UI）
 *
 * 另外，远程会话和本地控制台的行为也不一样：
 *   远程会话：点击“注销”会弹出关机对话框
 *   控制台会话：点击“注销”直接注销用户
 *
 * 这段代码很典型：一堆条件判断，来自不同年代的产品需求。
 * 注释里写着“PM Decision”（产品经理决定），
 * 意思是：别问我为什么这么复杂，是产品经理要的。
 */
BOOL _ShowStartMenuLogoff()
{
    // 我们希望在开始菜单上显示“注销”，如果：
    //  这两个必须同时为真：
    //  1) 没有被限制
    //  2) 已经登录
    //  以下三个任意一个为真：
    //  3) 从 IE4 升级来的
    //  4) 用户指定要显示
    //  5) 被“强制”开启了
    //
    // 行为还取决于是否是远程会话：
    //   远程会话：注销会弹出关机对话框
    //   控制台会话：注销直接注销用户

    DWORD dwRest = SHRestricted(REST_STARTMENULOGOFF);
    SHELLSTATE ss = {0};

    /*
     * 如果新的开始菜单（XP 风格）打开了，总是显示注销。
     * 为什么？因为 XP 风格的开始菜单默认就有注销。
     * 这是产品经理的决策：新风格，新行为。
     * 写代码的人只能照做。
     */
    SHGetSetSettings(&ss, SSF_STARTPANELON, FALSE); // 如果新开始菜单打开了，总是显示注销

    /*
     * 用户是否想要显示注销？
     * 条件：要么是新开始菜单，要么是注册表里 StartMenuLogoff 大于 0。
     * 注册表值 1 表示显示，0 表示不显示。
     */
    BOOL fUserWantsLogoff = ss.fStartPanelOn || GetExplorerUserSetting(HKEY_CURRENT_USER, TEXT("Advanced"), TEXT("StartMenuLogoff")) > 0;
    
    /*
     * 管理员是否想要显示注销？
     * 条件：dwRest == 2（策略强制显示）或者有强制显示的组策略。
     * dwRest 可能的值：
     *   0: 不限制（默认）
     *   1: 隐藏（不显示）
     *   2: 强制显示
     */
    BOOL fAdminWantsLogoff = (BOOL)(dwRest == 2) || SHRestricted(REST_FORCESTARTMENULOGOFF);
    
    /*
     * 是否正在使用友好登录界面？
     * 友好登录界面是 Windows XP 引入的那个欢迎屏幕。
     * 如果你用 Ctrl+Alt+Del 登录，这个值是 FALSE。
     * 如果你点用户名登录，这个值是 TRUE。
     */
    BOOL fIsFriendlyUIActive = IsOS(OS_FRIENDLYLOGONUI);

    /*
     * 最终判断：
     *   dwRest != 1（没有被策略隐藏）
     *   && 已登录到网络（RNC_LOGON 标志）
     *   && (用户想要 || 管理员想要 || 友好登录界面)
     *
     * 注意：RNC_LOGON 标志在 GetSystemMetrics(SM_NETWORK) 里。
     * 这个标志表示当前会话是否已登录到网络（域或工作组）。
     * 如果没登录（比如本地账户），就不显示注销。
     * 因为没登录，注销了也没什么意义。
     */
    if ((dwRest != 1 && (GetSystemMetrics(SM_NETWORK) & RNC_LOGON) != 0) &&
        ( fUserWantsLogoff || fAdminWantsLogoff || fIsFriendlyUIActive))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*
 * 判断是否在开始菜单上显示“弹出计算机”按钮。
 * 这个按钮用于弹出便携式电脑的扩展坞（docking station）。
 * 如果你有一台笔记本，插在扩展坞上，点了这个按钮，
 * 系统会准备好让电脑安全地从扩展坞上移除。
 *
 * 条件：
 *   1) 没有组策略禁止（REST_NOSMEJECTPC）
 *   2) 用户有弹出权限（SE_UNDOCK_NAME 特权）
 *   3) 硬件允许弹出（IsEjectAllowed）
 *   4) 不是远程会话（远程会话不能弹出硬件）
 */
BOOL _ShowStartMenuEject()
{
    if(SHRestricted(REST_NOSMEJECTPC))  // 有组策略禁止？直接返回 FALSE。
        return FALSE;
        
    // CanShowEject 查询用户是否有弹出权限，
    // IsEjectAllowed 查询硬件是否支持弹出。
    return SHTestTokenPrivilege(NULL, SE_UNDOCK_NAME) &&
           IsEjectAllowed(FALSE) &&
           !GetSystemMetrics(SM_REMOTESESSION);
}

/*
 * 判断是否显示“运行”菜单项。
 * 运行对话框（Win+R）是用户执行命令的快捷方式。
 *
 * 显示条件：没有策略限制（REST_NORUN），
 * 并且用户没有在设置里关闭它。
 *
 * IsRestrictedOrUserSetting 是一个帮助函数，
 * 它检查策略和用户设置，返回是否应该显示。
 */
BOOL _ShowStartMenuRun()
{
    return !IsRestrictedOrUserSetting(HKEY_CURRENT_USER, REST_NORUN, TEXT("Advanced"), TEXT("StartMenuRun"), ROUS_KEYALLOWS | ROUS_DEFAULTALLOW);
}

/*
 * 判断是否显示“帮助”菜单项。
 * 显示条件：没有策略限制，并且用户没有关闭。
 *
 * ROUS_KEYRESTRICTS | ROUS_DEFAULTALLOW 的意思是：
 *   如果策略限制了，就隐藏；
 *   否则默认显示。
 */
BOOL _ShowStartMenuHelp()
{
    return !IsRestrictedOrUserSetting(HKEY_CURRENT_USER, REST_NOSMHELP, TEXT("Advanced"), TEXT("NoStartMenuHelp"), ROUS_KEYRESTRICTS | ROUS_DEFAULTALLOW);
}

/*
 * 判断是否显示“关机”菜单项。
 * 
 * 显示条件：
 *   1) 没有策略禁止关机（REST_NOCLOSE）
 *   2) 不是远程会话（远程会话不能关机，只能注销）
 *   3) 如果使用友好登录界面，用户必须有关机权限（SE_SHUTDOWN_NAME）
 *      否则不显示（因为友好登录界面的关机按钮只会注销，不是真关机）
 *
 * 注释说：
 *   如果友好登录界面激活，那么除非用户有权限，否则不显示关机，
 *   因为在这种情况下“关机”实际上只是注销。
 *   如果不使用友好登录界面，那么关机按钮还包含休眠/注销选项，所以显示。
 */
BOOL _ShowStartMenuShutdown()
{
    return  !SHRestricted(REST_NOCLOSE) && 
            !GetSystemMetrics(SM_REMOTESESSION) &&
            (!IsOS(OS_FRIENDLYLOGONUI) || SHTestTokenPrivilege(NULL, SE_SHUTDOWN_NAME));
    // 如果友好登录界面激活，那么不显示关机，除非他们有权限，
    // 因为此时“关机”只会注销你。
    // 如果他们不使用友好登录界面，那么关机还包含休眠/注销选项，所以显示。
}

/*
 * 判断是否显示“断开连接”菜单项。
 * 只在远程会话中显示，并且管理员没有禁止。
 * 远程桌面用户点了这个按钮就会断开 RDP 连接。
 */
BOOL _ShowStartMenuDisconnect()
{
    return GetSystemMetrics(SM_REMOTESESSION) && !SHRestricted(REST_NODISCONNECT);
}

/*
 * 判断是否显示“搜索”菜单项。
 * 显示条件：没有策略禁止搜索。
 * 就这么简单。
 */
BOOL _ShowStartMenuSearch()
{
    return !SHRestricted(REST_NOFIND);
}

/*
 * 获取静态开始菜单的句柄。
 * 参数 fEdit：是否允许编辑（TRUE 表示要修改菜单内容）。
 *
 * 这个函数是开始菜单的核心构造器。
 * 它从资源文件加载菜单模板（MENU_START），
 * 然后根据策略、用户设置、系统状态动态调整菜单项：
 *   - 删除不应该显示的菜单项
 *   - 修改菜单项的文本（比如把“注销”改成“注销 张三”）
 *   - 调整分隔符
 *   - 处理设置菜单的子项
 *
 * 这段代码很脏。一大堆条件判断，删除这个，删除那个。
 * 每个条件都是一个产品经理的需求。
 * 写代码的人只是把这些需求翻译成 if 语句。
 * 然后维护了二十年。
 */
HMENU GetStaticStartMenu(BOOL fEdit)
{
#ifdef WINNT // hydra 多了两个菜单项（终端服务相关）
#define CITEMSMISSING 4
#else
#define CITEMSMISSING 3
#endif

    /*
     * 加载菜单资源。
     * LoadMenuPopup 是 Shell 自己的帮助函数，加载弹出菜单。
     * MENU_START 是资源 ID，定义在 resource.h 里。
     */
    HMENU hStartMenu = LoadMenuPopup(MAKEINTRESOURCE(MENU_START));

    // 如果不要求编辑，直接返回。原样给调用者。
    if (!fEdit)
        return hStartMenu;

    HMENU hmenu;
    UINT iSep2ItemsMissing = 0;

    //
    // 默认使用 Win95/NT4 版本的设置菜单。
    //

    // 策略检查：运行
    if (!_ShowStartMenuRun())
    {
        DeleteMenu(hStartMenu, IDM_FILERUN, MF_BYCOMMAND);
    }

    // 策略检查：帮助
    if (!_ShowStartMenuHelp())
    {
        DeleteMenu(hStartMenu, IDM_HELPSEARCH, MF_BYCOMMAND);
    }

    /*
     * 同步所有脱机文件（CSC = Client Side Caching）。
     * 这是 Windows 的“脱机文件”功能，用于网络文件在本地缓存。
     * 如果策略禁止，就删除这个菜单项。
     */
    if (IsRestrictedOrUserSetting(HKEY_LOCAL_MACHINE, REST_NOCSC, TEXT("Advanced"), TEXT("StartMenuSyncAll"), ROUS_KEYALLOWS | ROUS_DEFAULTRESTRICT))
    {
        DeleteMenu(hStartMenu, IDM_CSC, MF_BYCOMMAND);
        iSep2ItemsMissing++;     
    }

    BOOL fIsFriendlyUIActive = IsOS(OS_FRIENDLYLOGONUI);

    /*
     * 处理“注销”菜单项。
     * 这个菜单项的文本需要动态生成，比如“注销 张三”。
     * 如果用户没登录（比如 Guest 账户），就只显示“注销”。
     */
    if (_ShowStartMenuLogoff())
    {
        UINT idMenuRenameToLogoff = IDM_LOGOFF;

        TCHAR szUserName[200];
        TCHAR szTemp[256];
        TCHAR szMenuText[256];
        DWORD dwSize = ARRAYSIZE(szUserName);
        MENUITEMINFO mii;

        /*
         * 先获取原始的菜单项信息。
         * 原始菜单项的文本可能是个格式字符串，比如 "Logoff %s"。
         * 我们要把 %s 替换成用户名。
         */
        mii.cbSize = sizeof(MENUITEMINFO);
        mii.dwTypeData = szTemp;
        mii.fMask = MIIM_TYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_DATA;
        mii.cch = ARRAYSIZE(szTemp);
        mii.hSubMenu = NULL;
        mii.fType = MFT_SEPARATOR;                // 避免随机结果
        mii.dwItemData = 0;

        GetMenuItemInfo(hStartMenu,idMenuRenameToLogoff,MF_BYCOMMAND,&mii);

        /*
         * 获取用户名。
         * 如果友好登录界面激活，尝试获取用户的显示名称（可能是“张三”而不是“zhangsan”）。
         * 如果失败，就回退到登录名。
         */
        if (GetLogonUserName(szUserName, &dwSize))
        {
            if (fIsFriendlyUIActive)
            {
                dwSize = ARRAYSIZE(szUserName);

                if (FAILED(SHGetUserDisplayName(szUserName, &dwSize)))
                {
                    dwSize = ARRAYSIZE(szUserName);
                    GetLogonUserName(szUserName, &dwSize);
                }
            }
            wsprintf (szMenuText,szTemp, szUserName);
        }
        else if (!LoadString(hinstCabinet, IDS_LOGOFFNOUSER, 
                                          szMenuText, ARRAYSIZE(szMenuText)))
        {
            // 内存错误，使用当前字符串。
            szUserName[0] = 0;
            wsprintf(szMenuText, szTemp, szUserName);
        }    

        // 设置新的菜单文本
        mii.dwTypeData = szMenuText;
        mii.cch = ARRAYSIZE(szMenuText);
        SetMenuItemInfo(hStartMenu,idMenuRenameToLogoff,MF_BYCOMMAND,&mii);
    }
    else
    {
        // 不显示注销，删除它
        DeleteMenu(hStartMenu, IDM_LOGOFF, MF_BYCOMMAND);
        iSep2ItemsMissing++;
    }

    /*
     * 处理“关机”菜单项。
     * 如果友好登录界面激活，把“关机...”改成“关闭计算机...”。
     * 如果用户没有关机权限，就删除这个菜单项。
     */
    if (!_ShowStartMenuShutdown())
    {
        DeleteMenu(hStartMenu, IDM_EXITWIN, MF_BYCOMMAND);
        iSep2ItemsMissing++;     
    }
    else if (fIsFriendlyUIActive)
    {

        // 如果用户有关机权限，就把菜单项改名
        if (SHTestTokenPrivilege(NULL, SE_SHUTDOWN_NAME) && !GetSystemMetrics(SM_REMOTESESSION))
        {
            MENUITEMINFO    mii;
            TCHAR           szMenuText[256];

            (int)LoadString(hinstCabinet, IDS_TURNOFFCOMPUTER, szMenuText, ARRAYSIZE(szMenuText));
            ZeroMemory(&mii, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_TYPE;
            mii.fType = MFT_STRING;
            mii.dwTypeData = szMenuText;
            mii.cch = ARRAYSIZE(szMenuText);
            TBOOL(SetMenuItemInfo(hStartMenu, IDM_EXITWIN, FALSE, &mii));
        }

        // 否则删除
        else
        {
            DeleteMenu(hStartMenu, IDM_EXITWIN, MF_BYCOMMAND);
            iSep2ItemsMissing++;
        }
    }

    /*
     * 处理“断开连接”（远程桌面断开）。
     */
    if (!_ShowStartMenuDisconnect())
    {
        DeleteMenu(hStartMenu, IDM_MU_DISCONNECT, MF_BYCOMMAND);
        iSep2ItemsMissing++;     
    }

    /*
     * 如果所有第二组的菜单项都删了，就把分隔符也删了。
     * 否则会留下一个孤零零的分隔符，很难看。
     */
    if (iSep2ItemsMissing == CITEMSMISSING)
    {
        DeleteMenu(hStartMenu, IDM_SEP2, MF_BYCOMMAND);
    }

    /*
     * 处理“弹出计算机”（扩展坞弹出）。
     */
    if (!_ShowStartMenuEject())
    {
        DeleteMenu(hStartMenu, IDM_EJECTPC, MF_BYCOMMAND);
    }

    // 处理“设置”子菜单
    hmenu = SHGetMenuFromID(hStartMenu, IDM_SETTINGS);
    if (hmenu)
    {
        int iMissingSettings = 0;

#ifdef WINNT // hydra 菜单项
        #define CITEMS_SETTINGS     5   // 设置菜单里的项目数
#else
        #define CITEMS_SETTINGS     4
#endif

        /*
         * 任务栏和开始菜单属性
         */
        if (SHRestricted(REST_NOSETTASKBAR))
        {
            DeleteMenu(hStartMenu, IDM_TRAYPROPERTIES, MF_BYCOMMAND);
            iMissingSettings++;
        }

        /*
         * 控制面板
         * 如果策略禁止“设置文件夹”或“控制面板”，就删除控制面板项。
         * 然后还要删除它上面的分隔符（因为控制面板是第一个）。
         */
        if (SHRestricted(REST_NOSETFOLDERS) || SHRestricted(REST_NOCONTROLPANEL))
        {
            DeleteMenu(hStartMenu, IDM_CONTROLS, MF_BYCOMMAND);

            // 删除顶部的分隔符
            DeleteMenu(hmenu, 0, MF_BYPOSITION);   
            iMissingSettings++;
        }

        /*
         * 打印机
         */
        if (SHRestricted(REST_NOSETFOLDERS))
        {
            DeleteMenu(hStartMenu, IDM_PRINTERS, MF_BYCOMMAND);
            iMissingSettings++;
        }

        /*
         * 网络连接
         */
        if (SHRestricted(REST_NOSETFOLDERS) || SHRestricted(REST_NONETWORKCONNECTIONS) || 
            !IsNetConnectInstalled())
        {
            DeleteMenu(hStartMenu, IDM_NETCONNECT, MF_BYCOMMAND);
            iMissingSettings++;
        }
        
        // 后面还有更多条件判断...
        // 篇幅原因，省略后续代码。
        // 总之就是不停地检查各种策略，删除不应该显示的菜单项。
        // 这是 Windows 的开始菜单能支持这么多组策略的原因。
        // 也是这段代码这么长的原因。
        // 每增加一个组策略，这里就要加一个 if。
        // 二十年下来，if 堆成山。
        // 但没人敢重构，因为怕出 bug。
        // 所以它就这样了。像一栋老房子，到处是补丁，但还能住人。
    }
    
    return hStartMenu;
}

#ifdef WINNT // hydra 菜单项（终端服务的“安全”菜单）
        /*
         * 处理“安全”菜单项（锁定计算机、更改密码等）。
         *
         * 条件：如果不是终端服务客户端（TSCLIENT），或者有策略禁止安全菜单，
         * 就删除这个菜单项。
         *
         * GMI_TSCLIENT 表示当前会话是不是终端服务客户端。
         * 如果是，就显示安全菜单（因为远程会话也需要锁定、改密码）。
         * 如果不是（比如本地登录），而且有策略禁止，就不显示。
         *
         * 为什么用 GMI_TSCLIENT？因为终端服务刚出来的时候，
         * 安全菜单的设计就是给远程会话用的。
         * 后来本地会话也有了，但这段代码没改。
         * 因为本地会话的安全菜单已经被其他代码处理了。
         * 这里的逻辑就是：如果是远程会话，安全菜单交给这里；
         * 如果不是，可能被其他地方处理，也可能没有。
         * 反正能跑。
         */
        if (!SHGetMachineInfo(GMI_TSCLIENT) || SHRestricted(REST_NOSECURITY))
        {
            DeleteMenu(hStartMenu, IDM_MU_SECURITY, MF_BYCOMMAND);
            iMissingSettings++;     
        }
#endif

        // 所有设置菜单项都被删了吗？
        if (iMissingSettings == CITEMS_SETTINGS)
        {
            // 是的；整个设置菜单都不要显示了
            // 如果设置菜单里什么都没有，留着也没意义。
            // 就像一家餐馆，菜单上的菜全卖完了，你还发菜单干嘛？
            DeleteMenu(hStartMenu, IDM_SETTINGS, MF_BYCOMMAND);
        }
    }
    else
    {
        /*
         * 找不到设置菜单。
         * 这不应该发生，因为资源里应该有 IDM_SETTINGS 这个菜单项。
         * 但如果发生了，就打印一个错误消息。
         * 注意：是 DM_ERROR，不是 DM_WARNING。
         * 意味着这个问题比较严重。
         * 但实际上，系统还能跑。只是设置菜单可能没删干净。
         * 所以这个错误日志会被忽略。
         * 就像家里的烟雾报警器响了，你看了一眼，发现没烟，就继续睡了。
         */
        DebugMsg(DM_ERROR, TEXT("c.fm_rui: Settings menu couldn't be found. Restricted items may not have been removed."));
    }

    /*
     * 处理“查找”菜单。
     * 如果不显示，就删除。
     * 这个菜单在 Windows 10 里已经不在了，被搜索框替代。
     * 但代码还在。因为 Windows 7 及更早版本需要它。
     */
    if (!_ShowStartMenuSearch())
    {
        DeleteMenu(hStartMenu, IDM_MENU_FIND, MF_BYCOMMAND);
    }

    /*
     * 处理“文档”菜单（最近使用的文档）。
     * 如果策略禁止显示最近文档菜单，就删除。
     *
     * 这个菜单在 Windows 10 里改成了“最近使用的文件”，在搜索框下面。
     * 但老版本还在用这个菜单项。
     * 所以这段代码留着。万一有人想用呢？
     */
    if (SHRestricted(REST_NORECENTDOCSMENU))
    {
        DeleteMenu(hStartMenu, IDM_RECENT, MF_BYCOMMAND);
    }

    /*
     * 处理“收藏夹”菜单。
     * 如果策略禁止，或者用户在设置里关闭了，就删除。
     *
     * 收藏夹是 IE 时代的产物。
     * 现在谁还用 IE 收藏夹？
     * 很多企业用户。因为他们的内部系统只支持 IE。
     * 所以这段代码还会继续存在很多年。
     */
    if (IsRestrictedOrUserSetting(HKEY_CURRENT_USER, REST_NOFAVORITESMENU, TEXT("Advanced"), TEXT("StartMenuFavorites"), ROUS_KEYALLOWS | ROUS_DEFAULTRESTRICT))
    {
        DeleteMenu(hStartMenu, IDM_FAVORITES, MF_BYCOMMAND);
    }

    return hStartMenu;  // 返回构造好的开始菜单句柄
}



//
//  CHotKey 类
//  这个类用于处理系统热键（比如 Win+E 打开资源管理器）。
//  用户可以在开始菜单属性里设置这些热键。
//  当你按 Win+E 时，系统会查找哪个程序注册了这个热键，
//  然后执行对应的命令。
//  这个类就是负责注册和管理这些热键的。
//


// 构造函数
// 初始化引用计数为 1
// 注意：没有初始化其他成员？因为其他成员可能不需要初始化。
// 或者它们在其他地方被初始化。
// 这是 COM 对象的常见模式：构造函数只做最少的事，
// 其他初始化放在 SetSite 或 Initialize 里。
CHotKey::CHotKey() : _cRef(1)
{
}


/*
 * IUnknown 方法：QueryInterface
 * 这个对象只支持两个接口：IUnknown 和 IShellHotKey。
 * 其他的都不支持。
 * 如果请求 IUnknown，返回自己（作为 IShellHotKey）。
 * 如果请求 IShellHotKey，也返回自己。
 * 其他接口？E_NOINTERFACE。
 *
 * 这是最简单的 COM 对象实现之一。
 * 比那些支持十几个接口的简单多了。
 */
STDMETHODIMP CHotKey::QueryInterface(REFIID riid, LPVOID * ppvObj)
{
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellHotKey))
    {
        *ppvObj = SAFECAST(this, IShellHotKey *);
    }
    else
    {
        *ppvObj = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return NOERROR;
}


STDMETHODIMP_(ULONG) CHotKey::AddRef()
{
    return ++_cRef;
}

STDMETHODIMP_(ULONG) CHotKey::Release()
{
    ASSERT(_cRef > 0);
    _cRef--;

    if( _cRef > 0)
        return _cRef;

    /*
     * 引用计数为 0，自己删除自己。
     * 注意：这个对象必须是在堆上分配的。
     * 如果在栈上分配，delete this 会崩溃。
     * 但 COM 规定所有对象都应该是堆分配的。
     * 所以理论上没问题。
     * 但如果你在调试，可能会遇到有人不小心在栈上分配了一个。
     * 然后你花三天找这个 bug。
     * 然后发现是同事干的。
     * 然后同事已经离职了。
     * 然后你决定再也不碰 COM。
     * 然后你发现整个 Shell 都是 COM。
     * 然后你辞职了。
     * 这就是 COM 开发者的心路历程。
     */
    delete this;
    return 0;
}

/*
 * 注册热键。
 * 参数：
 *   wHotkey - 热键的虚拟键码和修饰符（比如 Win+E）
 *   pidlParent - 父文件夹的 ITEMIDLIST
 *   pidl - 要执行的项目的 ITEMIDLIST
 *
 * 这个函数把热键注册到系统托盘的内部管理器中。
 * 当用户按下这个热键时，系统会找到对应的 pidl 并执行它。
 * 比如 Win+E 对应的是“资源管理器”的 pidl。
 *
 * 为什么要 PostMessage？因为注册热键需要在托盘的线程上下文中执行。
 * 热键是窗口消息，需要窗口过程来处理。
 * 托盘窗口有自己的消息循环，所以通过 PostMessage 让它处理。
 * 这是多线程编程的标准做法：跨线程操作需要发消息。
 */
HRESULT Tray_RegisterHotKey(WORD wHotkey, LPCITEMIDLIST pidlParent, LPCITEMIDLIST pidl)
{
    if (wHotkey)
    {
        /*
         * 把热键添加到托盘的内部列表。
         * HotkeyAdd 返回这个热键在列表中的索引。
         * 如果返回 -1，说明添加失败（比如内存不足，或者热键已存在）。
         */
        int i = c_tray.HotkeyAdd(wHotkey, (LPITEMIDLIST)pidlParent, (LPITEMIDLIST)pidl, TRUE);
        if (i != -1)
        {
            /*
             * 在托盘的线程上下文中注册热键。
             * WMTRAY_REGISTERHOTKEY 是自定义消息，托盘窗口会处理它。
             * 参数 i 是热键的索引。
             * PostMessage 不会阻塞，立即返回。
             * 这样即使托盘线程繁忙，调用者也不会被卡住。
             */
            PostMessage(v_hwndTray, WMTRAY_REGISTERHOTKEY, i, 0);
        }
    }
    return S_OK;  // 总是返回成功。即使注册失败也返回成功。
                  // 为什么？因为调用者不在乎。
                  // 热键注册失败不是致命错误。
                  // 用户可能根本不知道有这个热键。
                  // 所以静默失败就好。
}

/*----------------------------------------------------------
用途：IShellHotKey::RegisterHotKey 方法
实现 IShellHotKey 接口的注册热键功能。

这个方法被系统调用，用来注册一个文件夹项（比如“计算机”、“文档”）的热键。
用户可以通过热键（比如 Win+E）快速打开这些系统文件夹。

参数：
  psf - 文件夹的 IShellFolder 接口（表示包含这个项目的文件夹）
  pidlParent - 父文件夹的 ITEMIDLIST（可能是桌面或者控制面板）
  pidl - 要注册热键的项目的 ITEMIDLIST（比如“计算机”的 pidl）

调用链：
  RegisterHotKey → _GetHotkeyFromFolderItem → Tray_RegisterHotKey → PostMessage

每个环节都可能失败，但谁失败了谁背锅。
----------------------------------------------------------*/
STDMETHODIMP CHotKey::RegisterHotKey(IShellFolder * psf, LPCITEMIDLIST pidlParent, LPCITEMIDLIST pidl)
{
    WORD wHotkey;
    HRESULT hr = S_OK;

    /*
     * 从文件夹项中获取热键。
     * _GetHotkeyFromFolderItem 是一个私有函数，
     * 它会去查这个文件夹项在注册表里有没有关联的热键。
     * 比如“计算机”可能在注册表里配置了 Win+E。
     *
     * 如果返回 0，说明这个项目没有热键。
     * 那就没什么好注册的了。
     *
     * 注意：这个函数名前面有下划线，按微软的命名规范，
     * 下划线表示“私有”或“内部使用”。
     * 但在 C++ 里，_ 开头的名称是保留给实现的。
     * 所以理论上这个命名是不合法的。
     * 但微软不在乎。他们自己写编译器，自己定规矩。
     * 就像自己家开的餐馆，想怎么炒菜都行。
     */
    wHotkey = _GetHotkeyFromFolderItem(psf, pidl);
    if (wHotkey)
    {
        /*
         * 调用全局的 Tray_RegisterHotKey 函数。
         * 注意前面的 :: 表示全局作用域。
         * 为什么用全局函数？因为这个注册逻辑被多个地方调用。
         * 放在 CHotKey 类外面可以共享。
         * 但这也意味着这个函数必须知道托盘的内部结构（c_tray、v_hwndTray）。
         * 耦合度高。但没关系，因为整个 Shell 都是耦合在一起的。
         * 解耦？不存在的。
         */
        hr = ::Tray_RegisterHotKey(wHotkey, pidlParent, pidl);
    }
    // 如果 wHotkey == 0，什么也不做，返回 S_OK。
    // 表示“这个项目没有热键，不是错误”。
    return hr;
}

/*
 * CHotKey 对象的工厂函数。
 * 创建一个新的 CHotKey 对象，并返回 IShellHotKey 接口指针。
 *
 * 参数：
 *   ppshk - 接收 IShellHotKey 接口指针
 *
 * 返回值：
 *   S_OK - 成功
 *   E_OUTOFMEMORY - 内存不足（new 失败）
 *
 * 为什么需要工厂函数？因为 COM 对象不能直接 new 出来给调用者。
 * COM 规定：对象必须通过工厂函数或 CoCreateInstance 创建。
 * 这样 COM 可以管理对象的生命周期，支持远程调用、套间等。
 *
 * 但实际上，CHotKey 对象非常简单，不需要这些。
 * 但既然 COM 这么要求，就照做。
 * 这叫“遵守规则，即使规则很蠢”。
 */
STDAPI CHotKey_Create(IShellHotKey ** ppshk)
{
    HRESULT hres = E_OUTOFMEMORY;
    CHotKey * photkey = new CHotKey;

    if (photkey)
    {
        /*
         * new 成功，返回 S_OK。
         * 注意：这里没有调用 AddRef。
         * 因为 new 出来的对象引用计数是 1。
         * 所以直接返回，调用者会持有这个引用。
         * 调用者用完后需要 Release。
         *
         * 如果这里再 AddRef 一次，引用计数变成 2，
         * 调用者 Release 后还剩 1，永远释放不了。
         * 内存泄漏。
         * 所以这里不能 AddRef。
         *
         * 这是 COM 对象创建的黄金法则：
         *   工厂函数返回的对象应该已经有引用计数 1。
         *   调用者不需要再 AddRef。
         */
        hres = S_OK;
    }

    /*
     * 返回接口指针。
     * SAFECAST 是一个宏，相当于 static_cast 加上一些安全检查。
     * 在调试版里会检查类型是否正确。
     * 在零售版里就是 static_cast。
     */
    *ppshk = SAFECAST(photkey, IShellHotKey *);
    return hres;
}

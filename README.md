# LiteDict · 纯 C 词典（Windows 原生）

[![build](https://github.com/Philohuare/LiteDict/actions/workflows/build.yml/badge.svg)](https://github.com/Philohuare/LiteDict/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/Philohuare/LiteDict?label=release)](https://github.com/Philohuare/LiteDict/releases/latest)

用 C 和 Win32 API 写的本地英汉词典。**单文件 exe，无 Python、无浏览器、无任何运行时依赖**，
双击即用。界面全部 GDI 自绘，跟随系统深浅色。

> ### 只想用，不想编译
> 到 [**Releases**](https://github.com/Philohuare/LiteDict/releases/latest) 下载
> `LiteDict-*-win64.zip`，解压后双击 `LiteDict.exe` 就行 —— exe 和 77 万词条的词库都在包里，
> 不用装 VS，不用联网。绿色版，删文件夹即卸载。

---

## 直接使用

```
LiteDict.exe          ← 双击这个
data/dict.ldict       ← 词典数据（77 万词条，84 MB）
```

两者放在一起即可，可以整体拷到任意目录或 U 盘。

> **仓库里只放源码**：`LiteDict.exe`（220 KB）和 `data/dict.ldict`（84 MB）不打进 Git，
> 前者每次 CI 都会构建，成品在 [Releases](https://github.com/Philohuare/LiteDict/releases/latest)；
> 后者按下面「词库格式 → 要重建」一节用 `tools/export_ldict.py` 生成。

启动后是一个 **416×390 的小窗，停在屏幕右上角**，适合一边看资料一边查词。
滚动条只在内容超出时才出现，首页这种短内容不会挂一条灰边。
窗口可自由拉伸，布局会自适应：变窄时品牌文字收起、联想列表从两列变两行，词头过长自动降一档字号。

右上角是生词本入口。窗口置顶、不透明度、清空最近查询都在**右键菜单**里。

## 右键面板

**在窗口里点右键**，弹出一个自绘面板：

```
清空最近查询
────────────────────────
窗口置顶                 [ ●]
────────────────────────
不透明度               100%
──────────●─────────────
```

- 三块功能各用一条横线隔开；面板走分层窗口（`UpdateLayeredWindow`）
  逐像素 alpha，圆角与投影都是平滑的
- 置顶是一个拨动开关，点了立即生效
- 不透明度是**滑块**，40% ~ 100% 实时预览，松手才写盘
- 清空最近查询放在最上面（无历史时不可点）

也可以用 `Ctrl`+`Shift`+`↑` / `↓` 每次调 5%。
设置存在 `data/user.txt`，下次启动沿用；半透明只影响显示，点击输入照常。

文件是 `H\t单词` / `F\t单词` / `T\t0|1` / `A\t40..100` 的纯文本，手工编辑也行：
读入时会把行尾的 `\r` 和空白切掉，查词前也会剪掉首尾空白，避免出现
「卡片上看着是 might、点了却说未收录」这种隐形字符问题。

## 全局呼出

在**任何程序里**按 `F1`：

1. 把 LiteDict 切到前台（已最小化则还原）
2. 聚焦搜索框
3. 全选里面现有的词，直接敲新词即替换

实现是 `RegisterHotKey` + `AttachThreadInput` —— 后台进程直接 `SetForegroundWindow`
会被系统拒绝，先挂到当前前台线程才能抢过来。热键注册失败（被别的软件占用）时静默跳过，
其余功能不受影响。

## 快捷键

| 按键 | 作用 |
|---|---|
| 直接输入 | 实时联想（`↑` `↓` 选择） |
| `Enter` | 查询 / 确认选中的联想词 |
| `Esc` | 清空并回到首页 |
| `F1` | 聚焦搜索框（已有内容时全选）；在别的程序里按则全局呼出 |
| `Ctrl` `D` | 收藏当前词到生词本（在结果页按） |
| `Ctrl` `Shift` `↑` `↓` | 调整窗口不透明度 |
| 滚轮 / `PgUp` `PgDn` / 拖动右边缘 | 滚动内容 |
| 右键 | 置顶开关 / 不透明度滑块 / 清空最近查询 |

## 功能

- **离线查词**：77 万词条，中英释义、音标、柯林斯星级、考试标签（中考/高考/CET4/CET6/考研/托福/雅思/GRE）
- **词形还原**：`went` 显示原形提示并可点击跳转，`studied` 这类既有过去式又有过去分词的形式自动合并标签
- **智能联想**：按词频分档 + 词长排序，输入 `stud` 第一个是 `study`
- **拼写纠错**：未收录时给相近词
- **在线例句**：联网时补充英文例句和真人发音，失败自动忽略，不影响离线使用
- **生词本 / 最近查询**：存在 `data/user.txt`，重启不丢（置顶开关、不透明度也记在这）
- **常驻小窗**：默认 1/6 面积，可一键置顶，窗口缩放时排版自适应
- **可调不透明度**：右键菜单的滑块，40% ~ 100%，实时预览
- **粗细一致的界面**：圆角、滚动条、图标全部自绘，不用系统控件
- **深浅色**：读系统主题，标题栏也跟随

## 代码结构

```
LiteDict.sln / LiteDict.vcxproj / test_dict.vcxproj   VS 工程
src/
├── app.c       主程序：窗口、GDI 自绘、排版、交互、联想
├── dict.c      词库层：mmap 映射 + 二分查找
├── online.c    WinHTTP 取例句 + SAPI 发音
└── store.c     生词本与历史的文本存储
src/dict.h, src/online.h, src/store.h
res/app.rc      图标资源
tools/
├── export_ldict.py   把 ECDICT 的 SQLite 导出成 .ldict
├── make_ico.py       白底源图 → 透明圆角多尺寸 ico
├── shot.py           窗口截图（调试用）
├── menu_shot.py      右键面板截图（调试用）
└── uitest.py         交互流程自测
```

## 词库格式

自定义二进制索引，不是 SQLite：

```
[40B 头][entry × N（44B）][var × M（12B）][字符串池]
```

- 条目按小写词字节序排列，变体按变体字节序排列 → C 侧直接二分，无需建索引
- UTF-8 存储，显示时才转 UTF-16
- 整文件 `CreateFileMapping` 映射，启动不读盘，内存占用只算用到的页

要重建：

```bash
# 1. 下载 ECDICT 的 ecdict.csv 放入任意位置
# 2. 先用 SQLite 建库（见 dict-app 项目），再用本工具导出
python tools/export_ldict.py
```

## 编译

### 用 Visual Studio 打开（推荐）

双击 `LiteDict.sln`（VS 2022 / 2026 都能直接开；工程用 `v145` 工具集，
若你装的是 VS2022，在「项目属性 → 常规 → 平台工具集」里改成 `v143` 即可）：

```
LiteDict.sln
├── LiteDict    ← 主程序，Windows 子系统，输出 bin\<Config>\LiteDict.exe
└── test_dict   ← 词库自测，控制台，输出 bin\<Config>\test_dict.exe
```

- 平台选 `x64`、配置选 `Release`，`Ctrl+Shift+B` 就能出包；`.c` 会按 C 编译，无需改设置
- 头文件路径（`src`）、`/utf-8`、`UNICODE`、子系统、14 个系统库都已经写进工程
- 本地若已有 `data/dict.ldict`，构建后会自动拷到 `bin\<Config>\data\`，按 F5 直接能查词
- 图标 `res/app.rc` 挂在「资源文件」下，换图标只需替换 `res/app.ico`

### 命令行（不开 IDE）

需要 Visual Studio 的 C++ 工具链（MSVC + Windows SDK）。

```bat
build.bat litedict
```

脚本里手动指定了 MSVC 与 SDK 的 include/lib 路径，因为 `vcvars64.bat` 会调用
`reg.exe`，在某些受限环境下会被安全策略拦截。

也可以直接构建解决方案（x64 原生工具）：

```bat
msbuild LiteDict.sln /p:Configuration=Release /p:Platform=x64
```

产物 `LiteDict.exe` 约 220 KB，链接：user32 / gdi32 / winhttp / winmm / sapi / dwmapi / imm32 / msimg32。

## 界面怎么画的

GDI 的 `RoundRect` 是硬边像素，圆角会呈阶梯状，这是"像素感"的来源。这里没用它：

1. 建一块 32bpp DIB，按**有符号距离场**逐像素算圆角矩形的覆盖率
2. 用覆盖率做填充与 1px 内描边的预乘 alpha 合成
3. `AlphaBlend` 贴回目标 DC

所以圆角是真正平滑的，边框可以带透明度。滚动条不再用系统的 `WS_VSCROLL`，
而是在右侧自绘一条 6px 圆角细条（悬停变深、可拖动），内容装得下时完全不出现。

图标用 `Segoe MDL2 Assets`，并指定 `ANTIALIASED_QUALITY` 而非 ClearType ——
小字号图标在 ClearType 下会带彩色边缘，放大看很脏。

## 说明

- 在线例句来自 `dictionaryapi.dev`，只发送当前查询的单词
- 发音优先用真人音频，失败退回系统 TTS（SAPI）
- 词库来源 [ECDICT](https://github.com/skywind3000/ECDICT)（MIT），商用请自行确认授权
- 不写注册表，不在系统目录留文件，删除文件夹即卸载

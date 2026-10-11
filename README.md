# W5100S + STM32F103（FSMC 间接总线模式）TCP 回显工程

STM32F103 通过 FSMC 以**间接总线模式（Indirect Bus Mode）**驱动 W5100S 以太网芯片，
跑通 WIZnet ioLibrary_Driver，对外提供一个 TCP 回显（echo）服务器（端口 50000）。

本工程最大的价值是**踩坑记录**：从「串口能打印、ping 不通、netinfo 读回全 `0x03`」
到完全稳定，中间挖出了 3 个独立且都足以致命的问题（IO 基地址、`setMR()` 走错通路、
复位稳定时间 60.3ms 竞态），全部有根因、有证据链、有修复方案。凡是用
**第三方 fork 版 ioLibrary + FSMC 间接总线**驱动 W5100S 的同学，强烈建议先看「踩坑记录」。

---

## 1. 硬件环境

| 项目 | 配置 |
|---|---|
| MCU | STM32F103xE（Cortex-M3，HSE 8MHz ×PLL9 = 72MHz） |
| 以太网芯片 | W5100S（WIZnet，硬件协议栈） |
| 总线接口 | FSMC Bank1 / **NE3**（PG10），8 位数据宽度，间接总线模式 |
| 芯片基地址 | **`0x68000000`**（NE3；注意不是 NE1 的 0x60000000，见坑 1） |
| W5100S 复位 | RSTn ← **PE1**，低有效 |
| 调试器 | CMSIS-DAP（SWD） |
| 串口 | USART1，115200-8-N-1（`printf` 重定向，打印网络配置与调试信息） |

FSMC 引脚（`Core/Src/fsmc.c`，CubeMX 生成）：

| MCU 引脚 | 功能 | | MCU 引脚 | 功能 |
|---|---|---|---|---|
| PD14 | FSMC_D0 | | PE9 | FSMC_D6 |
| PD15 | FSMC_D1 | | PE10 | FSMC_D7 |
| PD0 | FSMC_D2 | | PD4 | FSMC_NOE（RD） |
| PD1 | FSMC_D3 | | PD5 | FSMC_NWE（WR） |
| PE7 | FSMC_D4 | | PG10 | FSMC_NE3（CS） |
| PE8 | FSMC_D5 | | PE1 | W5100S RSTn（GPIO 输出） |

FSMC 时序（`fsmc.c`，72MHz HCLK 下）：AddressSetupTime=10、DataSetupTime=26、
BusTurnAroundDuration=5、AccessMode=A、8 位宽度。约合地址相 139ns / 数据相 361ns，
**偏保守**，但对 W5100S 足够稳定，且有裕量，不建议先动它。

## 2. 软件环境

| 项目 | 版本/说明 |
|---|---|
| 生成工具 | STM32CubeMX（`W5100S.ioc`），STM32F1 HAL |
| 以太网协议栈 | WIZnet **ioLibrary_Driver**（git submodule，见下） |
| 编译器 | Arm GNU Toolchain 14.2.rel1（`arm-none-eabi-gcc`） |
| 烧录/调试 | xPack OpenOCD 0.12.0 + CMSIS-DAP |
| 构建环境 | Windows + Git Bash（`make`） |

`Drivers/ioLibrary_Driver` 是 submodule，停在 `w5100s` 集成分支（基于官方库、含
W6100/W6300 扩展的社区 fork），**包含本工程必需的两处本地修复**（坑 2、坑 4），
详见踩坑记录。

## 3. 目录结构

```
├── Core/                    # CubeMX 生成 + 用户代码（main.c 驱动逻辑都在这）
├── Drivers/
│   ├── CMSIS/               # CMSIS 头文件
│   ├── STM32F1xx_HAL_Driver/# HAL 库
│   └── ioLibrary_Driver/    # WIZnet 官方库（submodule，含本地修复）
├── build/                   # 构建产物（make 输出）
├── W5100S.ioc               # CubeMX 工程文件
├── Makefile                 # make / make flash
└── bus_trace*.log           # 调试期间的总线跟踪日志（可删除，勿提交）
```

## 4. 构建与烧录

```bash
make          # 编译，产物在 build/W5100S.{elf,hex,bin}
make flash    # OpenOCD + CMSIS-DAP 烧录并复位运行
```

> `Makefile` 里 `OPENOCD` 变量是本机绝对路径，换机器请先改。

上电后串口（115200）应打印：

```
MAC address: 00:08:dc:78:91:71
IP address : 192.168.50.254
Subnet mask: 255.255.255.0
Gateway    : 192.168.50.1
DNS server : 192.168.50.1
```

## 5. 快速验证

把电脑网卡配到 `192.168.50.x/24` 网段后：

```bash
ping 192.168.50.254                 # 网络层通
nc 192.168.50.254 50000             # TCP echo：输入什么回什么
```

## 6. 配置要点

**编译期宏（`Makefile` 的 `C_DEFS`，一行都不能少）：**

```make
-D_WIZCHIP_=W5100S \
-D_WIZCHIP_IO_MODE_=_WIZCHIP_IO_MODE_BUS_INDIR_ \
-D_WIZCHIP_IO_BASE_=0x68000000 \
```

可选：`-D_LOOPBACK_DEBUG_`（loopback.h 中的默认定义被 `#if 0` 屏蔽，需经编译命令
打开）会在串口打印 TCP 连接/关闭状态，调试联网问题时很有用。

**运行期配置（`Core/Src/main.c`）：**

- 网络参数：`g_net_info`（IP/GW/DNS 由 `NET_SUBNET`/`NET_IP_HOST` 等宏拼出，
  静态 IP，无 DHCP）
- 回显端口：`TCP_ECHO_PORT 50000`
- 收发缓冲：`g_eth_buf[ETH_MAX_BUF_SIZE]`（轮询模式，无中断/DMA）

---

## 7. 踩坑记录（本项目核心产出）

### 坑 1：fork 库默认 IO 基地址是 NE1（0x60000000），本板是 NE3

**症状**：固件「正常运行」，但芯片从未被真正访问。
**根因**：fork 的 `wizchip_conf.h` 默认 `_WIZCHIP_IO_BASE_ = 0x60000000`（fork 作者
自己板子的 NE1）。本板片选是 NE3（PG10）= `0x68000000`，不传宏时所有 FSMC 访问
都打在未使能的 Bank1 上。
**修复**：`Makefile` 里 `-D_WIZCHIP_IO_BASE_=0x68000000`（头文件有 `#ifndef` 保护，
命令行定义优先生效，不用改库文件）。
**教训**：排查「芯片毫无反应」类问题，第一件事就是确认最终二进制里用的基地址——
注意 GCC 反汇编里地址是 `movw/movt` 或十进制字面量，直接 grep 十六进制字符串会漏。

### 坑 2：fork 的 `setMR()/getMR()` 裸解引用——相位 00 写入不落地（最深的一个）

**症状**：单字节读写全部正常（`getSn_SR()` 等轮询没问题），但
`ctlnetwork(CN_SET_NETINFO)` 之后读回 `MAC=03:03:03:03:03:03`、`IP=3.3.3.3`，
芯片里 SHAR/SIPR 全零；netinfo「写入了但消失了」。

**根因**：这个 fork 把 W5100S 在 BUS_INDIR 分支下的 `setMR()/getMR()` 写成了
`*(uint8_t*)MR` 裸解引用，等价于**相位 00（IDM_MR）总线直访问**。实测本板上相位 00
的**写**物理上不生效（读正常）。于是 `WIZCHIP_WRITE_BUF/READ_BUF` 内部的
`setMR(getMR() | MR_AI)`（打开地址自增）永远不生效 → **所有多字节传输塌缩到
同一个寄存器上**——SHAR 六个字节全砸进第一个地址，netinfo 全军覆没。

**修复**（`Drivers/ioLibrary_Driver/Ethernet/W5100S/w5100s.h`，与 WIZnet 官方 EVB
库一致）：

```c
#define setMR(mr)    WIZCHIP_WRITE(MR, mr)   /* AR=0x0000 + DR 间接访问 MR */
#define getMR()      WIZCHIP_READ(MR)
```

**附注**：
- MR 的 IND/AI 位经 DR 路径「只能置位、不能清零」（sticky），对库无害——库对 MR
  只做 OR 置位，且每次 BUF 传输前都会重载 AR，AI 常开不影响正确性。
- MR 复位后的默认值就是 `0x03`（IND|AI）——串口里读回的 `03:03:03` 指纹
  往往意味着「你在读一个没配置成功/还没稳定的芯片」。

**排查方法**（值得复用）：先用 OpenOCD 手工做三相位逐项读写（相位 01 写 AR0、
相位 10 写 AR1、相位 11 读写 DR），把「哪一相的读/写失效」隔离出来；再用 GDB 在
`bus_write_byte`（所有 FSMC 写的必经函数）挂 `dprintf` 跟踪整个 boot 的总线写序列，
和固件源码逐步对照。本坑就是靠这两招实锤的。

### 坑 3：复位后稳定时间 TSTA 最长 60.3ms——时序竞态（最隐蔽的一个）

**症状**：**同一份固件**，有时 ping/nc 全通，有时 netinfo 全 `0x03`、IMR=0、
socket 端口也写不进去。编译选项看起来毫无变化，故障却随机出现/消失。

**根因**：W5100S 数据手册 7.4.1 节 Table 16（复位时序）：

| 符号 | 含义 | 值 |
|---|---|---|
| TRST | RSTn 复位脉宽 | 210~560 ns |
| **TSTA** | **复位后内部初始化稳定时间** | **max 60.3 ms** |

复位（RSTn 硬复位或 `MR[RST]` 软复位）后，芯片内部初始化（含 25MHz 晶振起振）
最长要 **60.3ms**。而原代码：
- `w5100s_hw_reset()`：PE1 拉高后只 `HAL_Delay(5)`；
- 库的 `wizchip_sw_reset()`：`setMR(MR_RST)` 之后只做**一次** `getMR()`「假装延时」
  （µs 级），紧接着就写恢复值、memsize、IMR，`main()` 里再紧跟 netinfo 写入——
  **全部打进 60.3ms 的稳定窗口，随机丢失**。

晶振「热」的时候（连续复位、未断电）起振远快于冷启动，这就是「有时能通」的原因——
**纯粹的竞态**。参考工程（8MHz 主频、慢总线、init 与 netinfo 之间还隔着串口打印）
从未踩中，纯属侥幸。

**修复**（`Core/Src/main.c`）：

```c
static void w5100s_hw_reset(void)
{
  HAL_GPIO_WritePin(W5100S_RST_GPIO_Port, W5100S_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(W5100S_RST_GPIO_Port, W5100S_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);   /* > TSTA(max 60.3ms)，手册 7.4.1 */
}
```

**可选加固**：`wizchip_sw_reset()` 里 `setMR(MR_RST)` 后轮询 VERR
（Version Register，地址 `0x0080`，固定值 `0x51`，库宏 `getVER()`）直到读到期望值
或超时，彻底不赌运气。

### 坑 4：必须注册默认 buf 回调

fork 在 BUS_INDIR 模式用 `_read_data_buf/_write_data_buf` 做多字节传输，这两个
函数指针默认是 NULL，必须调用：

```c
reg_wizchip_bus_cbfunc(bus_read_byte, bus_write_byte);  /* 单字节回调 */
reg_wizchip_busbuf_cbfunc(NULL, NULL);                  /* NULL = 安装库的默认实现 */
```

只注册了前者、漏了后者的话，任何 `WIZCHIP_*_BUF` 调用会跳到空指针。

### 坑 5：把「时间上先后」当成了「因果」——`-D_LOOPBACK_DEBUG_` 的冤案

`loopback.h` 里默认的 `#define _LOOPBACK_DEBUG_` 被 `#if 0` 屏蔽，Makefile 传入
`-D_LOOPBACK_DEBUG_` 是**有效的**开关——打开后 loopback 会打印
`0:Listen, TCP server loopback, port [50000]`、`0:Connected - <ip> : <port>`、
`0:Socket Closed` 等连接调试信息。

本工程曾在这个宏上栽过跟头：加上它重新编译烧录后，netinfo 立刻回到全 `0x03`、
ping 失败——看起来就像「这个宏引发了回归」。真实原因是坑 3 的复位竞态恰好在
同一时间发作（当时 100ms 硬复位延时的修复还没编译进去），而这个宏与总线行为
**毫无关系**。

**教训**：改动与故障之间必须先建立因果（对照实验：去掉改动复现、加回复现），
再谈修复。时序竞态类问题的「复现/消失」天然是随机的，最容易制造这种冤案。

### 官方勘误（Errata v1.0.0）

仅一条：SPI 模式读 N 字节时的数据有效性问题（首字节需 `6×SYS_CLK+30ns`，
后续字节间至少 `3×SYS_CLK`）。只影响 SPI 模式，与 FSMC 总线模式无关。

---

## 8. 调试技巧（OpenOCD / GDB，按实战顺序）

**直探 W5100S 寄存器**（间接模式三相位访问，不用写一行固件代码）：

```bash
openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg -c init -c halt \
  -c "mwb 0x68000001 0x00"   `# AR0 ← 地址高字节（相位 01）` \
  -c "mwb 0x68000002 0x09"   `# AR1 ← 地址低字节（相位 10）` \
  -c "mdb 0x68000003"        `# 读 DR（相位 11）` \
  -c resume -c shutdown
```

> 注意：MR=0x03 时 AI（地址自增）是开的，连续读 DR 会依次读到后续地址；要逐字节
> 读必须每次重设 AR。反过来，连续读一片寄存器时利用 AI 最方便。

**GDB 跟踪全部总线写**（把 `bus_write_byte` 的每一次调用记成日志）：

```
dprintf bus_write_byte, "W %08x %02x\n", addr, data
```

配合 `break wizchip_setnetinfo` 先停在目标阶段再挂 dprintf，可以把 boot 各阶段的
总线序列完整解码，与源码逐步对照——「写没写出去」和「写出去没生效」从此可分辨。

**其他**：
- halt 后 `reg pc` + `addr2line -f -e build/W5100S.elf <addr>` 定位卡死点；
- 反复起 OpenOCD 前先 `taskkill /IM openocd.exe /F`，残留进程抢 CMSIS-DAP 会报
  `Protocol error / command mismatch`；
- `gdb_port` 等 `*_port` 配置必须写在 `init` 之前。

## 9. 参考资料

- [W5100S 数据手册 v1.2.8](https://docs.wiznet.io/Product/iEthernet/W5100S/Document)（7.4.1 复位时序、间接模式访问）
- [W5100S Errata v1.0.0](https://docs.wiznet.io/Product/iEthernet/W5100S/Errata)
- [WIZnet ioLibrary_Driver](https://github.com/Wiznet/ioLibrary_Driver)（官方库）
- WIZnet W5100S EVB 参考工程（官方 setMR/getMR 写法即本工程坑 2 的修复依据）

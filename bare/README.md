# bare/ - the same experiment with no driver

This is the probe at the root of this repository rewritten with no RadioLib and no Arduino: every
LR2021 host command is built here, byte for byte, out of the datasheet. It exists because
Semtech asked to compile and run the test in their own environment and compare the two driver
implementations, which needs a program where the whole stack is on the page.

It runs the same five phases, logs the same columns and asks the same question. Built with
Zephyr, it reproduced the RadioLib probe's hardware result line for line (see
[Results](#results)), so the two implementations can be compared byte for byte.

## Layout

| path | what it is |
| --- | --- |
| `src/lr2021_probe.h` | the six-function HAL the core needs, and the public entry points |
| `src/lr2021_probe.c` | the whole radio driver: opcodes, framing, bring-up, TX, RX, the phases |
| `src/main.c` | the Zephyr application: SPI, three GPIOs, the poll loop |
| `boards/*.overlay` | the pin map |
| `prj.conf`, `CMakeLists.txt` | the Zephyr build |
| `host/trace_host.c` | a stub chip, so the core compiles and runs on a workstation |

`src/lr2021_probe.c` is portable C11 with no Zephyr, Arduino or CMSIS header in it. Porting it to
another host means writing the six HAL functions in `lr2021_probe.h` - `spi_xfer`, `busy_high`,
`reset_drive_low`, `delay_ms`, `millis`, `log_line` - and nothing else.

## The protocol, in case only this file survives

From DS rev 2.2 section 5.4: a frame is a 16-bit big-endian opcode then its arguments, and BUSY
must be low before it starts. **A write is one frame.** **A read is two:** opcode and arguments,
wait for BUSY low, then clock dummy bytes, and that second frame returns `stat(15:0)` before the
payload - so the first interesting byte of a reply sits at offset 2. Every frame starts with
`stat(15:8) stat(7:0) IrqStat(31:0)` on MISO (section 5.4.1.1), which is why the probe reads the
IRQ status by clocking six zero bytes and issues no command for it, exactly as RadioLib does.
Reading the radio FIFO (`0x0001`) is the one single-frame exception, with no status bytes
(section 5.4.1.3).

Reset is NRESET low for 10 ms, high, then 300 ms and a wait for BUSY low.

## Build and run on hardware

This is the route that was tested on 2026-11-05, on Fedora with upstream Zephyr v4.4.0 (the
version `usp_zephyr` pins) and Zephyr SDK 1.0.1. Upstream Zephyr carries the XIAO nRF54L15 board,
so the nRF Connect SDK is not needed.

### 1. A Zephyr workspace (once)

```sh
python3 -m venv ~/zephyrproject/.venv
. ~/zephyrproject/.venv/bin/activate
pip install west
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.4.0 ~/zephyrproject
cd ~/zephyrproject
west config manifest.project-filter -- '-.*,+cmsis,+cmsis_6,+hal_nordic'   # optional: only what this needs
west update --narrow -o=--depth=1
west zephyr-export
pip install -r zephyr/scripts/requirements-base.txt
cd zephyr && west sdk install -t arm-zephyr-eabi
```

The project filter skips every module this application does not use. Without it, `west update`
fetches the full module set, which also works.

`west sdk install` asks the GitHub API which SDK to fetch. If it fails with HTTP 403 "API rate
limit exceeded", add `--personal-access-token <token>`, for example
`--personal-access-token "$(gh auth token)"`.

### 2. Build

Run this from the root of this repository, in a shell with the workspace's venv active:

```sh
. ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr
west build -b xiao_nrf54l15/nrf54l15/cpuapp bare -p -d build-rx                  # RX
west build -b xiao_nrf54l15/nrf54l15/cpuapp bare -p -d build-tx -- -DROLE=TX     # TX
```

The overlay in `boards/` is picked up automatically for that board target, and both builds finish
with no warnings. The overlay carries the real EVK wiring: NSS D3, BUSY D1, NRESET D2 and
SCK/MISO/MOSI D8/D9/D10, taken from the variant the RadioLib probe runs on.

### 3. Flash

Flash over the XIAO's on-board CMSIS-DAP probe with [pyocd](https://pyocd.io). Use `pyocd list`
to find each kit's unique ID:

```sh
pyocd load --target nrf54l --erase sector --uid <RX probe> build-rx/zephyr/zephyr.hex
pyocd load --target nrf54l --erase sector --uid <TX probe> build-tx/zephyr/zephyr.hex
```

For this board, `west flash` defaults to OpenOCD, which was not tried on this bench.

### 4. Watch

The console is the XIAO's USB serial port at 115200 baud. On Linux it is
`/dev/serial/by-id/*XIAO_nrf54*<probe>-if02`.

- **RX:** prints its init trace, then one line per reception.
- **TX:** runs the sequence three seconds after boot. It then idles for 30 s, resets its own chip
  and runs the sequence again, indefinitely.

### Results

On 2026-11-05 the Zephyr build of both roles reproduced the RadioLib probe's result line for line.
The raw serial logs are in [`logs/`](logs/):

| phases | reported `cr` |
| --- | --- |
| P1 | `0x04` ×10 |
| P2 and P3 | `0x05` ×11 |
| P4 and P5 | `0x07` ×6 |
| the transmitter's second sequence, after a reset of its own chip but not the receiver's | `0x07` throughout |

- **Register:** the `0x8003C4` nibble tracked the reported value on every frame.
- **First log line:** the first line of `logs/zephyr-run1-rx.txt` predates the receiver's reset.
  It came from an earlier image still transmitting on the other kit.
- **Bug fixed along the way:** the core's TX_DONE and TIMEOUT IRQ bits were bits 17 and 20 in the
  first draft. Those are CMD_ERROR and CAD_DONE.
- **Same result through Arduino:** before the Zephyr toolchain was available, the same core also ran
  unmodified through a temporary Arduino HAL, with the same result.

Unlike the RadioLib probe, this port reads no console keys: it only prints. The receiver prints a
summary line every `PROBE_SUMMARY_EVERY` receptions, and the register-write experiment the
RadioLib probe's `c` key performs is available as the build option `PROBE_AUTO_CLEAR=1`, which
writes the configured code into `0x8003C4` the first time the reported rate differs. Treat it as a
mechanism experiment - the address is undocumented, and nothing should ship a write to it.

## Build and run with no hardware

```sh
cc -std=c11 -Wall -Wextra -Isrc -o trace_host src/lr2021_probe.c host/trace_host.c
./trace_host > trace.txt
```

`host/trace_host.c` is a stub LR2021: it prints every frame in both directions, answers the few
reads the bring-up makes with canned values, and injects RX_DONE and TX_DONE so the state machines
run to the end. The 570-line result is the command stream as bytes on the wire - the thing to diff
against another driver's trace for the same configuration - and it needs no hardware and no
cross-compiler. Keep a copy before an edit and diff it after to see what the edit did to the
stream.

## Compile-time switches

| macro | default | what it does |
| --- | --- | --- |
| `ROLE=TX` (CMake) | RX | build the transmitter instead of the receiver |
| `PROBE_FREQ_HZ` | `915000000` | 868100000 failed front-end calibration on this hardware |
| `PROBE_SF`, `PROBE_BW` | 7, BW500 | modulation |
| `PROBE_CR_CONFIGURED` | `0x04` | the rate both ends are set to (4/8) |
| `PROBE_USE_TCXO` | `0` | the EVK has a plain crystal; 1 issues SetTcxoMode |
| `PROBE_IRQ_DIO` | `8` | DIO8 is what the Wio-LR2021 brings out |
| `PROBE_APPLY_DCDC_WORKAROUND` | `1` | Semtech's own DCDC sensitivity workaround, as RadioLib ports it |
| `PROBE_TRACE_ALL` | `0` | log every SPI frame on hardware too, not only in the harness |
| `PROBE_AUTO_CLEAR` | `0` | write the configured code back after each reception |

## Where this stream may differ from USP's

Four places worth comparing, found while writing it:

1. **The IRQ read issues no command.** RadioLib clocks six dummy bytes and takes the IRQ status out
   of the leading status words; it never sends `GetIrqStatus` or `GetAndClearIrqStatus`. The probe
   does the same, then clears explicitly with `ClearIrq`.
2. **Semtech's DCDC workaround is in our path.** It is a port of `lr20xx_workarounds.c` from
   Lora-net/usp, re-applied after every modulation-params write. `PROBE_APPLY_DCDC_WORKAROUND=0`
   builds without it.
3. **TCXO.** The firmware that produced the field observations drives a TCXO with a 1 s startup;
   this EVK has none, and the probe issues no `SetTcxoMode` by default.
4. **Codes `0x08` and `0x09` are unreachable from RadioLib's public API.** `setCodingRate` reaches
   `0x01`-`0x07` only. This core writes the raw modulation-params byte, so adding a `0x09` row to
   `PHASES` is all it takes to transmit one. The out-of-Table-9-4 values `0x0D` and `0x0F` were
   reproduced on the bench with the RadioLib probe's `tx_xiao_bit3` build, which writes the same
   raw byte.

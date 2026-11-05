# lr2021-cr-probe

A bench test for one question about the Semtech LR2021: does the coding-rate field of
`GetLoraPacketStatus` (`0x022A`, reply byte 2, bits 3:0, DS rev 2.2 §9.9.9 Table 9-13) report the
rate of the frame just received, or the bitwise OR of every header's rate since reset?

**It reports the OR.** After a single off-rate frame, the field keeps the ORed value for every
following frame until the chip is reset. Two implementations show this on two LR2021 LoRa Plus
EVKs (chip ROM 1.24): one built on RadioLib, and one with no driver code at all.

Background, and the firmware mitigation that shipped, are in
[meshtastic/firmware#12183](https://github.com/meshtastic/firmware/pull/12183).

## What is here

| | what it is | built with | results |
| --- | --- | --- | --- |
| **[Prebuilt images](https://github.com/meshtastic/lr2021-cr-probe/releases/tag/demo-release)** | RX, TX and bit-3 TX `.hex` files of the RadioLib probe, at 915 MHz | nothing: flash with pyocd | the same as the RadioLib probe |
| **Repository root** | the RadioLib probe: RadioLib on the Arduino core, with no Meshtastic stack, scheduler or concurrency | PlatformIO | [`logs/`](logs/), 2026-11-03 |
| **[`bare/`](bare/)** | the same test with no driver: every host command built from the datasheet in one portable C file behind a six-function HAL, plus a Zephyr port and a workstation harness | upstream Zephyr v4.4.0 | [`bare/logs/`](bare/logs/), 2026-11-05 |

Which one to use depends on what you want:

- **To see the effect:** flash the prebuilt images onto two kits and read the RX serial port.
- **To change the experiment,** for example to add a phase or try another rate: use the RadioLib
  probe, described below.
- **To check every byte on the wire,** or to compare against another LR2021 driver such as USP's:
  use [`bare/`](bare/). Its [README](bare/README.md) has the full build route from an empty
  machine.

## Results at a glance

Both implementations used the same five phases on the same two kits, with the receiver configured
at 4/8 (`0x04`):

| phases | RadioLib probe ([`logs/run1-rx.txt`](logs/run1-rx.txt)) | `bare/` on Zephyr ([`bare/logs/zephyr-run1-rx.txt`](bare/logs/zephyr-run1-rx.txt)) |
| --- | --- | --- |
| P1: 10 frames at `0x04` | `0x04` | `0x04` |
| P2: 1 frame at `0x01` | `0x05` | `0x05` |
| **P3: 10 frames at `0x04`** | **`0x05`** | **`0x05`** |
| P4: 1 frame at `0x02` | `0x07` | `0x07` |
| P5: 5 frames at `0x04` | `0x07` | `0x07` |

A chip that reported the received frame would read `0x04` throughout P3 and P5.

The RadioLib probe also has these results ([details below](#results-radiolib-probe-2026-11-03-us-915-bench)):

- **Bit 3:** one `0x09` frame gives `0x0D` from a `0x04` base, and `0x0F` from a receiver already
  at `0x07`. These are the out-of-table values seen in the field.
- **Register write:** writing the configured code into the undocumented word `0x8003C4` clears the
  field.
- **NRESET:** the chip's reset pin clears it too.

## Why this experiment

The two hypotheses produce the same output if the transmitter keeps cycling through coding rates,
because both give a stream of changing values. To tell them apart, send off-rate frames and then
**stop**. The receiver stays at 4/8 (raw code `0x04`) throughout, and the transmitter walks through
these phases:

| phase | transmit | correct chip | accumulating chip |
| --- | --- | --- | --- |
| P1 | 10 frames at `0x04` | `0x04` | `0x04` |
| P2 | 1 frame at `0x01` | `0x01` | `0x05` |
| **P3** | **10 frames at `0x04`** | **`0x04`** | **`0x05`** |
| P4 | 1 frame at `0x02` | `0x02` | `0x07` |
| P5 | 5 frames at `0x04` | `0x04` | `0x07` |

P3 is the test. By then every frame on air is at the configured rate again.

Each frame carries its own phase and coding rate in its payload, so the RX log alone is a complete
record of the run.

## Results: RadioLib probe (2026-11-03, US 915 bench)

| run | log | sent | reported `cr` |
| --- | --- | --- | --- |
| 1 | [`run1-rx.txt`](logs/run1-rx.txt) | default sequence | `0x04` ×10 → `0x05` from P2 and through **all of P3** → `0x07` from P4 through P5 |
| 2 | [`run2-rx-after-clear.txt`](logs/run2-rx-after-clear.txt) | `c` (register write), then default sequence | write moves `0x8003C4` from `…0117` to `…0114`, next frame reads `0x04`, then the same latch pattern |
| 3 | [`run3-rx-bit3.txt`](logs/run3-rx-bit3.txt) | `tx_xiao_bit3`: one `0x09` frame from a `0x04` base | **`0x0D`** (= `0x04 \| 0x09`), held for the next 10 frames |
| 3, before the reset | same file, first lines | the same `0x09` frame at a receiver still latched at `0x07` | **`0x0F`** (= `0x07 \| 0x09`) |

- **Clean link:** all 78 complete frames decoded without a CRC error, at about −30 dBm RSSI and
  about 15 dB SNR. RadioLib's `readData()` returned every payload, and none has a header-CRC error
  flagged in the `irq` column.
- **Two readouts agree:** on every line, the documented packet-status `cr` equals the low nibble of
  the undocumented word at `0x8003C4`.
- **Bit 3:** an LR20xx transmitter can send `0x08` or `0x09`, and a standard LoRa header cannot.
  The receiver ORs that bit in as well. This explains the values outside Table 9-4 (`0x0D`, `0x0F`)
  that were logged in the field.
- **NRESET clears it:** resetting the host makes RadioLib's `begin()` pulse the LR2021's NRESET pin,
  and that brings the field back to `0x04`. The tail of run 3's previous session (`0x0F`) is
  followed by the fresh boot (`0x04`).

## Hardware

**Semtech LR2021 LoRa Plus EVK:** a Seeed XIAO nRF54L15 on the LoRa Plus expansion board, with a
Wio-LR2021 module. You need two kits, one for RX and one for TX. Use the **LF** SMA port for
sub-GHz.

| signal | XIAO pin | note |
| --- | --- | --- |
| NSS | D3 | |
| IRQ | D0 | driven by the LR2021's **DIO8**, not RadioLib's default DIO5 |
| NRESET | D2 | |
| BUSY | D1 | |
| SPI | D8 SCK / D9 MISO / D10 MOSI | |
| clock | 32 MHz crystal | **no TCXO**. Setting 1.8 V makes `begin()` fail with −707 |
| RF switch | none | switchless direct-tie front end |

The pins come from Semtech's
[usp_zephyr](https://github.com/Lora-net/usp_zephyr) `semtech_wio_lr20xx` shield devicetree, and
meshtastic/firmware's `xiao_nrf54l15_lr2021` variant agrees with them. The devicetree's 1.8 V TCXO
setting does not match this hardware.

`variants/xiao_nrf54l15_lr2021/` shadows the framework's variant for one reason. The stock variant
puts `Serial` on header pins D6/D7, but the XIAO's USB serial port is the on-board SAMD11 bridge.

There are also `*_nucleo` and `*_esp32s3` envs for other hosts. They build, but their pin maps are
placeholders and have never run on hardware.

## Build, flash, run (RadioLib probe)

You need PlatformIO and pyocd. For `bare/`, see [its README](bare/README.md#build-and-run-on-hardware).

```sh
pyocd list                                  # note each kit's probe unique ID
# set the --uid values in platformio.ini's rx_xiao / tx_xiao upload_command
pio run -e rx_xiao -t upload
pio run -e tx_xiao -t upload                # or tx_xiao_bit3 for the 0x09 follow-on
pio device monitor -p /dev/serial/by-id/<RX probe>-if02 -b 115200
```

The TX starts its sequence 5 s after boot. Press `g` on its console to run it again.

The RX console takes these keys:

| key | action |
| --- | --- |
| `s` | summary |
| `c` | write the configured code into `0x8003C4` |
| `h` | help |

Treat `c` as a mechanism experiment. The address is undocumented, and nothing should ship a write
to it.

The default radio settings are 915.0 MHz, SF7, BW500, 4/8, sync word `0x12` and 10 dBm. Frames go
out 400 ms apart, 27 in a sequence. Change `PROBE_FREQ_MHZ` in `platformio.ini` for your region.

The frequency stays off Meshtastic's default US slots. Damaged foreign headers also feed the
accumulator, so stray mesh traffic would contaminate the run.

## Reading the log

```
# n    sent          cr   exp  crc rssi_dbm snr_q reg        irq
12    P3 F01 CR04   0x05 0x04 1   -30      60    0x9D4A0115 0x00060170  OFF-CONFIGURED
```

| column | meaning |
| --- | --- |
| `sent` | the transmitted payload: phase, frame number and coding rate |
| `cr` | the packet-status coding rate, the documented field |
| `exp` | the configured code |
| `crc` | the header's CRC-present flag (1 = the frame carries a payload CRC). USP decodes this bit as `LR20XX_RADIO_LORA_CRC_ENABLED`, not as a pass/fail result. |
| `rssi_dbm` | whole dBm |
| `snr_q` | quarter-dB |
| `reg` | the `0x8003C4` word, logged only |
| `irq` | the raw IRQ status |

Any line where `cr` differs from `exp` is tagged `OFF-CONFIGURED`. If an `OFF-CONFIGURED` line has
bit 9 (`0x200`) set in `irq`, it was a header-CRC error, not a decoded frame.

Most lines read `irq = 0x00060170`. The bits that matter there are 18 (`RX_DONE`) and 17
(`CMD_ERROR`); see [open questions](#open-questions).

### About the files in `logs/`

The logs are raw serial captures, kept exactly as received:

- **`run1-rx.txt`:** starts after the first two banner lines, which the board printed before the
  capture opened.
- **`run3-rx-bit3.txt`:** opens mid-line, with the end of the receiver's previous session. Two
  lines are merged there because the capture opened mid-print. That merged line holds the `0x0F`
  frame.
- **`run2-tx.txt`:** starts with a few junk bytes from the USB bridge at boot.
- **`rx-bringup*.txt`:** the bring-up attempts.
  - 868.1 MHz failed front-end calibration (−1300). This was not investigated.
  - The 1.8 V TCXO attempt failed with −707.
  - The 915 MHz boot was the first good one.

## Open questions

- **`CMD_ERROR` on every reception after the first.** IRQ bit 17 is set alongside `RX_DONE` on every
  frame except the first after boot. It appears in the RadioLib probe and in `bare/`, under both
  Arduino and Zephyr, so it is not a driver quirk. Both stacks re-arm with `ClearIrq` and then
  `SetRx` while the chip is already in continuous RX, so the likeliest explanation is that the chip
  rejects that `SetRx`. This is not verified, and it does not affect the coding-rate result.
- **868.1 MHz.** `begin()` failed front-end calibration there (−1300) on these kits. The bench moved
  to 915 MHz, and this was never investigated.
- **Not yet tested:**
  - channel-activity detection (CAD) interleaved with reception, which Meshtastic runs before every
    transmission;
  - a 125 kHz bandwidth run, which Semtech used and we did not;
  - whether `SetSleep` without retention clears the field.

## Software versions

- **RadioLib probe:** RadioLib is pinned to `5878f990`, the revision meshtastic/firmware pins. The
  XIAO uses Meshtastic's `platform-nordicnrf54` v0.3.1 with the S145 SoftDevice Arduino core.
- **`bare/`:** upstream Zephyr v4.4.0 with Zephyr SDK 1.0.1.

## License

GPL-3.0. The files in `variants/xiao_nrf54l15_lr2021/` are derived from
[meshtastic/firmware](https://github.com/meshtastic/firmware).

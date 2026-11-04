# lr2021-cr-probe

A standalone firmware for testing one question about the Semtech LR2021: does the coding-rate field
of `GetLoraPacketStatus` (`0x022A`, reply byte 2, bits 3:0, DS rev 2.2 §9.9.9 Table 9-13) report
the rate of the frame just received, or the bitwise OR of every header's rate since reset?

On two LR2021 LoRa Plus EVKs (chip ROM 1.24) it reports the **OR**. The raw logs are in [`logs/`](logs/).

The probe uses RadioLib only, with no Meshtastic stack, scheduler or concurrency. Background, and
the firmware mitigation that shipped, are in
[meshtastic/firmware#12183](https://github.com/meshtastic/firmware/pull/12183).

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

## Results (2026-11-03, US 915 bench)

| run | log | sent | reported `cr` |
| --- | --- | --- | --- |
| 1 | [`run1-rx.txt`](logs/run1-rx.txt) | default sequence | `0x04` ×10 → `0x05` from P2 and through **all of P3** → `0x07` from P4 through P5 |
| 2 | [`run2-rx-after-clear.txt`](logs/run2-rx-after-clear.txt) | `c` (register write), then default sequence | write moves `0x8003C4` from `…0117` to `…0114`, next frame reads `0x04`, then the same latch pattern |
| 3 | [`run3-rx-bit3.txt`](logs/run3-rx-bit3.txt) | `tx_xiao_bit3`: one `0x09` frame from a `0x04` base | **`0x0D`** (= `0x04 \| 0x09`), held for the next 10 frames |
| 3, before the reset | same file, first lines | the same `0x09` frame at a receiver still latched at `0x07` | **`0x0F`** (= `0x07 \| 0x09`) |

- **Clean link:** all 78 complete frames have good CRC, about −30 dBm RSSI and about 15 dB SNR, and
  none has a header-CRC error flagged in the `irq` column.
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

## Build, flash, run

You need PlatformIO and pyocd.

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
| `crc` | 1 if the payload CRC is OK |
| `rssi_dbm` | whole dBm |
| `snr_q` | quarter-dB |
| `reg` | the `0x8003C4` word, logged only |
| `irq` | the raw IRQ status |

Any line where `cr` differs from `exp` is tagged `OFF-CONFIGURED`. If an `OFF-CONFIGURED` line has
bit 9 (`0x200`) set in `irq`, it was a header-CRC error, not a decoded frame.

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

## Software versions

RadioLib is pinned to `5878f990`, the revision meshtastic/firmware pins. The XIAO uses Meshtastic's
`platform-nordicnrf54` v0.3.1 with the S145 SoftDevice Arduino core.

## License

GPL-3.0. The files in `variants/xiao_nrf54l15_lr2021/` are derived from
[meshtastic/firmware](https://github.com/meshtastic/firmware).

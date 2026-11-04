#pragma once

// Pin map for the host board. Every entry here is hardware, not behaviour: get these wrong
// and begin() returns -2 (chip not found) or -705 / timeouts on BUSY, not a wrong coding rate.
//
// Pick a board with one -D flag, or define all five LR2021_* macros on the command line and
// define neither board flag.

#if defined(PROBE_BOARD_XIAO_NRF54_LR2021)

// Semtech LR2021 LoRa Plus EVK: Seeed XIAO nRF54L15 + LoRa Plus expansion board + Wio-LR2021.
// Source: usp_zephyr boards/shields/semtech_wio_lr20xx/semtech_wio_lr20xx_common.dtsi, which
// meshtastic/firmware variants/nrf54l15/xiao_nrf54l15 (env xiao_nrf54l15_lr2021) agrees with.
// SPI is the default object on D8 SCK / D9 MISO / D10 MOSI (variants/xiao_nrf54l15_lr2021).
#define LR2021_NSS D3
#define LR2021_IRQ D0   // wired to the LR2021's DIO8, not RadioLib's default DIO5
#define LR2021_RESET D2
#define LR2021_BUSY D1
#define LR2021_IRQ_DIO 8
// Switchless direct-tie front end: no RF switch table. Use the LF SMA port for sub-GHz.

#elif defined(PROBE_BOARD_EVK_NUCLEO)

// Semtech LR2021 EVK shield on an ST Nucleo's Arduino headers.
// CONFIRM THESE AGAINST THE KIT SCHEMATIC BEFORE FLASHING - they follow the SX126x/LR11xx
// EVK convention (the shields are pin-compatible in this part of the header), but the LR2021
// shield's silkscreen is the authority.
#define LR2021_NSS D7    // SPI chip select
#define LR2021_IRQ D5    // DIO9 / generic IRQ line
#define LR2021_RESET A0  // NRESET
#define LR2021_BUSY D3   // BUSY
// SPI1 on the Arduino header: SCK D13, MISO D12, MOSI D11 - the default Arduino SPI object.

#elif defined(PROBE_BOARD_ESP32S3_GENERIC)

// Generic ESP32-S3 devkit wired to an LR2021 module. These are the free pins on a
// devkitc-1; change them to match your wiring.
#define LR2021_NSS 10
#define LR2021_IRQ 9
#define LR2021_RESET 8
#define LR2021_BUSY 7
#define LR2021_SCK 12
#define LR2021_MISO 13
#define LR2021_MOSI 11

#endif

#if !defined(LR2021_NSS) || !defined(LR2021_IRQ) || !defined(LR2021_RESET) || !defined(LR2021_BUSY)
#error "No pin map: select a PROBE_BOARD_* or define LR2021_NSS/IRQ/RESET/BUSY yourself"
#endif

// Some boards drive their front end from the LR2021's own DIOs. If this devkit does, the
// radio will transmit but hear almost nothing until the switch table is set. Define
// PROBE_HAS_RF_SWITCH and fill in the table in main.cpp; variants/esp32s3/t-beam-1w/rfswitch.h
// in meshtastic/firmware is a worked example of the same table for this part.

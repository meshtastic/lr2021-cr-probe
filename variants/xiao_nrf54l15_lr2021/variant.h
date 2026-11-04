#pragma once

/*
 * Seeed XIAO nRF54L15 on the Semtech LR2021 LoRa Plus EVK (Wio-LR2021 on the LoRa Plus expansion
 * board). Trimmed from meshtastic/firmware variants/nrf54l15/xiao_nrf54l15/variant.h.
 *
 * This header shadows the framework's variants/xiao_nrf54l15/variant.h. The reason it exists: the
 * framework's own variant puts Serial (= Serial1) on header pins D6/D7, but the XIAO's USB serial
 * port is the on-board SAMD11 bridge on P1.08/P1.09. Without this file the probe's log never
 * reaches the USB port.
 *
 * Arduino pins 0..10 are the XIAO header D0..D10, 11..23 are internal signals (see variant.cpp).
 */

#define VARIANT_MCK (128000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (24)
#define NUM_DIGITAL_PINS (24)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)
#define ADC_RESOLUTION 14

#define D0 (0ul)
#define D1 (1ul)
#define D2 (2ul)
#define D3 (3ul)
#define D4 (4ul)
#define D5 (5ul)
#define D6 (6ul)
#define D7 (7ul)
#define D8 (8ul)
#define D9 (9ul)
#define D10 (10ul)

#define PIN_A0 D0
#define PIN_A1 D1
#define PIN_A2 D2
#define PIN_A3 D3
#define PIN_A4 D4
#define PIN_A5 D5
static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;

// User LED P2.00 (active low)
#define PIN_LED1 16
#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 0

// User button P0.00 (active low)
#define PIN_BUTTON1 17

// Serial1 (= Serial): the SAMD11 USB-CDC bridge (UARTE20): nRF TX P1.09, nRF RX P1.08
#define PIN_SERIAL1_TX 18
#define PIN_SERIAL1_RX 19
#define SERIAL1_UARTE NRF_UARTE20
#define SERIAL1_IRQN SERIAL20_IRQn
#define SERIAL1_IRQ_HANDLER SERIAL20_IRQHandler

// Serial2: header D6 (TX) / D7 (RX) on UARTE21
#define PIN_SERIAL2_TX D6
#define PIN_SERIAL2_RX D7
#define SERIAL2_UARTE NRF_UARTE21
#define SERIAL2_IRQN SERIAL21_IRQn
#define SERIAL2_IRQ_HANDLER SERIAL21_IRQHandler

// SPI (SPIM00): D8 SCK, D9 MISO, D10 MOSI; the Wio-LR2021's NSS is D3
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO D9
#define PIN_SPI_MOSI D10
#define PIN_SPI_SCK D8
static const uint8_t SS = D3;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// Wire (TWIM22): header I2C on D4 (SDA, P1.10) / D5 (SCL, P1.11), the expansion board's OLED/Grove
#define PIN_WIRE_SDA D4
#define PIN_WIRE_SCL D5
#define WIRE_TWIM NRF_TWIM22
#define WIRE_TWIS NRF_TWIS22
#define WIRE_IRQN SERIAL22_IRQn
#define WIRE_IRQ_HANDLER SERIAL22_IRQHandler
#define WIRE_INTERFACES_COUNT 1

#ifdef __cplusplus
}
#endif

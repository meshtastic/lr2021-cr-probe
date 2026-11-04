// LR2021 packet-status coding-rate probe.
//
// What it tests: whether the cr field of GetLoraPacketStatus (0x022A, reply byte 2, bits 3:0 -
// DS rev 2.2 section 9.9.9 Table 9-13) reports the coding rate of the frame just received, or the
// bitwise OR of every header's rate since reset.
//
// The experiment is phase P3: after one off-rate frame, every frame on air is back at the
// configured rate. A correct receiver reports the configured code again. A receiver that
// accumulates keeps the ORed value for the rest of the boot.
//
// One source file, two roles. Build the RX env for one board, the TX env for another.
// The TX frames carry their own phase and coding rate in the payload, so the RX log alone is
// a complete record of the run.

#include <RadioLib.h>
#include <stdarg.h>
#include <stdio.h>

#include "board_pins.h"

#if !defined(PROBE_ROLE_RX) && !defined(PROBE_ROLE_TX)
#error "Build an rx_* or tx_* env (PROBE_ROLE_RX / PROBE_ROLE_TX)"
#endif

#ifndef PROBE_FREQ_MHZ
#define PROBE_FREQ_MHZ 868.1f
#endif
#ifndef PROBE_SF
#define PROBE_SF 7
#endif
#ifndef PROBE_BW_KHZ
#define PROBE_BW_KHZ 500.0f
#endif
#ifndef PROBE_BW_CODE
#define PROBE_BW_CODE RADIOLIB_LR2021_LORA_BW_500
#endif
#ifndef PROBE_POWER_DBM
#define PROBE_POWER_DBM 10
#endif
#ifndef PROBE_TCXO_V
#define PROBE_TCXO_V 1.6f
#endif
#ifndef PROBE_PREAMBLE
#define PROBE_PREAMBLE 8
#endif
#ifndef PROBE_SYNCWORD
#define PROBE_SYNCWORD RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE
#endif

// The rate both ends are configured for. Raw code 0x04 is 4/8 (Table 9-4); setCodingRate()
// takes the denominator, so 8 and 0x04 are the same rate expressed two ways.
#define CONFIGURED_CR_CODE RADIOLIB_LR2021_LORA_CR_4_8
#define CONFIGURED_CR_DENOM 8

// Undocumented, and only an instrument: the low nibble of this word equalled packet-status
// reply byte 2's low nibble on every read taken on two different boards. Logged, never relied on.
#define PROBE_STATUS_REG 0x8003C4UL
#define PROBE_CR_MASK 0x0000000FUL

#if defined(LR2021_SCK)
static SPIClass probeSpi(FSPI);
static Module probeMod(LR2021_NSS, LR2021_IRQ, LR2021_RESET, LR2021_BUSY, probeSpi);
#else
static Module probeMod(LR2021_NSS, LR2021_IRQ, LR2021_RESET, LR2021_BUSY);
#endif
static LR2021 radio(&probeMod);

#ifdef PROBE_HAS_RF_SWITCH
// Fill both of these in from the devkit's schematic: which of the LR2021's own DIOs drive the
// front end, and the state each takes in every mode. Exactly RFSWITCH_MAX_PINS (5) entries, with
// RADIOLIB_NC for the slots this board does not use. The values below are a placeholder shape,
// not this kit's wiring. variants/esp32s3/t-beam-1w/rfswitch.h in meshtastic/firmware is a
// worked example of the same table for this part.
static const uint32_t probeRfSwitchPins[Module::RFSWITCH_MAX_PINS] = {
    RADIOLIB_LR2021_DIO5, RADIOLIB_LR2021_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC,
};
static const Module::RfSwitchMode_t probeRfSwitchTable[] = {
    {LR2021::MODE_STBY, {LOW, LOW}},
    {LR2021::MODE_RX, {HIGH, LOW}},
    {LR2021::MODE_TX, {LOW, HIGH}},
    END_OF_MODE_TABLE,
};
#endif

// Serial.printf() does not exist on every Arduino core, and the newlib-nano printf that STM32
// builds link by default drops %f, so everything goes through vsnprintf and integers only.
static void probeLog(const char *fmt, ...)
{
    char line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    Serial.print(line);
}

static void halt(const char *what, int16_t code)
{
    for (;;) {
        probeLog("FATAL %s rc=%d\n", what, code);
        delay(2000);
    }
}

// Shared bring-up. begin() tunes first, which matters at this RadioLib revision: every LoRa
// modulation-params setter ends in a setFrequency() call, so one issued before begin() fails
// with -12 INVALID_FREQUENCY.
static void radioBegin(const char *role)
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) {
    }
    probeLog("\n# LR2021 CR probe, role=%s\n", role);
    probeLog("# freq=%d kHz SF%d BW%d kHz cr=0x%02X preamble=%d power=%d dBm\n", (int)(PROBE_FREQ_MHZ * 1000.0f),
             (int)PROBE_SF, (int)PROBE_BW_KHZ, CONFIGURED_CR_CODE, (int)PROBE_PREAMBLE, (int)PROBE_POWER_DBM);

#if defined(LR2021_SCK)
    probeSpi.begin(LR2021_SCK, LR2021_MISO, LR2021_MOSI, LR2021_NSS);
#endif

#ifdef LR2021_IRQ_DIO
    // begin() programs this DIO as the IRQ output, so it has to be set first. This sketch polls
    // the IRQ status over SPI rather than the pin, but the DIO driven should be the one wired.
    radio.irqDioNum = LR2021_IRQ_DIO;
#endif

    int16_t rc = radio.begin(PROBE_FREQ_MHZ, PROBE_BW_KHZ, PROBE_SF, CONFIGURED_CR_DENOM, PROBE_SYNCWORD,
                             PROBE_POWER_DBM, PROBE_PREAMBLE, PROBE_TCXO_V);
    if (rc != RADIOLIB_ERR_NONE) {
        // GetErrors says which block failed (XOSC start, PLL lock, image/front-end calibration).
        uint16_t chipErrors = 0;
        radio.getErrors(&chipErrors);
        probeLog("# begin failed, chip errors=0x%04X\n", chipErrors);
        halt("begin", rc);
    }

    // Explicit header is what carries the coding rate on air; in implicit-header mode there is
    // no received header rate to report and the field cannot be tested at all.
    rc = radio.explicitHeader();
    if (rc != RADIOLIB_ERR_NONE) {
        halt("explicitHeader", rc);
    }
    rc = radio.setCRC(2);
    if (rc != RADIOLIB_ERR_NONE) {
        halt("setCRC", rc);
    }

#ifdef PROBE_HAS_RF_SWITCH
    // After begin(), not before: on this part the table is programmed into the chip's DIOs, so
    // the chip has to be up first. This is the order LR20x0Interface uses in the firmware.
    radio.setRfSwitchTable(probeRfSwitchPins, probeRfSwitchTable);
#endif

    uint8_t major = 0, minor = 0;
    rc = radio.getVersion(&major, &minor);
    probeLog("# chip firmware major=0x%02X minor=0x%02X (rc=%d)\n", major, minor, rc);
}

#ifdef PROBE_ROLE_TX
// setCodingRate() reaches the standard codes 0x01-0x04 and, with longInterleave, 0x05-0x07.
// The two convolutional codes 0x08/0x09 have no public setter, so they go through the raw
// command. Table 9-4 footnotes them as incompatible with every non-LR20xx part.
static int16_t setCrCode(uint8_t code)
{
    switch (code) {
    case RADIOLIB_LR2021_LORA_CR_4_5:
        return radio.setCodingRate(5);
    case RADIOLIB_LR2021_LORA_CR_4_6:
        return radio.setCodingRate(6);
    case RADIOLIB_LR2021_LORA_CR_4_7:
        return radio.setCodingRate(7);
    case RADIOLIB_LR2021_LORA_CR_4_8:
        return radio.setCodingRate(8);
    case RADIOLIB_LR2021_LORA_CR_4_5_LI:
        return radio.setCodingRate(5, true);
    case RADIOLIB_LR2021_LORA_CR_4_6_LI:
        return radio.setCodingRate(6, true);
    case RADIOLIB_LR2021_LORA_CR_4_8_LI:
        return radio.setCodingRate(8, true);
    default:
        return radio.setLoRaModulationParams(PROBE_SF, PROBE_BW_CODE, code, RADIOLIB_LR2021_LORA_LDRO_DISABLED);
    }
}
#endif // PROBE_ROLE_TX

#ifdef PROBE_ROLE_RX

static uint32_t rxTotal = 0;
static uint8_t firstCr = 0xFF;
static uint8_t lastCr = 0xFF;
static uint32_t offConfigured = 0;

static void printHelp()
{
    Serial.println("# keys: s summary | c clear the cr nibble by register write | h help");
}

void setup()
{
    radioBegin("RX");
    int16_t rc = radio.startReceive(); // RADIOLIB_LR2021_RX_TIMEOUT_INF: continuous
    if (rc != RADIOLIB_ERR_NONE) {
        halt("startReceive", rc);
    }
    Serial.println("# n    sent          cr   exp  crc rssi_dbm snr_q reg        irq");
    printHelp();
}

static void handleReception(uint32_t irq)
{
    // Read the status BEFORE readData() and before re-arming: once the radio is listening
    // again the status describes whatever it is receiving now, not the frame just taken.
    uint8_t cr = 0, detector = 0xFF, len = 0;
    bool crcOk = false;
    float snr = 0, rssi = 0, rssiSignal = 0;
    int16_t rcStat = radio.getLoRaPacketStatus(&cr, &crcOk, &len, &snr, &rssi, &rssiSignal, &detector);

    uint32_t reg = 0;
    int16_t rcReg = radio.readRegMem32(PROBE_STATUS_REG, &reg, 1);

    String payload;
    int16_t rcData = radio.readData(payload);

    rxTotal++;
    if (firstCr == 0xFF) {
        firstCr = cr;
    }
    lastCr = cr;
    if (cr != CONFIGURED_CR_CODE) {
        offConfigured++;
    }

    // snr_q is quarter-dB, the chip's own unit, to keep floats out of the print path.
    probeLog("%-5lu %-13s 0x%02X 0x%02X %d   %-8d %-5d 0x%08lX 0x%08lX%s%s\n", (unsigned long)rxTotal,
             rcData == RADIOLIB_ERR_NONE ? payload.c_str() : "<no payload>", cr, CONFIGURED_CR_CODE, crcOk ? 1 : 0,
             (int)(rssi - 0.5f), (int)(snr * 4.0f), (unsigned long)(rcReg == RADIOLIB_ERR_NONE ? reg : 0),
             (unsigned long)irq, cr != CONFIGURED_CR_CODE ? "  OFF-CONFIGURED" : "",
             rcStat != RADIOLIB_ERR_NONE ? "  STATUS-ERR" : "");

    radio.clearIrqFlags(RADIOLIB_LR2021_IRQ_ALL);
    radio.startReceive();
}

void loop()
{
    const uint32_t rxEvents = RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_CRC_ERROR |
                              RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR | RADIOLIB_LR2021_IRQ_TIMEOUT;
    uint32_t irq = radio.getIrqFlags();
    if (irq & rxEvents) {
        handleReception(irq);
    }

    if (Serial.available()) {
        int key = Serial.read();
        if (key == 's') {
            probeLog("# received=%lu first_cr=0x%02X last_cr=0x%02X configured=0x%02X off_configured=%lu\n",
                     (unsigned long)rxTotal, firstCr, lastCr, CONFIGURED_CR_CODE, (unsigned long)offConfigured);
        } else if (key == 'c') {
            // Mechanism experiment, not a fix: write the configured code back into the nibble.
            uint32_t before = 0, after = 0;
            radio.readRegMem32(PROBE_STATUS_REG, &before, 1);
            int16_t rc = radio.writeRegMemMask32(PROBE_STATUS_REG, PROBE_CR_MASK, CONFIGURED_CR_CODE);
            radio.readRegMem32(PROBE_STATUS_REG, &after, 1);
            probeLog("# clear rc=%d 0x%08lX -> 0x%08lX; watch the next reception\n", rc, (unsigned long)before,
                     (unsigned long)after);
            radio.startReceive();
        } else if (key == 'h') {
            printHelp();
        }
    }
}

#endif // PROBE_ROLE_RX

#ifdef PROBE_ROLE_TX

struct ProbePhase {
    const char *name;
    uint8_t code;
    uint8_t frames;
    const char *note;
};

#ifdef PROBE_SEQUENCE_BIT3
// Bit-3 follow-on: one frame at 0x09 (4/8 LI convolutional, LR20xx-only) from a 0x04 base. A
// standard LoRa header carries three bits of coding rate, so only an LR20xx transmitter can
// advertise bit 3. Correct chip: 0x09 then 0x04 again. Accumulating chip: 0x0D (= 0x04 | 0x09),
// a value outside Table 9-4, and it holds.
static const ProbePhase PHASES[] = {
    {"P1", RADIOLIB_LR2021_LORA_CR_4_8, 10, "baseline at the configured rate"},
    {"B1", RADIOLIB_LR2021_LORA_CR_4_8_LI_CONV, 1, "one 0x09 frame: correct 0x09, accumulating 0x0D"},
    {"B2", RADIOLIB_LR2021_LORA_CR_4_8, 10, "back to configured: correct 0x04, accumulating 0x0D"},
};
#else
// P3 is the experiment: the off-rate traffic has stopped and every frame is at the configured
// rate again. P4 and P5 show a second step and that it holds.
static const ProbePhase PHASES[] = {
    {"P1", RADIOLIB_LR2021_LORA_CR_4_8, 10, "baseline at the configured rate"},
    {"P2", RADIOLIB_LR2021_LORA_CR_4_5, 1, "one off-rate frame"},
    {"P3", RADIOLIB_LR2021_LORA_CR_4_8, 10, "back to configured: correct 0x04, accumulating 0x05"},
    {"P4", RADIOLIB_LR2021_LORA_CR_4_6, 1, "a second off-rate frame"},
    {"P5", RADIOLIB_LR2021_LORA_CR_4_8, 5, "back again: correct 0x04, accumulating 0x07"},
};
#endif
static const size_t PHASE_COUNT = sizeof(PHASES) / sizeof(PHASES[0]);

#ifndef PROBE_FRAME_GAP_MS
#define PROBE_FRAME_GAP_MS 400
#endif
#ifndef PROBE_PHASE_GAP_MS
#define PROBE_PHASE_GAP_MS 2000
#endif

static void runSequence()
{
    Serial.println("# --- sequence start ---");
    for (size_t p = 0; p < PHASE_COUNT; p++) {
        const ProbePhase &ph = PHASES[p];
        int16_t rc = setCrCode(ph.code);
        probeLog("# %s cr=0x%02X x%u rc=%d  (%s)\n", ph.name, ph.code, (unsigned)ph.frames, rc, ph.note);
        if (rc != RADIOLIB_ERR_NONE) {
            probeLog("# %s SKIPPED: this build cannot set cr=0x%02X\n", ph.name, ph.code);
            continue;
        }
        for (uint8_t f = 1; f <= ph.frames; f++) {
            char payload[24];
            snprintf(payload, sizeof(payload), "%s F%02u CR%02X", ph.name, (unsigned)f, ph.code);
            int16_t rcTx = radio.transmit(payload);
            probeLog("TX %-13s rc=%d\n", payload, rcTx);
            delay(PROBE_FRAME_GAP_MS);
        }
        delay(PROBE_PHASE_GAP_MS);
    }
    Serial.println("# --- sequence done; press g to run it again ---");
}

void setup()
{
    radioBegin("TX");
    Serial.println("# press g to run the sequence, or wait 5 s for it to start");
    uint32_t t0 = millis();
    while ((millis() - t0) < 5000) {
        if (Serial.available() && Serial.read() == 'g') {
            break;
        }
    }
    runSequence();
}

void loop()
{
    if (Serial.available() && Serial.read() == 'g') {
        runSequence();
    }
}

#endif // PROBE_ROLE_TX

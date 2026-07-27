
# Industrial Motor Condition-Monitoring & Predictive Maintenance System

Multi-sensor system on STM32 to detect early mechanical fault signatures in industrial motors — before failure, not after.

**Status: In active development.** Vibration sensing and current sensing pipelines are fully operational and validated on-chip. This README reflects the real state of the build at all times.

---

## Why this exists

Unplanned motor failure is one of the most expensive routine events in a factory — downtime, emergency repair, lost production. Large plants buy imported condition-monitoring systems priced far beyond what small and mid-size Indian factories can justify. This project explores how much fault-detection capability can be built at hobbyist-to-SME cost using a general-purpose MCU and low-cost sensors.

Motor faults announce themselves before failure — through vibration signatures (bearing wear, imbalance, misalignment), current signatures (load anomalies, winding issues), heat, and sound. The system's goal: catch those announcements.

---

## System architecture


```

```
                    ┌────────────────────────────┐

```

ADXL345 (vibration) ──┤ I2C                        │
│                            │
SCT-013 (current) ────┤ ADC ← analog front-end     │──► UART telemetry
│       (burden R + DC bias) │    (live data out)
Thermal IR (planned) ─┤ I2C/ADC                    │
│      STM32F446RE           │
Acoustic (planned) ───┤ ADC  (ARM Cortex-M4)       │
└────────────────────────────┘

Processing roadmap: raw capture → RMS / peak-to-peak feature extraction → ML anomaly engine

```

---

## Hardware

| Component | Role | Interface | Status |
|-----------|------|-----------|--------|
| STM32 Nucleo-F446RE | Main controller (Cortex-M4 @ 180 MHz) | — | Active |
| ADXL345 | 3-axis vibration sensing | I2C | ✅ Working, validated on live motor |
| SCT-013-030 | Non-invasive current sensing (30A/1V CT clamp) | ADC1 (PA0) via voltage divider front-end | ✅ Working, verified live with RMS conversion |
| Thermal IR sensor | Temperature signature | I2C/ADC | Planned (Next Phase) |
| Acoustic sensor | Acoustic emission | ADC | Planned (Next Phase) |

### Current-Sensing Front End (Validated)

The SCT-013-030 outputs an AC voltage proportional to the primary load current ($1\text{V RMS} = 30\text{A RMS}$). To allow the single-supply STM32 ADC ($0\text{--}3.3\text{V}$) to read the bipolar AC waveform:

- **Internal Burden Resistor:** Utilized the module's integrated burden resistor to output a safe $0\text{--}1\text{V RMS}$ signal directly.
- **$1.65\text{V}$ DC-Bias Network:** Built a 2-resistor voltage divider across the $+3.3\text{V}$ rail to shift the AC zero-crossing point directly to the ADC mid-point ($\approx 2048$ raw counts on a 12-bit ADC).
- **On-Chip Signal Processing:** Configured bare-metal ADC1 on channel `PA0` to continuously sample over a $\sim 50\text{ms}$ window ($\approx 2.5$ full $50\text{Hz}$ AC cycles).
- **Software Noise Gate:** Implemented a software peak-to-peak cut-off threshold ($<350$ counts) to filter out high-frequency breadboard contact noise and ground plane ripple when idling.

---

## What's validated so far

- **Vibration Pipeline:** ADXL345 configured and read over bare-metal I2C (register-level init: data format, sampling rate, measurement mode); captured live vibration signatures under actual motor load.
- **Current Pipeline:** SCT-013 front-end hardware built and validated. Real-time ADC1 sampling converts raw peak-to-peak waveform swings into true $I_{RMS}$ values.
- **Hardware Bus Resilience:** Debugged and resolved bare-metal I2C bus hangs (missing timeouts/SDA low lockups) and ARM Cortex-M4 vector table stack alignment faults (`HardFault` handler routines).
- **Live Telemetry:** Streaming raw ADC, peak-to-peak counts, and real-time $I_{RMS}$ directly to debugger live memory inspection interfaces.

---

## Roadmap

1. ✅ Vibration acquisition (ADXL345 / I2C) — Done, validated on live motor
2. ✅ Current sensing (SCT-013 + analog front-end) — Done, $1.65\text{V}$ DC bias & $I_{RMS}$ calculation validated
3. 🔧 Multi-sensor baseline acquisition — Integrating thermal & acoustic channels to build a complete 4-parameter feature vector
4. Unsupervised ML model training (Isolation Forest / Autoencoder) for dynamic, self-learning anomaly thresholding
5. On-device edge inference deployment (C-array quantization via Edge Impulse / STM32Cube.AI)

---

## Repo layout


```

├── Core/                STM32CubeIDE project source (main, drivers, config)
├── docs/
│   ├── debug-log.md    Problems hit and how they were resolved
│   └── wiring.md       Sensor wiring and front-end schematic notes
└── data/                Sample captured sensor data

```

---

## Tools

STM32CubeIDE · Embedded C (STM32 HAL, register-level configuration where needed) · Git

---

## Team

Built by a 4-member team — final-year Electronics Engineering, Bangalore Institute of Technology:

- **Parshwa Sangame**
- **Siddharth N**
- **Sumit Sungar**
- **Sugreshwara H**

This repo is maintained by [Parshwa Sangame](https://github.com/YOUR-USERNAME) — my focus areas in the project: sensor integration and bring-up (ADXL345/I2C), the current-sensing analog front end, debugging, and component sourcing/BOM. Part of a broader interest in affordable industrial hardware for Indian SMEs — see my [sector research](https://github.com/YOUR-USERNAME/india-hardware-opportunities).

---

## Video link 
[DRIVE LINK](https://drive.google.com/file/d/1ArUdlh8uGeaoBaL-WMMEUXpvZn7hS0YO/view?usp=drive_link).

```

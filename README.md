# Industrial Motor Condition-Monitoring & Predictive Maintenance System

Multi-sensor system on STM32 to detect early mechanical fault signatures in industrial motors — before failure, not after.

**Status: In active development.** Current sensing and vibration sensing pipelines are fully operational and validated on-chip. This README reflects the real state of the build at all times.

---

## Why this exists

Unplanned motor failure is one of the most expensive routine events in a factory — downtime, emergency repair, lost production. Large plants buy imported condition-monitoring systems priced far beyond what small and mid-size Indian factories can justify. This project explores how much fault-detection capability can be built at hobbyist-to-SME cost using a general-purpose MCU and low-cost sensors.

Motor faults announce themselves before failure — through vibration signatures (bearing wear, imbalance, misalignment), current signatures (load anomalies, winding issues), heat, and sound. The system's goal: catch those announcements.

---

## System architecture

```
                    ┌────────────────────────────┐
 SCT-013 (current) ─┤ ADC ← analog front-end     │
                    │       (burden R + DC bias)  │
 MAX4466 (acoustic) ┤ ADC                         │──► live telemetry
                    │                             │    (debugger memory)
 ADXL345 (vibration)┤ SPI                         │
                    │      STM32F446RE            │
 MLX90614 (thermal) ┤ I2C                         │
                    │      (ARM Cortex-M4)        │
                    └────────────────────────────┘

 Processing pipeline: raw capture → RMS feature extraction per sensor
                      → 4-parameter feature vector → anomaly engine (planned)
```

All firmware is **bare-metal Embedded C** — direct register access, no HAL — for low-latency, deterministic acquisition and full control over each peripheral.

---

## Hardware

| Component | Role | Interface | Status |
|-----------|------|-----------|--------|
| STM32 Nucleo-F446RE | Main controller (Cortex-M4) | — | Active |
| SCT-013-030 | Non-invasive current sensing (30A/1V CT clamp) | ADC1 ch0 (PA0) via DC-bias front-end | ✅ Working, true-RMS validated on-chip |
| MAX4466 | Acoustic emission (electret mic amp) | ADC1 ch1 (PA1) | ✅ Working, RMS loudness validated |
| ADXL345 | 3-axis vibration sensing | SPI1 (PA4–PA7) | ✅ Working, DEVID + live axis data validated |
| MLX90614 | Thermal IR (object temperature) | I2C1 (PB8/PB9) | 🔧 Wired & coded; resolving physical connection |

### Current-Sensing Front End (Validated)

The SCT-013-030 outputs an AC voltage proportional to primary load current (1V RMS = 30A RMS). To let the single-supply STM32 ADC (0–3.3V) read the bipolar AC waveform:

- **Internal Burden Resistor:** Used the module's integrated burden resistor to output a safe 0–1V RMS signal directly.
- **1.65V DC-Bias Network:** A 2-resistor divider across +3.3V shifts the AC zero-crossing to the ADC mid-point (~2048 counts on the 12-bit ADC), with a 10µF cap decoupling the bias rail.
- **On-Chip Processing:** Bare-metal ADC1 samples PA0 over a fixed window, computing **true RMS via sum-of-squares** (per-window DC-offset removal), then converts to I_RMS using the sensor's calibration.
- **Noise Gate:** A software floor on RMS counts suppresses residual breadboard pickup when idling. (Sum-of-squares replaced an earlier peak-to-peak method that was corrupted by single-sample noise spikes — the corrected method is inherently spike-robust.)

---

## What's validated so far

- **Current Pipeline (SCT-013):** Analog front-end built; bare-metal ADC1 sampling; true-RMS extraction verified live via debugger memory inspection. Reads ~0 idle, sane current under load.
- **Acoustic Pipeline (MAX4466):** Added as a second ADC channel; same sum-of-squares RMS yields a relative loudness feature that responds to sound in real time.
- **Vibration Pipeline (ADXL345):** Bare-metal SPI (Mode 3, software chip-select). Bring-up verifies the DEVID register (0xE5) before trusting data; reads signed 16-bit X/Y/Z and computes a gravity-independent vibration-energy feature (RMS of acceleration-magnitude fluctuation).
- **Bus Resilience:** Bare-metal I2C driver written with bounded timeouts so a flaky sensor connection flags an error instead of hanging the whole node — one dead sensor can't freeze the others.
- **Live Telemetry:** Streaming raw ADC, RMS features, and per-axis acceleration to the debugger's live memory inspection interface.

---

## Roadmap

1. ✅ Current sensing (SCT-013 + analog front-end) — done, 1.65V DC bias & true I_RMS validated
2. ✅ Acoustic sensing (MAX4466) — done, RMS loudness feature validated
3. ✅ Vibration acquisition (ADXL345 / SPI) — done, DEVID + live axis data validated
4. 🔧 Thermal sensing (MLX90614 / I2C) — wired and coded; resolving a physical connection issue to complete the 4th channel
5. Assemble the 4-parameter feature vector `[current, acoustic, vibration, temperature]` per acquisition cycle
6. Threshold-based multi-sensor anomaly detection for real-time fault flagging
7. Unsupervised ML (Isolation Forest / Autoencoder) for self-learning anomaly thresholds
8. On-device edge inference (quantized model via Edge Impulse / STM32Cube.AI)

*Note: on-motor validation under real load is a planned test; current validation is on-bench with live signals.*

---

## Tools

STM32 Nucleo-F446RE · Embedded C (bare-metal, register-level) · STM32CubeIDE · SPI · I2C · ADC (multi-channel) · analog signal conditioning · Git

---

## Team

Built by a 4-member team — final-year Electronics Engineering, Bangalore Institute of Technology:

- **Parshwa Sangame**
- **Siddharth N**
- **Sumit Sungar**
- **Sugreshwara H**

Maintained by [Parshwa Sangame](https://github.com/YOUR-USERNAME). My focus areas: sensor integration and bring-up (ADXL345/SPI, MLX90614/I2C), the current-sensing analog front end, bare-metal driver debugging, and component sourcing/BOM. Part of a broader interest in affordable industrial hardware for Indian SMEs — see my [sector research](https://github.com/YOUR-USERNAME/india-hardware-opportunities).

---

## Video link
[DRIVE LINK](https://drive.google.com/file/d/1ArUdlh8uGeaoBaL-WMMEUXpvZn7hS0YO/view?usp=drive_link)

# Smart Fridge – Rotten Fruit Detection using ESP32-CAM

## Overview
This project implements a **Convolutional Neural Network (CNN)**–based system to detect whether a fruit is **fresh or rotten**. The model is deployed on an **ESP32-CAM** module and performs **on-device inference**, making it suitable for smart refrigerator applications where spoiled fruits can be identified automatically.

The CNN model is trained using **Edge Impulse**, optimized for embedded deployment, and exported as an Arduino library for execution on the ESP32 platform.

---

## Machine Learning Model

### Dataset
The model was trained using the publicly available dataset:
https://www.kaggle.com/datasets/sriramr/fruits-fresh-and-rotten-for-classification

### Data Preparation
- Image cleaning
- Resizing to **96 × 96**
- Label formatting: `fresh`, `rotten`

### Training Results

| Metric | Value |
|------|------|
| Validation Accuracy | ~96% |
| Test Accuracy | ~94.6% |
| Model Type | CNN |
| Quantization | int8 |

### Confusion Matrix
The confusion matrix indicates balanced classification performance between fresh and rotten fruit images.

---

## Deployment

### Edge Impulse Model
The trained and quantized model was exported as an **Arduino library** using Edge Impulse.

**Location in repository:**
edge-impulse-model/

**Installation in Arduino IDE:**
Sketch → Include Library → Add .ZIP Library…

### ESP32 Inference Code
The ESP32-CAM inference sketch is located at:
esp32/esp32_cam_rotten_fruit.ino
The code can be compiled and uploaded to the hardware

---

## Communication Interfaces

| Interface | Purpose |
|---------|---------|
| UART | Firmware upload and serial debugging |
| I2C (SCCB) | Camera sensor configuration |
| I2S | Image data transfer |

---

## Hardware (Planned)

| Component | Description |
|--------|------------|
| ESP32-CAM (AI Thinker) | Main processing and camera module |
| USB-to-TTL Adapter | Required to program ESP32-CAM |
| Jumper Wires | Board connections |
| Enclosure + Heater (optional) | Operation in low temperatures |

---

## Repository Structure

Smart-Fridge/
├── edge-impulse-model/
├── esp32/
├── docs/
├── .gitignore
├── LICENSE
└── README.md
---

## Future Work
- Collect real fridge images
- Fine-tune the model for real-world conditions
- Add thermal enclosure for operation below 0 °C
- Implement a notification or user interface system

---

## License
This project is distributed under the **MIT License**.

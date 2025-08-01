# 🌤️ SICLIMA - Sistema de Monitoreo Climático con ESP32

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**SICLIMA** es un sistema de monitoreo ambiental que utiliza sensores conectados a un microcontrolador ESP32 para recolectar datos climáticos como temperatura, humedad y calidad del aire. Los datos se envían en tiempo real y se almacenan directamente en **Google Sheets** a través de un webhook personalizado.

Este proyecto fue desarrollado como parte del curso de **Robótica II** en la **Universidad Nacional de Moquegua (UNAM)**.

---

## 📦 Características

- 📡 Recolección en tiempo real de datos climáticos.
- 🧪 Sensores como DHT22 y MQ-135 para temperatura, humedad y gases.
- 🔄 Envío automatizado de datos a Google Sheets.
- 🔐 Comunicación vía protocolo MQTT.
- 📈 Análisis posterior con herramientas como Python/Colab.
- 📊 Visualización sencilla y exportación de datos.

---

## 🧰 Tecnologías utilizadas

- **ESP32**
- **Arduino IDE**
- **Google Apps Script**
- **MQTT**
- **Google Sheets**
- **Python / Jupyter / Google Colab**

---


---

## 🚀 ¿Cómo funciona?

1. El ESP32 recolecta datos de los sensores conectados.
2. Se realiza una solicitud HTTP POST hacia un webhook de Google Apps Script.
3. El script almacena los datos directamente en una hoja de cálculo de Google Sheets.
4. Posteriormente, los datos pueden analizarse o visualizarse desde Python o Google Colab.

---

## 👥 Equipo del Proyecto

| Avatar | Nombre | Rol | GitHub |
|--------|--------|-----|--------|
| <img src="https://github.com/Esquema.png" width="80" height="80" /> | **Ever Quispe** | Sistema Meteorológico (sensores, IoT, Twitter,ThingSpeak ) | [@Esquema](https://github.com/Esquema) |


## 📁 Estructura del repositorio


📌 Project Overview

This project implements an interactive LED control system using ESP32 and FreeRTOS, integrated with the Blynk app. The system allows users to control the brightness of four LEDs using a virtual joystick, while monitoring their intensity through a virtual LCD.

⚙️ Features
🎮 Control LED selection and brightness using a virtual joystick
💡 Adjust LED intensity in steps of 20% (0% → 100%)
📺 Display real-time LED intensity on a virtual LCD
🔁 Cycle through LEDs using joystick (left/right)
🔘 Push button to:
Turn all LEDs ON (100%)
Turn all LEDs OFF (0%)
🔊 Buzzer alerts when:
Exceeding maximum intensity
Going below minimum intensity
⚡ Multitasking handled using FreeRTOS
🧠 System Behavior
Each LED starts at 0% intensity (OFF)
Joystick:
Up/Down → Increase/Decrease intensity
Left/Right → Switch between LEDs
Intensity values are always updated on the virtual LCD
System ensures limits (0%–100%) with buzzer feedback
🛠️ Hardware Components
ESP32
Breadboard
4 LEDs (different colors)
Resistors (220Ω, 4.7KΩ, 10KΩ)
Push button
Buzzer
🚀 Technologies Used
FreeRTOS (for task management)
Blynk IoT platform
Embedded C / ESP32 development

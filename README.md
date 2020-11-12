# DTEK0043-project

Functionality:
- Detect smoke and motion.
- Notify these events.
  - (E-mail/SMS? Maybe difficult and unnecessary.)
- Can control actuators through server.
  - Even send arbitrary audio and switch the LED
- Smoke alarm: play sound, blink LED.
  - Disable able
- Motion alarm: no sound or LED at the node?

Our components:
- Sensor/actuator node
  - Raspberry Pi
  - Sensors (GPIO)
    - Smoke Alarm
    - Infrared Motion Sensor
  - Actuators
    - LED Light (GPIO)
    - Audio Output (3.5 mm Audio)
  - WLAN dongle
- Server
  - PC/Laptop
  - WLAN adapter

Software:
- Sensor/actuator node
  - OS: BSD (or Linux QAQ)?
  - Node Program
    - GPIO, Audio, Network
    - Interacts with the devices and Server
    - An input queue for arbitrary audio to play
- Server
  - Web Server
    - User Interface
    - PHP & JavaScript
    - Switches
    - Upload an audio to play
      - Possible to do real-time microphone stuff with JavaScript, for example?
  - Communication with Sensor Node -program

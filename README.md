DCC-02 Energy Monitor
=====================

This is the firmware for an electrical energy monitoring controller. It consists of a custom board with a ESP8266 module as well as an Atmega 326 chip. The Atmega runs the [Open Energy Monitor](https://openenergymonitor.org/emon/) library and takes measurements from two current transformers and an AC voltage reference. It computes the results and sends messages to the ESP chip which relays that to InfluxDB.

You can read more about this project at http://tech.dominioncider.com/post/telemetry/.

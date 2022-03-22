# low-cost-frequency-relay
## Low Cost Frequency Relay (LCFR) implemented on an Altera DE2-115 FPGA. Used FreeRTOS for real time operating system.

Designed to read voltage from a Frequency Analyzer Unit (FUA) every 20ms and determine whether the loads connected need to be shedded.

Relay will begin to shed loads on either:
- Frequency below threshold. 
- Rate of change of frequency is too large.

This protects the electrical grid and household electrical system and electronics from being destroyed by reducing grid demand (load) at the household level. If this low power unit is implemented en masse.

This project is a university assignment for learning about FreeRTOS and Embedded System Design.

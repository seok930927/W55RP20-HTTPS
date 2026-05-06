# HTTPS Development Repository
<img width="446" height="408" alt="image" src="https://github.com/user-attachments/assets/141f1c5b-37d5-401d-9260-aef4a5d3e0ac" />

생성 PW: wiznet_w55rp20

<img width="841" height="423" alt="image" src="https://github.com/user-attachments/assets/d0c17db3-1d8d-4aee-b7e7-0e5264d43f57" />


## Warning

This project is currently under development.

Normal operation is not guaranteed, and some functions may be unstable or incomplete.

## Overview

This repository is dedicated only to HTTPS feature development and verification.

It does not describe other product plans, non-HTTPS features, or external project requirements.

The current firmware is intended to operate as an embedded HTTPS server on the target device.

## How It Works

The system works as follows:

1. The device boots and initializes the network stack.
2. The device gets an IP address from DHCP.
3. The HTTPS server opens port `443`.
4. A client connects to the device over HTTPS.
5. The device performs a TLS handshake.
6. After the handshake succeeds, the device returns an HTTP response over TLS.
7. The browser receives the HTML page from the embedded server.

At the moment, the HTTPS page is served directly by the firmware and is intended for HTTPS validation and page delivery testing.

## Certificate Notes

This project uses a development certificate for HTTPS testing.

- A browser security warning may appear when you access the device.
- For development and internal testing, that warning can be ignored.
- If the warning is inconvenient, a certificate update or certificate patch flow can be applied later.
- That later update flow is the recommended way to reduce browser warnings during internal use.

This repository is focused on HTTPS functionality itself, not on final public certificate deployment.

## System Architecture

The current HTTPS flow is organized like this:

- Network interface: W5500 Ethernet controller
- MCU / application firmware: device main application
- TLS layer: `mbedTLS`
- HTTPS server task: firmware task that listens on port `443`
- Socket handling: multiple HTTPS sockets are used to tolerate browser-side parallel connections
- Page delivery: static HTML content is returned by the embedded firmware

High-level path:

`Browser -> TCP 443 -> TLS handshake -> HTTPS server task -> HTML response`

Main implementation areas in this repository:

- `port/app/platform_handler/src/httpHandler.c`
- `port/app/mbedtls/src/SSLInterface.c`
- `port/app/html_file/Web_page.h`

## Future Plans

The current direction for this repository is:

- improve HTTPS stability
- improve certificate handling and update flow
- reduce browser warning friction for internal users
- refine session handling and response delivery
- continue HTTPS-focused validation and debugging

## Scope

This README intentionally documents only the HTTPS-related part of the project.

Other system plans, non-HTTPS features, and external requirements are out of scope for this repository documentation.

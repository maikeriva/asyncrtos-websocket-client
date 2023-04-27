# AsyncRTOS websocket client

A websocket client implementation built on top of AsyncRTOS and ESP-IDF.

## Why?

While working with ESP-IDF I grew unsatisfied of the official websocket client, as I found it with a complicated code base and not thread-safe. I decided to reimplement it on top of AsyncRTOS so that it:

- Supports AsyncRTOS asyncronous message passing
- Has idempotent behavior
- Is thread-safe
- Fails gracefully
- Has keep-alive functionality (automatically reconnects in case of errors)
- Performs multiple connection attepts before giving up

## How do I use this?

Check out the examples folder. 

## How do I contribute?

Feel free to contribute with code or a coffee :)

<a href="https://www.buymeacoffee.com/micriv" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

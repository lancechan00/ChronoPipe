# Platform ports

ChronoPipe core contains no operating-system, socket, credential, or device-driver
code. A platform integration supplies:

- acquisition callbacks that push complete `cp_frame_t` rows;
- caller-owned arenas and recorder/store buffers;
- `cp_flash_io_t` read, program, and erase functions aligned to the medium;
- a `cp_batch_transport_t` sender plus an application path for acknowledgements;
- optional live byte output using COBS framing;
- a monotonic microsecond clock and, if needed, separate wall-clock mapping events.

Keep ISR work bounded. An ISR should timestamp and enqueue or fill a preallocated slot;
graph execution, record sealing, erase, and network work belong in application tasks
whose priorities are chosen by the product.

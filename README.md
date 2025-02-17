Very simple way to send a gcode file on the command line to a printer/cnc
machine that uses the common `ok` and `error` feedback lines after each
block as 'flow control'.

It sends gcode line-by-line, removing CRLF line-endings and just sends LF
(otherwise Grbl gets confused). Waits for the acknowledging `ok` before sending
the next line; provides a simple continue/stop user interaction when it
encounters an `error`-response.

The tool removes `;`-based end-of-line comments and empty lines.

No claim to be complete, just useful for my local Marlin-based 3D printers and
Grbl-based CNC as well as various machines I run with [BeagleG].

```
usage:
./gcode-cli <gcode-file> [connection-string]

Connection string is either a path to a tty device or host:port
 * Serial connection
   A path to the device name with an optional bit-rate
   separated with a comma.
   Examples of valid connection strings:
        /dev/ttyACM0
        /dev/ttyACM0,b115200
  notice the 'b' prefix for the bit-rate.
  Available bit-rates are one of [b9600, b19200, b38400, b57600,  b115200, b230400, b460800]

 * TCP connection
   For devices that receive gcode via tcp (e.g. http://beagleg.org/)
   you specify the connection string as host:port. Example:
        localhost:4444

Examples:
./gcode-cli file.gcode /dev/ttyACM0,b115200
./gcode-cli file.gcode localhost:4444
```

[BeagleG]: http://beagleg.org/

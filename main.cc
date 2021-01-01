/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * (c) h.zeller@acm.org. Free Software. GNU Public License v3.0 and above
 */

#include <stdio.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

#include "machine-connection.h"

static bool reliable_write(int fd, const char *buffer, int len) {
    while (len) {
        int w = write(fd, buffer, len);
        if (w < 0) return false;
        len -= w;
        buffer += w;
    }
    return true;
}

static int usage(const char *progname) {
    fprintf(stderr, "usage:\n"
            "%s <gcode-file> [connection-string]\n"
            "\nConnection string is either a path to a tty device or "
            "host:port\n"
            " * Serial connection\n"
            "   A path to the device name with an optional bit-rate\n"
            "   separated with a comma.\n"
            "   Examples of valid connection strings:\n"
            "   \t/dev/ttyACM0\n"
            "   \t/dev/ttyACM0,b115200\n"
            "  notice the 'b' prefix for the bit-rate.\n"
            "  Available bit-rates are one of [b9600, b19200, b38400, b57600, "
            "b115200, b230400, b460800]\n\n"
            " * TCP connection\n"
            "   For devices that receive gcode via tcp "
            "(e.g. http://beagleg.org/)\n"
            "   you specify the connection string as host:port. Example:\n"
            "   \tlocalhost:4444\n",
            progname);

    fprintf(stderr, "\nExamples:\n"
            "%s file.gcode /dev/ttyACM0,b115200\n"
            "%s file.gcode localhost:4444\n",
            progname, progname);
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        return usage(argv[0]);

    const char *const filename = argv[1];
    std::ifstream input(filename);

    // quick peek to get length of input
    input.seekg(0, input.end);
    // add 1 to prevent div/0 possibility.  You won't notice, honest!
    // yes, integer math.  No need to pull in floating point library.
    const int input_length = ( input.tellg() / 100 ) + 1;
    input.seekg(0, input.beg);

    // make number formatting work (thousands separators)
    setlocale(LC_NUMERIC,"");

    const char *connect_str = (argc >= 3) ? argv[2] : "/dev/ttyUSB0,b115200";
    const int machine_fd = OpenMachineConnection(connect_str);
    if (machine_fd < 0) {
        fprintf(stderr, "Failed to connect to machine %s\n", connect_str);
        return 1;
    }

    const int DISCARDWAITMS = 1000;
    DiscardPendingInput(machine_fd, DISCARDWAITMS);

    std::string line;
    int line_count = 0;
    int lines_sent = 0;

    while (!input.eof()) {
        getline(input, line);
        line_count++;

        // Strip any comments that start with ; to the end of the line
        // (lornix) This can cause issues if you echo a message (M118)
        // containing a semi-colon!
        const size_t comment_start = line.find_first_of(';');
        if (comment_start != std::string::npos) {
            line.resize(comment_start);
        }

        // Now, strip away any trailing spaces
        while (!line.empty() && isspace(line[line.size()-1])) {
            line.resize(line.length()-1);
        }

        // If the line is empty, then skip it
        if (line.empty()) {
            continue;
        }

        int percent_complete = input.tellg() / input_length;
        printf("(%02d%%) %'8d | %s", percent_complete, line_count, line.c_str());
        fflush(stdout);
        line.append("\n");  // GRBL wants only newline, not CRLF

        if (!reliable_write(machine_fd, line.data(), line.length())) {
            fprintf(stderr, "Send error!\n");
            return EXIT_FAILURE;
        }

        printf("\n");
        fflush(stdout);

        lines_sent++;

        // The OK 'flow control' used by all these serial machine controls
        if (!WaitForOkAck(machine_fd)) {
            fprintf(stderr, "[ Error Response. CTRL-C to stop ]\n");
            getchar();
        }
    }

    close(machine_fd);

    printf("Sent %d non-empty lines out of %d total\n", lines_sent, line_count);

    return EXIT_SUCCESS;
}

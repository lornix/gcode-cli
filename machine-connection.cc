/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * (c) h.zeller@acm.org. Free Software. GNU Public License v3.0 and above
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

static bool SetTTYParams(int fd, const char *params) {
    speed_t speed = B115200;
    if (params[0] == 'b' || params[0] == 'B')
        params = params + 1;
    if (*params) {
        int speed_number = atoi(params);
        switch (speed_number) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        default:
            fprintf(stderr, "Invalid speed '%s'; valid speeds are "
                    "[9600, 19200, 38400, 57600, 115200, 230400, 460800]\n",
                    params);
            return false;
            break;
        }
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) < 0) {
        perror("tcgetattr() failed. Is this a tty ?");
        return false;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD);  // no modem controls
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;               // 8
    tty.c_cflag &= ~PARENB;           // N
    tty.c_cflag &= ~CSTOPB;           // 1
    tty.c_cflag &= ~CRTSCTS;          // No hardware flow-control

    // terminal magic. Non-canonical mode
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Error from tcsetattr: %s\n", strerror(errno));
        return false;
    }
    return true;
}

// Wait for input to become ready for read or timeout reached.
// If the file-descriptor becomes readable, returns number of milli-seconds
// left.
// Returns 0 on timeout (i.e. no millis left and nothing to be read).
// Returns -1 on error.
static int AwaitReadReady(int fd, int timeout_millis) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout_millis * 1000;

    FD_SET(fd, &read_fds);
    int s = select(fd + 1, &read_fds, NULL, NULL, &tv);
    if (s < 0)
        return -1;
    return tv.tv_usec / 1000;
}

static int ReadLine(int fd, char *result, int len, bool do_echo) {
    int bytes_read = 0;
    // reserve space for ending \0
    --len;
    // Anything <= 0 is an error
    while (read(fd, result, 1) > 0) {
        if (do_echo) {
            write(STDOUT_FILENO, result, 1);  // echo back.
        }
        if ((*result == '\n') || (*result == '\r') || (bytes_read >= len)) {
            *result=0;
            return bytes_read;
        }
        result++;
        bytes_read++;
    }
    return -1;
}

/*
 *
 *  Public interface functions
 *
 */

int OpenTCPSocket(const char *host) {
    char *host_copy = NULL;
    const char *port = "8888";
    const char *colon_pos;
    if ((colon_pos = strchr(host, ':')) != NULL) {
        port = colon_pos + 1;
        host_copy = strdup(host);
        host_copy[colon_pos - host] = '\0';
        host = host_copy;
    }
    struct addrinfo addr_hints = {};
    addr_hints.ai_family = AF_INET;
    addr_hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *addr_result = NULL;
    int rc;
    if ((rc = getaddrinfo(host, port, &addr_hints, &addr_result)) != 0) {
        fprintf(stderr, "Resolving '%s' (port %s): %s\n", host, port,
                gai_strerror(rc));
        free(host_copy);
        return -1;
    }
    free(host_copy);
    if (addr_result == NULL)
        return -1;
    int fd = socket(addr_result->ai_family,
                    addr_result->ai_socktype,
                    addr_result->ai_protocol);
    if (fd >= 0 &&
        connect(fd, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        perror("TCP connect()");
        close(fd);
        fd = -1;
    }

    freeaddrinfo(addr_result);
    return fd;
}

static int OpenTTY(const char *descriptor) {
    const char *comma = strchrnul(descriptor, ',');
    const std::string path(descriptor, comma);
    int fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        return -1;
    }
    if (!SetTTYParams(fd, *comma ? comma+1 : "")) {
        return -1;
    }
    return fd;
}

int OpenMachineConnection(const char *descriptor) {
    if (descriptor == nullptr) return -1;
    int fd = OpenTTY(descriptor);
    if (fd >= 0) return fd;
    return OpenTCPSocket(descriptor);
}

int DiscardPendingInput(int fd, int timeout_ms) {
    if (fd < 0) return 0;
    int total_bytes = 0;
    char buf[128];
    while (AwaitReadReady(fd, timeout_ms) > 0) {
        // remember to leave room for \0 terminator
        int r = read(fd, buf, sizeof(buf)-1);
        if (r < 0) {
            perror("reading trouble");
            return -1;
        }
        total_bytes += r;
        if (r > 0) {
            // echo discarded output
            buf[r]=0;
            fprintf(stderr,"DISCARD: %s\n", buf);
        }
    }
    return total_bytes;
}

// 'ok' begins a single line, maybe followed by something.
bool WaitForOkAck(int fd) {
    char buffer[512] = {};
    for (;;) {
        int got_chars = ReadLine(fd, buffer, sizeof(buffer), false);
        if (got_chars < 0) {
            fprintf(stderr, "\n--> RESPONSE ERROR <--\n");
            return false;
        }

        if (got_chars >= 2 && strncasecmp(buffer, "ok", 2) == 0) {
            return true;
        }

        // look for definite error returns, everything else is ignorable
        if ( ((got_chars >= 2) && (strncasecmp(buffer, "rs", 2) == 0))
            || ((got_chars >= 2) && (strncasecmp(buffer, "!!", 2) == 0))
            || ((got_chars >= 5) && (strncasecmp(buffer, "error", 5) == 0))
            || ((got_chars >= 5) && (strncasecmp(buffer, "fatal", 5) == 0))
            || ((got_chars >= 6) && (strncasecmp(buffer, "resend", 6) == 0)) ) {
            fprintf(stderr, "%s\n", buffer);
            return false;
        }
    }
}

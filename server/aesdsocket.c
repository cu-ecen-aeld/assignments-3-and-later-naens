#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/file.h>

#define THE_FILE "/var/tmp/aesdsocketdata"
#define SYS_LOCK_FILE "/var/run/aesdsocket-app.lock"
#define USR_LOCK_FILE "/tmp/aesdsocket-app.lock"

int sockfd;
int lockfd;

bool want_to_exit;
bool can_exit;
struct addrinfo *res;

void
terminate() {
    /*
     * Gracefully exit when SIGINT or SIGTERM is received, completing
     * any open connectoin operations, closing any open sockets,
     * and deleting the file /var/tmp/aesdsocketdata.
    */
    close(sockfd);
    close(lockfd);

    syslog(LOG_DEBUG, "removing the file %s\n", THE_FILE);
    if (unlink(THE_FILE) == -1 && errno != ENOENT) {
        perror("[-]");
        exit(33);
    }

    /* close log and exit */
    syslog(LOG_DEBUG, "Close log and exit\n");
    freeaddrinfo(res);
    closelog();
    exit(0);

}

static void handler(int sig) {
    /*
     * Log message to the syslog "Caught signal, exiting"
     * when SIGINT or SIGTERM is received.
     */
    syslog(LOG_ERR, "Caught signal, exiting\n");

    if (can_exit) {
        terminate();
    }
    want_to_exit = true;
}

int
get_lock(lock_file_path)
    char *lock_file_path;
{
    pid_t pid = getpid();
    int fd = open(lock_file_path, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "could not open lock file %s\n", lock_file_path);
        exit(1);
    }
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        return -1;
    }
    char pidstr[8];
    sprintf(pidstr, "%d\n", pid);
    if (write(fd, pidstr, strlen(pidstr)) == -1) {
        syslog(LOG_ERR, "could not write into lock file %s\n", lock_file_path);
        exit(1);
    }
    return fd;
}

void
stop(lock_file_path)
    char *lock_file_path;
{
    int fd = open(lock_file_path, O_RDONLY, 0);
    if (fd == -1) {
        if (errno == ENOENT) {
            return;
        }
        syslog(LOG_ERR, "error while opening lock file %s\n", lock_file_path);
        exit(1);
    }
    char pidstr[8];
    if (read(fd, pidstr, sizeof pidstr) == -1) {
        syslog(LOG_ERR, "error reading lock file %s\n", lock_file_path);
        exit(1);
    }
    pid_t pid = atoi(pidstr);
    if (kill(pid, SIGTERM) == -1) {
        if (errno == EINVAL || errno == ESRCH) {
            return;
        }
        syslog(LOG_ERR, "kill error\n");
        exit(1);
    }
    close(fd);
    unlink(lock_file_path);
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct addrinfo hints;

    want_to_exit = false;
    can_exit = true;

    openlog(NULL, LOG_PERROR, LOG_USER);
    syslog(LOG_DEBUG, "the file is %s\n", THE_FILE);

    char *lock_file_path;
    if (geteuid() == 0) {
        lock_file_path = SYS_LOCK_FILE;
    } else {
        lock_file_path = USR_LOCK_FILE;
    }

    if (argc == 2 && strcmp(argv[1], "-q") == 0) {
        stop(lock_file_path);
        exit(0);
    }

    /*
     * Open a stream socket bound to port 9000,
     * failing and returning -1 if any of the socket
     * connection steps fail.
     */
    syslog(LOG_DEBUG, "running socket\n");
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("[1:socket]");
        return 1;
    }
    const int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("[1:reuse]");
        return 1;
    }

    syslog(LOG_DEBUG, "rinning getaddrinfo\n");
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_addrlen = 0;
    hints.ai_addr = NULL;
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    int errcode;
    if ((errcode = getaddrinfo(NULL, "9000", &hints, &res)) != 0) {
        fprintf(stderr, "[2]: Error in getaddrinfo: %s\n", gai_strerror(errcode));
        return 2;
    }
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    char ip[0x100];
    inet_ntop (AF_INET, &sin->sin_addr, ip, sizeof (ip));
    syslog(LOG_DEBUG, "host: %s:%d\n", ip, htons(sin->sin_port));

    syslog(LOG_DEBUG, "running bind\n");
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("[3]");
        return 3;
    }

    /* check if running in daemon mode */
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        if (fork() != 0) {
            exit(0);
        }
        syslog(LOG_DEBUG, "running in daemon mode\n");
        lockfd = get_lock(lock_file_path);
        if (lockfd == -1) {
            syslog(LOG_ERR, "could not obtain lock\n");
            exit(1);
        }
    } else {
        syslog(LOG_DEBUG, "running in non daemon mode\n");
    }

    /* Listen for a connection */
    syslog(LOG_DEBUG, "running listen\n");
    if (listen(sockfd, 100) == -1) {
        perror("[4]");
        return 4;
    }

    if (signal(SIGINT, handler) == SIG_ERR) {
        perror("[8]");
        return 8;
    }
    if (signal(SIGTERM, handler) == SIG_ERR) {
        perror("[9]");
        return 9;
    }

    while (!want_to_exit) {
        /* Accept a connection */
        syslog(LOG_DEBUG, "running accept...");
        struct sockaddr addr;
        socklen_t addrlen = sizeof addr;
        int accfd;
        if ((accfd = accept(sockfd, &addr, &addrlen)) == -1) {
            perror("[5]");
            return 5;
        }
        if (addr.sa_family == AF_INET) {
            char ip[0x100];
            inet_ntop (AF_INET, &sin->sin_addr, ip, sizeof (ip));
            syslog(LOG_DEBUG, "accept ok: connection=%s:%d\n",
                ip, htons(sin->sin_port));
        } else {
            syslog(LOG_DEBUG, "accept ok: sa_family=%d\n", addr.sa_family);
        }
        can_exit = false;

        /* Receive data and append to file /var/tmp/aesdsocketdata */
        syslog(LOG_DEBUG, "receive...");
        char buf[0x100];
        int outfd;
        outfd = open(THE_FILE,
            O_WRONLY|O_CREAT|O_APPEND,
            S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);
        ssize_t recsz;
        ssize_t rec_tot = 0;
        while ((recsz = recv(accfd, buf, sizeof buf, 0)) == sizeof buf) {
            write(outfd, buf, sizeof buf);
            rec_tot += sizeof buf;
        }
        if (recsz == -1 && errno != EAGAIN) {
            perror("[6] error");
            return 6;
        }
        if (recsz > 0) {
            write(outfd, buf, recsz);
            rec_tot += recsz;
        }
        close(outfd);
        syslog(LOG_DEBUG, "received: %ld bytes\n", rec_tot);

        /*
         * Return the full content of /var/tmp/aesdsocketdata to the client
         * as soon as the received data packet completes.
         */
        syslog(LOG_DEBUG, "sending bytes...");
        addrlen = sizeof addr;
        int infd = open(THE_FILE, O_RDONLY);
        ssize_t count;
        ssize_t count_tot = 0;
        while ((count = read(infd, buf, sizeof buf)) == sizeof buf) {
            send(accfd, buf, sizeof buf, MSG_DONTWAIT);
            count_tot += sizeof buf;
        }
        if (count > 0) {
            send(accfd, buf, count, MSG_DONTWAIT);
            count_tot += count;
        }
        if (count == -1) {
            perror("[7]");
            return 7;
        }
        syslog(LOG_DEBUG, "sent: %ld bytes\n", count_tot);
        close(infd);
        can_exit = true;
        
        /*
         * Log the message to the syslog "Closed connection from xxx"
         * where xxx is the IP address of the connected client
         */
        inet_ntop (AF_INET, &sin->sin_addr, ip, sizeof (ip));
        syslog(LOG_ERR, "Closed connection from %s:%d\n", ip, htons(sin->sin_port));
        close(accfd);
    }
    terminate();
    return -1;
}

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
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#define THE_FILE "/var/tmp/aesdsocketdata"
#define SYS_LOCK_FILE "/var/run/aesdsocket-app.lock"
#define USR_LOCK_FILE "/tmp/aesdsocket-app.lock"

#define TIME_DELAY 10

int sockfd;
int lockfd;

bool want_to_exit;
bool can_exit;
bool exiting;

struct thread_info *threads;
pthread_t time_thread;

pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
struct timespec timer_abstime;


struct addrinfo *res;
struct thread_info {
    pthread_t id;
    int fd;
    struct sockaddr_in *sin;
    bool has_finished;
    bool deleted;
    struct thread_info *next;
};

void
terminate()
{
    if (exiting) {
        return;
    }
    exiting = true;
    close(sockfd);
    close(lockfd);

    syslog(LOG_DEBUG, "removing the file %s\n", THE_FILE);
    if (unlink(THE_FILE) == -1 && errno != ENOENT) {
        perror("[-]");
        exit(33);
    }

    /* free thread structures */
    struct thread_info *t = threads;
    struct thread_info *p = t;
    while (t != NULL) {
        if (!t -> deleted) {
            pthread_join(t->id, NULL);
        }
        t = t->next;
        free(p);
        p = t;
    }

    /* wait for timer to end */
    syslog(LOG_DEBUG, "Waiting for timer to end");
    pthread_mutex_lock(&timer_mutex);
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);
    int r;
    if ((r = pthread_join(time_thread, NULL)) != 0) {
        syslog(LOG_ERR, "could not join timer");
    }
    syslog(LOG_DEBUG, "timer: joined");
    syslog(LOG_DEBUG, "Timer closed");

    /* close log and exit */
    pid_t pid = getpid();
    syslog(LOG_DEBUG, "Close log and exit [pid=%d]\n", pid);
    freeaddrinfo(res);
    closelog();
    exit(0);
}

static void handler(int sig) {

    want_to_exit = true;
    if (exiting) {
        return;
    }
    syslog(LOG_ERR, "Caught signal, exiting\n");
    if (can_exit) {
        terminate();
    }
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

pthread_mutex_t mutex;
void*
thread_routine(info)
    struct thread_info *info;
{
    int recv_flags = 0;
//    int recv_flags = MSG_DONTWAIT;
    int send_flags = 0;
//    int send_flags = MSG_DONTWAIT;

    syslog(LOG_DEBUG, "thread_routine[%ld]: begin fd=%d", info->id, info->fd);
    /* get mutex */
    pthread_mutex_lock(&mutex);

    /* Receive data and append to file /var/tmp/aesdsocketdata */
    syslog(LOG_DEBUG, "thread_routine[%ld]: receive...", info->id);
    char buf[0x100];
    int outfd;
    outfd = open(THE_FILE,
        O_WRONLY|O_CREAT|O_APPEND,
        S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);
    ssize_t recsz;
    ssize_t rec_tot = 0;
    while ((recsz = recv(info->fd, buf, sizeof buf, recv_flags)) == sizeof buf) {
        write(outfd, buf, sizeof buf);
        rec_tot += sizeof buf;
    }
    if (recsz == -1 && errno != EAGAIN) {
        syslog(LOG_ERR, "thread_routine[%ld]: error fd=%d", info->id, info->fd);
        perror("[6] error");
        exit(6);
    }
    if (recsz > 0) {
        write(outfd, buf, recsz);
        rec_tot += recsz;
    }
    close(outfd);
    syslog(LOG_DEBUG, "thread_routine[%ld]: received: %ld bytes\n", info->id, rec_tot);

    /*
     * Return the full content of /var/tmp/aesdsocketdata to the client
     * as soon as the received data packet completes.
     */
    syslog(LOG_DEBUG, "thread_routine[%ld]: sending bytes...", info->id);
    int infd = open(THE_FILE, O_RDONLY);
    ssize_t count;
    ssize_t count_tot = 0;
    while ((count = read(infd, buf, sizeof buf)) == sizeof buf) {
        send(info->fd, buf, sizeof buf, send_flags);
        count_tot += sizeof buf;
    }
    if (count > 0) {
        send(info->fd, buf, count, send_flags);
        count_tot += count;
    }
    if (count == -1) {
        perror("[7]");
        exit(7);
    }
    syslog(LOG_DEBUG, "thread_routine[%ld]: sent: %ld bytes", info->id, count_tot);
    close(infd);


    info->has_finished = true;
    syslog(LOG_DEBUG, "thread_routine[%ld]: end, has_finished=true, fd=%d", info->id, info->fd);

    /* release mutex */
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int
socket_init()
{
    struct addrinfo hints;

    /*
     * Open a stream socket bound to port 9000,
     * failing and returning -1 if any of the socket
     * connection steps fail.
     */
    syslog(LOG_DEBUG, "running socket\n");
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("[1:socket]");
        exit(1);
    }
    const int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("[1:reuse]");
        exit(1);
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
        exit(2);
    }
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    char ip[0x100];
    inet_ntop (AF_INET, &sin->sin_addr, ip, sizeof (ip));
    syslog(LOG_DEBUG, "host: %s:%d\n", ip, htons(sin->sin_port));

    syslog(LOG_DEBUG, "running bind\n");
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("[3]");
        exit(3);
    }

    /* Listen for a connection */
    syslog(LOG_DEBUG, "running listen\n");
    if (listen(sockfd, 100) == -1) {
        perror("[4]");
        exit(4);
    }

    return sockfd;
}

void*
time_routine()
{
    while (!want_to_exit) {
        time_t t;
        struct tm *tmp;
        t = time(NULL);
        tmp = localtime(&t);
        if (tmp == NULL) {
            perror("localtime");
            exit(302);
        }
        char *RFC_2822 = "%a, %d %b %Y %T %z";
        char timestr[0x40];
        if (strftime(timestr, sizeof timestr, RFC_2822, tmp) == 0) {
            syslog(LOG_ERR, "stdftime error\n");
            exit(303);
        }

        /* writing to file */
        syslog(LOG_DEBUG, "writing time: %s", timestr);
        int fd = open(THE_FILE, O_WRONLY|O_CREAT|O_APPEND,
            S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);
        char out_buf[0x80];
        sprintf(out_buf, "timestamp:%s\n", timestr);
        pthread_mutex_lock(&mutex);
        write(fd, out_buf, strlen(out_buf));
        pthread_mutex_unlock(&mutex);

        /* sleeping */
        pthread_mutex_lock(&timer_mutex);
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timespec timer_abstime = {now.tv_sec+TIME_DELAY, now.tv_usec};
        pthread_cond_timedwait(&timer_cond, &timer_mutex, &timer_abstime);
        pthread_mutex_unlock(&timer_mutex);
    }
    return NULL;
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    want_to_exit = false;
    can_exit = true;
    exiting = false;

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

    /* initialize the socket */
    sockfd = socket_init();

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

    /* truncate the file */
    int the_file_fd = open(THE_FILE, O_WRONLY|O_CREAT|O_APPEND,
        S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);
    if (the_file_fd == -1) {
        syslog(LOG_ERR, "could not open the file");
        exit(-1);
    }
    if (ftruncate(the_file_fd, 0) == -1) {
        syslog(LOG_ERR, "could not truncate the file");
        exit(-1);
    }
    close(the_file_fd);

    /* start the timer */
    if (pthread_create(&time_thread, NULL, time_routine, NULL) != 0) {
        syslog(LOG_ERR, "timer thread creation error");
        exit(301);
    }

    /* initialize signal handlers */
    if (signal(SIGINT, handler) == SIG_ERR) {
        perror("[8]");
        return 8;
    }
    if (signal(SIGTERM, handler) == SIG_ERR) {
        perror("[9]");
        return 9;
    }

    /* initialize mutex */
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        perror("[x]");
        return 101;
    }

    int connections = 0;
    threads = NULL;
    while (!want_to_exit || connections > 0) {
        if (!want_to_exit) {
            /* Accept a connection */
            syslog(LOG_DEBUG, "running accept...");
            struct sockaddr addr;
            socklen_t addrlen = sizeof addr;
            int accfd;
            if ((accfd = accept(sockfd, &addr, &addrlen)) == -1) {
                perror("[5]");
                return 5;
            }
            can_exit = false;
            struct sockaddr_in *sin = (struct sockaddr_in *)(&addr);
            char ip[0x100];
            if (addr.sa_family == AF_INET) {
                inet_ntop (AF_INET, &sin->sin_addr, ip, sizeof ip);
                syslog(LOG_DEBUG, "accept ok: connection=%s:%d, fd=%d\n",
                    ip, htons(sin->sin_port), accfd);
            } else {
                syslog(LOG_DEBUG, "accept ok: sa_family=%d\n", addr.sa_family);
            }

            /* Start a new thread */
            struct thread_info *new_thread = malloc(sizeof (struct thread_info));
            new_thread->fd = accfd;
            new_thread->sin = sin;
            new_thread->has_finished = false;
            new_thread->deleted = false;
            new_thread->next = threads;
            if (pthread_create(&new_thread->id, NULL, thread_routine, new_thread) != 0) {
                syslog(LOG_ERR, "thread creation error");
                return -1;
            }
            threads = new_thread;
            syslog(LOG_DEBUG, "creating new thread id=%ld", new_thread->id);
            connections += 1;
        }

        /* look for an unfinished thread */
        struct thread_info *t = threads;
        while (t != NULL && (!t->has_finished || t->deleted)) {
            t = t->next;
        }
        if (t != NULL) {
            t -> deleted = true;
            syslog(LOG_DEBUG, "found finished finished=%d, fd=%d, thread=%ld", t->has_finished, t->fd, t->id);
            /*
             * Log the message to the syslog "Closed connection from xxx"
             * where xxx is the IP address of the connected client
             */
            char ip[0x100];
            inet_ntop (AF_INET, &t->sin->sin_addr, ip, sizeof ip);
            syslog(LOG_ERR, "Closed connection from %s:%d\n", ip, htons(t->sin->sin_port));
            close(t->fd);
            int r;
            syslog(LOG_DEBUG, "found thread finished: %ld", t->id);
            if ((r = pthread_join(t->id, NULL)) != 0) {
                syslog(LOG_ERR, "thread join error");
                switch (r) {
                    case EDEADLK:
                        syslog(LOG_ERR, "error: EDEADLK");
                        break;
                    case EINVAL:
                        syslog(LOG_ERR, "error: EINVAL");
                        break;
                    case ESRCH:
                        syslog(LOG_ERR, "error: ESRCH");
                        break;
                }
                return 102;
            }
            
            connections -= 1;
        }
        can_exit = true;
    }
}

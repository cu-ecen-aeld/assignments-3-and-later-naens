#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <string.h>
#include <syslog.h>

int
main(int argc, char **argv)
{
    if (argc != 3) {
        return 1;
    }
    char *file_name = argv[1];
    char *text_string = argv[2];
    openlog(NULL, LOG_PERROR, LOG_USER);

    syslog(LOG_DEBUG, "file_name=\"%s\", text_string=\"%s\"\n", file_name, text_string);

    int fd = creat(file_name, 0664);

    if (fd == -1) {
        syslog(LOG_ERR, "file open error\n");
        return -1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", text_string, file_name);
    int sz = strlen(text_string);
    int res = write(fd, text_string, sz);
    if (res == -1) {
        syslog(LOG_ERR, "file write error\n");
        goto err;
    }

    if (res != sz) {
        syslog(LOG_ERR, "file write size different: written %d instead of %d\n",
            res, sz);
        goto err;
    }

    if (close(fd) == -1) {
        syslog(LOG_ERR, "file close error\n");
        return -1;
    }
    closelog();
    return 0;

err:
    close(fd);
    closelog();
    return -1;
}

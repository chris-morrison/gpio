
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    int ret;
    int fd = open("/sys/class/gpio_inputs/export", O_WRONLY);
    char testString[] = "17";

    if (fd < 0)
    {
        perror("Failed to open the device\n");
        return errno;
    }

    ret = write(fd, testString, strlen(testString));
    if (ret < 0)
    {
        perror("Failed to write to the device\n");
        return errno;
    }


    {
        int gpiofd = open("/sys/class/gpio_inputs/gpio17/diffTime", O_RDONLY);
        char buf[512] = {0};
        int i;

        while (i < 10)
        {
            ret = read(gpiofd, buf, sizeof(buf));
            if (ret < 0)
            {
                perror("Failed to read from attribute\n");
                return errno;
            }
            else
            {
                printf("attribute returned: %s", buf);
            }

            i++;
        }
        
    }

    printf("End of the program reached\n");

    return 0;
}
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>

typedef struct point { 
    int x; 
    int y; 
    int id; 
}point;

int main (int argc, char **argv)
{
	char name[256] = "Unknown";
	int fd, rd, i;
	struct input_event ev[64];
	struct point tmpP;

	if (argc < 2) {
		printf("Usage: evtest /dev/input/eventX\n");
		return 1;
	}

	if ((fd = open(argv[argc - 1], O_RDONLY)) < 0) {
		perror("Getevent");
		return 1;
	}
	ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	printf("Input device name: \"%s\"\n", name);

	printf("Reading input from device ... (interrupt to exit)\n");
	while (1) { // Main loop
		rd = read(fd, ev, sizeof(struct input_event) * 64);
		if (rd < (int) sizeof(struct input_event)) {
			perror("\nGetevent: error reading");
			return 1;
		}
		for (i = 0; i < rd / sizeof(struct input_event); i++) {
			if (ev[i].type == EV_ABS){
					if (ev[i].code == ABS_MT_TRACKING_ID) tmpP.id = ev[i].value; 
					if (ev[i].code == ABS_MT_POSITION_X) tmpP.x  = ev[i].value;
					if (ev[i].code == ABS_MT_POSITION_Y) {
						tmpP.y  = ev[i].value;	
                    				printf("Touch ID:%d   X:%d   Y:%d\n",tmpP.id,tmpP.x,tmpP.y);
					}
			}
        }
	}
}


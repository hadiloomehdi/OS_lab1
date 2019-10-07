#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char buffer[8];

void
cpt (int fds, int fdd, _Bool isBufFile) {
	int readBytes;
	while ((readBytes = read(fds, buffer, sizeof(buffer) )) > 0) {
		if (write(fdd, buffer, readBytes) != readBytes) {
			printf(2, "an error has occured\n");
			exit();
		}
		if (isBufFile == 0 && buffer[readBytes - 1] == '\n')
			break;
	}
	exit();
}


int main (int argc, char *argv[]) {

	if (argc <= 1) {
		printf(2, "command has no argument\n");
		exit();
	}
	else if (argc == 2) {
		// unlink(argv[1]);
		int fd = open(argv[1], O_CREATE | O_WRONLY);
		cpt(0, fd, 0);
		close(fd);
	}
	else if (argc == 3) {
		int fds = open(argv[1], O_RDONLY);
		if (fds <= 0) {
			printf(2, "%s not found\n", argv[1]);
			exit();
		}
		unlink(argv[2]);
		int fdd = open(argv[2], O_CREATE | O_WRONLY);
		cpt (fds, fdd, 1);
		close(fdd);
		close(fds);
	}
	else if (argc >= 4) {
		printf(2, "command has too argument\n");
		exit();	
	}
	exit();
}
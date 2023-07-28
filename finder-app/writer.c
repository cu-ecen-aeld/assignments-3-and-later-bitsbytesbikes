#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char **argv)
{
	openlog(NULL, 0, LOG_USER);

	if(argc != 3) {
		syslog(LOG_ERR, "Usage %s <writefile> <writestr>", argv[0]);
		exit(EXIT_FAILURE);
	}

	FILE *fileptr = fopen(argv[1], "w");

	if(fileptr != NULL){
		if(fputs(argv[2], fileptr) != EOF) {
			syslog(LOG_DEBUG, "Writing %s to %s.", argv[2], argv[1]);
		}	
		else {
			syslog(LOG_ERR, "Error while writing %s to %s.", argv[2], argv[1]);
			exit(EXIT_FAILURE);
		}
	}
	else {
		syslog(LOG_ERR, "Error while writing %s to %s.", argv[2], argv[1]);
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}

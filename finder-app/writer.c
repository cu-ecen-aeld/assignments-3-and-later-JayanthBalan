
#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
	openlog("writer", LOG_PID, LOG_USER);
	if(argc != 3) { // 2 Arguments; filename = $1 and message = $2
		syslog(LOG_ERR,"Invalid Number of arguments");
		closelog();
		return 1;
	}

	FILE *file = fopen(argv[1], "w");
	if(file == NULL) {
		syslog(LOG_ERR,"Failed to open %s", argv[1]);
		closelog();
		return 1;
	}
	
	if(fprintf(file, "%s", argv[2]) < 0) {
		syslog(LOG_ERR, "Failed to write to %s", argv[1]);
		fclose(file);
		closelog();
		return 1;
	}
	
	syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
	fclose(file);
	closelog();

	return 0;
}

/*
 * Winston Privacy 
 * Will Watts will@winstonprivacy.com
 * Modification of progress tool for tracking status of swupdate progress
 * SPDX-License-Identifier:     GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <getopt.h>

#include <progress_ipc.h>

#define RESET		0

// Function will run verification tasks on new parition
// Currently switches state to 2 - STATE_TESTING
static bool verification()
{
	system("fw_setenv ustate 2");
	fprintf(stdout, "change ustate to 2");
	return 1; // TRUE
}

static void resetterm(void)
{
	fprintf(stdout, "%c[%dm", 0x1B, RESET);
}

static struct option long_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"reboot", no_argument, NULL, 'r'},
	{NULL, 0, NULL, 0}
};

static void usage(char *programname)
{
	fprintf(stdout, "%s (compiled %s)\n", programname, __DATE__);
	fprintf(stdout, "Usage %s [OPTION]\n",
			programname);
	fprintf(stdout,
		" -r, --reboot            : reboot after a successful update\n"
		" -h, --help              : print this help and exit\n"
		);
}

int main(int argc, char **argv)
{
	int connfd;
	struct progress_msg msg;
	const char *tmpdir;
	unsigned int curstep = 0;
	char bar[60];
	unsigned int filled_len;
	int opt_w = 0;
	int opt_r = 0;
	int opt_p = 0;
	int c;
	RECOVERY_STATUS	status = IDLE;		/* Update Status (Running, Failure) */

	/* Process options with getopt */
	while ((c = getopt_long(argc, argv, "cwprhs:",
				long_options, NULL)) != EOF) {
		switch (c) {
		case 'w':
			opt_w = 1;
			break;
		case 'p':
			opt_p = 1;
			break;
		case 'r':
			opt_r = 1;
			break;
		case 's':
			SOCKET_PROGRESS_PATH = strdup(optarg);
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		default:
			usage(argv[0]);
			exit(1);
			break;
		}
	}
		
	tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";

	connfd = -1;
	while (1) {
		if (connfd < 0) {
			connfd = progress_ipc_connect(opt_w);
		}

		if (progress_ipc_receive(&connfd, &msg) == -1) {
			continue;
		}

		/*
		 * Something happens, show the info
		 */
		if ((status == IDLE) && (msg.status != IDLE)) {
			fprintf(stdout, "\nUpdate started !\n");
			fprintf(stdout, "Interface: ");
			switch (msg.source) {
			case SOURCE_SURICATTA:
				fprintf(stdout, "BACKEND\n\n");
				break;
			}

		}

		if (msg.infolen > 0)
			fprintf(stdout, "INFO : %s\n\n", msg.info);


		if ((msg.cur_step != curstep) && (curstep != 0))
			fprintf(stdout, "\n");

		filled_len = sizeof(bar) * msg.cur_percent / 100;
		if (filled_len > sizeof(bar))
			filled_len = sizeof(bar);

		memset(bar,'=', filled_len);
		memset(&bar[filled_len], '-', sizeof(bar) - filled_len);

		fprintf(stdout, "[ %.60s ] %d of %d %d%% (%s)\r",
			bar,
			msg.cur_step, msg.nsteps, msg.cur_percent,
		       	msg.cur_image);
		fflush(stdout);


		switch (msg.status) {
		case SUCCESS:
		case FAILURE:
			fprintf(stdout, "\n\n");

			fprintf(stdout, "%s !\n", msg.status == SUCCESS
							  ? "SUCCESS"
							  : "FAILURE");
			resetterm();

			//TODO add method to verify validity of partition after msg.status SUCCESS and before reboot
			if ((msg.status == SUCCESS)) {
				fprintf(stdout, "SUCCESS about to verify");				
				sleep(5);
				if (verification()){
					if (system("reboot") < 0) { /* It should never happen */
						fprintf(stdout, "Please reset the board.\n");
						system("fw_setenv ustate 3");
						//system("/etc/init.d/swupdate restart");
					}
				}
			} else if(msg.status == FAILURE) {
				fprintf(stdout, "Change to FAILED ustate = 3");		
				system("fw_setenv ustate 3");
			}
			break;
		case DONE:
			fprintf(stdout, "\nDONE.\n");
			break;
		default:
			break;
		}

		status = msg.status;
	}
}

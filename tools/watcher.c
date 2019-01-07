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
#include <util.h>

// Function will run verification tasks on new parition
// Currently switches state to 2 - STATE_TESTING
static bool verification()
{
	system("fw_setenv ustate 2");
	INFO("change ustate to 2");
	return 1; // TRUE
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
	int c;
	RECOVERY_STATUS	status = IDLE;		/* Update Status (Running, Failure) */

	/* Process options with getopt */

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
			INFO("Update started !");
			INFO("Interface: ");
			switch (msg.source) {
				case SOURCE_UNKNOWN:
					INFO("UNKNOWN");
					break;	
				case SOURCE_SURICATTA:
					INFO("BACKEND");
					break;
			}

		}





		switch (msg.status) {
			case SUCCESS:
			case FAILURE:
				INFO("%s !", msg.status == SUCCESS ? "SUCCESS" : "FAILURE");

				//TODO add method to verify validity of partition after msg.status SUCCESS and before reboot
				if ((msg.status == SUCCESS)) {
					INFO("SUCCESS about to verify");				
					if (verification()){
						sleep(5);
						if (system("reboot") < 0) { /* It should never happen */
							fprintf(stdout, "Please reset the board.\n");
							INFO("Please reset the board, reboot failed");
							system("fw_setenv ustate 3");
						}
					}
				} else if(msg.status == FAILURE) {
					INFO("Change to FAILED ustate = 3");
					system("fw_setenv ustate 3");
				}
				break;
			case DONE:
				INFO("DONE. ");
				break;
			default:
				break;
		}

		status = msg.status;
	}
}

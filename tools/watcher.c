/*
 * Winston Privacy 
 * Will Watts will@winstonprivacy.com
 * Modification of progress tool for tracking status of swupdate progress
 * SPDX-License-Identifier:     GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/file.h>
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
#include <dirent.h>
#include <syslog.h>
#include "generated/autoconf.h"
#include "uboot.h"
#include "progress_ipc.h"
#include "suricatta/state.h"
#include "suricatta/suricatta.h"

#define STATE_KEY "ustate"


struct env_opts *fw_env_opts = &(struct env_opts) {
	.config_file = (char *)CONFIG_UBOOT_FWENV
};

// log info function
static void log_info(char *message){
	int length = strlen(message) + 1;
	char log_msg[length];
        snprintf(log_msg, length, "%s", message);
	syslog (LOG_INFO, log_msg);
}

/*
 * The lockfile is the same as defined in U-Boot for
 * the fw_printenv utilities
 */
static const char *lockname = "/var/lock/fw_printenv.lock";
static int lock_uboot_env(void)
{
	int lockfd = -1;
	lockfd = open(lockname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (lockfd < 0) {
		log_info("Error opening U-Boot lock file");
		return -1;
	}
	if (flock(lockfd, LOCK_EX) < 0) {
		log_info("Error locking file");
		close(lockfd);
		return -1;
	}

	return lockfd;
}

static void unlock_uboot_env(int lock)
{
	flock(lock, LOCK_UN);
	close(lock);
}

static int bootloader_env_set(const char *name, const char *value)
{
	int lock = lock_uboot_env();
	int ret;

	if (lock < 0)
		return -1;

	if (fw_env_open (fw_env_opts)) {
		log_info("Error: environment not initialized");
		unlock_uboot_env(lock);
		return -1;
	}
	fw_env_write ((char *)name, (char *)value);
	ret = fw_env_flush(fw_env_opts);
	fw_env_close (fw_env_opts);

	unlock_uboot_env(lock);

	return ret;
}

static int bootloader_env_unset(const char *name)
{
	return bootloader_env_set(name, "");
}

static char *bootloader_env_get(const char *name)
{
	int lock;
	char *value = NULL;
	char *var;

	lock = lock_uboot_env();

	if (lock < 0)
		return NULL;

	if (fw_env_open (fw_env_opts)) {
		log_info("Error: environment not initialized");
		unlock_uboot_env(lock);
		return NULL;
	}

	var = fw_getenv((char *)name);

	if (var)
		value = strdup(var);

	fw_env_close (fw_env_opts);

	unlock_uboot_env(lock);

	return value;
}

static int save_ustate(char *key, update_state_t value)
{
	int ret;
	char value_str[2] = {value, '\0'};

	ret = bootloader_env_set(key, value_str);

	return ret;
}

// Function will run verification tasks on new parition
// Currently switches state to 2 - STATE_TESTING
// TODO add diagnostics to determine that update is verified
static int verification()
{
	int result;

	// set ustate to STATE_TESTING, return 0 is SUCCESS
	result = save_ustate((char*)STATE_KEY, STATE_TESTING);
	
	return result;
}

int main(int argc, char **argv)
{
	int connfd;
	struct progress_msg msg;
	const char *tmpdir;
	int opt_w = 0;
	int result = 0;
	RECOVERY_STATUS	status = IDLE;		/* Update Status (Running, Failure) */
	openlog ("swupdate-watcher", LOG_CONS | LOG_PID, LOG_USER);
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
			// log_info("Update started !");
			switch (msg.source) {
				case SOURCE_UNKNOWN:
					log_info("Interface: UNKNOWN");
					break;	
				case SOURCE_SURICATTA:
					log_info("Interface: BACKEND");
					break;
			}

		}
		switch (msg.status) {
			case IDLE:
				log_info("msg.status: IDLE");
				break;
			case START:
				log_info("msg.status START and LED will show update pattern");
				break;
			case RUN:
				log_info("swupdate-watcher RUN and LED will continue running");	
			case SUCCESS:
			case FAILURE:
				if ((msg.status == SUCCESS)) {
					log_info("SUCCESS about to verify");
					log_info("swupdate-watcher: msg.status SUCCESS");
					sleep(3); //give time for post install script to switch mmcrootpart 
					result = verification();
					if (result == 0) {
						log_info("system rebooting");
						if (system("reboot") < 0) { // It should never happen 
							log_info("Please reset the board, reboot failed");
							result = save_ustate((char *)STATE_KEY, STATE_FAILED);
						}
					} else {
					
						log_info("Error while setting ustate on u-boot");
						result = save_ustate((char *)STATE_KEY, STATE_FAILED);
					}

				} else if(msg.status == FAILURE) {
					log_info("Change to FAILED ustate = 3");
					result = save_ustate((char *)STATE_KEY, STATE_FAILED);
				}
				break;
			case DONE:
				log_info("Update is DONE. ");
				log_info("msg.status DONE");
				break;
			default:
				break;
		}

		status = msg.status;
	}
	closelog();
}

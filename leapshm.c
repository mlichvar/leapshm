/*
 * SHM/SOCK refclock for ntpd/chronyd to simulate leap second
 *
 * Copyright (C) 2015  Miroslav Lichvar <mlichvar@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/ipc.h>
#include <sys/socket.h>
#include <sys/shm.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* 1 Jul 2015 00:00:00 UTC */
#define LEAP_TIME 1435708800

#define SHMKEY 0x4e545030

struct shmTime {
  int    mode; /* 0 - if valid set
                *       use values,
                *       clear valid
                * 1 - if valid set
                *       if count before and after read of values is equal,
                *         use values
                *       clear valid
                */
  volatile int count;
  time_t clockTimeStampSec;
  int    clockTimeStampUSec;
  time_t receiveTimeStampSec;
  int    receiveTimeStampUSec;
  int    leap;
  int    precision;
  int    nsamples;
  volatile int valid;
  int    clockTimeStampNSec;
  int    receiveTimeStampNSec;
  int    dummy[8];
};

struct sock_sample {
  struct timeval tv;
  double offset;
  int pulse;
  int leap;
  int _pad;
  int magic;
};

int main(int argc, char **argv) {
	struct shmTime *shm = NULL;
	struct sock_sample sock;
	struct sockaddr_un sun;
	struct timespec ts_ref, ts_system;
	time_t offset;
	int shmid, leap, sock_fd = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: %s shm-number|sock-path secs-before-leap\n", argv[0]);
		return 1;
	}

	if (argv[1][0] != '/') {
		shmid = shmget(0x4e545030 + atoi(argv[1]), sizeof (struct shmTime), 0);

		if (shmid == -1)
			return 1;

		shm = (struct shmTime *)shmat(shmid, 0, 0);
		if (shm == (void *)-1)
			return 1;
	} else {
		sun.sun_family = AF_UNIX;
		if (snprintf(sun.sun_path, sizeof sun.sun_path, "%s", argv[1]) >=
				sizeof (sun.sun_path))
			return 1;

		sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
		if (sock_fd < 0)
			return 1;

		if (connect(sock_fd, (struct sockaddr *)&sun, sizeof (sun)) < 0) {
			close(sock_fd);
			return 1;
		}
	}

	offset = 0;
	leap = 1;

	while (1) {
		clock_gettime(CLOCK_REALTIME, &ts_system);
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts_ref);

		if (!offset)
			offset = LEAP_TIME - atoi(argv[2]) - ts_ref.tv_sec;

		if (leap && ts_ref.tv_sec + offset >= LEAP_TIME) {
			offset--;
			leap = 0;
		}

		ts_ref.tv_sec += offset;

		if (shm) {
			shm->valid = 0;

			shm->mode = 1;
			shm->clockTimeStampSec = ts_ref.tv_sec;
			shm->clockTimeStampUSec = ts_ref.tv_nsec / 1000;
			shm->clockTimeStampNSec = ts_ref.tv_nsec;
			shm->receiveTimeStampSec = ts_system.tv_sec;
			shm->receiveTimeStampUSec = ts_system.tv_nsec / 1000;
			shm->receiveTimeStampNSec = ts_system.tv_nsec;
			shm->leap = leap;
			shm->precision = -30;

			shm->valid = 1;
			shm->count++;
		} else {
			sock.tv.tv_sec = ts_system.tv_sec;
			sock.tv.tv_usec = ts_system.tv_nsec / 1000;
			sock.offset = (ts_ref.tv_sec - ts_system.tv_sec) +
				(ts_ref.tv_nsec - ts_system.tv_nsec) / 1e9;
			sock.pulse = 0;
			sock.leap = leap;
			sock._pad = 0;
			sock.magic = 0x534f434b;

			if (send(sock_fd, &sock, sizeof sock, 0) != sizeof sock) {
				fprintf(stderr, "Could not send sample: %m\n");
				return 1;
			}
		}

		printf("%6ld\t%+.9f\n", LEAP_TIME - ts_system.tv_sec,
				(ts_ref.tv_sec - ts_system.tv_sec) +
				(ts_ref.tv_nsec - ts_system.tv_nsec) / 1e9);
		usleep(1000000);
	}

	return 0;
}

/**
 *******************************************************************************
 * @file    ps.c
 * @author  Olli Vanhoja
 * @brief   ps.
 * @section LICENSE
 * Copyright (c) 2019 Olli Vanhoja <olli.vanhoja@alumni.helsinki.fi>
 * Copyright (c) 2016, 2017 Olli Vanhoja <olli.vanhoja@cs.helsinki.fi>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>
#include "utils.h"

static pid_t * get_pids(void)
{
    int mib_maxproc[] = { CTL_KERN, KERN_MAXPROC };
    int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID };
    size_t size = sizeof(size_t), pids_size;
    pid_t * pids;

    if (sysctl(mib_maxproc, num_elem(mib_maxproc), &pids_size, &size, 0, 0))
        return NULL;
    pids_size *= sizeof(pid_t);

    pids = calloc(pids_size + sizeof(pid_t), 1);
    if (!pids)
        return NULL;

    if (sysctl(mib, num_elem(mib), pids, &pids_size, 0, 0))
        return NULL;

    return pids;
}

static int pid2pstat(struct kinfo_proc * ps, pid_t pid)
{
    int mib[5];
    size_t size = sizeof(*ps);

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = pid;
    mib[4] = KERN_PROC_PSTAT;

    return sysctl(mib, num_elem(mib), ps, &size, 0, 0);
}

int main(int argc, char * argv[], char * envp[])
{
    pid_t pid;
    pid_t * pids;
    pid_t * pid_iter;
    long clk_tck;

    clk_tck = sysconf(_SC_CLK_TCK);
    init_ttydev_arr();

    pids = get_pids();
    if (!pids) {
        perror("Failed to get PIDs");
        return EX_OSERR;
    }

    printf("  PID TTY          TIME CMD\n");
    pid_iter = pids;
    while ((pid = *pid_iter++) != 0) {
        struct kinfo_proc ps;
        clock_t sutime;

        if (pid2pstat(&ps, pid) == -1)
            continue;
        sutime = (ps.utime + ps.stime) / clk_tck;
        printf("%5d %-6s   %02u:%02u:%02u %s\n",
               ps.pid,
               devttytostr(ps.ctty),
               sutime / 3600, (sutime % 3600) / 60, sutime % 60,
               ps.name);
    }

    free(pids);
    return 0;
}

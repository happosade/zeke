/**
 *******************************************************************************
 * @file    proc_fork.c
 * @author  Olli Vanhoja
 * @brief   Kernel process management source file. This file is responsible for
 *          thread creation and management.
 * @section LICENSE
 * Copyright (c) 2013, 2014 Olli Vanhoja <olli.vanhoja@cs.helsinki.fi>
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

#define KERNEL_INTERNAL 1
#include <kstring.h>
#include <libkern.h>
#include <tsched.h>
#include <thread.h>
#include <kerror.h>
#include <kinit.h>
#include <syscall.h>
#include <errno.h>
#include <vm/vm.h>
#include <sys/sysctl.h>
#include <fs/procfs.h>
#include <ptmapper.h>
#include <dynmem.h>
#include <kmalloc.h>
#include <buf.h>
#include <proc.h>
#include "_proc.h"

static pid_t proc_lastpid;  /*!< last allocated pid. */

static proc_info_t * clone_proc_info(proc_info_t * const old_proc);
static int clone_L2_pt(proc_info_t * const new_proc,
        proc_info_t * const old_proc);
static int clone_stack(proc_info_t * new_proc, proc_info_t * old_proc);
static void set_proc_inher(proc_info_t * old_proc, proc_info_t * new_proc);

pid_t proc_fork(pid_t pid)
{
    /*
     * http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
     */

#ifdef configPROC_DEBUG
    char buf[80];
    ksprintf(buf, sizeof(buf), "fork(%u)\n", pid);
    KERROR(KERROR_DEBUG, buf);
#endif

    proc_info_t * const old_proc = proc_get_struct_l(pid);
    proc_info_t * new_proc;
    int err;
    pid_t retval = -EAGAIN;

    /* Check that the old PID was valid. */
    if (!old_proc || (old_proc->state == PROC_STATE_INITIAL)) {
        retval = -EINVAL;
        goto out;
    }

    new_proc = clone_proc_info(old_proc);
    if (!new_proc) { /* Check that clone was ok */
        retval = -ENOMEM;
        goto out;
    }

    procarr_realloc();

    /* Clear some things required to be zeroed at this point */
    new_proc->state = PROC_STATE_INITIAL;
    new_proc->files = 0;
    /* ..and then start to fix things. */

    /* Allocate a master page table for the new process. */
    new_proc->mm.mpt.vaddr = 0;
    new_proc->mm.mpt.type = MMU_PTT_MASTER;
    new_proc->mm.mpt.dom = MMU_DOM_USER;
    if (ptmapper_alloc(&(new_proc->mm.mpt))) {
        retval = -ENOMEM;
        goto free_res;
    }

    /* Allocate an array for regions. */
    new_proc->mm.regions =
        kmalloc(old_proc->mm.nr_regions * sizeof(struct buf *));
    if (!new_proc->mm.regions) {
        retval = -ENOMEM;
        goto free_res;
    }

    /* Clone master page table. */
    if (mmu_ptcpy(&(new_proc->mm.mpt), &(old_proc->mm.mpt))) {
        retval = -EAGAIN; // Actually more like -EINVAL
        goto free_res;
    }

    /* Clone L2 page tables. */
    if (clone_L2_pt(new_proc, old_proc) < 0) {
        retval = -ENOMEM;
        goto free_res;
    }

    /* Variables for cloning and referencing regions. */
    struct buf * vm_reg_tmp;

    /* Copy code region pointer. */
    vm_reg_tmp = (*old_proc->mm.regions)[MM_CODE_REGION];
    if (!vm_reg_tmp) {
        KERROR(KERROR_ERR, "Old proc code region can't be null\n");
        retval = -EINVAL; /* Not allowed but this shouldn't happen */
        goto free_res;
    }
    if (vm_reg_tmp->vm_ops)
        vm_reg_tmp->vm_ops->rref(vm_reg_tmp);
    (*new_proc->mm.regions)[MM_CODE_REGION] = vm_reg_tmp;

    /* Clone stack region */
    if ((retval = clone_stack(new_proc, old_proc))) {
#ifdef configPROC_DEBUG
        ksprintf(buf, sizeof(buf), "Cloning stack region failed.\n");
        KERROR(KERROR_DEBUG, buf);
#endif
        goto free_res;
    }

    /* Copy other region pointers.
     * As an iteresting sidenote, what we are doing here and earlier when L1
     * page table was cloned is that we are losing link between the region
     * structs and the actual L1 page table of this process. However it
     * doesn't matter at all because we are doing COW anyway so no information
     * is ever completely lost but we have to just keep in mind that COW regions
     * are bit incomplete and L1 is not completely reconstructable by using just
     * buf struct.
     *
     * Many BSD variants have fully reconstructable L1 tables but we don't have
     * it directly that way because shared regions can't properly point to more
     * than one page table struct.
     */
    for (int i = MM_HEAP_REGION; i < old_proc->mm.nr_regions; i++) {
        vm_reg_tmp = (*old_proc->mm.regions)[i];
        if (vm_reg_tmp->vm_ops && vm_reg_tmp->vm_ops->rref)
            vm_reg_tmp->vm_ops->rref(vm_reg_tmp);

        (*new_proc->mm.regions)[i] = vm_reg_tmp;

        /* Skip for regions in system page table */
        if ((*new_proc->mm.regions)[i]->b_mmu.vaddr <= MMU_VADDR_KERNEL_END)
            continue;

        /* Set COW bit */
        if ((*new_proc->mm.regions)[i]->b_uflags & VM_PROT_WRITE) {
            (*new_proc->mm.regions)[i]->b_uflags |= VM_PROT_COW;
#if 0
            /* Unnecessary as vm_map_region() will do this too. */
            vm_updateusr_ap((*new_proc->mm.regions)[i]);
#endif
        }

        err = vm_mapproc_region(new_proc, (*new_proc->mm.regions)[i]);
        if (err) {
            retval = -ENOMEM;
            goto free_res;
        }
    }

    /* fork() signals */
    ksignal_signals_fork_reinit(&new_proc->sigs);

    /* Copy file descriptors */
#ifdef configPROC_DEBUG
    KERROR(KERROR_DEBUG, "Copy file descriptors\n");
#endif
    new_proc->files = kmalloc(SIZEOF_FILES(old_proc->files->count));
    if (!new_proc->files) {
#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG,
               "\tENOMEM when tried to allocate memory for file descriptors\n");
#endif
        retval = -ENOMEM;
        goto free_res;
    }
    new_proc->files->count = old_proc->files->count;
    for (int i = 0; i < old_proc->files->count; i++) {
        new_proc->files->fd[i] = old_proc->files->fd[i];
        fs_fildes_ref(new_proc->files, i, 1); /* null pointer safe */
    }
#ifdef configPROC_DEBUG
    KERROR(KERROR_DEBUG, "All file descriptors copied\n");
#endif

    /* Select PID */
    if (nprocs != 1) { /* Tecnically it would be good idea to have lock on
                        * nprocs before reading it but I think this should
                        * work fine... */
        new_proc->pid = proc_get_random_pid();
    } else { /* Proc is init */
#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG, "Assuming this process to be init\n");
#endif
        new_proc->pid = 1;
    }

    if (new_proc->cwd) {
#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG, "Increment refcount for the cwd\n");
#endif
        //vref(new_proc->cwd); /* Increment refcount for the cwd */
        new_proc->cwd->vn_refcount++; /* TODO Not safe. */
    }

    /* A process shall be created with a single thread. If a multi-threaded
     * process calls fork(), the new process shall contain a replica of the
     * calling thread.
     * We left main_thread null if calling process has no main thread.
     */
#ifdef configPROC_DEBUG
    KERROR(KERROR_DEBUG, "Handle main_thread");
#endif
    if (old_proc->main_thread) {
#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG,
               "Call thread_fork() to get a new main thread for the fork.\n");
#endif
        //pthread_t old_tid = get_current_tid();
        pthread_t new_tid = thread_fork();
        if (new_tid < 0) {
#ifdef configPROC_DEBUG
            KERROR(KERROR_DEBUG, "thread_fork() failed\n");
#endif
            retval = -EAGAIN; /* TODO ?? */
            goto free_res;
        } else if (new_tid > 0) { /* thread of the forking process returning */
#ifdef configPROC_DEBUG
            KERROR(KERROR_DEBUG, "\tthread_fork() fork OK\n");
#endif
            new_proc->main_thread = sched_get_thread_info(new_tid);
            new_proc->main_thread->pid_owner = new_proc->pid;
        } else {
            panic("\tThread forking failed");
        }
    } else {
#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG, "No main thread to fork.\n");
#endif
        new_proc->main_thread = NULL;
    }
    retval = new_proc->pid;

    /* Update inheritance attributes */
    set_proc_inher(old_proc, new_proc);

    /* TODO state */
    new_proc->state = PROC_STATE_READY;

    /* Insert the new process into the process array */
    procarr_insert(new_proc);

#ifdef configPROCFS
    procfs_mkentry(new_proc);
#endif

    if (new_proc->main_thread) {
        sched_thread_set_exec(new_proc->main_thread->id);
    }
#ifdef configPROC_DEBUG
    ksprintf(buf, sizeof(buf), "Fork created.\n");
    KERROR(KERROR_DEBUG, buf);
#endif
    goto out; /* Fork created. */

free_res:
    _proc_free(new_proc);
out:
    return retval;
}

/**
 * Clone old process descriptor.
 */
static proc_info_t * clone_proc_info(proc_info_t * const old_proc)
{
    proc_info_t * new_proc;
#ifdef configPROC_DEBUG
    char buf[80];

    ksprintf(buf, sizeof(buf), "clone_proc_info of pid %u\n", old_proc->pid);
    KERROR(KERROR_DEBUG, buf);
#endif

    new_proc = kmalloc(sizeof(proc_info_t));
    if (!new_proc) {
        return 0;
    }
    memcpy(new_proc, old_proc, sizeof(proc_info_t));

    return new_proc;
}

/**
 * Clone L2 page tables of a process.
 * @param new_proc is the target process.
 * @param old_proc is a the source process.
 * @return  Returns positive value indicating number of copied page tables;
 *          Zero idicating that no page tables were copied;
 *          Negative value idicating that copying page tables failed.
 */
static int clone_L2_pt(proc_info_t * const new_proc,
        proc_info_t * const old_proc)
{
    struct vm_pt * old_vpt;
    struct vm_pt * new_vpt;
    int retval = 0;

    /* Clone L2 page tables. */
    RB_INIT(&(new_proc->mm.ptlist_head));
    if (RB_EMPTY(&(old_proc->mm.ptlist_head))) {
        goto out;
    }
    RB_FOREACH(old_vpt, ptlist, &(old_proc->mm.ptlist_head)) {
        /* TODO for some reason linkcount might be invalid! */
#if 0
        if (old_vpt->linkcount <= 0) {
            continue; /* Skip unused page tables; ie. page tables that are
                       * not referenced by any region. */
        }
#endif

        new_vpt = kmalloc(sizeof(struct vm_pt));
        if (!new_vpt) {
            retval = -ENOMEM;
            goto out;
        }

        new_vpt->linkcount = 1;
        new_vpt->pt.vaddr = old_vpt->pt.vaddr;
        new_vpt->pt.master_pt_addr = new_proc->mm.mpt.pt_addr;
        new_vpt->pt.type = MMU_PTT_COARSE;
        new_vpt->pt.dom = old_vpt->pt.dom;

        /* Allocate the actual page table, this will also set pt_addr. */
        if (ptmapper_alloc(&(new_vpt->pt))) {
            retval = -ENOMEM;
            goto out;
        }

        mmu_ptcpy(&(new_vpt->pt), &(old_vpt->pt));

        /* Insert vpt (L2 page table) to the new new process. */
        RB_INSERT(ptlist, &(new_proc->mm.ptlist_head), new_vpt);
        mmu_attach_pagetable(&(new_vpt->pt));

        retval++; /* Increment vpt copied count. */
    }

out:
    return retval;
}

static int clone_stack(proc_info_t * new_proc, proc_info_t * old_proc)
{
    struct vm_pt * vpt;
    struct buf * const old_region = (*old_proc->mm.regions)[MM_STACK_REGION];
    struct buf * new_region = 0;

    if (old_region && old_region->vm_ops) { /* Only if vmp_ops are defined. */
#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG, "Cloning stack\n");
#endif
        if (!old_region->vm_ops->rclone) {
            KERROR(KERROR_ERR, "No clone operation\n");
            return -ENOMEM;
        }

        new_region = old_region->vm_ops->rclone(old_region);
        if (!new_region) {
            return -ENOMEM;
        }
    } else if (old_region) { /* Try to clone the stack manually. */
        const size_t rsize = MMU_SIZEOF_REGION(&(old_region->b_mmu));

#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG, "Cloning stack manually\n");
#endif

        new_region = geteblk(rsize);
        if (!new_region) {
            return -ENOMEM;
        }

        memcpy((void *)(new_region->b_mmu.paddr), (void *)(old_region->b_data),
                rsize);
        new_region->b_uflags = VM_PROT_READ | VM_PROT_WRITE;
        new_region->b_mmu.vaddr = old_region->b_mmu.vaddr;
        new_region->b_mmu.ap = old_region->b_mmu.ap;
        new_region->b_mmu.control = old_region->b_mmu.control;
        /* paddr already set */
        new_region->b_mmu.pt = old_region->b_mmu.pt;
        vm_updateusr_ap(new_region);
    } else { /* else: NO STACK */
#ifdef configPROC_DEBUG
        KERROR(KERROR_DEBUG, "fork(): No stack created\n");
#endif
    }

    if (new_region) {
        if ((vpt = ptlist_get_pt(
                        &(new_proc->mm.ptlist_head),
                        &(new_proc->mm.mpt),
                        new_region->b_mmu.vaddr)) == 0) {
            return -ENOMEM;
        }

        (*new_proc->mm.regions)[MM_STACK_REGION] = new_region;
        vm_map_region((*new_proc->mm.regions)[MM_STACK_REGION], vpt);
    }

    return 0;
}

static void set_proc_inher(proc_info_t * old_proc, proc_info_t * new_proc)
{
    proc_info_t * last_node;
    proc_info_t * tmp;

#ifdef configPROC_DEBUG
    KERROR(KERROR_DEBUG, "Updating inheriance attributes of new_proc\n");
#endif

    /* Initial values */
    new_proc->inh.parent = old_proc;
    new_proc->inh.first_child = NULL;
    new_proc->inh.next_child = NULL;

    if (old_proc->inh.first_child == NULL) {
        /* This is the first child of this parent */
        old_proc->inh.first_child = new_proc;
        new_proc->inh.next_child = NULL;

        return; /* All done */
    }

    /* Find the last child process
     * Assuming first_child is a valid pointer
     */
    tmp = old_proc->inh.first_child;
    do {
        last_node = tmp;
    } while ((tmp = last_node->inh.next_child) != NULL);

    /* Set newly forked thread as the last child in chain. */
    last_node->inh.next_child = new_proc;
}

pid_t proc_get_random_pid(void)
{

    pid_t last_maxproc;
    pid_t newpid;

#ifdef configPROC_DEBUG
    KERROR(KERROR_DEBUG, "proc_get_random_pid()");
#endif

    PROC_LOCK();
    last_maxproc = act_maxproc;
    newpid = last_maxproc + 1;

    /*
     * The new PID will be "randomly" selected between proc_lastpid and
     * maxproc
     */
    do {
        if (newpid > last_maxproc)
            newpid = proc_lastpid + kunirand(last_maxproc - proc_lastpid - 1);
        newpid++;
#ifdef configPROC_DEBUG
        kputs(".");
#endif
    } while (proc_get_struct(newpid));

    proc_lastpid = newpid;

    PROC_UNLOCK();

#ifdef configPROC_DEBUG
    kputs("done\n");
#endif

    return newpid;
}

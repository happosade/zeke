/**
 *******************************************************************************
 * @file    llist.h
 * @author  Olli Vanhoja
 * @brief   Generic linked list.
 * @section LICENSE
 * Copyright (c) 2013 Olli Vanhoja <olli.vanhoja@cs.helsinki.fi>
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

#pragma once
#ifndef LLIST_H
#define LLIST_H

/* TODO Not implemented */

typedef struct {
    struct llist * lst;
    void * next;
} llist_snodedsc_t;

typedef struct {
    struct llist * lst;
    void * next;
    void * prev;
} llist_dnodedsc_t;

/**
 * Generic linked list.
 */
typedef struct llist {
    size_t offset; /*!< Offset of node entry. */
    void * head;
    void * tail;
    int count;
    /* Functions */
    void * (*get)(struct llist *, int i);
    void (*add_head)(struct llist * lst, void * new_node);
    void (*add_tail)(struct llist * lst, void * new_node);
    void (*add_after)(struct llist *, void * node, void * new_node);
    void (*add_before)(struct llist *, void * node, void * new_node);
    void (*remove)(struct llist *, void * node);
    void (*remove_head)(struct llist *, void * node);
    void (*remove_tail)(struct llist *, void * node);
} llist_t;

#endif /* LLIST_H */

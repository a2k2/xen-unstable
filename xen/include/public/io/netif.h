/******************************************************************************
 * netif.h
 * 
 * Unified network-device I/O interface for Xen guest OSes.
 * 
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_NETIF_H__
#define __XEN_PUBLIC_IO_NETIF_H__

typedef struct netif_tx_request {
    unsigned long addr;   /* Machine address of packet.  */
    u16      csum_blank:1; /* Proto csum field blank?   */
    u16      id:15;  /* Echoed in response message. */
    u16      size;   /* Packet size in bytes.       */
} netif_tx_request_t;

typedef struct netif_tx_response {
    u16      id;
    s8       status;
} netif_tx_response_t;

typedef struct {
    u16       id;    /* Echoed in response message.        */
#ifdef CONFIG_XEN_NETDEV_GRANT
    grant_ref_t gref;	/* 2: Reference to incoming granted frame */
#endif
} netif_rx_request_t;

typedef struct {
#ifdef CONFIG_XEN_NETDEV_GRANT
    u32      addr;   /*  0: Offset in page of start of received packet  */
#else
    unsigned long addr; /* Machine address of packet.              */
#endif
    u16      csum_valid:1; /* Protocol checksum is validated?       */
    u16      id:15;
    s16      status; /* -ve: BLKIF_RSP_* ; +ve: Rx'ed pkt size. */
} netif_rx_response_t;

/*
 * We use a special capitalised type name because it is _essential_ that all 
 * arithmetic on indexes is done on an integer type of the correct size.
 */
typedef u32 NETIF_RING_IDX;

/*
 * Ring indexes are 'free running'. That is, they are not stored modulo the
 * size of the ring buffer. The following macros convert a free-running counter
 * into a value that can directly index a ring-buffer array.
 */
#define MASK_NETIF_RX_IDX(_i) ((_i)&(NETIF_RX_RING_SIZE-1))
#define MASK_NETIF_TX_IDX(_i) ((_i)&(NETIF_TX_RING_SIZE-1))

#ifdef __x86_64__
/*
 * This restriction can be lifted when we move netfront/netback to use
 * grant tables. This will remove memory_t fields from the above structures
 * and thus relax natural alignment restrictions.
 */
#define NETIF_TX_RING_SIZE 128
#define NETIF_RX_RING_SIZE 128
#else
#define NETIF_TX_RING_SIZE 256
#define NETIF_RX_RING_SIZE 256
#endif

/* This structure must fit in a memory page. */
typedef struct netif_tx_interface {
    /*
     * Frontend places packets into ring at tx_req_prod.
     * Frontend receives event when tx_resp_prod passes tx_event.
     * 'req_cons' is a shadow of the backend's request consumer -- the frontend
     * may use it to determine if all queued packets have been seen by the
     * backend.
     */
    NETIF_RING_IDX req_prod;
    NETIF_RING_IDX req_cons;
    NETIF_RING_IDX resp_prod;
    NETIF_RING_IDX event;
    union {
        netif_tx_request_t  req;
        netif_tx_response_t resp;
    } ring[NETIF_TX_RING_SIZE];
} netif_tx_interface_t;

/* This structure must fit in a memory page. */
typedef struct netif_rx_interface {
    /*
     * Frontend places empty buffers into ring at rx_req_prod.
     * Frontend receives event when rx_resp_prod passes rx_event.
     */
    NETIF_RING_IDX req_prod;
    NETIF_RING_IDX resp_prod;
    NETIF_RING_IDX event;
    union {
        netif_rx_request_t  req;
        netif_rx_response_t resp;
    } ring[NETIF_RX_RING_SIZE];
} netif_rx_interface_t;

/* Descriptor status values */
#define NETIF_RSP_DROPPED         -2
#define NETIF_RSP_ERROR           -1
#define NETIF_RSP_OKAY             0

#endif

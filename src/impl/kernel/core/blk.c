#include "include/autoconf.h"
#include <stdint.h>
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// Block request structure
typedef struct blk_request {
    uint64_t sector;
    uint32_t count;
    void *buffer;
    int read;               // 1 = read, 0 = write
    int priority;
    uint64_t deadline;      // For deadline scheduler
    struct blk_request *next;
} blk_request_t;

// I/O Scheduler interface
typedef struct iosched {
    const char *name;
    void (*init)(void);
    void (*add_request)(blk_request_t *req);
    blk_request_t *(*get_next)(void);
} iosched_t;

// Request queues
static blk_request_t *read_queue = NULL;
static blk_request_t *write_queue = NULL;

// Current scheduler
static iosched_t *current_scheduler = NULL;

// --- MQ-Deadline Scheduler ---
#ifdef CONFIG_IOSCHED_MQ_DEADLINE
static void mq_deadline_init(void) {
    kprintf("IOSCHED: MQ-Deadline scheduler initialized\n", 0x00FF0000);
}

static void mq_deadline_add(blk_request_t *req) {
    // Add to appropriate queue based on type
    blk_request_t **queue = req->read ? &read_queue : &write_queue;
    
    // Insert by deadline (priority queue)
    if (!*queue || req->deadline < (*queue)->deadline) {
        req->next = *queue;
        *queue = req;
    } else {
        blk_request_t *curr = *queue;
        while (curr->next && curr->next->deadline <= req->deadline) {
            curr = curr->next;
        }
        req->next = curr->next;
        curr->next = req;
    }
}

static blk_request_t *mq_deadline_get_next(void) {
    // Prefer reads over writes (read latency matters more)
    if (read_queue) {
        blk_request_t *req = read_queue;
        read_queue = read_queue->next;
        return req;
    }
    if (write_queue) {
        blk_request_t *req = write_queue;
        write_queue = write_queue->next;
        return req;
    }
    return NULL;
}

static iosched_t mq_deadline = {
    .name = "mq-deadline",
    .init = mq_deadline_init,
    .add_request = mq_deadline_add,
    .get_next = mq_deadline_get_next
};
#endif

// --- Kyber Scheduler ---
#ifdef CONFIG_IOSCHED_KYBER
static int kyber_tokens_read = 8;
static int kyber_tokens_write = 2;

static void kyber_init(void) {
    kprintf("IOSCHED: Kyber scheduler initialized (tokens: read=%d, write=%d)\n", 
            0x00FF0000, kyber_tokens_read, kyber_tokens_write);
}

static void kyber_add(blk_request_t *req) {
    // Simple token-based batching (stub)
    mq_deadline_add(req);
}

static blk_request_t *kyber_get_next(void) {
    return mq_deadline_get_next();
}

static iosched_t kyber = {
    .name = "kyber",
    .init = kyber_init,
    .add_request = kyber_add,
    .get_next = kyber_get_next
};
#endif

// --- BFQ Scheduler ---
#ifdef CONFIG_IOSCHED_BFQ
static void bfq_init(void) {
    kprintf("IOSCHED: BFQ scheduler initialized\n", 0x00FF0000);
}

static void bfq_add(blk_request_t *req) {
    // Budget-based queueing (stub)
    mq_deadline_add(req);
}

static blk_request_t *bfq_get_next(void) {
    return mq_deadline_get_next();
}

static iosched_t bfq = {
    .name = "bfq",
    .init = bfq_init,
    .add_request = bfq_add,
    .get_next = bfq_get_next
};
#endif

// Block layer initialization
void blk_init(void) {
    kprintf("BLK: Initializing block layer...\n", 0x00FF0000);
    
    #ifdef CONFIG_IOSCHED_MQ_DEADLINE
    current_scheduler = &mq_deadline;
    #elif defined(CONFIG_IOSCHED_KYBER)
    current_scheduler = &kyber;
    #elif defined(CONFIG_IOSCHED_BFQ)
    current_scheduler = &bfq;
    #endif
    
    if (current_scheduler) {
        current_scheduler->init();
        kprintf("BLK: Using '%s' I/O scheduler\n", 0x00FF0000, current_scheduler->name);
    } else {
        kprintf("BLK: No I/O scheduler configured (FIFO mode)\n", 0xFFFF0000);
    }
}

void blk_submit_request(blk_request_t *req) {
    if (current_scheduler && current_scheduler->add_request) {
        current_scheduler->add_request(req);
    }
}

blk_request_t *blk_dispatch(void) {
    if (current_scheduler && current_scheduler->get_next) {
        return current_scheduler->get_next();
    }
    return NULL;
}

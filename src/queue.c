#include <pthread.h>
#include <time.h>

#include "lock.h"
#include "queue.h"

void queue_init(struct Queue *queue) {
  queue->size = 0;
  queue->in = 0;
  queue->out = 0;
  queue->closed = 0;

  lock_init(&queue->lock);
  lock_condition_init(&queue->cond_item_added);
  lock_condition_init(&queue->cond_item_removed);
  lock_condition_init(&queue->cond_work_done);
}

void queue_destroy(struct Queue *queue) {
  queue->size = 0;
  queue->in = 0;
  queue->out = 0;
  queue->closed = 0;

  lock_destroy(&queue->lock);
  lock_condition_destroy(&queue->cond_item_added);
  lock_condition_destroy(&queue->cond_item_removed);
  lock_condition_destroy(&queue->cond_work_done);
}

void queue_enqueue(struct Queue *queue, struct QueueItem value) {
  lock_lock(&queue->lock);
  while (queue->size == QUEUE_BUFFER_SIZE) {
    lock_condition_timedwait(&queue->lock, &queue->cond_item_removed, 200);
    if (queue->closed != 0) {
      lock_unlock(&queue->lock);
      pthread_exit(0);
    }
  }
  queue->buffer[queue->in] = value;
  ++queue->size;
  ++queue->in;
  queue->in %= QUEUE_BUFFER_SIZE;
  ++queue->num_work_items;
  lock_unlock(&queue->lock);
  lock_condition_signal(&queue->cond_item_added);
}

struct QueueItem queue_dequeue(struct Queue *queue) {
  lock_lock(&queue->lock);
  while (queue->size == 0) {
    lock_condition_timedwait(&queue->lock, &queue->cond_item_added, 200);
    if (queue->closed != 0) {
      lock_unlock(&queue->lock);
      pthread_exit(0);
    }
  }
  struct QueueItem value = queue->buffer[queue->out];
  --queue->size;
  ++queue->out;
  queue->out %= QUEUE_BUFFER_SIZE;
  lock_unlock(&queue->lock);
  lock_condition_signal(&queue->cond_item_removed);
  return value;
}

void queue_workdone(struct Queue *queue) {
  lock_lock(&queue->lock);
  size_t n = --queue->num_work_items;
  size_t size = queue->size;
  lock_unlock(&queue->lock);
  if (n == 0 && size == 0) {
    lock_condition_broadcast(&queue->cond_work_done);
  }
}

void queue_close(struct Queue *queue) {
  lock_lock(&queue->lock);
  queue->closed = 1;
  lock_unlock(&queue->lock);
}

size_t queue_size(struct Queue *queue) {
  lock_lock(&queue->lock);
  size_t size = queue->size;
  lock_unlock(&queue->lock);
  return size;
}

void queue_wait_until_done(struct Queue *queue) {
  lock_lock(&queue->lock);
  while (queue->num_work_items != 0 || queue->size != 0) {
    lock_condition_wait(&queue->lock, &queue->cond_work_done);
  }
  lock_unlock(&queue->lock);
}
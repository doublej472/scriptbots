#include <time.h>

#include "queue.h"

void queue_init(struct Queue *queue) {
  pthread_condattr_t monoattr;
  pthread_condattr_init(&monoattr);
  pthread_condattr_setclock(&monoattr, CLOCK_MONOTONIC);

  queue->size = 0;
  queue->in = 0;
  queue->out = 0;
  queue->closed = 0;
  queue->num_work_items = 0;

  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->cond_item_added, &monoattr);
  pthread_cond_init(&queue->cond_item_removed, &monoattr);
  pthread_cond_init(&queue->cond_work_done, &monoattr);
}

void queue_enqueue(struct Queue *queue, struct QueueItem value) {
  pthread_mutex_lock(&(queue->mutex));
  while (queue->size == QUEUE_BUFFER_SIZE) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 1;

    pthread_cond_timedwait(&(queue->cond_item_removed), &(queue->mutex), &ts);
    if (queue->closed != 0) {
      pthread_mutex_unlock(&(queue->mutex));
      pthread_exit(0);
    }
  }
  queue->buffer[queue->in] = value;
  ++queue->size;
  ++queue->in;
  queue->in %= QUEUE_BUFFER_SIZE;
  ++queue->num_work_items;
  pthread_mutex_unlock(&(queue->mutex));
  pthread_cond_signal(&(queue->cond_item_added));
}

struct QueueItem queue_dequeue(struct Queue *queue) {
  pthread_mutex_lock(&(queue->mutex));
  while (queue->size == 0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 1;

    pthread_cond_timedwait(&(queue->cond_item_added), &(queue->mutex), &ts);
    if (queue->closed != 0) {
      pthread_mutex_unlock(&(queue->mutex));
      pthread_exit(0);
    }
  }
  struct QueueItem value = queue->buffer[queue->out];
  --queue->size;
  ++queue->out;
  queue->out %= QUEUE_BUFFER_SIZE;
  pthread_mutex_unlock(&(queue->mutex));
  pthread_cond_broadcast(&(queue->cond_item_removed));
  return value;
}

void queue_workdone(struct Queue *queue) {
  pthread_mutex_lock(&(queue->mutex));
  --queue->num_work_items;
  size_t workdone = queue->num_work_items;
  size_t size = queue->size;
  pthread_mutex_unlock(&(queue->mutex));
  if (workdone == 0 && size == 0) {
    pthread_cond_broadcast(&(queue->cond_work_done));
  }
}

void queue_close(struct Queue *queue) {
  pthread_mutex_lock(&(queue->mutex));
  queue->closed = 1;
  pthread_mutex_unlock(&(queue->mutex));
}

size_t queue_size(struct Queue *queue) {
  pthread_mutex_lock(&(queue->mutex));
  size_t size = queue->size;
  pthread_mutex_unlock(&(queue->mutex));
  return size;
}

/*
c-pthread-queue - c implementation of a bounded buffer queue using posix threads
Copyright (C) 2008  Matthew Dickinson
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <pthread.h>
#include <stdio.h>

#ifndef _QUEUE_H
#define _QUEUE_H

#define QUEUE_BUFFER_SIZE 1000

struct QueueItem {
  void (*function)(void *);
  void *data;
};

struct Queue {
  struct QueueItem buffer[QUEUE_BUFFER_SIZE];
  size_t size;
  size_t in;
  size_t out;
  int closed;
  pthread_mutex_t mutex;
  pthread_cond_t cond_item_added;
  pthread_cond_t cond_item_removed;

  size_t num_work_items;
  pthread_cond_t cond_work_done;
};

void queue_init(struct Queue *queue);
void queue_enqueue(struct Queue *queue, struct QueueItem value);
struct QueueItem queue_dequeue(struct Queue *queue);
size_t queue_size(struct Queue *queue);
void queue_workdone(struct Queue *queue);
void queue_close(struct Queue *queue);

#endif

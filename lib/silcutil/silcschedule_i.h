/*

  silcschedule_i.h.

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C) 2001 - 2007 Pekka Riikonen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

*/

#ifndef SILCSCHEDULE_I_H
#define SILCSCHEDULE_I_H

#ifndef SILCSCHEDULE_H
#error "Do not include this header directly"
#endif

#include "silchashtable.h"
#include "silclist.h"

/* Task types */
typedef enum {
  /* File descriptor task that performs some event over file descriptors.
     These tasks are for example network connections. */
  SILC_TASK_FD           = 0,

  /* Timeout tasks are tasks that are executed after the specified
     time has elapsed. After the task is executed the task is removed
     automatically from the scheduler. It is safe to re-register the
     task in task callback. It is also safe to unregister a task in
     the task callback. */
  SILC_TASK_TIMEOUT,

  /* Platform specific process signal task.  On Unix systems this is one of
     the signals described in signal(7).  On other platforms this may not
     be available at all.  Only one callback per signal may be added. */
  SILC_TASK_SIGNAL
} SilcTaskType;

/* Task header */
struct SilcTaskStruct {
  struct SilcTaskStruct *next;
  SilcTaskCallback callback;
  void *context;
  unsigned int type    : 1;	/* 0 = fd, 1 = timeout */
  unsigned int valid   : 1;	/* Set if task is valid */
};

/* Timeout task */
typedef struct SilcTaskTimeoutStruct {
  struct SilcTaskStruct header;
  struct timeval timeout;
} *SilcTaskTimeout;

/* Fd task */
typedef struct SilcTaskFdStruct {
  struct SilcTaskStruct header;
  unsigned int scheduled  : 1;
  unsigned int events     : 14;
  unsigned int revents    : 15;
  SilcUInt32 fd;
} *SilcTaskFd;

/* Scheduler context */
struct SilcScheduleStruct {
  void *internal;
  void *app_context;		   /* Application specific context */
  SilcTaskNotifyCb notify;	   /* Notify callback */
  void *notify_context;		   /* Notify context */
  SilcHashTable fd_queue;	   /* FD task queue */
  SilcList fd_dispatch;		   /* Dispatched FDs */
  SilcList timeout_queue;	   /* Timeout queue */
  SilcList free_tasks;		   /* Timeout task freelist */
  SilcMutex lock;		   /* Scheduler lock */
  struct timeval timeout;	   /* Current timeout */
  unsigned int max_tasks     : 29; /* Max FD tasks */
  unsigned int has_timeout   : 1;  /* Set if timeout is set */
  unsigned int valid         : 1;  /* Set if scheduler is valid */
  unsigned int signal_tasks  : 1;  /* Set if to dispatch signals */
};

/* Locks. These also blocks signals that we care about and thus guarantee
   that while we are in scheduler no signals can happen.  This way we can
   synchronise signals with SILC Scheduler. */
#define SILC_SCHEDULE_LOCK(schedule)				\
do {								\
  silc_mutex_lock(schedule->lock);				\
  schedule_ops.signals_block(schedule, schedule->internal);	\
} while (0)
#define SILC_SCHEDULE_UNLOCK(schedule)				\
do {								\
  schedule_ops.signals_unblock(schedule, schedule->internal);	\
  silc_mutex_unlock(schedule->lock);				\
} while (0)

/* Platform specific scheduler operations */
typedef struct {
  /* Initializes the platform specific scheduler.  This for example initializes
     the wakeup mechanism of the scheduler.  In multi-threaded environment
     the scheduler needs to be wakenup when tasks are added or removed from
     the task queues.  Returns context to the platform specific scheduler.
     If this returns NULL the scheduler initialization will fail.  Do not
     add FD tasks inside function.  Timeout tasks can be added. */
  void *(*init)(SilcSchedule schedule, void *app_context);

  /* Uninitializes the platform specific scheduler context. */
  void (*uninit)(SilcSchedule schedule, void *context);

  /* System specific waiter. This must fill the schedule->fd_dispatch queue
     with valid tasks that has something to dispatch, when this returns. */
  int (*schedule)(SilcSchedule schedule, void *context);

  /* Schedule `task' with events `event_mask'. Zero `event_mask'
     unschedules the task. */
  SilcBool (*schedule_fd)(SilcSchedule schedule, void *context,
			  SilcTaskFd task, SilcTaskEvent event_mask);

  /* Wakes up the scheduler. This is platform specific routine */
  void (*wakeup)(SilcSchedule schedule, void *context);

  /* Register signal */
  void (*signal_register)(SilcSchedule schedule, void *context,
			  SilcUInt32 signal, SilcTaskCallback callback,
			  void *callback_context);

  /* Unregister signal */
  void (*signal_unregister)(SilcSchedule schedule, void *context,
			    SilcUInt32 signal);

  /* Call all signals */
  void (*signals_call)(SilcSchedule schedule, void *context);

  /* Block registered signals in scheduler. */
  void (*signals_block)(SilcSchedule schedule, void *context);

  /* Unblock registered signals in schedule. */
  void (*signals_unblock)(SilcSchedule schedule, void *context);
} SilcScheduleOps;

/* The generic function to add any type of task to the scheduler.  This
   used to be exported as is to application, but now they should use the
   macro wrappers defined in silcschedule.h.  For Fd task the timeout must
   be zero, for timeout task the timeout must not be zero, for signal task
   the fd argument is the signal. */
SilcTask silc_schedule_task_add(SilcSchedule schedule, SilcUInt32 fd,
				SilcTaskCallback callback, void *context,
				long seconds, long useconds,
				SilcTaskType type);


#endif /* SILCSCHEDULE_I_H */

#ifndef _ALPHA_CURRENT_H
#define _ALPHA_CURRENT_H

#include <linux/thread_info.h>

//获取当前线程的信息
#define get_current() (current_thread_info()->task)
#define current get_current()

#endif /* _ALPHA_CURRENT_H */

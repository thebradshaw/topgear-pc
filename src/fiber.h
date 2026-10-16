#ifndef TG_FIBER_H
#define TG_FIBER_H

#include <stdbool.h>

// A second stack for game code that waits for interrupts (docs/notes/frame-loop.md).
// tg_fiber_resume runs the fiber until it yields or returns; tg_fiber_yield, called
// inside it, goes back to whoever resumed it.
typedef struct TgFiber TgFiber;
typedef void TgFiberFn(void* arg);

TgFiber* tg_fiber_create(TgFiberFn* fn, void* arg, size_t stack_size);
void tg_fiber_destroy(TgFiber* fiber);
// Returns true while the fiber has more to do, false once its function has returned.
bool tg_fiber_resume(TgFiber* fiber);
void tg_fiber_yield(void);
bool tg_fiber_active(void);

#endif

// Fibers: Windows fibers, or ucontext elsewhere.

#include <stddef.h>
#include <stdlib.h>

#include "fiber.h"

#ifdef _WIN32
#include <windows.h>

struct TgFiber {
  void* handle;
  void* caller;
  TgFiberFn* fn;
  void* arg;
  bool done;
};

static TgFiber* g_current;

static VOID CALLBACK entry(LPVOID p) {
  TgFiber* f = p;
  f->fn(f->arg);
  f->done = true;
  SwitchToFiber(f->caller);
}

TgFiber* tg_fiber_create(TgFiberFn* fn, void* arg, size_t stack_size) {
  TgFiber* f = calloc(1, sizeof *f);
  f->fn = fn;
  f->arg = arg;
  f->handle = CreateFiber(stack_size, entry, f);
  return f;
}

void tg_fiber_destroy(TgFiber* f) {
  DeleteFiber(f->handle);
  free(f);
}

bool tg_fiber_resume(TgFiber* f) {
  if(f->done) return false;
  void* self = GetCurrentFiber();
  if(self == NULL || self == (void*) 0x1e00) self = ConvertThreadToFiber(NULL);
  f->caller = self;
  TgFiber* prev = g_current;
  g_current = f;
  SwitchToFiber(f->handle);
  g_current = prev;
  return !f->done;
}

void tg_fiber_yield(void) { SwitchToFiber(g_current->caller); }

#else
#include <ucontext.h>

struct TgFiber {
  ucontext_t ctx, caller;
  void* stack;
  TgFiberFn* fn;
  void* arg;
  bool done;
};

static TgFiber* g_current;

static void entry(void) {
  TgFiber* f = g_current;
  f->fn(f->arg);
  f->done = true;
}

TgFiber* tg_fiber_create(TgFiberFn* fn, void* arg, size_t stack_size) {
  TgFiber* f = calloc(1, sizeof *f);
  f->fn = fn;
  f->arg = arg;
  f->stack = malloc(stack_size);
  getcontext(&f->ctx);
  f->ctx.uc_stack.ss_sp = f->stack;
  f->ctx.uc_stack.ss_size = stack_size;
  f->ctx.uc_link = &f->caller;
  makecontext(&f->ctx, entry, 0);
  return f;
}

void tg_fiber_destroy(TgFiber* f) {
  free(f->stack);
  free(f);
}

bool tg_fiber_resume(TgFiber* f) {
  if(f->done) return false;
  TgFiber* prev = g_current;
  g_current = f;
  swapcontext(&f->caller, &f->ctx);
  g_current = prev;
  return !f->done;
}

void tg_fiber_yield(void) {
  TgFiber* f = g_current;
  swapcontext(&f->ctx, &f->caller);
}
#endif

bool tg_fiber_active(void) { return g_current != NULL; }

#ifndef __LAMA_RUNTIME__
#define __LAMA_RUNTIME__

#include "runtime_common.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#define WORD_SIZE (CHAR_BIT * sizeof(ptrt))

extern size_t __gc_stack_top, __gc_stack_bottom;
void failure (char *s, ...);
extern aint Lread();
extern aint Lwrite(aint n);
extern void *Bstring(aint* args);
extern void *Belem(void *p, aint i);
extern aint Llength(void *p);
extern void *Bsta(void *x, aint i, void *v);
extern void *Barray(aint* args, aint bn);
extern void *Bsexp(aint* args, aint bn);
extern aint Btag(void *d, aint t, aint n);
extern aint LtagHash(char *);
extern void Bmatch_failure(void *v, const char *fname, aint line, aint col);
extern void *Lstring(aint* args);
extern void *Bclosure(aint* args, aint bn);
extern aint Bclosure_tag_patt(void *x);
extern aint Bboxed_patt(void *x);
extern aint Bunboxed_patt(void *x);
extern aint Barray_tag_patt(void *x);
extern aint Bstring_tag_patt(void *x);
extern aint Bsexp_tag_patt(void *x);
extern aint Bstring_patt(void *x, void *y);
extern aint Barray_patt (void *d, aint n);

#endif

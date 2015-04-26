/**
 * XOpt - command line parsing library
 *
 * Copyright (c) 2015 JD Ballard.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "xopt.h"

#define EXTRAS_INIT 10
#define ERRBUF_SIZE 1024 * 4

static char errbuf[ERRBUF_SIZE];

struct xoptContext {
  xoptOption *options;
  long flags;
  const char *name;
};

int rpl_vsnprintf(char *, size_t, const char *, va_list);

static void _xopt_set_err(const char **err, const char *const fmt, ...);
static bool _xopt_parse_arg(xoptContext *ctx, int argc, const char **argv,
    int *argi, void *data, const char **err);
static void _xopt_assert_increment(const char ***extras, int extrasCount,
    size_t *extrasCapac, const char **err);
static int _xopt_get_size(const char *arg);
static bool _xopt_get_arg(const char *arg, size_t len, xoptOption *options,
    int size, xoptOption **out);
static void _xopt_set(void *data, xoptOption *option, const char *val,
    const char **err);

xoptContext* xopt_context(const char *name, xoptOption *options, long flags,
    const char **err) {
  xoptContext* ctx;
  *err = 0;

  /* malloc context and check */
  ctx = malloc(sizeof(xoptContext));
  if (!ctx) {
    ctx = 0;
    _xopt_set_err(err, "could not allocate context");
  } else {
    ctx->options = options;
    ctx->flags = flags;
    ctx->name = name;
  }

  return ctx;
}

int xopt_parse(xoptContext *ctx, int argc, const char **argv, void* data,
    const char ***inextras, const char **err) {
  int argi;
  int extrasCount;
  size_t extrasCapac;
  const char **extras;
  bool parseResult;

  *err = 0;
  argi = 0;
  extrasCount = 0;
  extrasCapac = EXTRAS_INIT;
  extras = malloc(sizeof(*extras) * EXTRAS_INIT);

  /* check if extras malloc'd okay */
  if (!extras) {
    _xopt_set_err(err, "could not allocate extras array");
    goto end;
  }

  /* increment argument counter if we aren't
     instructed to check argv[0] */
  if (!(ctx->flags & XOPT_CTX_KEEPFIRST)) {
    ++argi;
  }

  /* iterate over passed command line arguments */
  for (; argi < argc; argi++) {
    /* parse, breaking if there was a failure
       parseResult is true if extra, false if option */
    parseResult = _xopt_parse_arg(ctx, argc, argv, &argi, data, err);
    if (*err) {
      break;
    }

    /* is the argument an extra? */
    if (parseResult) {
      /* make sure we have enough room, or realloc if we don't -
         check that it succeeded */
      _xopt_assert_increment(&extras, extrasCount, &extrasCapac, err);
      if (*err) {
        break;
      }

      /* add extra to list */
      extras[extrasCount++] = argv[argi];
    } else {
      /* make sure we're super-posix'd if specified to be
         (check that no extras have been specified when an option is parsed,
         enforcing options to be specific before [extra] arguments */
      if ((ctx->flags & XOPT_CTX_POSIXMEHARDER) && extrasCount) {
        _xopt_set_err(err, "options cannot be specified after arguments: %s",
            argv[argi]);
        break;
      }
    }
  }

end:
  if (*err) {
    free(extras);
    *inextras = 0;
    return 0;
  } else {
    *inextras = extras;
    return extrasCount;
  }
}

static void _xopt_set_err(const char **err, const char *const fmt, ...) {
  va_list list;
  va_start(list, fmt);
  rpl_vsnprintf(&errbuf[0], ERRBUF_SIZE, fmt, list);
  va_end(list);
  *err = &errbuf[0];
}

static bool _xopt_parse_arg(xoptContext *ctx, int argc, const char **argv,
    int *argi, void *data, const char **err) {
  int size;
  size_t length;
  bool isExtra = false;
  const char* arg = argv[*argi];
  ((void)data);/* XXX */
  ((void)argc);/* XXX */

  /* get argument 'size' (long/short/extra) */
  size = _xopt_get_size(arg);

  /* adjust to parse from beginning of actual content */
  arg += size;
  length = strlen(arg);

  /* TODO handle 0 length (and if long, check for XOPT_CTX_DOUBLEDASH
     which allows everything after a `--' to forward straight to extras */

  switch (size) {
    xoptOption *option;
    bool requiresArg;
  case 1: /* short */
    if (length > 1 && (ctx->flags & XOPT_CTX_NOCONDENSE)
        && !(ctx->flags & XOPT_CTX_SLOPPYSHORTS)) {
      /* invalid argument? */
      _xopt_set_err(err, "short options cannot be combined: %s", argv[*argi]);
    } else if (length > 1 && ctx->flags & XOPT_CTX_SLOPPYSHORTS) {
      /* get argument or error if not found and strict mode enabled. */
      /* we also disregard the return value here (requiresArg) since
         we're already here because it's been supplied one; we'll let
         the handler check the validity of it */
      _xopt_get_arg(arg, 1, ctx->options, size, &option);
      if (!option) {
        if (ctx->flags & XOPT_CTX_STRICT) {
          _xopt_set_err(err, "invalid argument: -%c", arg[0]);
        }
        break;
      }

      /* set argument and check */
      _xopt_set(data, option, arg + 1, err);
      if (*err) {
        break;
      }
    } else {
      /* parse all */
      while (length--) {
        /* get argument or error if not found and strict mode enabled. */
        requiresArg = _xopt_get_arg(arg++, 1, ctx->options, size, &option);
        if (!option) {
          if (ctx->flags & XOPT_CTX_STRICT) {
            _xopt_set_err(err, "invalid argument: -%c", arg[0]);
          }
          break;
        }

        if (requiresArg) {
          /* is it the last? */
          if (length == 1) {
            /* is there another argument? */
            if (argc == *argi + 1) {
              /* TODO check if next is an option and error if it is */
              /* TODO ^ will require moving size checking into its own func */
              _xopt_set(data, option, argv[++*argi], err);
            } else {
              _xopt_set_err(err, "missing option value: -%c",
                  option->shortArg);
            }
          } else {
            _xopt_set_err(err, "combined short option requiring value "
                "not last: -%c", option->shortArg);
          }
        } else {
          /* TODO check if next size is 0 and xset it if it is */
          _xopt_set(data, option, 0, err);
        }
      }
    }

    break;
  case 2: /* long */
    break;
  case 0: /* extra */
/*forward_to_extra:*/ /* XXX */
    isExtra = true;
    break;
  }

  return isExtra;
}

static void _xopt_assert_increment(const char ***extras, int extrasCount,
    size_t *extrasCapac, const char **err) {
  /* have we hit the list size limit? */
  if ((size_t) extrasCount == *extrasCapac) {
    /* increase capcity, realloc, and check for success */
    *extrasCapac += EXTRAS_INIT;
    *extras = realloc(*extras, *extrasCapac);
    if (!*extras) {
      _xopt_set_err(err, "could not realloc arguments array");
    }
  }
}

static int _xopt_get_size(const char *arg) {
  int size;
  for (size = 0; size < 2; size++) {
    if (arg[size] != '-') {
      break;
    }
  }
  return size;
}

#include "emit.h"

#include <stdarg.h>
#include <stdio.h>

/*----------------------------------------------------------------*/

static unsigned indent_ = 0;

enum {
        INDENT = 4
};

void push() { indent_ += INDENT; }
void pop() {indent_ -= INDENT; }

static int newline_ = 1;

void emit(const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        if (newline_) {
                int i;
                for (i = 0; i < indent_; i++)
                        printf(" ");
                newline_ = 0;
        }
        vprintf(fmt, ap);
        va_end(ap);
}

void nl()
{
        printf("\n");
        newline_ = 1;
}

/*----------------------------------------------------------------*/

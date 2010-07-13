#include "emit.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

/*----------------------------------------------------------------*/

FILE *output_;

void set_output_file(FILE *f)
{
        output_ = f;
}

/* using an int so we can detect under runs */
static int indent_ = 0;

enum {
        INDENT = 4
};

void push() { indent_ += INDENT; }
void pop() {indent_ -= INDENT; assert(indent_ >= 0); }

static int newline_ = 1;

void emit(const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        if (newline_) {
                int i;
                for (i = 0; i < indent_; i++)
                        fprintf(output_, " ");
                newline_ = 0;
        }
        vfprintf(output_, fmt, ap);
        va_end(ap);
}

void nl()
{
        fprintf(output_, "\n");
        newline_ = 1;
}

void emit_caps(const char *str)
{
        while (*str) {
                emit("%c", toupper(*str));
                str++;
        }
}

/*----------------------------------------------------------------*/

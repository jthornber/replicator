#include <stdio.h>
#include <string.h>

#include "xdrgen.h"
#include "pretty_print.h"

struct format {
        char *name;
        void (*fn)(struct specification *spec);
};

struct format *lookup_format(const char *str)
{
        static struct format formats[] = {
                { "ast", pretty_print },
                { "header", print_header },
                { "body", print_body },
                { NULL, NULL }
        };

        int i;
        for (i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
                if (!strcmp(formats[i].name, str))
                        return formats + i;
        }

        return NULL;
}

void usage()
{
        fprintf(stderr, "usage: xdrgen --format <format>\n");
        exit(1);
}

int main(int argc, char **argv)
{
        int i, r;
        struct format *f;

        for (i = 1; i < argc; i++) {
                if (!strcmp(argv[i], "--format")) {
                        if (i + 1 == argc)
                                usage();

                        f = lookup_format(argv[i + 1]);
                        if (!f) {
                                fprintf(stderr, "unkown format\n");
                                usage();
                        }
                } else if (!strcmp(argv[i], "--debug")) {
                        yydebug = 1;
                }
        }

        if (!f) {
                fprintf(stderr, "no format specified\n");
                usage();
        }

        init();
        r = yyparse();
        f->fn(get_result());
        fin();

        return r;
}

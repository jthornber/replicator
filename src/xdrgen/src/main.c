#include <stdio.h>
#include <string.h>

#include "emit.h"
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
                { "ruby", print_ruby },
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
        fprintf(stderr, "usage: xdrgen --format <format> [-o <file>] <file>\n");
        exit(1);
}

extern FILE *yyin;

int main(int argc, char **argv)
{
        int i, r;
        struct format *f;
        const char *input = NULL, *output = NULL;
        FILE *out;

        for (i = 1; i < argc; i++) {
                if (!strcmp(argv[i], "--format")) {
                        if (i + 1 == argc)
                                usage();

                        f = lookup_format(argv[i + 1]);
                        if (!f) {
                                fprintf(stderr, "unkown format\n");
                                usage();
                        }
                        i++;

                } else if (!strcmp(argv[i], "--debug")) {
                        yydebug = 1;

                } else if (!strcmp(argv[i], "-o")) {
                        if (i + 1 == argc)
                                usage();

                        output = argv[i + 1];
                        i++;
                } else {
                        if (input) {
                                fprintf(stderr, "multiple input files specified.\n");
                                usage();
                        }

                        input = argv[i];
                }
        }

        if (!input) {
                fprintf(stderr, "no input file specified\n");
                usage();
        }

        if (!f) {
                fprintf(stderr, "no format specified\n");
                usage();
        }

        init();

        yyin = fopen(input, "r");
        if (!yyin) {
                fprintf(stderr, "Couldn't open '%s'\n", input);
                exit(1);
        }

        if (!output)
                set_output_file(stdout);
        else {
                out = fopen(output, "w");
                if (!out) {
                        fprintf(stderr, "unable to open '%s'", output);
                        exit(1);
                }
                set_output_file(out);
        }

        r = yyparse();
        f->fn(get_result());

        fclose(yyin);
        if (output)
                fclose(out);

        fin();

        return r;
}

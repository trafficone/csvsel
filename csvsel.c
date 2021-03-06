/*
 * CSV Selector
 *
 * by William R. Fraser, 10/22/2011
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sysexits.h>
#include <string.h>
#include <errno.h>

#include "growbuf.h"
#include "csvformat.h"
#include "queryeval.h"
#include "util.h"
#include "csvsel.h"
#include "postprocess.h"

#define DEBUG if (false)
//#define DEBUG

int query_debug = 0;

void eval_and_print(growbuf* fields, size_t rownum, row_evaluator_args* args)
{
    FILE* output = args->output;

    if (query_evaluate(fields, rownum, args->root_condition)) {
        for (size_t i = 0; i < args->selectors->size / sizeof(void*); i++) {
            selector* c = ((selector**)(args->selectors->buf))[i];
            switch (c->type) {
            case SELECTOR_COLUMN:
                if (c->un1.column == SIZE_MAX) {
                    for (size_t j = 0; j < fields->size / sizeof(void*); j++) {
                        growbuf* field = ((growbuf**)(fields->buf))[j];
                        print_csv_field((char*)field->buf, output);
                        if (j+1 != fields->size / sizeof(void*)) {
                            fprintf(output, ",");
                        }
                    }
                }
                else if (c->un1.column >= (fields->size / sizeof(void*))) {
                    //
                    // An out-of-bounds column is defined as empty string.
                    //

                    print_csv_field("", output);
                }
                else {
                    growbuf* field = ((growbuf**)(fields->buf))[c->un1.column];
                    print_csv_field((char*)field->buf, output);
                }
                break;

            case SELECTOR_VALUE:
                {
                    val v = value_evaluate(&(c->un1.value), fields, rownum);
                    
                    if (v.is_num) {
                        fprintf(output, "%ld", v.un.num);
                    }
                    else if (v.is_dbl) {
                        fprintf(output, "%lf", v.un.dbl);
                    }
                    else if (v.is_str) {
                        print_csv_field(v.un.str, output);
                    }
                    else {
                        fprintf(stderr, "Error: invalid value type!\n");
                        return;
                    }

                    val_free(&v);
                }
                break;
            }

            if (i+1 != args->selectors->size / sizeof(void*)) {
                fprintf(output, ",");
            }
        }
        fprintf(output, "\n");
    }
}

int csv_select(FILE* input, FILE* output, const char* query, size_t query_len, FILE* header)
{
    int retval = 0;

    growbuf* selectors = NULL;
    growbuf* froms = NULL;
    growbuf* columnnames = NULL;
    compound* root_condition = NULL;

    selectors = growbuf_create(1);
    if (NULL == selectors) {
        fprintf(stderr, "malloc failed\n");
        retval = 2;
        goto cleanup;
    }
    
    froms = growbuf_create(1);
    if(NULL == froms){
       fprintf(stderr, "malloc failed\n");
       retval = 2;
       goto cleanup;
    }

    columnnames = growbuf_create(1);
    if(NULL == columnnames){
       fprintf(stderr, "malloc failed\n");
       retval = 2;
       goto cleanup;
    }

    //parse query into selectors (columns and constants), conditions, from files, and column names
    if (0 != queryparse(query, query_len, selectors, &root_condition, froms, columnnames))
    {
        retval = 1;
        goto cleanup;
    }

    //process query to turn files and column names into selectors which can be evaluated by eval_and_print
    if (0 != querypostprocess(selectors,froms,columnnames,input,header)){
        retval = 3;
        goto cleanup;
    }

    if (query_debug)
    {
        fprintf(stderr, "condition:\n");
        print_condition(root_condition, 0);
    }
    
    /*handling (implicit) joins:
     *  Perform the queries on each of the intermediate files, making sure to include the join column(s).
     *  A join condition is determined in the postprocessor as a special condition w/ two files.
     *  Then perform the join using a final query which performs a search from (larger) file on (smaller) file
     *  Normal full inner join logic is expected: all rows that pass the criteria are passed as a joined row
     */

    row_evaluator_args args = { root_condition, selectors, output };
      
    if (0 != read_csv(input, (row_evaluator)&eval_and_print, (void*)&args)) {
        retval = EX_DATAERR;
    }
    
cleanup:
    if (NULL != selectors) {
        for (size_t i = 0; i < selectors->size / sizeof(void*); i++) {
            selector* s = ((selector**)selectors->buf)[i];
            if (NULL != s) {
                selector_free(s);
                free(s);
            }
        }
        growbuf_free(selectors);
    }
    
    if (NULL != root_condition) {
        free_compound(root_condition);
    }

    return retval;
}


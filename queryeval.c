/*
 * CSV Selector
 *
 * by William R. Fraser, 10/22/2011
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "growbuf.h"
#include "queryparse.h"
#include "queryparse.tab.h"
#include "util.h"
#include "functions.h"

//#define DEBUG
#define DEBUG if (false)

extern int query_debug;

extern functionspec FUNCTIONS[];

void val_free(val* val)
{
    if (val->is_str) {
        free((val->un).str);
    }
}

void selector_free(selector* s)
{
    if (s->type == SELECTOR_VALUE) {
        val_free(&s->un1.value);
    }
}

double csvsel_strtod(const char *str, char **unused)
{
    (void)unused;

    char *buf = (char*)malloc(strlen(str)+1);
    if (buf == NULL) {
        fprintf(stderr, "malloc failed!\n");
        abort();
    }

    for (size_t i = 0, j = 0; ; i++) {
        if (str[i] != '$' && str[i] != ',') {
            buf[j++] = str[i];
            if (str[i] == '\0') {
                break;
            }
        }
    }

    double d = strtod(buf, NULL);

    free(buf);
    return d;
}

long csvsel_atol(const char *str)
{
    char *buf = (char*)malloc(strlen(str)+1);
    if (NULL == buf) {
        fprintf(stderr, "malloc failed!\n");
        abort();
    }

    for (size_t i = 0, j = 0; ; i++) {
        if (str[i] != '$' && str[i] != ',') {
            buf[j++] = str[i];
            if (str[i] == '\0') {
                break;
            }
        }
    }

    long l = atol(buf);

    free(buf);
    return l;
}

/**
 * Evaluates a val to a constant.
 * Returns a new val with all strings copied.
 */
val value_evaluate(const val* val, growbuf* fields, size_t rownum)
{
    struct _val ret;
    memset(&ret, 0, sizeof(struct _val));

    if (val->is_num) {
        ret.un.num = val->un.num;
        ret.is_num = true;
        ret.conversion_type = TYPE_LONG;
    }
    else if (val->is_dbl) {
        ret.un.dbl = val->un.dbl;
        ret.is_dbl = true;
        ret.conversion_type = TYPE_DOUBLE;
    }
    else if (val->is_str) {
        size_t n = strlen(val->un.str) + 1;
        ret.un.str = (char*)malloc(n);
        strncpy(ret.un.str, val->un.str, n);
        ret.is_str = true;
        ret.conversion_type = TYPE_STRING;
    }
    else if (val->is_col) {
        size_t colnum = val->un.col;

        if (colnum >= fields->size / sizeof(void*)) {
            //
            // Selected an out-of-bounds column
            // This is defined as empty string.
            //

            ret.un.str = strdup("");
        }
        else {
            growbuf* field = ((growbuf**)fields->buf)[colnum];
            ret.un.str = strdup(field->buf);
        }

        ret.is_str = true;
        ret.conversion_type = TYPE_STRING;
    }
    else if (val->is_special) {
        switch (val->un.special) {
        case SPECIAL_NUMCOLS:
            ret.un.num = fields->size / sizeof(void*);
            ret.is_num = true;
            ret.conversion_type = TYPE_LONG;
            break;
        case SPECIAL_ROWNUM:
            ret.un.num = rownum;
            ret.is_num = true;
            ret.conversion_type = TYPE_LONG;
            break;
        }
    }
    else if (val->is_func) {
        struct _val args[MAX_ARGS];
        for (size_t i = 0; i < val->un.func->num_args; i++) {
            args[i] = value_evaluate(&(val->un.func->args[i]), fields, rownum);
        }

        switch (val->un.func->func) {
        case FUNC_SUBSTR:
            {
                ssize_t start  = args[1].un.num;
                ssize_t len    = args[2].un.num;
                size_t  in_len = strlen(args[0].un.str);

                if (start < 0) {
                    if (-1*start >= in_len) {
                        start = 0;
                    }
                    else {
                        start += in_len;
                    }
                }
                else if (start >= in_len) {
                    start = in_len;
                    len = 0;
                }

                if (val->un.func->num_args == 2) {
                    len = -1;
                }

                if (len < 0) {
                    if (-1*len >= in_len) {
                        len = in_len - start;
                    }
                    else {
                        len += in_len - start + 1;
                    }
                }
                else if (start + len > in_len) {
                    len = in_len - start;
                }
                
                DEBUG printf("start(%zu) len(%zu) in_len(%zu)\n",
                        start, len, in_len);

                char* result = (char*)malloc(len + 1);
                
                if (len > 0) {
                    memcpy(result, args[0].un.str + start, len);
                }

                result[len] = '\0';

                ret.un.str = result;
                ret.is_str = true;
                ret.conversion_type = TYPE_STRING;
            }
            break;

        case FUNC_STRLEN:
            {
                ret.un.num = strlen(args[0].un.str);
                ret.is_num = true;
                ret.conversion_type = TYPE_LONG;
            }
            break;

        case FUNC_MAX:
        case FUNC_MIN:
            {
                double arg0 = 0.0;
                double arg1 = 0.0;

                if (args[0].is_dbl) {
                    arg0 = args[0].un.dbl;
                }
                else if (args[0].is_num) {
                    arg0 = (double)(args[0].un.num);
                }

                if (args[1].is_dbl) {
                    arg1 = args[1].un.dbl;
                }
                else if (args[1].is_num) {
                    arg1 = (double)(args[1].un.num);
                }

                if (val->un.func->func == FUNC_MAX) {
                    ret.un.dbl = (arg0 > arg1) ? arg0 : arg1;
                }
                else {
                    ret.un.dbl = (arg0 < arg1) ? arg0 : arg1;
                }

                ret.is_dbl = true;
                ret.conversion_type = TYPE_DOUBLE;
            }
            break;

        case FUNC_ABS:
            {
                double arg0 = 0.0;

                if (args[0].is_dbl) {
                    arg0 = args[0].un.dbl;
                }
                else if (args[0].is_num) {
                    arg0 = (double)args[0].un.num;
                }

                ret.un.dbl = fabs(arg0);
                ret.is_dbl = true;
                ret.conversion_type = TYPE_DOUBLE;
            }
            break;

        case FUNC_LOWER:
        case FUNC_UPPER:
            {
                size_t len = strlen(args[0].un.str);
                ret.un.str = (char*)malloc(len + 1);

                for (size_t i = 0; i <= len; i++) {
                    if (val->un.func->func == FUNC_LOWER
                            && args[0].un.str[i] >= 'A' && args[0].un.str[i] <= 'Z') {
                        ret.un.str[i] = args[0].un.str[i] + ('a' - 'A');
                    }
                    else if (val->un.func->func == FUNC_UPPER
                            && args[0].un.str[i] >= 'a' && args[0].un.str[i] <= 'z') {
                        ret.un.str[i] = args[0].un.str[i] - ('a' - 'A');
                    }
                    else {
                        ret.un.str[i] = args[0].un.str[i];
                    }
                }

                ret.is_str = true;
                ret.conversion_type = TYPE_STRING;
            }
            break;

        case FUNC_TRIM:
            {
                size_t len = strlen(args[0].un.str);
                size_t start, end;

                #define IS_WHITESPACE(c) \
                    ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
                
                for (start = 0; start < len; start++) {
                    if (!IS_WHITESPACE(args[0].un.str[start])) {
                        break;
                    }
                }

                for (end = len - 1; end > start; end--) {
                    if (!IS_WHITESPACE(args[0].un.str[end])) {
                        break;
                    }
                }

                len = end - start + 1;
                ret.un.str = (char*)malloc(len + 1);
                memcpy(ret.un.str, args[0].un.str + start, len);
                ret.un.str[len] = '\0';

                ret.is_str = true;
                ret.conversion_type = TYPE_STRING;
            }
            break;

        default:
            fprintf(stderr, "ERROR: no implementation for function %s\n",
                    FUNCTIONS[val->un.func->func].name);
        }

        for (size_t i = 0; i < val->un.func->num_args; i++) {
            val_free(&args[i]);
        }
    }

    //
    // Evaluation to a constant is now complete.
    // Now, do any explicit type conversion that was specified.
    //

    if (val->conversion_type == TYPE_LONG) {
        if (ret.is_str) {
            char* str = ret.un.str;
            ret.un.num = csvsel_atol(str);
            ret.is_str = false;
            free(str);
        }
        else if (ret.is_dbl) {
            ret.un.num = (long)ret.un.dbl;
            ret.is_dbl = false;
        }
        ret.is_num = true;
    }
    else if (val->conversion_type == TYPE_DOUBLE) {
        if (ret.is_str) {
            char* str = ret.un.str;
            ret.un.dbl = csvsel_strtod(str, NULL);
            ret.is_str = false;
            free(str);
        }
        else if (ret.is_num) {
            ret.un.dbl = (double)ret.un.num;
            ret.is_num = false;
        }
        ret.is_dbl = true;
    }
    else if (val->conversion_type == TYPE_STRING) {
        if (ret.is_num) {
            asprintf(&(ret.un.str), "%ld", ret.un.num);
            ret.is_num = false;
        }
        else if (ret.is_dbl) {
            asprintf(&(ret.un.str), "%lf", val->un.dbl);
            ret.is_dbl = false;
        }
        ret.is_str = true;
    }

    return ret;
}

bool query_evaluate(growbuf* fields, size_t rownum, compound* condition)
{
    bool retval = true;

    if (NULL == condition) {
        // return true
        goto cleanup;
    }

    switch (condition->oper) {
    case OPER_SIMPLE:
        {
            val left = value_evaluate(
                                &(condition->simple.left),  fields, rownum);
            val right = value_evaluate(
                                &(condition->simple.right), fields, rownum);

            if (condition->simple.oper == TOK_CONTAINS) {
                //
                // 'contains' is a special case.
                //

                if (left.is_num) {
                    asprintf(&(left.un.str), "%ld", left.un.num);
                }
                else if (left.is_dbl) {
                    asprintf(&(left.un.str), "%lf", left.un.dbl);
                }

                if (right.is_num) {
                    asprintf(&(right.un.str), "%ld", right.un.num);
                }
                else if (right.is_dbl) {
                    asprintf(&(right.un.str), "%lf", right.un.dbl);
                }

                retval = (NULL != strstr(left.un.str, right.un.str));

                free(left.un.str);
                free(right.un.str);

                goto cleanup;
            }

            //
            // Next we do automatic type conversion if needed.
            //
            // conversion | num dbl str
            // -----------+----------------
            //        num | num dbl  *
            //        dbl | dbl dbl dbl
            //        str |  *  dbl str
            //
            // *: If the string has a dot in it, both are converted to dbl.
            //    Otherwise, the string is converted to num.
            //

#define CONVERSION(a, b) \
            if (b.is_dbl) { \
                if (a.is_str) { \
                    char* temp = a.un.str; \
                    a.un.dbl = csvsel_strtod(temp, NULL); \
                    a.is_dbl = true; \
                    a.is_str = false; \
                    free(temp); \
                } \
                else if (a.is_num) { \
                    a.un.dbl = (double)a.un.num; \
                    a.is_dbl = true; \
                    a.is_num = false; \
                } \
            } \
            else if (b.is_num && a.is_str) { \
                if (strchr(a.un.str, '.') != NULL) { \
                    char* temp = a.un.str; \
                    a.un.dbl = csvsel_strtod(temp, NULL); \
                    a.is_dbl = true; \
                    a.is_str = false; \
                    free(temp); \
                    b.un.dbl = (double)b.un.num; \
                    b.is_dbl = true; \
                    b.is_num = false; \
                } \
                else { \
                    char* temp = a.un.str; \
                    a.un.num = csvsel_atol(temp); \
                    a.is_num = true; \
                    a.is_str = false; \
                    free(temp); \
                } \
            }
           
            //
            // The macro only modifies one side (except the special case).
            // Need to apply it both ways.
            //
            CONVERSION(left, right);
            CONVERSION(right, left);

#define COMPARE(operator) \
            if (left.is_dbl) { \
                retval = (left.un.dbl operator right.un.dbl); \
            } else if (left.is_num) { \
                retval = (left.un.num operator right.un.num); \
            } \
            else { \
                retval = (strcmp(left.un.str, right.un.str) operator 0); \
                /* these strings are intermediate results, so free them */ \
                free(left.un.str); \
                free(right.un.str); \
            } \

            switch (condition->simple.oper) {
            case TOK_EQ:
                COMPARE(==)
                break;

            case TOK_NEQ:
                COMPARE(!=)
                break;

            case TOK_GT:
                COMPARE(>)
                break;

            case TOK_LT:
                COMPARE(<)
                break;

            case TOK_GTE:
                COMPARE(>=)
                break;

            case TOK_LTE:
                COMPARE(<=)
                break;
            }
        }
        break;

    case OPER_NOT:
        retval = ! query_evaluate(fields, rownum, condition->left);
        break;

    case OPER_AND:
        retval = (query_evaluate(fields, rownum, condition->left) && query_evaluate(fields, rownum, condition->right));
        break;

    case OPER_OR:
        retval = (query_evaluate(fields, rownum, condition->left) || query_evaluate(fields, rownum, condition->right));
        break;
    }

cleanup:
    return retval;
}


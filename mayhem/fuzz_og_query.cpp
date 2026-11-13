// Fuzzes libog_query's raw SQL parser (src/bin/libog_query): lexer, grammar and the parse-tree -> JSON walker.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
typedef struct {
    char* parse_tree_json;
    char* err_msg;
    bool is_passed;
} OgQueryParseResult;

OgQueryParseResult raw_parser_opengauss_dolphin(const char* str);
void free_parse_result(OgQueryParseResult* result);
void og_query_release_state(void);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    char* sql = (char*)malloc(size + 1);
    if (sql == NULL) {
        return -1;
    }
    memcpy(sql, data, size);
    sql[size] = '\0';

    OgQueryParseResult result = raw_parser_opengauss_dolphin(sql);
    free_parse_result(&result);
    og_query_release_state();
    free(sql);
    return 0;
}

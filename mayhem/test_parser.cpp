// Known-answer oracle for libog_query: parses fixed SQL and diffs the compact JSON parse tree / error text.
#include <stdio.h>
#include <string.h>
#include <cjson/cJSON.h>

extern "C" {
typedef struct {
    char* parse_tree_json;
    char* err_msg;
    bool is_passed;
} OgQueryParseResult;

OgQueryParseResult raw_parser_opengauss_dolphin(const char* str);
void free_parse_result(OgQueryParseResult* result);
}

struct Case {
    const char* sql;
    bool passed;
    const char* expect;  // compact JSON tree when passed, error message when not
};

static const Case kCases[] = {
    {"create table t3(a pg_catalog.int4, b int5 default 10, c varchar2(255) not null, d date, primary key(a,b));", true, "{\"stmts\":[{\"stmtType\":\"create\",\"stmts\":[],\"relations\":[{\"relname\":\"t3\"}],\"fields\":[{\"fieldName\":\"a\",\"fieldType\":\"int4\"},{\"fieldName\":\"b\",\"fieldType\":\"int5\"},{\"fieldName\":\"c\",\"fieldType\":\"varchar\"},{\"fieldName\":\"d\",\"fieldType\":\"date\"}],\"constraints\":[{\"contype\":\"DEFAULT\",\"keys\":[\"b\"]},{\"contype\":\"NOTNULL\",\"keys\":[\"c\"]},{\"contype\":\"PRIMARY_KEY\",\"keys\":[\"a\",\"b\"]}]}]}"},
    {"select a.id, count(*) from t1 a join t2 b on a.id = b.id where a.x > 10 group by a.id order by 1 limit 5;", true, "{\"stmts\":[{\"stmtType\":\"select\",\"stmts\":[],\"fields\":[{\"fieldName\":\"a.id\"},{\"fieldName\":\"a.id\"},{\"fieldName\":\"b.id\"},{\"fieldName\":\"a.x\"},{\"fieldName\":\"a.id\"}],\"funcs\":[{\"funcName\":\"count\"}],\"relations\":[{\"relname\":\"t1\",\"alias\":\"a\"},{\"relname\":\"t2\",\"alias\":\"b\"}],\"exprs\":[{\"exprName\":\"=\"},{\"exprName\":\">\"}]}]}"},
    {"insert into t1(a,b) values (1,'x'),(2,'y');", true, "{\"stmts\":[{\"stmtType\":\"insert\",\"stmts\":[{\"stmtType\":\"select\",\"stmts\":[]}],\"relations\":[{\"relname\":\"t1\"}],\"fields\":[{\"fieldName\":\"a\"},{\"fieldName\":\"b\"}]}]}"},
    {"update t1 set a = a + 1 where id in (select id from t2);", true, "{\"stmts\":[{\"stmtType\":\"update\",\"stmts\":[{\"stmtType\":\"select\",\"stmts\":[],\"fields\":[{\"fieldName\":\"id\"}],\"relations\":[{\"relname\":\"t2\"}]}],\"fields\":[{\"fieldName\":\"a\"},{\"fieldName\":\"a\"},{\"fieldName\":\"id\"}],\"exprs\":[{\"exprName\":\"+\"}],\"relations\":[{\"relname\":\"t1\"}]}]}"},
    {"delete from t1 where a between 1 and 10;", true, "{\"stmts\":[{\"stmtType\":\"delete\",\"stmts\":[],\"funcs\":[{\"funcName\":\"pg_catalog.b_between_and\"}],\"fields\":[{\"fieldName\":\"a\"}],\"relations\":[{\"relname\":\"t1\"}]}]}"},
    {"alter table t3 rename to t4; drop table t1,t2; truncate table t5;", true, "{\"stmts\":[{\"stmtType\":\"rename\",\"stmts\":[],\"relname\":\"t3\",\"newname\":\"t4\"},{\"stmtType\":\"drop\",\"stmts\":[],\"objectType\":\"object_table\",\"objects\":[{\"objectName\":\"t1\"},{\"objectName\":\"t2\"}]},{\"stmtType\":\"truncate\",\"stmts\":[],\"relations\":[{\"relname\":\"t5\"}]}]}"},
    {"create index idx_t1_a on t1 (a, b);", true, "{\"stmts\":[{\"stmtType\":\"create index\",\"stmts\":[],\"keys\":[\"a\",\"b\"],\"relname\":\"t1\",\"idxname\":\"idx_t1_a\"}]}"},
    {"create sequence s1 start with 1 increment by 2;", true, "{\"stmts\":[{\"stmtType\":\"create sequence\",\"stmts\":[],\"relations\":[{\"relname\":\"s1\"}]}]}"},
    {"selct * frm t1;", false, "syntax error at or near \"selct\""},
    {"select * from t1 where;", false, "syntax error at or near \";\""}
};

static void compact(const char* json, char* out, size_t outsz)
{
    cJSON* root = cJSON_Parse(json);
    out[0] = '\0';
    if (root == NULL) {
        return;
    }
    cJSON_DeleteItemFromObject(root, "version");
    char* s = cJSON_PrintUnformatted(root);
    if (s != NULL) {
        snprintf(out, outsz, "%s", s);
        cJSON_free(s);
    }
    cJSON_Delete(root);
}

int main(int argc, char** argv)
{
    char buf[16384];
    if (argc > 1 && strcmp(argv[1], "--print") == 0) {
        for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
            OgQueryParseResult r = raw_parser_opengauss_dolphin(kCases[i].sql);
            compact(r.parse_tree_json, buf, sizeof(buf));
            printf("%zu\t%d\t%s\n", i, r.is_passed, r.is_passed ? buf : (r.err_msg ? r.err_msg : ""));
            free_parse_result(&r);
        }
        return 0;
    }
    int passed = 0, failed = 0;
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        const Case& c = kCases[i];
        OgQueryParseResult r = raw_parser_opengauss_dolphin(c.sql);
        compact(r.parse_tree_json, buf, sizeof(buf));
        const char* got = r.is_passed ? buf : (r.err_msg ? r.err_msg : "");
        bool ok = (r.is_passed == c.passed) && strcmp(got, c.expect) == 0;
        if (ok) {
            passed++;
        } else {
            failed++;
            fprintf(stderr, "FAIL case %zu: %s\n  want(%d): %s\n  got (%d): %s\n", i, c.sql, c.passed, c.expect,
                    r.is_passed, got);
        }
        free_parse_result(&r);
    }
    printf("OGQ_TESTS passed=%d failed=%d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

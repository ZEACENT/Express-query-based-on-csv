#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include "private.h"

#define EXP_COL_NUM         8
#define STATUS_OFFSET       9
#define LIST_OFFSET         8
#define DELIVERY_OFFSET     17

#define FILEPATH        "/my_docker_fs/express.csv"
#define FILEPATH_TMP    "/my_docker_fs/express.tmp.csv"
#define FILEPATH_DST    "/my_docker_fs/express.done.csv"
#define STDERR_HANDLER()                                    \
    do {                                                    \
        fprintf(stderr, "%s\n", strerror(errno));           \
        return -1;                                          \
    }while(0);

static inline bool check_exp_status(const char *token)
{
    if ('0' == *token) {
        return true;
    }
    else {
        return false;
    }
}

static inline void set_slash_before_quote(char *dst, const char *src)
{
    char *token;
    u_int32_t src_len = strlen(src);
    const char *end_char = src + src_len;

    while (token = strstr(src, "\"")) {
        strncpy(dst, src, token - src);
        dst += token - src;
        *dst = '\\';
        dst++;
        *dst = '\"';
        dst++;
        src += token - src + 1;
    }

    strncpy(dst, src, end_char - src + 1);      // include \0
}

static inline void del_comma(char *src)
{
    char *token;
    while (token = strstr(src, ",")) {
        *token = ' ';
        src += token - src + 1;
    }
}

int main(void)
{
    FILE *exp_file_stream = NULL;
    FILE *exp_info_stream = NULL;
    char exp_file_buf[64];
    char exp_info_buf[4096];
    char buf_trans[8192];
    char cmd[16384];

    memset(exp_file_buf, 0, sizeof(exp_file_buf));
    remove(FILEPATH_TMP);
    remove(FILEPATH_DST);

    snprintf(cmd, sizeof(cmd), "cat "FILEPATH" | awk -F, '{print $%u}'", EXP_COL_NUM);
    if (!(exp_file_stream = popen(cmd, "r"))) {
        STDERR_HANDLER();
    }
    u_int32_t line_num = 0;
    while (fgets(exp_file_buf, sizeof(exp_file_buf), exp_file_stream) != NULL) {
        // Get express info
        line_num++;
        char *token;
        token  = exp_file_buf + strlen(exp_file_buf)-1;
        *token = '\0';

        snprintf(cmd, sizeof(cmd), API_CURL, exp_file_buf, APPCODE);
        if (!(exp_info_stream = popen(cmd, "r"))) {
            STDERR_HANDLER();
        }
        sleep(1);
        memset(exp_info_buf, 0, sizeof(exp_info_buf));
        while (fgets(exp_info_buf, sizeof(exp_info_buf), exp_info_stream) != NULL) {
            char *buf_offset;
            if (buf_offset = strstr(exp_info_buf, "status")) {
                buf_offset += STATUS_OFFSET;
                if (check_exp_status(buf_offset)) {
                    if (buf_offset = strstr(exp_info_buf, "deliverystatus")) {
                        buf_offset += DELIVERY_OFFSET;
                        /* 0：快递收件(揽件)1.在途中 2.正在派件 3.已签收 4.派送失败 5.疑难件 6.退件签收  */
                        char *deliverystatus;
                        switch (*buf_offset)
                        {
                        case '0':
                            deliverystatus = "快递收件(揽件)";
                        break;
                        case '1':
                            deliverystatus = "在途中";
                        break;
                        case '2':
                            deliverystatus = "正在派件";
                        break;
                        case '3':
                            deliverystatus = "已签收";
                        break;
                        case '4':
                            deliverystatus = "派送失败";
                        break;
                        case '5':
                            deliverystatus = "疑难件";
                        break;
                        case '6':
                            deliverystatus = "退件签收";
                        break;
                        }
                        snprintf(cmd, sizeof(cmd), "awk -F, -vOFS=, 'NR==%u{$%u=$%u\",%s\"; print}' "FILEPATH" >> "FILEPATH_TMP"",
                                                        line_num, EXP_COL_NUM, EXP_COL_NUM, deliverystatus);
                        system(cmd);
                    }
                    if (buf_offset = strstr(exp_info_buf, "list")) {
                        buf_offset += LIST_OFFSET;
                        if (token = strstr(buf_offset, "}")) {
                            *token = '\0';
                        }
                        del_comma(buf_offset);
                        memset(buf_trans, 0, sizeof(buf_trans));
                        set_slash_before_quote(buf_trans, buf_offset);
                        snprintf(cmd, sizeof(cmd), "awk -F, -vOFS=, 'NR==%u{$%u=$%u\",%s\"; print}' "FILEPATH_TMP" >> "FILEPATH_DST"",
                                                        line_num, EXP_COL_NUM+1, EXP_COL_NUM+1, buf_trans);
                        system(cmd);
                        printf("%u done.\n", line_num);
                    }
                }
                else {
                    printf("%s\n", cmd);
                    printf("%u status error: %c\n", line_num, *buf_offset);
                    snprintf(cmd, sizeof(cmd), "awk -F, -vOFS=, 'NR==%u{$%u=$%u\",%s\"; print}' "FILEPATH" >> "FILEPATH_DST"",
                                                    line_num, EXP_COL_NUM, EXP_COL_NUM, "null");
                    system(cmd);
                }
            }
        }
        pclose(exp_info_stream);
    }
    pclose(exp_file_stream);

    return 0;
}
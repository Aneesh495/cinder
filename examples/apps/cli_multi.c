#include "records/records.h"
#include <stdlib.h>
#include <string.h>

static int bound(const char *text, long *value) {
    char *end; *value = strtol(text, &end, 10);
    return end != text && !*end && *value >= -1000000 && *value <= 1000000;
}
int main(int argc, char **argv) {
    int group = argc == 4 && strcmp(argv[1], "group") == 0;
    int select = argc == 6 && strcmp(argv[1], "select") == 0;
    long minimum = 0, maximum = 0;
    if ((!group && !select) || (select && (!bound(argv[4], &minimum) || !bound(argv[5], &maximum) || minimum > maximum))) {
        fputs("records: group INPUT OUTPUT | select INPUT OUTPUT MIN MAX\n", stderr); return 2;
    }
    FILE *input = fopen(argv[2], "rb");
    if (!input) { fputs("records: cannot open input\n", stderr); return 2; }
    struct Records records = {0}; int valid = records_read(input, &records);
    if (fclose(input) != 0) valid = 0;
    if (!valid) { records_destroy(&records); fputs("records: invalid input or allocation limit\n", stderr); return 2; }
    FILE *output = fopen(argv[3], "wb");
    if (!output) { records_destroy(&records); fputs("records: cannot open output\n", stderr); return 2; }
    if (group) records_group(output, &records); else records_select(output, &records, minimum, maximum);
    int status = ferror(output) ? 2 : 0;
    if (fclose(output) != 0) status = 2;
    records_destroy(&records);
    if (status) fputs("records: output failure\n", stderr);
    return status;
}

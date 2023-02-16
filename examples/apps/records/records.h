#ifndef APP_RECORDS_H
#define APP_RECORDS_H
#include <stdio.h>
struct Record { char key[32]; long value; };
struct Records { struct Record *data; size_t size, capacity; };
int records_read(FILE *file, struct Records *records);
void records_group(FILE *output, struct Records *records);
void records_select(FILE *output, struct Records *records, long minimum, long maximum);
void records_destroy(struct Records *records);
#endif

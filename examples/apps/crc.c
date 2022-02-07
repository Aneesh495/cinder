int crc_step(int crc, int bit) { int mix = (crc ^ bit) & 1; crc = crc >> 1; if (mix != 0) crc = crc ^ 0x8c; return crc & 255; }
int main(void) { int crc = 0; int i = 0; while (i < 16) { crc = crc_step(crc, i & 1); i++; } return crc; }

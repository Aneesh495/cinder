int main(void) { char rows[2][4] = {"ab", "c"}; return sizeof(rows)+(rows[0][2]==0)+(rows[1][3]==0); }

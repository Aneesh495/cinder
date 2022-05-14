enum State { Cold = -3, Warm, Hot = 7, Done };
int main(void) { enum State state = Warm; return state != -2 || Done != 8 || sizeof(enum State) != 4; }

int main(void) { static int value=3; int sum=value; { extern int value; sum+=++value; } return sum!=11 || value!=3; }

struct Hidden; int main(void) { extern struct Hidden value; return &value==(struct Hidden *)0; }

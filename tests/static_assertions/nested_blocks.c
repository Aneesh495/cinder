int main(void) { { _Static_assert(sizeof(void*)==8,"pointer width"); } for(int i=0;i<3;++i) { _Static_assert(sizeof i==4,"loop variable"); } return 0; }

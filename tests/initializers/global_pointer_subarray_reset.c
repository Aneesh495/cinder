int target=9; int *a[2][2]={{&target,&target},{&target,&target},[0]={&target}}; int main(void) { return *a[0][0]+(a[0][1]!=0); }

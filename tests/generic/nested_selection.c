int main(void) { return _Generic(1,int:_Generic(2.0,double:7,default:9),default:11)-7; }

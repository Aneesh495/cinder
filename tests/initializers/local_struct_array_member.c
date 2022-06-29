struct Data { int values[3]; int tail; }; int main(void) { struct Data value={.values[1]=7,11,13}; return value.values[1]+value.values[2]+value.tail; }

struct Data { char text[5]; int x; }; int main(void) { struct Data value={"hi",17}; return value.text[0]-100+value.text[4]+value.x; }

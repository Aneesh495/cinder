struct finish { int finish; }; int main(void) { struct finish x={3}; goto finish; x.finish=7; finish: return x.finish!=3; }

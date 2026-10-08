union U { int x; double y; }; int main(void) { register union U u={3}; u.x+=4; return u.x!=7; }

const char *explorer_text = "</pre><script>alert('source')</script>&\"";
int main(void) { return explorer_text[0] == '<' ? 0 : 1; }

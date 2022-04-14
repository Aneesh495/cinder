#if -1 < 1U
wrong
#else
selected
#endif
#if (1 ? -1 : 0U) > 1U
unsigned_branch
#endif

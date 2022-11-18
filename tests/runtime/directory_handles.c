#include <dirent.h>
int main(void) {
    DIR *directory = opendir(".");
    if (directory == 0) return 1;
    return closedir(directory) != 0;
}

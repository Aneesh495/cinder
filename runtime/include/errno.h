#ifndef CINDER_ERRNO_H
#define CINDER_ERRNO_H

int *__errno_location(void);
#define errno (*__errno_location())
#define EINTR 4
#define EIO 5
#define ENOMEM 12
#define EEXIST 17
#define EINVAL 22
#define ERANGE 34

#endif

/* Private freestanding errno names for upstream jitterentropy. No hosted errno
 * storage: its bare-metal branch never accesses an errno lvalue. */
#ifndef KYIL_JENT_ERRNO_H
#define KYIL_JENT_ERRNO_H
#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EAGAIN 11
#define ENOMEM 12
#define EFAULT 14
#define EBUSY 16
#define EINVAL 22
#define ERANGE 34
#define ENOSYS 38
#define EOVERFLOW 75
#define EOPNOTSUPP 95
#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif
#endif

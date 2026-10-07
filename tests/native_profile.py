"""Declare a bounded stack budget before starting native test subprocesses."""
import platform
import resource


def configure_stack():
    before, hard = resource.getrlimit(resource.RLIMIT_STACK)
    requested = 64 * 1024 * 1024
    if platform.system() == 'Linux':
        assert hard == resource.RLIM_INFINITY or hard >= requested, (
            'native test profile requires a 64 MiB stack budget', before, hard)
        resource.setrlimit(resource.RLIMIT_STACK, (requested, hard))
    after, hard = resource.getrlimit(resource.RLIMIT_STACK)
    return dict(soft_before=before, soft_after=after, hard=hard,
                requested=requested if platform.system() == 'Linux' else None)

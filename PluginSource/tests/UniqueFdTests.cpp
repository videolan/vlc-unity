#include "../linux/UniqueFd.h"

#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {

int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool isOpen(int fd)
{
    return fcntl(fd, F_GETFD) >= 0;
}

void testFailedImportOwnership()
{
    int pipeFds[2] = {-1, -1};
    check(pipe(pipeFds) == 0, "create fd ownership fixture");
    const int owned = pipeFds[0];
    {
        UniqueFd fd(owned);
        check(isOpen(owned), "failure-path fd must remain owned in scope");
    }
    check(!isOpen(owned), "failed import must close its duplicate");
    close(pipeFds[1]);
}

void testSuccessfulImportOwnershipTransfer()
{
    int pipeFds[2] = {-1, -1};
    check(pipe(pipeFds) == 0, "create fd transfer fixture");
    int transferred = -1;
    {
        UniqueFd fd(pipeFds[0]);
        transferred = fd.release();
    }
    check(isOpen(transferred),
          "successful import must transfer instead of closing the fd");
    close(transferred);
    close(pipeFds[1]);
}

} // namespace

int main()
{
    testFailedImportOwnership();
    testSuccessfulImportOwnershipTransfer();
    if (failures)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}

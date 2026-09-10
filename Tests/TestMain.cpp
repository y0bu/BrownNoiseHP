#include "TestFramework.h"

#include <cstring>

namespace testing
{

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

int failures = 0;
int checks   = 0;
std::string currentTest;

void reportFailure (const char* file, int line, const std::string& message)
{
    ++failures;
    std::printf ("  FAIL  %s\n        %s:%d\n        %s\n", currentTest.c_str(), file, line, message.c_str());
}

} // namespace testing

int main (int argc, char** argv)
{
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    int run = 0, failedTests = 0;

    for (auto& test : testing::registry())
    {
        if (filter != nullptr && test.name.find (filter) == std::string::npos)
            continue;

        testing::currentTest = test.name;
        const int before = testing::failures;

        std::printf ("[ RUN ] %s\n", test.name.c_str());
        test.fn();
        ++run;

        if (testing::failures > before) { ++failedTests; std::printf ("[FAIL] %s\n", test.name.c_str()); }
        else                            { std::printf ("[ OK ] %s\n", test.name.c_str()); }
    }

    std::printf ("\n%d test(s) run, %d check(s), %d failed check(s) in %d test(s)\n",
                 run, testing::checks, testing::failures, failedTests);

    return failedTests == 0 ? 0 : 1;
}

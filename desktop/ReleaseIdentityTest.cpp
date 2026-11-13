#include "ReleaseIdentity.h"

int main() {
    using mu_pilot::parseReleaseTag;
    if (parseReleaseTag("mu2000-v1.0.10") <= parseReleaseTag("mu2000-v1.0.9")) return 1;
    for (auto tag : {"v9.0.0", "windows-v9.0.0", "bank-tools-v9.0.0", "mu2000-v1.0",
                     "mu2000-v1.0.0-rc1", "mu2000-v1.00.0", "mu2000-v-1.0.0", "mu2000-v1..0",
                     "mu2000-v4294967296.0.0"}) {
        if (parseReleaseTag(tag)) return 2;
    }
}

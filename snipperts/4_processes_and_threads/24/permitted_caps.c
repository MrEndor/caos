#define _GNU_SOURCE
#include <sys/capability.h>
#include <stdio.h>

int main() {
    cap_t caps = cap_get_proc();

    char *cap_text = cap_to_text(caps, NULL);
    printf("Capabilities: %s\n", cap_text);

    cap_flag_value_t val;
    cap_get_flag(caps, CAP_NET_RAW, CAP_PERMITTED, &val);
    printf("CAP_NET_RAW (Permitted): %s\n", val ? "yes" : "no");

    cap_free(caps);
}
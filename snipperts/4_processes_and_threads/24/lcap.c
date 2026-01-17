#include <sys/capability.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
	pid_t pid;
	cap_t cap;
	cap_value_t cap_list[CAP_LAST_CAP+1];
	cap_flag_t cap_flags;
	cap_flag_value_t cap_flags_value;

	const char *cap_name[CAP_LAST_CAP+1] = {
		"cap_chown",
		"cap_dac_override",
		"cap_dac_read_search",
		"cap_fowner",
		"cap_fsetid",
		"cap_kill",
		"cap_setgid",
		"cap_setuid",
		"cap_setpcap",
		"cap_linux_immutable",
		"cap_net_bind_service",
		"cap_net_broadcast",
		"cap_net_admin",
		"cap_net_raw",
		"cap_ipc_lock",
		"cap_ipc_owner",
		"cap_sys_module",
		"cap_sys_rawio",
		"cap_sys_chroot",
		"cap_sys_ptrace",
		"cap_sys_pacct",
		"cap_sys_admin",
		"cap_sys_boot",
		"cap_sys_nice",
		"cap_sys_resource",
		"cap_sys_time",
		"cap_sys_tty_config",
		"cap_mknod",
		"cap_lease",
		"cap_audit_write",
		"cap_audit_control",
		"cap_setfcap",
		"cap_mac_override",
		"cap_mac_admin",
		"cap_syslog"
	};

	pid = getpid();
	cap = cap_get_pid(pid);
	if (cap == NULL) {
		perror("cap_get_pid");
		exit(-1);
	}

	cap_list[0] = CAP_CHOWN;
	if (cap_set_flag(cap, CAP_EFFECTIVE, 1, cap_list, CAP_SET) == -1) {
		perror("cap_set_flag cap_chown");
		cap_free(cap);
		exit(-1);
	}

	cap_list[0] = CAP_MAC_ADMIN;
	if (cap_set_flag(cap, CAP_PERMITTED, 1, cap_list, CAP_SET) == -1) {
		perror("cap_set_flag cap_mac_admin");
		cap_free(cap);
		exit(-1);
	}

	cap_list[0] = CAP_SETFCAP;
	if (cap_set_flag(cap, CAP_INHERITABLE, 1, cap_list, CAP_SET) == -1) {
		perror("cap_set_flag cap_setfcap");
		cap_free(cap);
		exit(-1);
	}

	int i;
	for (i = 0; i < CAP_LAST_CAP + 1; ++i) {
		cap_from_name(cap_name[i], &cap_list[i]);
		printf("%-20s %d\t\t", cap_name[i], cap_list[i]);
		printf("flags: \t\t");
		cap_get_flag(cap, cap_list[i], CAP_EFFECTIVE, &cap_flags_value);
		printf(" EFFECTIVE %-4s ", (cap_flags_value == CAP_SET) ? "YES" : "NO");
		cap_get_flag(cap, cap_list[i], CAP_PERMITTED, &cap_flags_value);
		printf(" PERMITTED %-4s ", (cap_flags_value == CAP_SET) ? "YES" : "NO");
		cap_get_flag(cap, cap_list[i], CAP_INHERITABLE, &cap_flags_value);
		printf(" INHERITABLE %-4s ", (cap_flags_value == CAP_SET) ? "YES" : "NO");
		printf("\n");
	}

	cap_free(cap);

	return 0;
}
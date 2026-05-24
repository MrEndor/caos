# Изоляция и контейнеры

Контейнеры в Linux — это не отдельная kernel-фича, а композиция трёх независимых механизмов: **namespaces** изолируют
то, что процесс **видит** (свой mount tree, PID space, сетевой стек), **cgroups** ограничивают то, что процесс *
*использует** (CPU time, RAM, I/O), **seccomp** фильтрует системные вызовы, которые процесс может делать. Этот раздел
рассматривает их все по отдельности и показывает, как из них собирается контейнер.

## Темы

| Страница                          | Что рассматривается                                                                                                                           |
|-----------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|
| [seccomp](seccomp.md)             | Фильтрация системных вызовов: strict mode, BPF-фильтры, libseccomp, whitelist-подход                                                          |
| [Linux namespaces](namespaces.md) | 8 типов namespaces, `clone`/`unshare`/`setns`, mount propagation, PID translation, user ns + UID mapping, veth pair, как собирается контейнер |
| [cgroups: углублённо](cgroups.md) | v2 hierarchy, memory/io/pids controllers, PSI, systemd-oomd, freezer, eBPF cgroup programs                                                    |

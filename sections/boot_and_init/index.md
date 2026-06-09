# Boot и init

Между нажатием Power и моментом, когда пользователь видит login prompt, происходят десятки этапов в трёх разных режимах процессора: firmware (UEFI/BIOS) загружает bootloader, bootloader парсит конфиг и грузит kernel + initramfs, kernel поднимает все подсистемы и запускает PID 1 — init, который дальше доводит систему до готовности. Этот раздел разбирает обе половины: boot как путь от железа до первого процесса, и init system (systemd) как менеджер всего что после.

## Темы

| Страница | Что рассматривается |
|----------|---------------------|
| [Загрузка системы: от UEFI до init](boot_process.md) | BIOS vs UEFI (MBR/GPT); ESP, `efibootmgr`, Secure Boot, shim; GRUB2/systemd-boot/rEFInd; стадии GRUB; layout `bzImage`; real → protected → long mode; `boot_params`, e820, ACPI; `start_kernel` → `rest_init` → `/sbin/init`; initramfs, `switch_root` |
| [systemd: init и process manager](systemd.md) | unit types (service/socket/timer/mount/target/slice/scope), граф зависимостей, socket activation, journald, cgroup integration, systemd-oomd, systemd-nspawn, boot sequence через targets |

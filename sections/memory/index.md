# Раздел: Память

Память — один из ключевых ресурсов, которым управляет операционная система. Каждый процесс получает
собственное **виртуальное адресное пространство**: изолированный, непрерывный вид памяти, не зависящий
от того, как физические страницы распределены в RAM или частично вытеснены в своп. Этот раздел объясняет,
как устроена эта абстракция на уровне железа (MMU, TLB, таблицы страниц), как ядро Linux организует
сегменты процесса (`.text`, `.data`, `.bss`, heap, stack), как работает `mmap` для файлов и анонимных
отображений, какими механизмами защищается память (W^X, ASLR, NX-бит) и что происходит внутри `malloc`
при каждом вызове.

Ключевые концепции раздела: страничная адресация и Page Fault, copy-on-write при `fork`, отображение
файлов в память, управление правами доступа через `mprotect`, архитектура кучи glibc (bins, chunks,
coalescing).

## Темы

| Страница                                                         | Что рассматривается                                                                                                                              |
|------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------|
| [Виртуальная память](virtual_memory.md)                          | Адресное пространство процесса, страничная организация, MMU, TLB, виды Page Fault, COW, системные вызовы `mmap`/`brk`/`mremap`                   |
| [Устройство памяти и аллокация](memory_layout_and_allocation.md) | Сегменты адресного пространства: `.text`, `.data`, `.bss`, heap, stack; как ELF-секции проецируются в память; расширение heap через `brk`/`sbrk` |
| [mmap и маппинг файлов](mmap_and_file_mapping.md)                | Системный вызов `mmap`: анонимная память, файловые отображения, `MAP_SHARED` vs `MAP_PRIVATE`, `msync`, `mremap`, `MAP_FIXED`                    |
| [Защита памяти](memory_protection.md)                            | `mprotect`, принцип W^X, NX-бит, ASLR, `SIGSEGV` и `SIGILL`, загрузка кода через `mmap`                                                          |
| [Реализация malloc и free](malloc_and_free_implementation.md)    | Структура chunk, bins (fastbins, smallbins, largebins, unsorted bin), алгоритмы `malloc` и `free`, коалесценция, `brk` vs `mmap` в куче glibc    |
| [Page cache](page_cache.md)                                      | Кэш страниц файлов в RAM, `address_space`, dirty pages и writeback, readahead, `fadvise`/`madvise`, `O_DIRECT`, `fsync`/`fdatasync`, drop_caches |
| [NUMA](numa.md)                                                  | Non-Uniform Memory Access, local vs remote latency, memory policies, `numactl`/`mbind`/`move_pages`, auto-balancing, NUMA-aware аллокаторы       |

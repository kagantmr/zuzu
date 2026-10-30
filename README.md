# zuzu

![Boot screen](docs/img/shell_new.png)

zuzu is a education/learning microkernel written from scratch in C and ARM assembly targeting IoT devices. **zuzuOS** is the userspace that runs on top of it with drivers, services for protocol stacks, shell and otehr applications. Microkernels have a reputation for being too slow for practical use, mostly due to Mach. zuzu is an argument that this is no longer true for the embedded, IoT, and router-class systems.

## About

This began as a hobby project, a way to put every piece of systems programming I cared about into one place and became a master's thesis and undergraduate capstone. It is still the project I most wanted to build in my life. It is named after our family cat, Zuzu, a Scottish Fold.

## Design

The zuzu kernel revolves entirely around managing the **five types of objects** that userspace uses: Spaces, Tasks, Regions (currently Memory), Ports and Events. zuzu argues that these five objects, when designed the right way, are sufficient to describe what an IoT system will need.

## Status

zuzu is still under active development. Contributions are appreciated, please read CONTRIBUTING.md!

### zuzu

| Feature                                                | Status  | Version (planned or complete) |
| ------------------------------------------------------ | ------- | ----------------------------- |
| Groundwork: Boot code, UART, DTB parsing, ARM code     | done    | Loaf                          |
| Memory management (PMM, VMM, demand paging groundwork) | done    | Loaf                          |
| Task lifecycle & scheduling                            | done    | Loaf                          |
| Loaf rewrite                                           | done    | Prowl                         |
| Object management                                      | done    | Prowl                         |
| AArch64 port                                           | pending | Prowl 2.x                     |
| Settlement of the object model semantics               | pending | Knead                         |
| Global lap and SMP                                     | done    | Knead                         |

### zuzuOS

| Feature                             | Status  | Version (planned or complete) |
| ----------------------------------- | ------- | ----------------------------- |
| User: Booting into a shell          | done    | zuzuOS v0.5                   |
| BSP: UART & SD card drivers         | done    | zuzuOS v0.5                   |
| Network stack: link layer, IP       | done    | zuzuOS v0.7                   |
| Network stack: UDP & TCP            | done    | zuzuOS v0.8                   |
| BSP: Graphics stack & KMI drivers   | done    | zuzuOS v0.8                   |
| Raspberry Pi 4: Ethernet (GENET)    | pending | zuzuOS v0.8                   |
| Network stack: multiple devices     | pending | zuzuOS v0.8                   |
| Resurrection: Driver crash recovery | pending | zuzuOS v0.8                   |
| Resurrection: Full crash recovery   | pending | zuzuOS v0.9                   |
| User: full user acct support        | pending | zuzuOS v1.0 Sherbet           |

## Documentation

All documentation is at the zuzu docs website.
Visit [https://kagantmr.github.io/zuzu-docs](https://kagantmr.github.io/zuzu-docs) for the latest information on zuzu, zuzuOS and their development.

## Credits

- zuzu logo designed by Zoz
- zuzuFONT designed by Esther (fbcons)

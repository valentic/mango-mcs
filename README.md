# MANGO Camera System (MCS) Embedded Data Transport System

# Overview

This repository builds the disk image for a remote **Embedded Data
Transport System**.

The build process uses **Buildroot** to generate a complete embedded
Linux system. Buildroot manages the full build pipeline, including:

* Cross-compilation toolchain (compiler, libraries, utilities)
* Linux kernel
* Base user-space utilities
* Application packages
* Filesystem image generation

The resulting system is a **compact but fully functional embedded Linux environment**.

Application layers, including the data transport system, are installed
on top of the base operating system. The final output is a **single disk
image** that can be written directly to the embedded device.

The primary goal of this repository is to provide a **fully reproducible
build process** that requires no manual configuration on the target
hardware.

---

# Container-Based Build Environment

All builds are performed inside a container to ensure:

* Consistent host environment
* Reproducible builds
* Isolation from host OS dependencies

The container environment:

* Is based on **Debian Buster**
* Includes all required build dependencies
* Uses **Podman** as the primary container engine
* Can also run under **Docker** if necessary

Helper scripts are provided to automatically build and launch the
container environment.

---

# Quick Start

## 1. Clone the Repository

This repository uses **Git submodules**, so clone it recursively:

```bash
git clone --recurse-submodules https://github.com/valentic/mango-mcs.git
cd mango-mcs
```

---

## 2. Start the Build Container

Using **Podman (recommended)**:

```
./scripts/run_podman_buildroot.sh
```

Docker can also be used if Podman is unavailable:

```
./scripts/run_docker_buildroot.sh
```

The script will:

* Build the container image if needed
* Mount the current working directory into the container
* Launch an interactive shell inside the container

All build commands should be run from inside this shell.

---

# Build Process

Initialize the configuration and build the system:

```
make mango-rpi4-v1_defconfig all
```

During the build, Buildroot will:

1. Download source packages (GCC, Linux kernel, libraries, etc.)
2. Extract and patch the sources
3. Build the cross-compilation toolchain
4. Compile kernel and user-space packages
5. Assemble the root filesystem
6. Generate the final disk image

The resulting image will be written to:

```
buildroot/output/images/sdcard.img
```

---

## Resource Requirements

Typical requirements:

| Resource   | Requirement |
| ---------- | ----------- |
| Disk space | 15–20 GB    |
| Build time | ~2 hours    |

Subsequent builds are typically **much faster** because only modified
components are rebuilt.

---

# Installation to SD Card

The generated disk image can be written to an SD card using `dd`.

## Identify the Device

Insert the SD card and run:

```
lsblk
```

Example output:

```
sdd              8:48   1  29.7G  0 disk
├─sdd1           8:49   1    32M  0 part
└─sdd2           8:50   1  29.7G  0 part
```

## Write the Image

```
sudo dd if=buildroot/output/images/sdcard.img of=/dev/sdd bs=1M status=progress
```

> [!WARNING]
> Ensure the correct device is selected to avoid overwriting another disk.

Once complete, insert the SD card into the target system and boot
the device.

---

# Development Workflow

The Buildroot environment supports iterative development.

Common tasks include:

* Adding packages
* Modifying system configuration
* Updating filesystem overlays
* Developing application software

## Initial Configuration

The Buildroot configuration must be initialized once:

```
make mango-rpi4-v1_defconfig
```

## Common Buildroot Commands

```
make all
make <package>
make menuconfig
make savedefconfig
```

## Reinstalling the Root Filesystem Overlay

If the filesystem overlay changes:

```
make target-clean
make
```

Subsequent builds are typically much faster than the initial build
because only modified components are rebuilt.

---

# Repository Layout

Typical repository structure:

```
mango-mcs/
├─ buildroot/              # Buildroot source tree (submodule)
├─ external/               # External Buildroot tree
│  ├─ board/               # Board-specific configurations
│  ├─ package/             # Custom Buildroot packages
│  └─ overlay/             # Root filesystem overlay
├─ scripts/
│  ├─ run_podman_buildroot.sh
│  └─ run_docker_buildroot.sh
├─ dl/                     # Downloaded source packages
└─ buildroot/output/       # Build artifacts
```

---

# Container Architecture

The build runs inside a container to ensure a consistent and reproducible
environment.

The container runtime mounts **only the current working directory**
into the container. This directory contains:

* The Buildroot source tree
* External board definitions
* Package definitions
* Download cache
* Build output artifacts

Because the entire project directory is mounted, all build outputs
persist on the host system even if the container is recreated.

---

# Updating Buildroot Safely

Buildroot is maintained as a Git submodule in this repository. Updating
it should be done carefully to avoid breaking board configurations or
custom packages.

## 1. Update the Buildroot Submodule

```
cd buildroot
git fetch
git checkout <new-buildroot-version>
cd ..
git add buildroot
git commit -m "Update Buildroot to <version>"
```

---

## 2. Clean the Existing Build

Buildroot version changes usually require a clean rebuild:

```
make distclean
```

---

## 3. Reapply the Project Configuration

```
make mango-rpi4-v1_defconfig
```

---

## 4. Build the System

```
make
```

---

## 5. Resolve Configuration Changes

New Buildroot releases may:

* Rename configuration options
* Remove packages
* Change dependencies

If configuration updates are required:

```
make menuconfig
make savedefconfig
```

Then update the stored defconfig if necessary.

---

# Adding a New Buildroot Package

Custom packages can be added through the **external Buildroot tree**.

## 1. Create the Package Directory

Create a new package directory:

```
external/package/<package-name>/
```

Typical structure:

```
external/package/<package-name>/
├─ Config.in
├─ <package-name>.mk
```

---

## 2. Define the Package Build Rules

`<package-name>.mk` defines how the package is built.

Example:

```
MY_PACKAGE_VERSION = 1.0
MY_PACKAGE_SITE = https://example.com
MY_PACKAGE_LICENSE = MIT

$(eval $(generic-package))
```

---

## 3. Add the Package to the Buildroot Menu

Update:

```
external/package/Config.in
```

Add:

```
source "external/package/<package-name>/Config.in"
```

---

## 4. Enable the Package

Run:

```
make menuconfig
```

Navigate to:

```
External options  →  Packages
```

Select the new package and save the configuration.

---

## 5. Build the Package

```
make <package-name>
```

Or rebuild the full system:

```
make
```

---

# Troubleshooting

## Out of Disk Space

The full build requires approximately **15–20 GB**.

Clean the build directory:

```
make clean
```

Or remove cached downloads:

```
rm -rf dl/*
```

---

## Buildroot Configuration Issues

If the configuration becomes inconsistent:

```
make distclean
make mango-rpi4-v1_defconfig
```

---

## Container Build Failures

If the container image becomes corrupted or outdated, rebuild it:

```
podman rmi mango-buildroot
./scripts/run_podman_buildroot.sh
```

---

# References

Buildroot — Build system for embedded Linux
[https://buildroot.org](https://buildroot.org)

BusyBox — Lightweight implementations of common UNIX utilities
[https://busybox.net](https://busybox.net)

---

# Revision history


| Date       | Author        | Description                                |
| ---------- | ------------- | ------------------------------------------ |
| 2021-07-21 | Todd Valentic | Initial implementation                     |
| 2026-03-30 | Todd Valentic | Update for container-based builds          |



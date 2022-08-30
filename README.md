# DRAFT

draft is a file transfer tool that was designed to fill a niche need for fast
transfers of multi-terabyte files across multiple Ethernet links.

draft is currently in very early stages of development.

## Quick Start

### Build

Run the build wrapper script from the root of the repository, specifying
appropriate compiler versions (this example is from Ubuntu 20.04):

`CC=gcc-10 CXX=g++-10 CUDACXX=/usr/local/cuda-11.5/bin/nvcc ./tools/buildit`

### Transfer

Draft currently requires manually running a receiver on the target machine, and
a sender on the source machine.

Interfaces are determined by specifying IP address:port pairs, and network
parallelism is determined by the routing configuration between the sender and
receiver.

To transfer a directory of files across three 10Gb NIC ports with
uniquely-assigned IP addresses on separate subnets:

Source:

|interface  | IP/netmask        | 
|-----------|-------------------|
| enp6s0    | 192.168.1.200/24  |
| enp7s0    | 192.168.2.200/24  |
| enp8s0    | 192.168.3.200/24  |

Target

|interface  | IP/netmask        | 
|-----------|-------------------|
| enp74s0   | 192.168.1.201/24  |
| enp75s0   | 192.168.2.201/24  |
| enp76s0   | 192.168.3.201/24  |

Run a receiver on the target machine:

`draft recv --service 192.168.1.201:5000 --target 192.168.1.201:5001 --target 192.168.2.201:5001 --target 192.168.3.201:5001`

draft will listen on the service IP & port (192.168.1.201:5000) for transfer
setup - this IP/port will not be used for data transfer.

The receiver recreates the transferred file tree in the current working
directory.

Each target IP:port combination will bind a parallel receive target for file
transfer.

Once the receiver is running, start the sender.  The sender uses the same
arguments as the receiver, plus an additional path argument:

`draft send --service 192.168.1.201:5000 --target 192.168.1.201:5001 --target 192.168.2.201:5001 --target 192.168.3.201:5001 --path /path/to/directory/to/transfer`

# Build

The build is broken into separate phases for building external dependencies and
draft, itself.

The external build has its own CMake file, and can be skipped if the required
dependencies are already installed on the build system.

## Compiler Versions

draft requires c++20 support, and has been built with g++-10+ and clang-12.

## External Dependencies

To build draft's dependencies into an "environment" directory run something
like the following:

`cmake -B exbuild -S external -DCMAKE_INSTALL_PREFIX=env -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF`

### Options

`-DDRAFT_ENABLE_CBLOSC=ON` - build cblosc, for cpu compression support via the `draft compress` command (experimental).

` -DDRAFT_ENABLE_CUDA=ON` - build nvcomp, for gpu compression support via the `draft nvcompress` command (experimental).

## draft

To build draft, we just point to the environment directory from the external
build (or rely on your system's existing configuration to resolve dependencies
if the external build was skipped):

`cmake -B build -S . -DCMAKE_INSTALL_PREFIX=install -DCMAKE_PREFIX_PATH=env -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF`

If building draft with nvcomp/CUDA support, make sure the cuda libraries are on
the ld path, or point to them with the prefix path, e.g.:

`cmake -B build -S . -DCMAKE_INSTALL_PREFIX=install -DCMAKE_PREFIX_PATH=env\;/usr/local/cuda-11.5/targets/x86_64-linux -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF`

# Development

The hope is that draft will eventually live up to its name, providing durable,
resumable, asynchronous file transfers.

To this end, we aim to address the following requests:

- [ ] dynamically handle link up/down events during transfers, and resubmit failed chunks.
- [ ] verify file integrity during transfers, and support post-transfer verification.
- [ ] support resuming file transfers.
- [x] transfer files.

## Testing

### Unit Tests

With the `DRAFT_ENABLE_TESTS` cmake option enabled (it is enabled by default):

`make -C build test`

### Simulated Network Tests

To do a local test of parallel transfers, you can run `tools/setup_local_test`.

This sets-up a set of 3 [virtual
ethernet](https://man7.org/linux/man-pages/man4/veth.4.html) interfaces in a
[network namespace](https://man7.org/linux/man-pages/man7/namespaces.7.html)
named `draft-rx`, and adds a netem tc queue discipline to simulate reduced
bandwidth over each link:

The draft-rx namespace will contain the following interfaces:

| interface | IP/netmask        | 
|-----------|-------------------|
| draft-rx0 | 10.77.2.101/24    |
| draft-rx1 | 10.77.3.101/24    |
| draft-rx2 | 10.77.4.101/24    |

The draft-tx namespace will contain the following interfaces:

| interface | IP/netmask        | 
|-----------|-------------------|
| draft-tx0 | 10.77.2.100/24    |
| draft-tx1 | 10.77.3.100/24    |
| draft-tx2 | 10.77.4.100/24    |

To start the receiver, open a shell in the `draft-rx` namespace, and run the
`draft recv` command:

`sudo ip netns exec draft-rx bash`
`# draft recv -s 10.77.2.101:5000 -t 10.77.2.101:5001 -t 10.77.3.101:5001 -t 10.77.4.101:5001`

To start the sender, open a shell (without specifying any namespace) and run
the `draft send` command.

`draft send -s 10.77.2.101:5000 -t 10.77.2.101:5001 -t 10.77.3.101:5001 -t 10.77.4.101:5001 -p .`

You may also reverse the roles, running send in the draft-rx namespace.

To tear-down the test setup, run `tools/setup_local_test teardown`.

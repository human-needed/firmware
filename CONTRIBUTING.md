# Contributing to the HumanNeeded firmware

Thanks for helping! Bug reports, ideas and pull requests are all welcome.

## Before you start

- **Bugs and ideas:** open an [issue](https://github.com/human-needed/firmware/issues/new) first,
  so we can agree on the fix before you spend time on it.
- **Security problems:** don't open an issue. See the
  [security policy](https://github.com/human-needed/app/blob/main/SECURITY.md).
- **Small fixes** (typos, docs, an obvious one-line bug) can go straight to a pull request.
- **Protocol changes:** the USB protocol (`hn-usb/2`) is shared with the app. Propose a change in
  an issue first; the firmware and the app change together.

## Build and test

Set up the toolchain as in [README.md](README.md#build-and-flash-windows). The unit tests run on
your PC, with no puck:

```bash
g++ -std=gnu++17 -Wall -Ihumanneeded test/core_test.cpp humanneeded/core.cpp humanneeded/settings.cpp -o core_test && ./core_test
```

CI runs the same. With a puck, `python test/hil_test.py` drives it through every command (close
the HumanNeeded app first: it holds the port).

Build both boards before you push:

```bash
arduino-cli compile --fqbn "rp2040:rp2040:generic:flash=2097152_0,freq=125" humanneeded
```

```bash
arduino-cli compile --fqbn "rp2040:rp2040:generic:flash=2097152_0,freq=125" --build-property "compiler.cpp.extra_flags=-DBOARD_HN_V2" humanneeded
```

## Pull requests

- One change per pull request, with a short description of what and why.
- Add or update tests for behaviour you change (`test/core_test.cpp`; `test/hil_test.py` for
  anything only a real puck shows).
- Keep the style of the code around you; comments say *why*.
- Saved settings store numbers: never renumber the light or sound lists, only add at the end.
- New source files start with `// SPDX-License-Identifier: FSL-1.1-ALv2` (`#` in Python and
  PowerShell).

## Sign your commits (DCO)

Every commit must carry a `Signed-off-by` line. It certifies that you wrote the change, or
otherwise have the right to submit it under this project's licence, as set out in the
[Developer Certificate of Origin 1.1](https://developercertificate.org/):

```text
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

Add the line with `git commit -s`. It uses your Git name and email:

```text
Signed-off-by: Your Name <you@example.com>
```

Forgot? `git commit --amend -s` fixes the last commit; `git rebase --signoff main` fixes them all.

## Licence

The HumanNeeded firmware is licensed under the [Functional Source License, Version 1.1, ALv2
Future License](LICENSE) (FSL-1.1-ALv2). By contributing, you agree that your contribution is
licensed under the same terms, including its change to the Apache License 2.0 two years after
release. In the DCO above, "the open source license" means this licence.

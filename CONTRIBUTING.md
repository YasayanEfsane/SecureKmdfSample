# Contributing

Contributions that improve correctness, documentation, testing, or defensive
engineering are welcome.

## Development Workflow

1. Fork the repository and create a focused branch.
2. Open `SecureKmdfSample.sln` in a compatible Visual Studio and WDK setup.
3. Build `Debug | x64` and `Release | x64`.
4. Run `ProtocolTests.exe`; run `ProtocolTests.exe --integration` in an elevated
   test VM after loading the driver.
5. Run Driver Verifier and static analysis for kernel changes.
6. Submit a pull request explaining the behavior, security impact, and tests.

## Security Invariants

Changes must preserve these properties unless a reviewed protocol revision
explicitly replaces them:

- every IOCTL uses `METHOD_BUFFERED`;
- input and output sizes are checked exactly before access;
- the shared ABI contains no pointers or architecture-sized fields;
- all reserved bytes and unused payload bytes are validated or cleared;
- device access remains restricted to SYSTEM and Administrators;
- pageable callbacks execute only at `PASSIVE_LEVEL`;
- failures complete with zero output bytes.

Do not add `METHOD_NEITHER`, embedded user pointers, unchecked arithmetic,
security-through-obscurity, or production secrets.

## Style and Pull Requests

Keep source and documentation in English, compile at warning level 4, avoid
unrelated formatting changes, and use concise imperative commit messages. A pull
request should be small enough to review, link related issues, update tests and
documentation, and disclose any compatibility change to `Public.h`.

Report suspected vulnerabilities through [SECURITY.md](SECURITY.md), not a
public pull request or issue.

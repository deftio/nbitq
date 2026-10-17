# Security Policy

Fixes land on `main` and ship in the next release; only the latest release
is supported.

## Reporting

Report privately rather than opening a public issue:

- [GitHub private vulnerability reporting](https://github.com/deftio/nbitq/security/advisories/new)
- or email deftio@deftio.com

Include the version, the toolchain, and a minimal reproduction. For a
parsing issue that means the buffer (hex or base64) and the call that
misbehaves on it. You will get an acknowledgement, and credit in the release
notes unless you would rather not be named.

## What counts

nbitq takes bit positions and widths that often come from untrusted data, so
bounds checking is the part that matters. Of particular interest:

- A read or write that touches a bit at or past `bit_len` / `bit_cap`,
  including through overflow in `bit_pos + n` arithmetic
- A varint decode that reads without bound, or accepts an encoding the
  encoder would never produce (overlong, or wider than 64 bits)
- An operation that fails but leaves the buffer partially written or the
  cursor moved; callers are told they may retry after a failure
- A `.nbq` container header that makes `nbq_container_read` over-read, loop,
  or report a payload longer than the buffer
- A buffer that passes CRC validation but parses to something other than
  what was written

A valid CRC means the bytes are intact, not that they are trustworthy:
anyone who can hand you a file can hand you a matching checksum. Treat
CRC-64 as corruption detection, never as authentication, and send untrusted
input through `nbq_container_read`, not `nbq_container_read_unchecked`.

## Out of scope

- `nbq_container_read_unchecked`, which skips the CRC by design
- A `bit_len` or `bit_cap` larger than the buffer actually passed; the
  library checks against the length you give it
- Denial of service from input you chose to hand the library, such as a
  legitimately enormous bit run

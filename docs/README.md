# nbitq documentation

Long-form documentation for nbitq. The [README](../README.md) at the
repository root is the short version.

| Page | Contents |
|---|---|
| [What nbitq is](what-is-nbitq.md) | What it does, where it fits and where it does not, fixed conventions |
| [Getting started](getting-started.md) | Quickstart, install, result codes, varints, embedded builds |
| [How it works](how-it-works.md) | Which bit goes where, with worked examples down to the byte |
| [API reference](api-reference.md) | Overview of every function, signatures, and what is not provided |
| [AGENTS.md](../AGENTS.md) | For coding agents: why the library exists, fixed conventions, build and release rules |
| [llms.txt](../llms.txt) | Index of this material in the llms.txt format |

The same material is published as a website at
<https://deftio.github.io/nbitq/>, built from the static pages in
[`../pages/`](../pages/). The Markdown here is the source of record and the
two are kept in step by hand.

Every byte value and bit count in these pages was produced by running the
library. A wrong one is a bug;
[report it](https://github.com/deftio/nbitq/issues).

# Security Policy

## Reporting a Vulnerability

Please **don't open a public issue** for security problems in this custom edition. Report them
privately through your repository security advisories on GitHub or directly to
**Nawaf Al-Otaibi (Ravok)** (`noan511ee-dot`).

Include what you found, the release (`v1.6.6`, …) or commit, and how to
reproduce it. I'll review it as soon as I can and credit you in the fix unless you'd
rather stay anonymous.

## Your Account

The app stores its Microsoft sign-in (a refresh token) in
`/data/pbox-ps4/account.json` on the console (before v0.9.7:
`/data/homebrew/PPSA99810/account.json`). Treat that file like a
password: never share it or attach it to an issue. If it may have leaked, sign
out of all sessions at <https://account.microsoft.com/security> and sign in on
the console again. The log (`xcloud.log`) contains no passwords or tokens.

## Scope

In scope: this repository's code and customizations by **Nawaf Al-Otaibi (Ravok)**
(sign-in, streaming, UI, build and packaging scripts) and the release packages published here,
built upon the upstream open-source foundation under GPL-3.0.

Out of scope:

- Microsoft's services (Xbox Live, xCloud) and their APIs;
- the PS4/PS5 homebrew environment, payload SDKs, and platform graphics tooling;
- third-party libraries (report those upstream; see `THIRD_PARTY_NOTICES.md`).

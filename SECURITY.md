# Security Policy

## Supported versions

Security fixes are applied to the latest published release and the default branch. Older releases may not receive fixes.

## Reporting a vulnerability

Do not open a public issue for a vulnerability or suspected credential exposure. Use GitHub's private vulnerability reporting for this repository:

1. Open the repository's **Security** tab.
2. Select **Advisories** and **Report a vulnerability**.
3. Include affected versions, reproduction steps, impact, and any suggested mitigation.

If private vulnerability reporting is unavailable, contact a repository maintainer privately and ask for a secure reporting channel. Do not include secrets or exploit details in ordinary email, discussions, issues, pull requests, or chat.

You should receive an acknowledgement within 7 days. A maintainer will coordinate validation, remediation, disclosure, and any CVE request. Please allow time for a fix before public disclosure.

## Credential exposure

Treat a committed credential as compromised even after the file is deleted. Rotate or revoke it first, then remove it from the current tree and purge it from Git history. Avoid copying the credential into issues, pull requests, commit messages, or logs.

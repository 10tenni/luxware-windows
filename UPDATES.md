# Publishing Windows updates

Repository: https://github.com/10tenni/luxware-windows

The app reads `https://github.com/10tenni/luxware-windows/releases/latest/download/latest.json`.
The release contains a versioned portable ZIP and a manifest with app identity, semantic version,
exact byte count and SHA-256. Downloads and redirects are restricted to GitHub's HTTPS hosts.

To publish a subsequent release as the repository owner:

1. Make and verify the code changes.
2. Run `python tools/set_version.py 1.2.0` (choose the next version).
3. Commit and push to main. Review the Windows CI build and test results.
4. Tag that verified commit `v1.2.0` and push the tag.

The tagged workflow builds with MSVC on a Windows runner, runs the production dispatch loop
through an injected event sink plus PowerShell updater filesystem tests, then publishes the ZIP
and `latest.json`. It requires the tag to match `release.json`. No private captures, avatar crops,
or local settings belong in the public repository. The app cannot obtain improvements until
a maintainer publishes a new version; an updater does not generate code updates itself.

The 1.1 initial release may be cross-built with the included MinGW script; the release notes
identify the actual build and test hosts. Generated GitHub source archives contain the application,
vendor headers, model/export sources and licenses. Personal source training images are not public.

The shipped unsigned PowerShell script is copied to a per-check local cache before running.
It never evaluates instructions from the release manifest, installs a driver, requests elevation,
changes machine execution policy, or writes into saved settings. Installation waits for the exact
app process to exit, then replaces only files owned by the package. If replacement fails,
changed files are restored from backup. A backup of replaced files is retained for manual rollback.

Users upgrading from Windows 1.0 must download/extract 1.1 once. Their existing local settings
are retained. Unpublished or offline feeds show an update error while the main app remains usable.
